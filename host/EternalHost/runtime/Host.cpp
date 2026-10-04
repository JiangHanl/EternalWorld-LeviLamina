#include "Host.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace eternal::host {
namespace {
bool validUtf8(std::string_view s) {
    for (std::size_t i=0;i<s.size();) {
        const auto a=static_cast<unsigned char>(s[i++]);
        if (a<0x80) { if (a<32 || a==127) return false; continue; }
        unsigned n=0; uint32_t cp=0, minimum=0;
        if (a>=0xc2 && a<=0xdf) { n=1;cp=a&31;minimum=0x80; }
        else if (a>=0xe0 && a<=0xef) { n=2;cp=a&15;minimum=0x800; }
        else if (a>=0xf0 && a<=0xf4) { n=3;cp=a&7;minimum=0x10000; }
        else return false;
        if (i+n>s.size()) return false;
        while(n--) { auto b=static_cast<unsigned char>(s[i++]); if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63); }
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    }
    return true;
}
std::string_view view(EmUtf8View v) { return v.data ? std::string_view(v.data,v.length) : std::string_view{}; }
bool valid(EmUtf8View v,std::size_t max) { return v.data && v.length>0 && v.length<=max && !v.reserved && validUtf8(view(v)); }
bool idValid(std::string_view id) {
    return !id.empty() && id.size()<=64 && id.front()>='a' && id.front()<='z' &&
        std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';});
}
bool identifier(std::string_view s) {
    return !s.empty() && s.size()<=128 && std::all_of(s.begin(),s.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';
    });
}
template<class T> bool header(const T* p) { return p && p->struct_size==sizeof(T) && p->struct_version==EM_STRUCT_VERSION; }
bool versionAtLeast(EmSemVer have,EmSemVer need) {
    return std::array{have.major,have.minor,have.patch}>=std::array{need.major,need.minor,need.patch};
}
std::string utf8Path(const std::filesystem::path& p) { auto s=p.u8string(); return {reinterpret_cast<const char*>(s.data()),s.size()}; }
class PlatformLibrary final : public Library {
    void* image_{};
public:
    explicit PlatformLibrary(void* image):image_(image){}
    ~PlatformLibrary() override {
        if(!image_)return;
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(image_));
#else
        dlclose(image_);
#endif
    }
    void* symbol(const char* name) noexcept override {
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(image_),name));
#else
        return dlsym(image_,name);
#endif
    }
    void quarantine() noexcept override { image_=nullptr; }
};
class PlatformLoader final : public LibraryLoader {
public:
    std::unique_ptr<Library> open(const std::filesystem::path& path,std::string& error) override {
#ifdef _WIN32
        auto image=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!image) { error="LoadLibrary failed: "+std::to_string(GetLastError());return {}; }
#else
        auto image=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
        if(!image) { auto e=dlerror();error=e?e:"dlopen failed";return {}; }
#endif
        return std::make_unique<PlatformLibrary>(reinterpret_cast<void*>(image));
    }
};
}

