#include "../../host/EternalHost/runtime/Host.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace eternal::host;
namespace {
struct Fixture {
    std::string id;
    EmModuleDescriptor descriptor{};
    std::array<EmDependency,4> dependencies{};
    EmHostContext context{};
    EmStatus load_result{EM_OK},enable_result{EM_OK},disable_result{EM_OK},unload_result{EM_OK};
    int loads{},enables{},disables{},unloads{},closes{},quarantines{},events{};
    bool subscribe{},self_unsubscribe{},loop{},throw_load{};
    uint64_t token{};
    EmStatus callback_result{EM_OK};
    std::string service;
};
std::array<Fixture,3> f;
std::vector<std::string> trace;
std::filesystem::path folder;
Host* lifecycle_probe{};
bool shutdown_rejected{},disable_rejected{};
const uint32_t table=17;
EmUtf8View view(std::string_view s){return {s.data(),static_cast<uint32_t>(s.size()),0};}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void reset() {
    trace.clear();
    for(size_t i=0;i<f.size();++i){f[i]=Fixture{};auto& x=f[i];x.id=i==0?"core":i==1?"addon":"extra";
        x.service=x.id+".status";
        x.descriptor={sizeof(EmModuleDescriptor),EM_STRUCT_VERSION,view(x.id),view(x.id),{0,1,0,0},EM_ABI_MAJOR,EM_ABI_MINOR,EM_HOST_CAPABILITIES,0,1,nullptr,0,0,{0,0}};
    }
}
void depend(size_t who,size_t on,bool optional=false,uint64_t caps=0) {
    auto& x=f[who];auto& d=x.dependencies[x.descriptor.dependency_count++];
    d={sizeof(EmDependency),EM_STRUCT_VERSION,view(f[on].id),{0,1,0,0},caps,optional?EM_DEPENDENCY_OPTIONAL:0,0};x.descriptor.dependencies=x.dependencies.data();
}
void EM_CALL eventCallback(void* user,const EmEvent* event) noexcept {
    auto& x=*static_cast<Fixture*>(user);++x.events;
    if(lifecycle_probe){std::string e;shutdown_rejected=!lifecycle_probe->shutdown(e);disable_rejected=!lifecycle_probe->disable(e);}
    if(x.self_unsubscribe)x.callback_result=x.context.unsubscribe(x.context.instance,x.token);
    if(x.loop)x.callback_result=x.context.publish_event(x.context.instance,event);
}
template<size_t I> const EmModuleDescriptor* EM_CALL descriptor() noexcept{return &f[I].descriptor;}
template<size_t I> EmStatus EM_CALL load(const EmHostContext* context) noexcept {
    auto& x=f[I];++x.loads;trace.push_back("load:"+x.id);
    try {if(x.throw_load)throw std::runtime_error("module exception");x.context=*context;return x.load_result;}catch(...){return EM_INTERNAL_ERROR;}
}
template<size_t I> EmStatus EM_CALL enable() noexcept {
    auto& x=f[I];++x.enables;trace.push_back("enable:"+x.id);
    EmServiceOffer offer{sizeof(EmServiceOffer),EM_STRUCT_VERSION,view(x.service),1,0,0,&table,sizeof(table),0};
    auto result=x.context.publish_service(x.context.instance,&offer);if(result!=EM_OK)return result;
    if(x.subscribe){EmSubscription sub{sizeof(sub),EM_STRUCT_VERSION,view("core.tick"),eventCallback,&x,0};result=x.context.subscribe(x.context.instance,&sub,&x.token);if(result!=EM_OK)return result;}
    return x.enable_result;
}
template<size_t I> EmStatus EM_CALL disable() noexcept {auto& x=f[I];++x.disables;trace.push_back("disable:"+x.id);return x.disable_result;}
template<size_t I> EmStatus EM_CALL unload() noexcept {auto& x=f[I];++x.unloads;trace.push_back("unload:"+x.id);return x.unload_result;}
const std::array<EmGetDescriptorFn,3> descriptors{descriptor<0>,descriptor<1>,descriptor<2>};
const std::array<EmLoadFn,3> loads{load<0>,load<1>,load<2>};
const std::array<EmLifecycleFn,3> enables{enable<0>,enable<1>,enable<2>},disables{disable<0>,disable<1>,disable<2>},unloads{unload<0>,unload<1>,unload<2>};
class FakeLibrary final:public Library {
    size_t index_;bool quarantined_{};
public:
    explicit FakeLibrary(size_t i):index_(i){}
    ~FakeLibrary()override{if(!quarantined_)++f[index_].closes;}
    void* symbol(const char* name)noexcept override {
        const std::string_view n(name);
        if(n==EM_GET_DESCRIPTOR_EXPORT)return reinterpret_cast<void*>(descriptors[index_]);
        if(n==EM_LOAD_EXPORT)return reinterpret_cast<void*>(loads[index_]);
        if(n==EM_ENABLE_EXPORT)return reinterpret_cast<void*>(enables[index_]);
        if(n==EM_DISABLE_EXPORT)return reinterpret_cast<void*>(disables[index_]);
        if(n==EM_UNLOAD_EXPORT)return reinterpret_cast<void*>(unloads[index_]);return nullptr;
    }
    void quarantine()noexcept override{quarantined_=true;++f[index_].quarantines;}
};
class FakeLoader final:public LibraryLoader {
public:
    bool throw_open{};
    std::unique_ptr<Library> open(const std::filesystem::path& path,std::string&)override {
        if(throw_open)throw std::runtime_error("loader exception");
        for(size_t i=0;i<f.size();++i)if(path.stem().string()==f[i].id)return std::make_unique<FakeLibrary>(i);return {};
    }
};
std::vector<Candidate> candidates(bool extra=false){std::vector<Candidate> result{{"addon",folder/"addon.dll",true,true},{"core",folder/"core.dll",true,true}};if(extra)result.push_back({"extra",folder/"extra.dll",true,false});return result;}
Host host(){return Host(std::make_unique<FakeLoader>());}
EmServiceRequest request(std::string_view id){return {sizeof(EmServiceRequest),EM_STRUCT_VERSION,view(id),1,0,0,0};}
EmServiceReference reference(){EmServiceReference r{};r.struct_size=sizeof(r);r.struct_version=EM_STRUCT_VERSION;return r;}
EmEvent event(){return {sizeof(EmEvent),EM_STRUCT_VERSION,view("core.tick"),nullptr,0,1,0};}
template<class F>void test(const char* name,F fn){reset();fn();std::cout<<"PASS: "<<name<<'\n';}
}
int main(int argc,char**) {
    if(argc!=1){std::cerr<<"HostRuntimeTests is a mock runtime suite; use ModuleArtifactTests for real DLLs\n";return 2;}
    try {
        folder=std::filesystem::temp_directory_path()/"eternal-host-runtime-fixtures";
        std::filesystem::create_directories(folder);
        for(auto id:{"core","addon","extra"})std::ofstream(folder/(std::string(id)+".dll"))<<"mock";
        test("dependency order, reverse shutdown and idempotent shutdown",[]{depend(1,0);auto h=host();std::string error;require(h.load(candidates(),error),error.c_str());require(trace==std::vector<std::string>{"load:core","load:addon"},"Wrong dependency load order");require(h.enable(error),error.c_str());require(h.shutdown(error),error.c_str());require(h.shutdown(error),"Repeated shutdown failed");require(f[0].unloads==1&&f[1].unloads==1&&f[0].closes==1&&f[1].closes==1,"Double unload/free");require(trace[4]=="disable:addon"&&trace[5]=="disable:core"&&trace[6]=="unload:addon","Wrong reverse order");});
        test("disable revokes service and event, enable republishes with new generation",[]{depend(1,0);f[0].subscribe=true;auto h=host();std::string error;require(h.load(candidates(),error)&&h.enable(error),error.c_str());auto r=reference();require(h.queryService(request("core.status"),r)==EM_OK,"Service unavailable");auto old=r.generation;require(h.enable(error)&&f[0].enables==1,"Repeated enable called module twice");require(h.disable(error),error.c_str());require(h.snapshot().service_count==0&&h.snapshot().subscription_count==0,"Disable did not revoke");require(h.queryService(request("core.status"),r)==EM_NOT_FOUND,"Stale service visible");require(h.publishEvent(event())==EM_NOT_READY,"Disabled event accepted");require(f[0].context.publish_service(f[0].context.instance,nullptr)==EM_NOT_READY,"Old context remained active");require(h.enable(error),error.c_str());require(h.queryService(request("core.status"),r)==EM_OK&&r.generation>old&&f[0].enables==2,"Service not republished");require(h.publishEvent(event())==EM_OK&&f[0].events==1,"Event subscription not renewed");});
        test("Core mandatory, disabled and missing required refuse",[]{auto h=host();std::string e;require(!h.load({{"addon",folder/"addon.dll",true,false}},e),"Missing core accepted");auto c=candidates();c[1].enabled=false;require(!h.load(c,e),"Disabled Core accepted");c[1].enabled=true;c[1].path=folder/"absent.dll";require(!h.load(c,e),"Missing Core accepted");});
        test("missing optional DLL and dependency safely skip",[]{depend(1,2,true);auto c=candidates();c.push_back({"absent",folder/"absent.dll",true,false});auto h=host();std::string e;require(h.load(c,e)&&h.enable(e),e.c_str());require(h.snapshot().modules.back().state==ModuleState::Skipped,"Optional missing not skipped");});
        test("missing required dependency and cycle before any Load",[]{depend(1,2);auto h=host();std::string e;require(!h.load(candidates(),e)&&f[0].loads==0,"Missing dependency activated modules");reset();depend(0,1);depend(1,0);require(!h.load(candidates(),e)&&f[0].loads==0,"Cycle activated modules");});
        test("descriptor ABI and required capabilities are enforced",[]{auto h=host();std::string e;f[1].descriptor.abi_major=2;require(!h.load(candidates(),e)&&f[0].loads==0,"Bad ABI accepted");reset();f[1].descriptor.required_host_capabilities=UINT64_C(1)<<20;require(!h.load(candidates(),e),"Missing Host capability accepted");reset();depend(1,0,false,2);require(!h.load(candidates(),e),"Missing provider capability accepted");});
        test("dependency semver, unknown flags, duplicate ID and PLANNED refuse",[]{depend(1,0);f[1].dependencies[0].minimum_version={1,0,0,0};auto h=host();std::string e;require(!h.load(candidates(),e),"Old version accepted");reset();f[1].descriptor.flags=4;require(!h.load(candidates(),e),"Unknown flags accepted");reset();f[1].descriptor.flags=EM_MODULE_PLANNED;require(!h.load(candidates(),e)&&f[1].loads==0,"Planned module activated");reset();auto c=candidates();c.push_back(c.front());require(!h.load(c,e),"Duplicate ID accepted");});
        test("failed Load cleans failing module and earlier dependencies",[]{depend(1,0);f[1].throw_load=true;auto h=host();std::string e;require(!h.load(candidates(),e),"Load exception falsely passed");require(f[0].disables==1&&f[1].disables==1&&f[0].unloads==1&&f[1].unloads==1&&f[0].closes==1&&f[1].closes==1,"Incomplete failure cleanup");});
        test("failed Enable cleans published services and allows retry",[]{depend(1,0);f[1].enable_result=EM_INTERNAL_ERROR;auto h=host();std::string e;require(h.load(candidates(),e)&&!h.enable(e),"Enable failure not handled");require(h.snapshot().service_count==0&&!h.snapshot().enabled,"Half-ready service leaked");f[1].enable_result=EM_OK;require(h.enable(e),e.c_str());});
        test("loader exception cannot escape Host boundary",[]{auto loader=std::make_unique<FakeLoader>();loader->throw_open=true;Host h(std::move(loader));std::string e;require(!h.load(candidates(),e)&&!e.empty(),"Loader exception escaped or passed");});
        test("service namespace and declared dependencies prevent provider spoof",[]{auto h=host();std::string e;require(h.load(candidates(),e)&&h.enable(e),e.c_str());auto r=reference();auto req=request("core.status");require(f[1].context.query_service(f[1].context.instance,&req,&r)==EM_CONFLICT,"Undeclared provider accessed");require(h.disable(e),e.c_str());f[1].service=EM_CORE_SERVICE_ID;require(!h.enable(e)&&h.snapshot().service_count==0,"Core provider spoof accepted");});
        test("synchronous event mutation and payload limits",[]{depend(1,0);f[0].subscribe=true;f[1].subscribe=true;f[0].self_unsubscribe=true;auto h=host();std::string e;require(h.load(candidates(),e)&&h.enable(e),e.c_str());require(h.publishEvent(event())==EM_OK&&h.publishEvent(event())==EM_OK,"Event dispatch failed");require(f[0].events==1&&f[1].events==2,"Unsubscribe mutation unsafe");auto huge=event();huge.data_size=65537;require(h.publishEvent(huge)==EM_INVALID_ARGUMENT,"Payload limit not enforced");});
        test("event recursion budget and bound module thread including unsubscribe",[]{f[0].subscribe=true;f[0].loop=true;auto h=host();std::string e;require(h.load(candidates(),e)&&h.enable(e),e.c_str());require(h.publishEvent(event())==EM_OK&&f[0].events==8,"Recursion budget failed");EmStatus result=EM_OK,removed=EM_OK;std::thread worker([&]{result=f[0].context.log(f[0].context.instance,EM_LOG_INFO,view("worker"));removed=f[0].context.unsubscribe(f[0].context.instance,f[0].token);});worker.join();require(result==EM_WRONG_THREAD&&removed==EM_WRONG_THREAD&&h.snapshot().subscription_count==1,"Foreign thread accepted or removed subscription");require(f[0].context.unsubscribe(f[0].context.instance,f[0].token)==EM_OK&&h.snapshot().subscription_count==0,"Owner thread cannot unsubscribe");});
        test("event callback cannot disable or unload its active DLL stack",[]{f[0].subscribe=true;auto h=host();std::string e;require(h.load(candidates(),e)&&h.enable(e),e.c_str());lifecycle_probe=&h;require(h.publishEvent(event())==EM_OK,"Event failed");lifecycle_probe=nullptr;require(shutdown_rejected&&disable_rejected&&h.snapshot().enabled&&f[0].closes==0,"Active callback image could unload");require(h.shutdown(e),e.c_str());});
        test("quarantine retains provider closure and prevents unload",[]{depend(1,0);f[1].disable_result=EM_INTERNAL_ERROR;auto h=host();std::string e;require(h.load(candidates(),e)&&h.enable(e),e.c_str());require(!h.disable(e)&&h.snapshot().quarantined,"Disable failure not quarantined");require(h.snapshot().service_count==0&&f[0].quarantines==1&&f[1].quarantines==1,"Provider closure not retained/revoked");require(!h.shutdown(e)&&f[0].unloads==0&&f[1].unloads==0&&f[0].closes==0&&f[1].closes==0,"Unsafe FreeLibrary after failure");});
        test("optional invalid ABI does not unload healthy Core",[]{f[1].descriptor.abi_major=2;auto c=candidates();c[0].required=false;auto h=host();std::string e;require(h.load(c,e)&&h.enable(e),e.c_str());require(f[0].loads==1&&f[0].enables==1&&f[0].closes==0&&f[1].loads==0,"Optional ABI failure harmed Core");require(h.snapshot().modules[0].state==ModuleState::Failed,"Rejected module not diagnosed");});
        test("optional failed Load and Enable reject only affected branch",[]{depend(1,0);f[1].load_result=EM_INTERNAL_ERROR;auto c=candidates();c[0].required=false;auto h=host();std::string e;require(h.load(c,e)&&h.enable(e)&&f[0].closes==0&&f[1].unloads==1,"Optional Load failure harmed Core");require(h.shutdown(e),e.c_str());reset();depend(1,0);f[1].enable_result=EM_INTERNAL_ERROR;auto h2=host();require(h2.load(c,e)&&h2.enable(e)&&f[0].closes==0&&f[1].unloads==1,"Optional Enable failure harmed Core");});
        test("optional cycles and consumers reject without Core failure",[]{depend(1,2);depend(2,1);auto c=candidates(true);c[0].required=false;auto h=host();std::string e;require(h.load(c,e)&&h.enable(e)&&f[0].enables==1&&f[1].loads==0&&f[2].loads==0,"Optional cycle harmed Core");});
        test("one explicit startup-to-server-thread handoff preserves callback affinity",[]{
            depend(1,0);f[0].subscribe=true;auto h=host();std::string e;require(h.load(candidates(),e),e.c_str());
            std::promise<void> ready,finish;auto ready_wait=ready.get_future();auto finish_wait=finish.get_future();
            bool initial_wrong{},bound{},duplicate_rejected{},enabled{},callback_ok{},query_ok{},event_ok{},stopped{};
            std::thread server([&]{std::string error;initial_wrong=!h.enable(error)&&error=="Wrong Host thread";
                bound=h.bindFirstEnableThread(error);duplicate_rejected=!h.bindFirstEnableThread(error);enabled=h.enable(error);
                callback_ok=f[0].context.log(f[0].context.instance,EM_LOG_INFO,view("server"))==EM_OK;
                auto r=reference();query_ok=h.queryService(request("core.status"),r)==EM_OK;event_ok=h.publishEvent(event())==EM_OK;
                ready.set_value();finish_wait.wait();stopped=h.disable(error)&&h.enable(error)&&h.shutdown(error);
            });
            ready_wait.wait();const auto old_callback=f[0].context.log(f[0].context.instance,EM_LOG_INFO,view("startup"));
            auto r=reference();const auto old_query=h.queryService(request("core.status"),r);
            const bool running_rebind_rejected=!h.bindFirstEnableThread(e);const bool old_enable_rejected=!h.enable(e);
            finish.set_value();server.join();
            require(initial_wrong&&bound&&duplicate_rejected&&enabled&&callback_ok&&query_ok&&event_ok&&stopped,"Explicit lifecycle thread handoff failed");
            require(old_callback==EM_WRONG_THREAD&&old_query==EM_WRONG_THREAD&&running_rebind_rejected&&old_enable_rejected,"Running Host rebound or old thread retained authority");
            require(f[0].closes==1&&f[1].closes==1,"Handoff teardown failed");
        });
        test("thread handoff rejects unloaded, failed-Enable and disabled Hosts",[]{
            auto h=host();std::string e;require(!h.bindFirstEnableThread(e),"Unloaded Host rebound");
            require(h.load(candidates(),e),e.c_str());f[1].enable_result=EM_INTERNAL_ERROR;require(!h.enable(e)&&h.snapshot().service_count==0,"Fixture Enable did not fail cleanly");
            bool rejected{};std::thread foreign([&]{std::string error;rejected=!h.bindFirstEnableThread(error);});foreign.join();
            require(rejected,"Failed Enable reopened thread handoff");f[1].enable_result=EM_OK;require(h.enable(e)&&h.disable(e),e.c_str());
            require(!h.bindFirstEnableThread(e),"Disabled Host rebound after its first Enable");require(h.shutdown(e),e.c_str());
        });
#ifdef _WIN32
        test("terminal stop refuses a live server thread and ordinary Disable cannot migrate",[]{
            depend(1,0);auto h=host();std::string e;require(h.load(candidates(),e),e.c_str());
            std::promise<void> ready,finish;auto ready_wait=ready.get_future();auto finish_wait=finish.get_future();bool enabled{},callback_ok{};
            std::thread server([&]{std::string error;enabled=h.bindFirstEnableThread(error)&&h.enable(error);ready.set_value();finish_wait.wait();
                callback_ok=h.onBoundThread()&&f[0].context.log(f[0].context.instance,EM_LOG_INFO,view("still server"))==EM_OK;
            });
            ready_wait.wait();const bool live_rejected=!h.finishStopAfterServerThreadExit(e)&&e=="Server thread exit is not confirmed";
            const bool ordinary_rejected=!h.disable(e)&&e=="Wrong Host thread";const bool affinity_unchanged=!h.onBoundThread();
            finish.set_value();server.join();require(enabled&&live_rejected&&ordinary_rejected&&affinity_unchanged&&callback_ok,"Live thread migration was allowed");
            require(h.finishStopAfterServerThreadExit(e),e.c_str());
        });
        test("joined server thread permits one permanent reverse Disable and Unload",[]{
            depend(1,0);f[0].subscribe=true;auto h=host();std::string e;require(h.load(candidates(),e),e.c_str());bool enabled{};
            std::thread server([&]{std::string error;enabled=h.bindFirstEnableThread(error)&&h.enable(error);});server.join();
            require(enabled&&h.finishStopAfterServerThreadExit(e),e.c_str());const auto stopped=h.snapshot();
            require(h.onBoundThread()&&!stopped.loaded&&!stopped.enabled&&stopped.service_count==0&&stopped.subscription_count==0,"Final stop retained active registrations");
            require(f[0].disables==1&&f[1].disables==1&&f[0].unloads==1&&f[1].unloads==1&&f[0].closes==1&&f[1].closes==1,"Terminal cleanup incomplete");
            require(trace[4]=="disable:addon"&&trace[5]=="disable:core"&&trace[6]=="unload:addon"&&trace[7]=="unload:core","Final cleanup order unsafe");
            require(!h.enable(e)&&!h.load(candidates(),e)&&!h.bindFirstEnableThread(e),"Terminal Host reopened");
            require(h.finishStopAfterServerThreadExit(e)&&h.shutdown(e)&&f[0].closes==1,"Repeated terminal stop was not idempotent");
        });
        test("terminal stop without OS proof refuses and failing cleanup quarantines providers",[]{
            auto unbound=host();std::string e;require(unbound.load(candidates(),e)&&!unbound.finishStopAfterServerThreadExit(e),"Missing thread proof accepted");require(unbound.shutdown(e),e.c_str());
            reset();depend(1,0);f[1].disable_result=EM_INTERNAL_ERROR;auto h=host();require(h.load(candidates(),e),e.c_str());bool enabled{};
            std::thread server([&]{std::string error;enabled=h.bindFirstEnableThread(error)&&h.enable(error);});server.join();
            require(enabled&&!h.finishStopAfterServerThreadExit(e)&&h.snapshot().quarantined,"Unsafe terminal teardown claimed success");
            require(f[0].quarantines==1&&f[1].quarantines==1&&f[0].unloads==0&&f[1].unloads==0&&f[0].closes==0&&f[1].closes==0,"Quarantined image/provider was freed");
            require(!h.enable(e)&&!h.finishStopAfterServerThreadExit(e),"Quarantined terminal Host reopened");
        });
#endif
        for(auto id:{"core","addon","extra"})std::filesystem::remove(folder/(std::string(id)+".dll"));
        std::filesystem::remove(folder);
#ifdef _WIN32
        std::cout<<"PASS: 23 Host runtime behavior groups (mock modules; no BDS claim)\n";
#else
        std::cout<<"PASS: 20 Host runtime behavior groups (mock modules; terminal handoff unsupported; no BDS claim)\n";
#endif
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