struct Host::Impl {
    enum class Phase { Bound, Loaded, Enabling, Enabled, Disabling, Disabled, Unloading, Quarantined, Unloaded };
    struct Dependency { std::string id; EmSemVer minimum{}; uint64_t caps{}; bool optional{}; };
    struct Node {
        Impl* host{};
        ModuleSnapshot report;
        std::unique_ptr<Library> library;
        EmLoadFn load{};
        EmLifecycleFn enable{},disable{},unload{};
        EmHostContext context{};
        std::string config,data,resources;
        std::vector<Dependency> dependencies;
        uint64_t provided{};
        Phase phase{Phase::Bound};
        bool load_attempted{};
        bool required{};
    };
    struct Service { Node* owner{}; EmServiceOffer offer{}; std::string id; uint64_t generation{}; };
    struct Subscription { Node* owner{}; std::string topic; EmEventCallback callback{}; void* user{}; };
    std::unique_ptr<LibraryLoader> loader;
    LogSink logger;
    std::thread::id thread{std::this_thread::get_id()};
    void* enable_thread_handle{};
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Node*> order;
    std::unordered_map<std::string,Service> services;
    std::unordered_map<uint64_t,Subscription> subscriptions;
    uint64_t serial{0};
    uint32_t event_depth{0}, dispatch_count{0};
    bool loaded{},enabled{},quarantined{},busy{},enable_attempted{},thread_handoff{},final_stop{};
    explicit Impl(std::unique_ptr<LibraryLoader> l,LogSink log):loader(l?std::move(l):std::make_unique<PlatformLoader>()),logger(std::move(log)){}
    ~Impl() {
#ifdef _WIN32
        if(enable_thread_handle)CloseHandle(static_cast<HANDLE>(enable_thread_handle));
#endif
    }
    bool onThread()const noexcept { return thread==std::this_thread::get_id(); }
    bool callable(const Node& n)const { return n.phase==Phase::Loaded || n.phase==Phase::Enabling || n.phase==Phase::Enabled; }
    static Node* caller(void* p) { return static_cast<Node*>(p); }
    void log(uint32_t level,std::string_view message) noexcept { try { if(logger)logger(level,message); } catch(...){} }
    static EmStatus EM_CALL logging(void* p,uint32_t level,EmUtf8View message) noexcept {
        try { auto* n=caller(p);if(!n)return EM_INVALID_ARGUMENT;
            if(!n->host->onThread())return EM_WRONG_THREAD;
            if(n->phase==Phase::Quarantined || n->phase==Phase::Unloaded)return EM_NOT_READY;
            if(level>EM_LOG_ERROR || !valid(message,4096))return EM_INVALID_ARGUMENT;
            n->host->log(level,view(message));return EM_OK;
        }catch(...){return EM_INTERNAL_ERROR;}
    }
    static EmStatus EM_CALL publishService(void* p,const EmServiceOffer* offer) noexcept {
        try {auto* n=caller(p);if(!n)return EM_INVALID_ARGUMENT;auto& h=*n->host;
            if(!h.onThread())return EM_WRONG_THREAD;
            if(n->phase!=Phase::Enabling)return EM_NOT_READY;
            if(!header(offer)||!valid(offer->id,128)||!identifier(view(offer->id))||!offer->api_major||!offer->table||!offer->table_size||offer->table_size>65536||offer->reserved)return EM_INVALID_ARGUMENT;
            std::string id(view(offer->id));
            const bool own=id.starts_with(n->report.id+".") || (n->report.id==EM_CORE_MODULE_ID && id==EM_CORE_SERVICE_ID);
            if(!own)return EM_CONFLICT;
            if(h.services.contains(id))return EM_CONFLICT;
            auto count=std::count_if(h.services.begin(),h.services.end(),[&](auto& x){return x.second.owner==n;});
            if(count>=64 || h.services.size()>=256 || h.serial==UINT64_MAX)return EM_LIMIT_EXCEEDED;
            Service s{n,*offer,id,++h.serial};s.offer.id={};h.services.emplace(id,std::move(s));return EM_OK;
        }catch(...){return EM_INTERNAL_ERROR;}
    }
    EmStatus query(const EmServiceRequest* request,EmServiceReference* out) {
        if(!onThread())return EM_WRONG_THREAD;
        if(!header(request)||!header(out)||!valid(request->id,128)||!identifier(view(request->id))||!request->api_major||request->reserved)return EM_INVALID_ARGUMENT;
        auto it=services.find(std::string(view(request->id)));
        if(it==services.end())return EM_NOT_FOUND;
        const auto& s=it->second;
        if(s.owner->phase!=Phase::Enabled)return EM_NOT_READY;
        if(request->api_major!=s.offer.api_major || request->minimum_minor>s.offer.api_minor)return EM_ABI_MISMATCH;
        if((request->required_capabilities&s.offer.capabilities)!=request->required_capabilities)return EM_UNSUPPORTED;
        *out={sizeof(*out),EM_STRUCT_VERSION,s.offer.table,s.offer.table_size,s.offer.api_major,s.offer.api_minor,0,s.offer.capabilities,s.generation};return EM_OK;
    }
    static EmStatus EM_CALL queryService(void* p,const EmServiceRequest* request,EmServiceReference* out) noexcept {
        try {auto* n=caller(p);if(!n)return EM_INVALID_ARGUMENT;if(!n->host->onThread())return EM_WRONG_THREAD;
            if(!n->host->callable(*n))return EM_NOT_READY;
            if(!header(request)||!valid(request->id,128))return EM_INVALID_ARGUMENT;
            auto service=n->host->services.find(std::string(view(request->id)));
            if(service!=n->host->services.end() && service->second.owner!=n &&
               std::none_of(n->dependencies.begin(),n->dependencies.end(),[&](const auto& d){return d.id==service->second.owner->report.id;}))return EM_CONFLICT;
            return n->host->query(request,out);
        }catch(...){return EM_INTERNAL_ERROR;}
    }
    static EmStatus EM_CALL subscribe(void* p,const EmSubscription* sub,uint64_t* token) noexcept {
        try {auto* n=caller(p);if(!n)return EM_INVALID_ARGUMENT;auto& h=*n->host;
            if(!h.onThread())return EM_WRONG_THREAD;if(n->phase!=Phase::Enabling)return EM_NOT_READY;
            if(!header(sub)||!token||!valid(sub->topic,128)||!identifier(view(sub->topic))||!sub->callback||sub->reserved)return EM_INVALID_ARGUMENT;
            auto count=std::count_if(h.subscriptions.begin(),h.subscriptions.end(),[&](auto& x){return x.second.owner==n;});
            if(count>=64 || h.subscriptions.size()>=256 || h.serial==UINT64_MAX)return EM_LIMIT_EXCEEDED;
            const auto id=++h.serial;h.subscriptions.emplace(id,Subscription{n,std::string(view(sub->topic)),sub->callback,sub->user});*token=id;return EM_OK;
        }catch(...){return EM_INTERNAL_ERROR;}
    }
    static EmStatus EM_CALL unsubscribe(void* p,uint64_t token) noexcept {
        try {auto* n=caller(p);if(!n||!token)return EM_INVALID_ARGUMENT;auto& h=*n->host;
            if(!h.onThread())return EM_WRONG_THREAD;if(!h.callable(*n))return EM_NOT_READY;
            auto i=h.subscriptions.find(token);if(i==h.subscriptions.end())return EM_NOT_FOUND;
            if(i->second.owner!=n)return EM_CONFLICT;h.subscriptions.erase(i);return EM_OK;
        }catch(...){return EM_INTERNAL_ERROR;}
    }
    EmStatus dispatch(const EmEvent* event) {
        if(!onThread())return EM_WRONG_THREAD;
        if(!header(event)||!valid(event->topic,128)||!identifier(view(event->topic))||event->data_size>65536||(!event->data&&event->data_size)||!event->payload_version||event->reserved)return EM_INVALID_ARGUMENT;
        if(event_depth>=8)return EM_LIMIT_EXCEEDED;
        if(!event_depth)dispatch_count=0;
        ++event_depth;
        struct Depth { uint32_t& v;~Depth(){--v;} } depth{event_depth};
        std::vector<uint64_t> tokens;
        for(const auto& [id,s]:subscriptions)if(s.topic==view(event->topic)&&s.owner->phase==Phase::Enabled)tokens.push_back(id);
        std::sort(tokens.begin(),tokens.end());
        for(auto id:tokens){auto i=subscriptions.find(id);if(i==subscriptions.end()||i->second.owner->phase!=Phase::Enabled)continue;
            if(++dispatch_count>1024)return EM_LIMIT_EXCEEDED;
            const auto cb=i->second.callback;auto* user=i->second.user;cb(user,event);
        }
        return EM_OK;
    }
    static EmStatus EM_CALL publishEvent(void* p,const EmEvent* event) noexcept {
        try {auto* n=caller(p);if(!n)return EM_INVALID_ARGUMENT;auto& h=*n->host;
            if(!h.onThread())return EM_WRONG_THREAD;if(n->phase!=Phase::Enabled)return EM_NOT_READY;
            if(!header(event)||!valid(event->topic,128))return EM_INVALID_ARGUMENT;
            if(!view(event->topic).starts_with(n->report.id+"."))return EM_CONFLICT;
            return h.dispatch(event);
        }catch(...){return EM_INTERNAL_ERROR;}
    }
    void revoke(Node& n) {
        std::erase_if(services,[&](const auto& x){return x.second.owner==&n;});
        std::erase_if(subscriptions,[&](const auto& x){return x.second.owner==&n;});
    }
    void quarantine(Node& n,std::string reason) {
        if(n.phase==Phase::Quarantined)return;
        revoke(n);n.phase=Phase::Quarantined;n.report.state=ModuleState::Quarantined;n.report.diagnostic=std::move(reason);quarantined=true;
        if(n.library)n.library->quarantine();
        for(const auto& dependency:n.dependencies)for(auto& provider:nodes)
            if(provider->report.id==dependency.id && provider->library && provider->phase!=Phase::Unloaded)
                quarantine(*provider,"Retained because dependent "+n.report.id+" is quarantined");
    }
    bool stop(Node& n,std::string& error) {
        if(n.phase==Phase::Quarantined)return false;
        revoke(n);n.phase=Phase::Disabling;
        auto status=n.disable();
        if(status!=EM_OK){error=n.report.id+": Disable failed ("+std::to_string(status)+")";quarantine(n,error);return false;}
        n.phase=Phase::Disabled;n.report.state=ModuleState::Disabled;return true;
    }
    bool unloadAll(std::string& error) {
        bool ok=true;
        for(auto i=order.rbegin();i!=order.rend();++i){auto& n=**i;
            if(n.phase==Phase::Unloaded)continue;
            if(n.phase==Phase::Quarantined){ok=false;continue;}
            if(n.load_attempted && n.phase!=Phase::Disabled && n.phase!=Phase::Unloaded)if(!stop(n,error)){ok=false;continue;}
            if(n.load_attempted){n.phase=Phase::Unloading;auto result=n.unload();
                if(result!=EM_OK){error=n.report.id+": Unload failed ("+std::to_string(result)+")";quarantine(n,error);ok=false;continue;}}
            n.library.reset();n.load_attempted=false;n.phase=Phase::Unloaded;n.report.state=ModuleState::Unloaded;
        }
        services.clear();subscriptions.clear();loaded=false;enabled=false;return ok;
    }
    bool reject(Node& n,std::string reason,std::string& error) {
        if(n.load_attempted){if(!stop(n,error))return false;n.phase=Phase::Unloading;
            auto result=n.unload();if(result!=EM_OK){error=n.report.id+": rejected module Unload failed";quarantine(n,error);return false;}}
        revoke(n);n.library.reset();n.load_attempted=false;n.phase=Phase::Unloaded;
        n.report.state=ModuleState::Failed;n.report.diagnostic=std::move(reason);
        log(EM_LOG_WARNING,n.report.id+": "+n.report.diagnostic);return true;
    }
    bool descriptor(Node& n,const EmModuleDescriptor* d,std::string& error) {
        if(!header(d)||d->abi_major!=EM_ABI_MAJOR||d->abi_minor>EM_ABI_MINOR||d->version.reserved||d->reserved[0]||d->reserved[1]||(d->flags&~EM_MODULE_PLANNED)) {error="Invalid module ABI descriptor";return false;}
        if(!valid(d->id,64)||!idValid(view(d->id))||view(d->id)!=n.report.id||!valid(d->display_name,128)) {error="Module ID or display name invalid/mismatched";return false;}
        if((d->required_host_capabilities&EM_HOST_CAPABILITIES)!=d->required_host_capabilities) {error="Required Host capabilities missing";return false;}
        if(d->flags&EM_MODULE_PLANNED){error="PLANNED module cannot be enabled";return false;}
        if(d->dependency_count>32||(!d->dependencies&&d->dependency_count)) {error="Invalid dependency count";return false;}
        n.report.display_name=std::string(view(d->display_name));n.report.version=d->version;n.provided=d->provided_capabilities;
        std::unordered_set<std::string> ids;
        for(uint32_t i=0;i<d->dependency_count;++i){const auto& dep=d->dependencies[i];
            if(!header(&dep)||!valid(dep.module_id,64)||!idValid(view(dep.module_id))||dep.minimum_version.reserved||dep.reserved||(dep.flags&~EM_DEPENDENCY_OPTIONAL)||view(dep.module_id)==n.report.id||!ids.emplace(view(dep.module_id)).second){error="Invalid, duplicate or self dependency";return false;}
            n.dependencies.push_back({std::string(view(dep.module_id)),dep.minimum_version,dep.required_capabilities,(dep.flags&EM_DEPENDENCY_OPTIONAL)!=0});}
        return true;
    }
};

Host::Host(std::unique_ptr<LibraryLoader> loader,LogSink log):impl_(std::make_unique<Impl>(std::move(loader),std::move(log))){}
Host::~Host(){std::string error;if(!shutdown(error)&&impl_->quarantined)impl_.release();}
bool Host::discover(const std::filesystem::path& base,const std::vector<ModuleConfig>& config,std::vector<Candidate>& out,std::string& error) {
    try {std::vector<Candidate> result;std::unordered_set<std::string> ids,paths;bool core=false;
        if(config.empty()||config.size()>64){error="Module count must be 1..64";return false;}
        const auto root=std::filesystem::weakly_canonical(std::filesystem::absolute(base));
        for(const auto& m:config){if(!idValid(m.id)||!ids.insert(m.id).second||m.path.empty()||m.path.is_absolute()||m.path.has_root_name()){error="Invalid or duplicate module config";return false;}
            for(const auto& part:m.path)if(part==".."){error="Module path escapes Host directory";return false;}
            const auto path=std::filesystem::weakly_canonical(root/m.path);auto relative=path.lexically_relative(root);
            if(relative.empty()||*relative.begin()==".."||path.extension()!=".dll"||!paths.insert(utf8Path(path)).second){error="Invalid, duplicate or escaping DLL path";return false;}
            if(m.id==EM_CORE_MODULE_ID){if(!m.enabled||!m.required){error="Core must be enabled and required";return false;}core=true;}
            result.push_back({m.id,path,m.enabled,m.required});}
        if(!core){error="Core is required";return false;}out=std::move(result);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Host::load(const std::vector<Candidate>& candidates,std::string& error) {
    auto& h=*impl_;if(!h.onThread()){error="Wrong Host thread";return false;}if(h.loaded||h.busy||h.event_depth||h.quarantined||h.final_stop){error="Host already loaded, dispatching, busy, quarantined or finally stopped";return false;}
    h.busy=true;struct Busy { bool& b;~Busy(){b=false;} } busy{h.busy};
    h.nodes.clear();h.order.clear();
    try {if(candidates.empty()||candidates.size()>64)throw std::runtime_error("Module count must be 1..64");
        std::unordered_map<std::string,Impl::Node*> active;std::unordered_set<std::string> ids,paths;bool core=false;
        for(const auto& c:candidates){if(!idValid(c.id)||!ids.insert(c.id).second||!paths.insert(utf8Path(std::filesystem::absolute(c.path))).second)throw std::runtime_error("Duplicate/invalid module ID or library path");
            if(c.id==EM_CORE_MODULE_ID){if(!c.enabled||!c.required)throw std::runtime_error("Core must be enabled and required");core=true;}
            auto ptr=std::make_unique<Impl::Node>();auto& n=*ptr;n.host=&h;n.report.id=c.id;n.required=c.required||c.id==EM_CORE_MODULE_ID;h.nodes.push_back(std::move(ptr));
            if(!c.enabled){n.report.state=ModuleState::Skipped;n.report.diagnostic="Disabled by config";continue;}
            if(!std::filesystem::exists(c.path)){if(c.required||c.id==EM_CORE_MODULE_ID)throw std::runtime_error(c.id+": required library missing");n.report.state=ModuleState::Skipped;n.report.diagnostic="Optional library missing";continue;}
            std::string failure;
            try {n.library=h.loader->open(c.path,failure);}catch(const std::exception& e){failure=e.what();}
            if(!n.library){if(n.required)throw std::runtime_error(c.id+": "+failure);h.reject(n,failure,error);continue;}
            const auto desc=reinterpret_cast<EmGetDescriptorFn>(n.library->symbol(EM_GET_DESCRIPTOR_EXPORT));
            n.load=reinterpret_cast<EmLoadFn>(n.library->symbol(EM_LOAD_EXPORT));n.enable=reinterpret_cast<EmLifecycleFn>(n.library->symbol(EM_ENABLE_EXPORT));n.disable=reinterpret_cast<EmLifecycleFn>(n.library->symbol(EM_DISABLE_EXPORT));n.unload=reinterpret_cast<EmLifecycleFn>(n.library->symbol(EM_UNLOAD_EXPORT));
            if(!desc||!n.load||!n.enable||!n.disable||!n.unload)failure="missing module export";
            else if(h.descriptor(n,desc(),failure))failure.clear();
            if(!failure.empty()){if(n.required)throw std::runtime_error(c.id+": "+failure);h.reject(n,failure,error);continue;}
            auto base=c.path.parent_path();while(base.filename()!="modules" && base.has_parent_path() && base!=base.parent_path())base=base.parent_path();
            if(base.filename()=="modules")base=base.parent_path();else base=c.path.parent_path();
            n.config=utf8Path(base/"config"/c.id);n.data=utf8Path(base/"data"/c.id);n.resources=utf8Path(base/"resources"/c.id);
            auto v=[](const std::string& s){return EmUtf8View{s.data(),static_cast<uint32_t>(s.size()),0};};
            n.context={sizeof(EmHostContext),EM_STRUCT_VERSION,EM_ABI_MAJOR,EM_ABI_MINOR,EM_HOST_CAPABILITIES,&n,v(n.config),v(n.data),v(n.resources),Impl::logging,Impl::publishService,Impl::queryService,Impl::subscribe,Impl::publishEvent,Impl::unsubscribe,{0,0}};
            active.emplace(c.id,&n);
        }
        if(!core||!active.contains(EM_CORE_MODULE_ID))throw std::runtime_error("Core is required");
        bool changed=true;
        while(changed){changed=false;for(auto& owned:h.nodes){auto& n=*owned;if(!active.contains(n.report.id))continue;std::string failure;
            for(const auto& dep:n.dependencies){auto it=active.find(dep.id);
                if(it==active.end()){if(!dep.optional)failure="required dependency missing: "+dep.id;}
                else if(!versionAtLeast(it->second->report.version,dep.minimum))failure="dependency version too old: "+dep.id;
                else if((dep.caps&it->second->provided)!=dep.caps)failure="dependency capabilities missing: "+dep.id;
                if(!failure.empty())break;}
            if(failure.empty())continue;if(n.required)throw std::runtime_error(n.report.id+": "+failure);
            h.reject(n,failure,error);active.erase(n.report.id);changed=true;}}
        std::vector<Impl::Node*> pending;for(auto& n:h.nodes)if(active.contains(n->report.id))pending.push_back(n.get());
        std::stable_sort(pending.begin(),pending.end(),[](auto* a,auto* b){return a->report.id==EM_CORE_MODULE_ID&&b->report.id!=EM_CORE_MODULE_ID;});
        std::unordered_set<std::string> ordered;
        while(!pending.empty()){auto it=std::find_if(pending.begin(),pending.end(),[&](auto* n){return std::none_of(n->dependencies.begin(),n->dependencies.end(),[&](const auto& dep){return active.contains(dep.id)&&!ordered.contains(dep.id);});});
            if(it==pending.end()){for(auto* n:pending)if(n->required)throw std::runtime_error(n->report.id+": dependency cycle or blocked by cycle");
                for(auto* n:pending){h.reject(*n,"dependency cycle or blocked by cycle",error);active.erase(n->report.id);}break;}
            ordered.insert((*it)->report.id);h.order.push_back(*it);pending.erase(it);}
        for(auto* n:h.order){std::string failure;
            for(const auto& dep:n->dependencies)if(!dep.optional){auto it=active.find(dep.id);if(it==active.end()||it->second->phase!=Impl::Phase::Loaded){failure="dependency Load failed: "+dep.id;break;}}
            if(failure.empty()){n->load_attempted=true;auto result=n->load(&n->context);if(result!=EM_OK)failure="Load failed ("+std::to_string(result)+")";}
            if(!failure.empty()){if(n->required)throw std::runtime_error(n->report.id+": "+failure);
                if(!h.reject(*n,failure,error))throw std::runtime_error(error);active.erase(n->report.id);continue;}
            n->phase=Impl::Phase::Loaded;n->report.state=ModuleState::Loaded;}
        h.loaded=true;return true;
    }catch(const std::exception& e){error=e.what();}catch(...){error="Module load boundary failure";}
    std::string cleanup;h.unloadAll(cleanup);if(!cleanup.empty())error+="; "+cleanup;
    for(auto& n:h.nodes)if(n->library && n->phase!=Impl::Phase::Quarantined)n->library.reset();return false;
}
bool Host::bindFirstEnableThread(std::string& error) {
    auto& h=*impl_;
    if(!h.loaded||h.quarantined||h.enabled||h.busy||h.event_depth||h.enable_attempted||h.thread_handoff||h.final_stop||!h.services.empty()||!h.subscriptions.empty()) {
        error="Host thread handoff requires a quiescent Load before the first Enable attempt";return false;
    }
#ifdef _WIN32
    HANDLE thread_handle{};
    if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&thread_handle,SYNCHRONIZE,FALSE,0)) {
        error="Cannot retain server thread synchronization handle: "+std::to_string(GetLastError());return false;
    }
    h.enable_thread_handle=thread_handle;
#endif
    h.thread=std::this_thread::get_id();h.thread_handoff=true;return true;
}
bool Host::onBoundThread() const noexcept { return impl_->onThread(); }
bool Host::finishStopAfterServerThreadExit(std::string& error) {
    auto& h=*impl_;
#ifdef _WIN32
    // Verify the retained OS handle before reading mutable lifecycle state.
    // A stop request or an empty event queue does not prove owner-thread exit.
    if(!h.enable_thread_handle||WaitForSingleObject(static_cast<HANDLE>(h.enable_thread_handle),0)!=WAIT_OBJECT_0) {
        error="Server thread exit is not confirmed";return false;
    }
#else
    error="Terminal thread handoff requires a Windows synchronization handle";return false;
#endif
    if(h.final_stop) { if(!h.onThread()){error="Wrong Host terminal thread";return false;}return !h.quarantined; }
    if(!h.loaded||h.busy||h.event_depth||h.quarantined) { error="Host cannot complete terminal stop while unloaded, busy, dispatching or quarantined";return false; }
    h.thread=std::this_thread::get_id();h.final_stop=true;return shutdown(error);
}
bool Host::enable(std::string& error) {
    auto& h=*impl_;if(!h.onThread()){error="Wrong Host thread";return false;}if(h.busy||h.event_depth){error="Host lifecycle busy or dispatching";return false;}if(h.enabled)return true;if(!h.loaded||h.quarantined){error="Host not loaded or quarantined";return false;}
    if(h.final_stop){error="Host has finally stopped";return false;}
    h.enable_attempted=true;
    h.busy=true;struct Busy { bool& b;~Busy(){b=false;} } busy{h.busy};
    for(auto* n:h.order){if(n->phase==Impl::Phase::Unloaded)continue;std::string failure;
        for(const auto& dep:n->dependencies)if(!dep.optional){auto provider=std::find_if(h.order.begin(),h.order.end(),[&](auto* p){return p->report.id==dep.id;});if(provider==h.order.end()||(*provider)->phase!=Impl::Phase::Enabled){failure="dependency Enable failed: "+dep.id;break;}}
        EmStatus result=EM_OK;if(failure.empty()){n->phase=Impl::Phase::Enabling;result=n->enable();if(result!=EM_OK)failure="Enable failed ("+std::to_string(result)+")";}
        if(!failure.empty()){if(!n->required){std::string cleanup;if(h.reject(*n,failure,cleanup))continue;error=cleanup;}
            else error=n->report.id+": "+failure;
            for(auto i=h.order.rbegin();i!=h.order.rend();++i)if((*i)->phase==Impl::Phase::Enabled||(*i)->phase==Impl::Phase::Enabling){std::string cleanup;if(!h.stop(**i,cleanup)&&!cleanup.empty())error+="; "+cleanup;}return false;}
        n->phase=Impl::Phase::Enabled;n->report.state=ModuleState::Enabled;}
    h.enabled=true;return true;
}
bool Host::disable(std::string& error) {
    auto& h=*impl_;if(!h.onThread()){error="Wrong Host thread";return false;}if(h.busy||h.event_depth){error="Host lifecycle busy or dispatching";return false;}if(!h.loaded)return !h.quarantined;
    h.busy=true;struct Busy { bool& b;~Busy(){b=false;} } busy{h.busy};bool ok=!h.quarantined;
    for(auto i=h.order.rbegin();i!=h.order.rend();++i)if((*i)->phase==Impl::Phase::Enabled||(*i)->phase==Impl::Phase::Enabling)if(!h.stop(**i,error))ok=false;
    h.enabled=false;return ok;
}
bool Host::shutdown(std::string& error) {
    auto& h=*impl_;if(!h.onThread()){error="Wrong Host thread";for(auto& n:h.nodes)if(n->library)h.quarantine(*n,error);return false;}
    if(h.busy||h.event_depth){error="Host lifecycle busy or dispatching";return false;}
    disable(error);return h.unloadAll(error);
}
Snapshot Host::snapshot() const {
    const auto& h=*impl_;Snapshot result{h.loaded,h.enabled,h.quarantined,{},h.services.size(),h.subscriptions.size()};
    for(const auto& n:h.nodes)result.modules.push_back(n->report);return result;
}
EmStatus Host::queryService(const EmServiceRequest& request,EmServiceReference& out) noexcept {try{return impl_->query(&request,&out);}catch(...){return EM_INTERNAL_ERROR;}}
EmStatus Host::publishEvent(const EmEvent& event) noexcept {try{if(!impl_->onThread())return EM_WRONG_THREAD;if(!impl_->enabled)return EM_NOT_READY;return impl_->dispatch(&event);}catch(...){return EM_INTERNAL_ERROR;}}
const char* stateName(ModuleState state) noexcept {
    switch(state){case ModuleState::Disabled:return "disabled";case ModuleState::Discovered:return "discovered";case ModuleState::Loaded:return "loaded";case ModuleState::Enabled:return "enabled";case ModuleState::Failed:return "failed";case ModuleState::Quarantined:return "quarantined";case ModuleState::Skipped:return "skipped";case ModuleState::Unloaded:return "unloaded";}return "unknown";
}
} // namespace eternal::host
