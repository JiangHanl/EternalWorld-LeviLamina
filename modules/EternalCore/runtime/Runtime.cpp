#include "Runtime.hpp"
#include "../domain/Core.hpp"
#include "../domain/Sha256.hpp"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <vector>
#pragma comment(lib,"bcrypt.lib")

namespace eternal::core::runtime {
namespace {
using Json=nlohmann::json;
template<class T> T dto(){T value{};value.struct_size=sizeof(T);value.struct_version=1;return value;}
template<class T> bool valid(const T* value){return value&&value->struct_size==sizeof(T)&&value->struct_version==1;}
bool equal(EcId128 a,EcId128 b){return a.low==b.low&&a.high==b.high;}
bool zero(EcId128 a){return !a.low&&!a.high;}
bool zero(EcUuid a){for(auto b:a.bytes)if(b)return false;return true;}
bool equal(EcUuid a,EcUuid b){return !std::memcmp(a.bytes,b.bytes,16);}
std::uint64_t wall(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
std::uint64_t steady(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
EcId128 randomId(){EcId128 value{};if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||zero(value))throw std::runtime_error("Secure random source unavailable");return value;}
std::string hex64(std::uint64_t value){std::array<char,16> out{};out.fill('0');std::array<char,16> raw{};auto end=std::to_chars(raw.data(),raw.data()+raw.size(),value,16).ptr;std::copy(raw.data(),end,out.end()-(end-raw.data()));return {out.data(),out.size()};}
std::string id(EcId128 value){return hex64(value.high)+hex64(value.low);}
EcId128 parseId(std::string_view value){EcId128 out{};if(value.size()!=32)return out;auto a=std::from_chars(value.data(),value.data()+16,out.high,16);auto b=std::from_chars(value.data()+16,value.data()+32,out.low,16);if(a.ec!=std::errc{}||a.ptr!=value.data()+16||b.ec!=std::errc{}||b.ptr!=value.data()+32)return {};return out;}
EcId128 keyId(std::string_view key){return parseId(detail::sha256(key).substr(0,32));}
std::string uuid(EcUuid value){constexpr char chars[]="0123456789abcdef";std::string out;for(unsigned i=0;i<16;++i){if(i==4||i==6||i==8||i==10)out+='-';out+=chars[value.bytes[i]>>4];out+=chars[value.bytes[i]&15];}return out;}
EcUuid uuid(std::string_view value){EcUuid out{};std::string flat;for(char c:value)if(c!='-')flat+=c;if(flat.size()!=32)return out;for(unsigned i=0;i<16;++i){unsigned byte=0;auto result=std::from_chars(flat.data()+i*2,flat.data()+i*2+2,byte,16);if(result.ec!=std::errc{}||result.ptr!=flat.data()+i*2+2)return {};out.bytes[i]=static_cast<uint8_t>(byte);}return out;}
EcId128 playerId(std::string_view value){return value.starts_with("player_")?parseId(value.substr(7)):EcId128{};}
std::string playerKey(EcId128 value){return "player_"+id(value);}
bool textValid(std::string_view value,std::size_t limit,bool empty=false){
    if(value.size()>limit||(!empty&&value.empty()))return false;
    for(std::size_t i=0;i<value.size();){auto byte=static_cast<unsigned char>(value[i++]);if(byte<32||byte==127)return false;if(byte<128)continue;
        unsigned count=0;std::uint32_t scalar=0,min=0;
        if(byte>=0xc2&&byte<=0xdf){count=1;scalar=byte&31;min=128;}else if(byte>=0xe0&&byte<=0xef){count=2;scalar=byte&15;min=2048;}else if(byte>=0xf0&&byte<=0xf4){count=3;scalar=byte&7;min=65536;}else return false;
        if(i+count>value.size())return false;while(count--){auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)return false;scalar=(scalar<<6)|(next&63);}
        if(scalar<min||scalar>0x10ffff||(scalar>=0x80&&scalar<=0x9f)||(scalar>=0xd800&&scalar<=0xdfff))return false;
    }return true;
}
std::optional<std::string> text(EcUtf8View value,std::size_t maximum,bool empty=false){if(value.reserved||value.length>maximum||(!value.data&&value.length))return {};std::string out(value.data?value.data:"",value.length);if(!textValid(out,maximum,empty))return {};return out;}
EcStatus buffer(EcUtf8Buffer* out,std::string_view value){if(!out||(!out->data&&out->capacity))return EC_INVALID_ARGUMENT;out->required=static_cast<uint32_t>(value.size()+1);if(!out->data||out->capacity<out->required)return EC_BUFFER_TOO_SMALL;std::memcpy(out->data,value.data(),value.size());out->data[value.size()]=0;return EC_OK;}
EcStatus status(Status value){switch(value){case Status::Ok:return EC_OK;case Status::Busy:return EC_PENDING;case Status::Invalid:return EC_INVALID_ARGUMENT;case Status::Overflow:return EC_LIMIT_EXCEEDED;case Status::PermissionDenied:return EC_DENIED;case Status::Conflict:return EC_CONFLICT;case Status::NotFound:return EC_NOT_FOUND;case Status::InsufficientFunds:return EC_INSUFFICIENT_COIN;default:return EC_STORAGE_FAILURE;}}
Status denialStatus(EcStatus value){if(value==EC_CONFLICT)return Status::Conflict;if(value==EC_NOT_FOUND)return Status::NotFound;if(value==EC_STORAGE_FAILURE)return Status::StorageError;if(value==EC_INVALID_ARGUMENT||value==EC_ABI_MISMATCH||value==EC_INVALID_UTF8)return Status::Invalid;return Status::PermissionDenied;}
Asset asset(std::uint32_t value){return value==EC_P2_ASSET_REPUTATION?Asset::Reputation:Asset::Coin;}
std::uint64_t permissionBit(std::uint32_t operation){return operation>=1&&operation<=11?UINT64_C(1)<<(operation-1):0;}
std::uint64_t moduleCap(std::uint32_t operation,std::uint32_t which){if(operation==EC_P2_OP_IDENTITY_READ||operation==EC_P2_OP_ROLE_READ)return EC_MODULE_CAP_PLAYER_READ;if(operation==EC_P2_OP_PERMISSION_CHECK||operation==EC_P2_OP_ROLE_GRANT||operation==EC_P2_OP_ROLE_REVOKE)return EC_MODULE_CAP_PERMISSION_CHECK;if(operation==EC_P2_OP_OUTBOX_READ)return EC_MODULE_CAP_EVENTS;if(operation==EC_P2_OP_RECEIPT_READ)return EC_MODULE_CAP_AUDIT;return which==EC_P2_ASSET_REPUTATION?EC_MODULE_CAP_REPUTATION:EC_MODULE_CAP_MONEY;}
Permission permission(std::uint32_t operation,std::uint32_t which){switch(operation){case EC_P2_OP_IDENTITY_READ:return Permission::IdentityRead;case EC_P2_OP_RECEIPT_READ:case EC_P2_OP_OUTBOX_READ:case EC_P2_OP_ROLE_READ:case EC_P2_OP_PERMISSION_CHECK:return Permission::PermissionRead;case EC_P2_OP_ROLE_GRANT:case EC_P2_OP_ROLE_REVOKE:return Permission::RoleManage;case EC_P2_OP_ASSET_READ:return which==EC_P2_ASSET_REPUTATION?Permission::ReputationRead:Permission::MoneyRead;case EC_P2_OP_TRANSFER:return Permission::MoneyTransfer;default:return which==EC_P2_ASSET_REPUTATION?Permission::ReputationAdjust:Permission::MoneyAdjust;}}
std::optional<Role> role(std::string_view value){constexpr std::array<std::string_view,13> names{"Build","Economy","Law","Content","Resources","Operations","Owner","Administrator","ContentManager","EconomyManager","Builder","Moderator","Player"};for(std::size_t i=0;i<names.size();++i)if(names[i]==value)return static_cast<Role>(i);return {};}
std::string operationName(std::uint32_t op){switch(op){case EC_P2_OP_ASSET_ADD:return "asset.add";case EC_P2_OP_ASSET_DEDUCT:return "asset.deduct";case EC_P2_OP_TRANSFER:return "money.transfer";case EC_P2_OP_ROLE_GRANT:return "role.grant";case EC_P2_OP_ROLE_REVOKE:return "role.revoke";default:return "core.query";}}
}

struct Runtime::Impl {
    struct Session{Actor actor;std::uint64_t generation;bool online;};
    struct Binding{EternalCorePhase2Api table{};std::string module;std::uint64_t caps{},permissions{},generation{},period{60000},window{},issuedAt{};std::int64_t moneyLimit{},repLimit{},moneyBudget{},repBudget{},moneySpent{},repSpent{};bool active{true};};
    struct Capability{EcCapability token;std::string binding,actorUuid,target,recipient;std::uint64_t identityRevision{},roleRevision{},session{},issuedAt{},expires{};std::uint32_t operation{},asset{};std::int64_t amount{};std::uint64_t roleMask{},expectedRevision{};};
    struct Pending{EcPhase2MutationRequest request{};std::string reason;std::string actorUuid;std::uint64_t expires{};std::optional<Actor> attributedActor;std::string module;std::uint64_t roleRevision{},accountRevision{};};
    struct Cached{std::string fingerprint,content;EcNativeReply reply{};std::uint64_t expires{};std::string bucket;std::uint64_t session{},roleRevision{};};
    static inline Impl* current{};
    std::filesystem::path configDirectory,dataDirectory;
    TestClock clock;std::string failure;std::unique_ptr<Core> core;
    bool loaded{},enabled{},devValidation{},validatedAssets{};
    EmHostContext host{};std::thread::id thread;std::uint64_t epoch{};HANDLE ownerThread{};
    EternalCorePhase2Api shared{};EcNativeIngressApi ingress{};
    std::map<std::string,std::uint64_t> policies,generations;
    std::map<std::string,Session> sessions;
    std::map<std::string,std::unique_ptr<Binding>> bindings;
    std::map<std::string,Capability> capabilities;
    std::map<std::string,Pending> pending;
    std::map<std::string,Cached> cache;
    std::string nativeBinding;
    std::uint64_t lastNoticePoll{};bool noticePolled{};
#ifdef ETERNAL_CORE_RUNTIME_TESTING
    std::uint64_t testFeatures{};
#endif
    Impl(std::filesystem::path c,std::filesystem::path d,TestClock t):configDirectory(std::move(c)),dataDirectory(std::move(d)),clock(t?std::move(t):TestClock{steady}){}
    std::uint64_t now()const{return clock();}
    EcStatus gate()const {if(!enabled)return EC_NOT_READY;if(std::this_thread::get_id()!=thread)return EC_WRONG_THREAD;return EC_OK;}
    std::uint64_t productionFeatures()const{
#ifdef ETERNAL_CORE_RUNTIME_TESTING
        return testFeatures;
#else
        return 0;
#endif
    }
    EcStatus bridge(EcNativeBridgeToken token,bool terminalRevoke=false)const{auto result=gate();if(result==EC_WRONG_THREAD&&terminalRevoke&&ownerThread&&WaitForSingleObject(ownerThread,0)==WAIT_OBJECT_0)result=EC_OK;if(result!=EC_OK)return result;if(!equal(token,ingress.bridge_nonce)||zero(token))return EC_DENIED;return core?EC_OK:EC_UNSUPPORTED;}
    template<class F> static EcStatus call(F action)noexcept{try{if(!current)return EC_NOT_READY;return action(*current);}catch(...){return EC_INTERNAL_ERROR;}}
    void purge(){auto t=now();std::erase_if(capabilities,[&](const auto& row){return row.second.expires+240000<t;});std::erase_if(pending,[&](const auto& row){return row.second.expires+240000<t;});std::erase_if(cache,[&](const auto& row){return row.second.expires<t;});}
    std::string diagnostics()const{Json out{{"healthOnly",!core},{"phase2ProductionFeatures",0},{"legacyFeatures",0},{"enabled",enabled},{"developmentValidation",devValidation}};if(core){auto result=core->selfcheck();out["selfcheck"]=name(result.status);if(result.status==Status::Ok){out["players"]=result.value.players;out["transactions"]=result.value.transactions;out["ledgerConsistent"]=result.value.ledgerConsistent;out["receiptsConsistent"]=result.value.receiptsConsistent;}}return out.dump();}
    bool load(){
        if(loaded){failure="Already loaded";return false;}
        try{
            auto filename=configDirectory/"core.json";
            if(std::filesystem::exists(filename)){
                std::ifstream input(filename,std::ios::binary);if(!input)throw std::runtime_error("Private Core config unreadable");
                std::vector<std::vector<std::string>> keys;auto callback=[&](int,Json::parse_event_t event,Json& parsed){if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();else if(event==Json::parse_event_t::key){auto k=parsed.get<std::string>();if(std::find(keys.back().begin(),keys.back().end(),k)!=keys.back().end())throw std::runtime_error("Duplicate Core config key");keys.back().push_back(k);}return true;};
                auto config=Json::parse(input,callback);if(!config.is_object())throw std::runtime_error("Invalid Core config");
                for(auto it=config.begin();it!=config.end();++it)if(it.key()!="ownerXuid"&&it.key()!="developmentValidation"&&it.key()!="validatedAssets"&&it.key()!="moduleCapabilities"&&it.key()!="roleDisplayNames")throw std::runtime_error("Unknown Core config field");
                if(config.contains("developmentValidation")){if(!config["developmentValidation"].is_boolean())throw std::runtime_error("Invalid developmentValidation");devValidation=config["developmentValidation"].get<bool>();}
                if(config.contains("validatedAssets")){if(!config["validatedAssets"].is_boolean())throw std::runtime_error("Invalid validatedAssets");validatedAssets=config["validatedAssets"].get<bool>();if(validatedAssets)throw std::runtime_error("Production asset validation has not completed");}
                if(config.contains("moduleCapabilities")){
                    auto& list=config["moduleCapabilities"];if(!list.is_object()||list.size()>64)throw std::runtime_error("Invalid module policies");
                    constexpr std::array<std::string_view,6> names{EC_MODULE_CAP_PLAYER_READ_NAME,EC_MODULE_CAP_PERMISSION_CHECK_NAME,EC_MODULE_CAP_MONEY_NAME,EC_MODULE_CAP_REPUTATION_NAME,EC_MODULE_CAP_AUDIT_NAME,EC_MODULE_CAP_EVENTS_NAME};
                    for(auto it=list.begin();it!=list.end();++it){if(!moduleName(it.key())||!it.value().is_array()||it.value().size()>6)throw std::runtime_error("Invalid module policy");std::uint64_t mask=0;for(const auto& entry:it.value()){if(!entry.is_string())throw std::runtime_error("Invalid module capability");auto label=entry.get<std::string>();auto pos=std::find(names.begin(),names.end(),label);if(pos==names.end())throw std::runtime_error("Unknown module capability");auto bit=UINT64_C(1)<<std::distance(names.begin(),pos);if(mask&bit)throw std::runtime_error("Duplicate module capability");mask|=bit;}policies[it.key()]=mask;}
                }
                if(config.contains("roleDisplayNames")){if(!config["roleDisplayNames"].is_object())throw std::runtime_error("Invalid role display names");for(auto it=config["roleDisplayNames"].begin();it!=config["roleDisplayNames"].end();++it)if(!role(it.key())||!it.value().is_string()||!textValid(it.value().get<std::string>(),128))throw std::runtime_error("Invalid role display name");}
                if(config.contains("ownerXuid")){
                    if(!config["ownerXuid"].is_string())throw std::runtime_error("Owner identifier must be a canonical string");auto owner=config["ownerXuid"].get<std::string>();std::uint64_t numeric=0;auto result=std::from_chars(owner.data(),owner.data()+owner.size(),numeric);if(result.ec!=std::errc{}||result.ptr!=owner.data()+owner.size()||!numeric||std::to_string(numeric)!=owner)throw std::runtime_error("Invalid Owner identifier");
                    std::filesystem::create_directories(dataDirectory);core=std::make_unique<Core>((dataDirectory/"core.sqlite3").string(),owner);
                    if(core->registerConsumer("core.notice")!=Status::Ok)throw std::runtime_error("Core notice consumer unavailable");
                }
            }
            loaded=true;return true;
        }catch(const std::exception&){failure="Core private configuration or database failed validation";core.reset();policies.clear();return false;}
    }
    static bool moduleName(std::string_view name){return !name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';});}
    void tables(){
        shared={};shared.v1_0=*EternalCore_QueryApi(EC_API_MAJOR,EC_API_MINOR);
        shared.struct_size=sizeof(shared);shared.struct_version=1;shared.api_major=1;shared.api_minor=1;shared.instance_epoch=epoch;
        shared.get_phase2_features=getFeatures;shared.read_identity_v2=readIdentity;shared.read_roles=readRoles;shared.check_permission=checkPermission;
        shared.read_asset=readAsset;shared.submit_mutation=submitMutation;shared.poll_receipt_v2=pollReceipt;shared.read_outbox=readOutbox;
        ingress={sizeof(ingress),1,EC_NATIVE_INGRESS_MAJOR,EC_NATIVE_INGRESS_MINOR,randomId(),epoch,bindModule,revokeModule,authenticate,disconnect,command,complete,tick,{}};
    }
    EmStatus enable(const EmHostContext& value){
        if(!loaded)return EM_NOT_READY;if(enabled||current)return EM_CONFLICT;
        if(!valid(&value)||value.abi_major!=EM_ABI_MAJOR||value.abi_minor<EM_ABI_MINOR||!value.instance||!value.publish_service)return EM_ABI_MISMATCH;
        host=value;thread=std::this_thread::get_id();ownerThread=OpenThread(SYNCHRONIZE,FALSE,GetCurrentThreadId());if(!ownerThread)return EM_INTERNAL_ERROR;epoch=randomId().low;if(!epoch)epoch=1;tables();enabled=true;current=this;
        if(core){auto policy=policies.find("core");if(policy!=policies.end()&&policy->second)nativeBinding=createBinding("core",1,policy->second,EC_P2_PERMISSION_ALL,1000000,10000,6000000,60000,60000);}
        // Module adapter owns publication/rollback of all three services.
        return EM_OK;
    }
    EmStatus disable()noexcept{enabled=false;capabilities.clear();pending.clear();cache.clear();bindings.clear();sessions.clear();generations.clear();nativeBinding.clear();ingress.bridge_nonce={};if(ownerThread){CloseHandle(ownerThread);ownerThread=nullptr;}if(current==this)current=nullptr;host={};return EM_OK;}
    std::string createBinding(std::string module,std::uint64_t generation,std::uint64_t caps,std::uint64_t permissions,std::int64_t ml,std::int64_t rl,std::int64_t mb,std::int64_t rb,std::uint64_t period){auto b=std::make_unique<Binding>();b->module=std::move(module);b->generation=generation;b->caps=caps;b->permissions=permissions;b->moneyLimit=ml;b->repLimit=rl;b->moneyBudget=mb;b->repBudget=rb;b->period=period;b->window=now();b->issuedAt=b->window;b->table=shared;b->table.caller_context=randomId();b->table.caller_generation=generation;auto key=id(b->table.caller_context);bindings.emplace(key,std::move(b));return key;}
    EcStatus extract(const EcNativePlayerIdentity& identity,Session*& out){
        if(!valid(&identity)||identity.is_fully_authenticated!=1||identity.is_simulated||!identity.trusted_xuid||zero(identity.trusted_uuid)||!equal(identity.trusted_uuid,identity.client_uuid)||!text(identity.display_name,128))return EC_DENIED;
        auto found=sessions.find(uuid(identity.client_uuid));if(found==sessions.end()||!found->second.online||found->second.actor.xuid()!=std::to_string(identity.trusted_xuid))return EC_DENIED;
        auto snap=core->player(found->second.actor.playerId());if(!snap.player||snap.player->identityVersion!=found->second.actor.identityVersion())return EC_REVOKED;out=&found->second;return EC_OK;
    }
    std::string issue(Session& session,const std::string& binding,std::uint32_t op,std::uint32_t which,const std::string& target,const std::string& recipient,std::int64_t amount,std::uint64_t roleMask){
        purge();if(capabilities.size()>=4096)throw std::runtime_error("Capability capacity reached");auto issuedAt=now();if(issuedAt>UINT64_MAX-60000)throw std::overflow_error("Capability lifetime overflow");auto token=randomId();Capability cap{token,binding,session.actor.uuid(),target,recipient,session.actor.identityVersion(),core->permissionRevision(session.actor.uuid()).value_or(0),session.generation,issuedAt,issuedAt+60000,op,which,amount,roleMask,0};auto key=id(token);capabilities.emplace(key,std::move(cap));return key;
    }
    EcStatus authorize(const EcPhase2RequestMeta& meta,std::uint32_t size,std::uint32_t op,std::uint32_t which,std::string_view target,std::string_view recipient,std::int64_t amount,std::uint64_t roleMask,Capability*& granted,Binding*& binding,Session*& session){
        auto result=gate();if(result!=EC_OK)return result;
        if(meta.struct_size!=size||meta.struct_version!=1)return EC_ABI_MISMATCH;
        if(meta.reserved||zero(meta.caller_context)||zero(meta.capability))return EC_INVALID_ARGUMENT;
        auto found=capabilities.find(id(meta.capability));if(found==capabilities.end())return EC_DENIED;granted=&found->second;
        auto b=bindings.find(id(meta.caller_context));if(b==bindings.end()||!b->second->active||granted->binding!=b->first)return EC_DENIED;binding=b->second.get();
        if(granted->expires<=now())return EC_EXPIRED;
        auto s=sessions.find(granted->actorUuid);if(s==sessions.end()||!s->second.online||s->second.generation!=granted->session)return EC_REVOKED;session=&s->second;
        auto snapshot=core->player(session->actor.playerId());if(!snapshot.player||snapshot.player->identityVersion!=granted->identityRevision||snapshot.player->permissionRevision!=granted->roleRevision)return EC_REVOKED;
        if(granted->operation!=op||granted->asset!=which||granted->target!=target||granted->recipient!=recipient||granted->amount!=amount||granted->roleMask!=roleMask||granted->expectedRevision!=meta.expected_revision)return EC_DENIED;
        auto cap=moduleCap(op,which);if(!(binding->caps&cap)||!(binding->permissions&permissionBit(op)))return EC_DENIED;
        if(!core->hasPermission(session->actor,permission(op,which)))return EC_DENIED;
        if(op==EC_P2_OP_TRANSFER&&target!=session->actor.uuid())return EC_DENIED;
        if(op==EC_P2_OP_ASSET_ADD||op==EC_P2_OP_ASSET_DEDUCT||op==EC_P2_OP_TRANSFER){auto limit=which==EC_P2_ASSET_REPUTATION?binding->repLimit:binding->moneyLimit;if(amount<=0||amount>limit)return EC_BUDGET_EXCEEDED;}
        return EC_OK;
    }
    Json canonical(const EcPhase2MutationRequest& request,std::string_view reason){return Json{{"asset",request.asset},{"expectedRevision",request.meta.expected_revision},{"minorUnits",request.minor_units},{"operation",request.operation},{"reason",reason},{"recipient",id(request.recipient)},{"roleMask",request.role_mask},{"target",id(request.target)}};}
    DomainRequestMetadata metadata(const EcPhase2MutationRequest& request,const Capability& cap,const Binding& b,std::string_view reason){return {b.module,operationName(request.operation),id(request.meta.request_id),cap.roleRevision,canonical(request,reason).dump()};}
    void audit(EcStatus result,const Session* actor,std::string_view module,std::string_view action,std::string_view target){if(core)core->auditDenied({actor?std::optional<std::string>{actor->actor.uuid()}:std::nullopt,std::string(module),std::string(action),std::string(target),"Runtime authorization rejected",{}, {},denialStatus(result),actor?core->permissionRevision(actor->actor.uuid()).value_or(0):0});}
    Result mutate(const EcPhase2MutationRequest& request,bool trustedDevelopment,EcStatus& code){
        Result failed{Status::Invalid,{},false,{}};code=EC_INVALID_ARGUMENT;auto reason=text(request.reason,256);
        bool roleChange=request.operation==EC_P2_OP_ROLE_GRANT||request.operation==EC_P2_OP_ROLE_REVOKE;
        bool moneyChange=request.operation==EC_P2_OP_ASSET_ADD||request.operation==EC_P2_OP_ASSET_DEDUCT||request.operation==EC_P2_OP_TRANSFER;
        if(request.reserved[0]||request.reserved[1]||!reason||zero(request.meta.request_id)||zero(request.meta.idempotency_key)||(!roleChange&&!moneyChange))return failed;
        if(roleChange&&(request.asset||request.minor_units||!zero(request.recipient)||!std::has_single_bit(request.role_mask)||(request.role_mask&~EC_P2_ROLE_ALL)))return failed;
        if(moneyChange&&((request.asset!=EC_P2_ASSET_MONEY&&request.asset!=EC_P2_ASSET_REPUTATION)||request.role_mask||request.minor_units<=0||(request.operation!=EC_P2_OP_TRANSFER&&!zero(request.recipient))||(request.operation==EC_P2_OP_TRANSFER&&(request.asset!=EC_P2_ASSET_MONEY||zero(request.recipient)))))return failed;
        if(moneyChange&&!(trustedDevelopment&&devValidation)&&!(productionFeatures()&EC_P2_FEATURE_ASSET_MUTATION)){code=EC_UNSUPPORTED;failed.status=Status::PermissionDenied;return failed;}
        auto target=core->player(playerKey(request.target));auto recipient=zero(request.recipient)?PlayerResult{}:core->player(playerKey(request.recipient));
        if(!target.player||(request.operation==EC_P2_OP_TRANSFER&&!recipient.player)){
            code=EC_NOT_FOUND;auto cap=capabilities.find(id(request.meta.capability));auto binding=bindings.find(id(request.meta.caller_context));
            if(cap!=capabilities.end()&&binding!=bindings.end()&&cap->second.binding==binding->first){auto actor=core->restoreActor(cap->second.actorUuid);if(actor.actor){auto meta=metadata(request,cap->second,*binding->second,*reason);return core->recordRejectedRequest(*actor.actor,playerKey(request.target),meta.action,meta.canonicalRequestPayload,*reason,id(request.meta.idempotency_key),Status::NotFound,meta);}}
            audit(code,nullptr,"unknown",operationName(request.operation),playerKey(request.target));failed.status=Status::NotFound;return failed;
        }
        Capability* cap=nullptr;Binding* b=nullptr;Session* session=nullptr;
        code=authorize(request.meta,sizeof(request),request.operation,request.asset,target.player->uuid,recipient.player?recipient.player->uuid:"",request.minor_units,request.role_mask,cap,b,session);
        auto saveRejected=[&](EcStatus rejected){
            if(cap&&b){auto actor=core->restoreActor(cap->actorUuid);if(actor.actor){auto meta=metadata(request,*cap,*b,*reason);return core->recordRejectedRequest(*actor.actor,target.player->uuid,operationName(request.operation),meta.canonicalRequestPayload,*reason,id(request.meta.idempotency_key),denialStatus(rejected),meta);}}
            audit(rejected,session,b?b->module:"unknown",operationName(request.operation),target.player->uuid);return Result{denialStatus(rejected),{},false,{}};
        };
        if(code!=EC_OK)return saveRejected(code);
        if(roleChange&&(request.role_mask&~EC_P2_ROLE_MUTABLE_ALL)){code=EC_DENIED;return saveRejected(code);}
        auto meta=metadata(request,*cap,*b,*reason);
        auto prior=core->receipt(session->actor,id(request.meta.idempotency_key),meta);
        if(prior.status==Status::NotFound&&moneyChange){
            if(now()-b->window>=b->period){b->window=now();b->moneySpent=b->repSpent=0;}
            auto spent=request.asset==EC_P2_ASSET_REPUTATION?b->repSpent:b->moneySpent;auto budget=request.asset==EC_P2_ASSET_REPUTATION?b->repBudget:b->moneyBudget;
            if(request.minor_units>budget-spent){code=EC_BUDGET_EXCEEDED;return saveRejected(code);}
            if(request.meta.expected_revision){auto currentBalance=core->balance(target.player->uuid,asset(request.asset));if(currentBalance.status!=Status::Ok||currentBalance.revision!=request.meta.expected_revision){code=EC_CONFLICT;return saveRejected(code);}}
        }
        Result result;
        if(roleChange)result=core->setRole(session->actor,target.player->uuid,static_cast<Role>(std::countr_zero(request.role_mask)),request.operation==EC_P2_OP_ROLE_GRANT,*reason,id(request.meta.idempotency_key),meta);
        else if(request.operation==EC_P2_OP_TRANSFER)result=core->transfer(session->actor,recipient.player->uuid,request.minor_units,*reason,id(request.meta.idempotency_key),meta);
        else result=core->adjustAsset(session->actor,target.player->uuid,asset(request.asset),request.operation==EC_P2_OP_ASSET_DEDUCT?-request.minor_units:request.minor_units,*reason,id(request.meta.idempotency_key),meta);
        code=status(result.status);if(result.status==Status::Conflict)result=core->recordRejectedRequest(session->actor,target.player->uuid,operationName(request.operation),meta.canonicalRequestPayload,*reason,id(request.meta.idempotency_key),Status::Conflict,meta);
        if(result.status==Status::Ok&&!result.replayed&&moneyChange){auto& spent=request.asset==EC_P2_ASSET_REPUTATION?b->repSpent:b->moneySpent;spent+=request.minor_units;}
        return result;
    }
    static EcStatus EC_CALL getFeatures(EcPhase2FeatureInfo* out)noexcept{return call([&](Impl& self)->EcStatus{if(!valid(out))return EC_INVALID_ARGUMENT;auto code=self.gate();if(code!=EC_OK)return code;*out=dto<EcPhase2FeatureInfo>();out->instance_epoch=self.epoch;out->lifecycle=EC_LIFECYCLE_READY;out->domain_thread_model=EC_THREAD_DOMAIN_GAME_THREAD;out->implemented=self.core?EC_P2_FEATURE_ALL:0;out->development_only=self.devValidation&&self.core?EC_P2_FEATURE_ALL:0;out->enabled=self.productionFeatures();return EC_OK;});}
    EcStatus production(std::uint64_t feature)const{auto code=gate();return code!=EC_OK?code:(productionFeatures()&feature)==feature?EC_OK:EC_UNSUPPORTED;}
    static EcStatus EC_CALL readIdentity(const EcPhase2QueryRequest* request,EcPhase2IdentitySnapshot* out,EcUtf8Buffer* name)noexcept{return call([&](Impl& self)->EcStatus{if(!request||!valid(out)||!name||request->reserved)return EC_INVALID_ARGUMENT;auto code=self.production(EC_P2_FEATURE_IDENTITY);if(code!=EC_OK)return code;return self.identity(*request,*out,name);});}
    EcStatus identity(const EcPhase2QueryRequest& request,EcPhase2IdentitySnapshot& out,EcUtf8Buffer* display){
        auto target=core->player(playerKey(request.target));if(!target.player)return EC_NOT_FOUND;Capability* cap=nullptr;Binding* binding=nullptr;Session* session=nullptr;
        auto code=authorize(request.meta,sizeof(request),EC_P2_OP_IDENTITY_READ,0,target.player->uuid,"",0,0,cap,binding,session);if(code!=EC_OK)return code;
        if(request.meta.expected_revision&&request.meta.expected_revision!=target.player->identityVersion)return EC_CONFLICT;
        code=buffer(display,target.player->displayName);if(code!=EC_OK)return code;
        out=dto<EcPhase2IdentitySnapshot>();out.player_id=playerId(target.player->playerId);out.uuid=uuid(target.player->uuid);
        std::from_chars(target.player->xuid.data(),target.player->xuid.data()+target.player->xuid.size(),out.xuid);
        out.first_seen_unix_ms=target.player->firstSeen;out.last_seen_unix_ms=target.player->lastSeen;out.identity_revision=target.player->identityVersion;out.role_revision=target.player->permissionRevision;
        auto online=sessions.find(target.player->uuid);if(online!=sessions.end()&&online->second.online){out.flags|=EC_P2_IDENTITY_ONLINE;out.session_generation=online->second.generation;}
        auto actor=core->restoreActor(target.player->uuid);if(actor.actor&&core->hasRole(*actor.actor,Role::Owner))out.flags|=EC_P2_IDENTITY_OWNER;return EC_OK;
    }
    static EcStatus EC_CALL readRoles(const EcPhase2QueryRequest* request,EcPhase2RoleSnapshot* out)noexcept{return call([&](Impl& self)->EcStatus{if(!request||!valid(out)||request->reserved)return EC_INVALID_ARGUMENT;auto code=self.production(EC_P2_FEATURE_ROLES);if(code!=EC_OK)return code;auto target=self.core->restoreActor(playerKey(request->target));if(!target.actor)return EC_NOT_FOUND;Capability* cap=nullptr;Binding* b=nullptr;Session* s=nullptr;code=self.authorize(request->meta,sizeof(*request),EC_P2_OP_ROLE_READ,0,target.actor->uuid(),"",0,0,cap,b,s);if(code!=EC_OK)return code;*out=dto<EcPhase2RoleSnapshot>();out->player_id=request->target;out->role_revision=self.core->permissionRevision(target.actor->uuid()).value_or(0);for(unsigned i=0;i<13;++i)if(self.core->hasRole(*target.actor,static_cast<Role>(i)))out->role_mask|=UINT64_C(1)<<i;for(unsigned operation=1;operation<=11;++operation)if(self.core->hasPermission(*target.actor,permission(operation,0)))out->permission_mask|=permissionBit(operation);return EC_OK;});}
    static EcStatus EC_CALL checkPermission(const EcPhase2PermissionRequest* request,EcPhase2PermissionDecision* out)noexcept{return call([&](Impl& self)->EcStatus{if(!request||!valid(out)||request->reserved||!permissionBit(request->operation))return EC_INVALID_ARGUMENT;auto code=self.production(EC_P2_FEATURE_PERMISSIONS);if(code!=EC_OK)return code;auto target=self.core->player(playerKey(request->target));if(!target.player)return EC_NOT_FOUND;Capability* cap=nullptr;Binding* b=nullptr;Session* s=nullptr;code=self.authorize(request->meta,sizeof(*request),EC_P2_OP_PERMISSION_CHECK,request->asset,target.player->uuid,"",request->minor_units,request->role_mask,cap,b,s);if(code!=EC_OK)return code;if(!equal(request->subject,playerId(s->actor.playerId())))return EC_DENIED;*out=dto<EcPhase2PermissionDecision>();out->subject=request->subject;out->allowed=self.core->hasPermission(s->actor,permission(request->operation,request->asset));out->result=out->allowed?EC_OK:EC_DENIED;out->role_revision=cap->roleRevision;out->identity_revision=cap->identityRevision;out->session_generation=cap->session;return EC_OK;});}
    static EcStatus EC_CALL readAsset(const EcPhase2AssetRequest* request,EcPhase2AssetSnapshot* out)noexcept{return call([&](Impl& self)->EcStatus{if(!request||!valid(out)||request->reserved0||request->reserved1||(request->asset!=1&&request->asset!=2))return EC_INVALID_ARGUMENT;auto code=self.production(EC_P2_FEATURE_ASSET_READ);if(code!=EC_OK)return code;auto target=self.core->player(playerKey(request->target));if(!target.player)return EC_NOT_FOUND;Capability* cap=nullptr;Binding* b=nullptr;Session* s=nullptr;code=self.authorize(request->meta,sizeof(*request),EC_P2_OP_ASSET_READ,request->asset,target.player->uuid,"",0,0,cap,b,s);if(code!=EC_OK)return code;auto balance=self.core->balance(target.player->uuid,asset(request->asset));if(balance.status!=Status::Ok)return status(balance.status);*out=dto<EcPhase2AssetSnapshot>();out->player_id=request->target;out->asset=request->asset;out->minor_units=balance.amount;out->account_revision=balance.revision;return EC_OK;});}
    void submission(const Result& result,EcStatus code,const EcPhase2MutationRequest& request,EcPhase2Submission& out){out=dto<EcPhase2Submission>();out.request_id=request.meta.request_id;out.result=code;out.state=code==EC_OK?EC_RECEIPT_COMMITTED:EC_RECEIPT_REJECTED;out.accepted_at_unix_ms=wall();if(result.replayed)out.flags|=EC_P2_SUBMISSION_REPLAYED;if(!result.receipt.empty()){auto json=Json::parse(result.receipt);auto tx=json.value("transactionId","");if(tx.starts_with("tx_"))out.receipt_id=parseId(std::string_view(tx).substr(3));}}
    static EcStatus EC_CALL submitMutation(const EcPhase2MutationRequest* request,EcPhase2Submission* out)noexcept{return call([&](Impl& self)->EcStatus{if(!request||!valid(out))return EC_INVALID_ARGUMENT;auto roleChange=request->operation==EC_P2_OP_ROLE_GRANT||request->operation==EC_P2_OP_ROLE_REVOKE;auto code=self.production(roleChange?EC_P2_FEATURE_ROLES:EC_P2_FEATURE_ASSET_MUTATION);if(code!=EC_OK)return code;auto result=self.mutate(*request,false,code);self.submission(result,code,*request,*out);return code;});}
    EcStatus receiptValue(const Result& receipt,EcPhase2Receipt& out){
        if(receipt.receipt.empty())return status(receipt.status);
        auto json=Json::parse(receipt.receipt);if(!json.contains("request")||!json.contains("accounts")||!json.contains("players"))return EC_UNSUPPORTED;
        auto& request=json.at("request");out=dto<EcPhase2Receipt>();
        auto tx=json.at("transactionId").get<std::string>();if(!tx.starts_with("tx_"))return EC_STORAGE_FAILURE;
        out.receipt_id=parseId(std::string_view(tx).substr(3));out.request_id=parseId(json.value("requestId",""));
        out.state=json.value("status","")=="committed"?EC_RECEIPT_COMMITTED:EC_RECEIPT_REJECTED;out.result=status(receipt.status);
        out.operation=request.at("operation").get<uint32_t>();out.asset=request.at("asset").get<uint32_t>();
        out.target=parseId(request.at("target").get<std::string>());out.recipient=parseId(request.at("recipient").get<std::string>());
        out.minor_units=request.at("minorUnits").get<int64_t>();out.role_mask=request.at("roleMask").get<uint64_t>();
        out.completed_at_unix_ms=json.at("completedAt").get<uint64_t>();out.ledger_revision=json.at("ledgerRevision").get<uint64_t>();out.audit_revision=json.at("auditRevision").get<uint64_t>();
        auto target=core->player(playerKey(out.target));auto recipient=zero(out.recipient)?PlayerResult{}:core->player(playerKey(out.recipient));
        const std::string label=out.asset==EC_P2_ASSET_REPUTATION?"reputation":"coin";
        for(const auto& account:json.at("accounts"))if(account.at("asset")==label){auto who=account.at("uuid").get<std::string>();if(target.player&&who==target.player->uuid){out.target_balance=account.at("balance").get<int64_t>();out.target_revision=account.at("revision").get<uint64_t>();}if(recipient.player&&who==recipient.player->uuid){out.recipient_balance=account.at("balance").get<int64_t>();out.recipient_revision=account.at("revision").get<uint64_t>();}}
        if(out.operation==EC_P2_OP_ROLE_GRANT||out.operation==EC_P2_OP_ROLE_REVOKE){for(const auto& player:json.at("players"))if(target.player&&player.at("uuid")==target.player->uuid)out.target_revision=player.at("permissionRevision").get<uint64_t>();}
        if(receipt.replayed)out.flags|=EC_P2_SUBMISSION_REPLAYED;return EC_OK;
    }
    static EcStatus EC_CALL pollReceipt(const EcPhase2ReceiptRequest* request,EcPhase2Receipt* out)noexcept{return call([&](Impl& self)->EcStatus{
        if(!request||!valid(out)||request->reserved||zero(request->receipt_id))return EC_INVALID_ARGUMENT;
        auto code=self.production(EC_P2_FEATURE_RECEIPTS);if(code!=EC_OK)return code;auto found=self.capabilities.find(id(request->meta.capability));if(found==self.capabilities.end())return EC_DENIED;
        Capability* cap=nullptr;Binding* binding=nullptr;Session* actor=nullptr;code=self.authorize(request->meta,sizeof(*request),EC_P2_OP_RECEIPT_READ,0,found->second.actorUuid,"",0,0,cap,binding,actor);if(code!=EC_OK)return code;
        DomainRequestMetadata metadata;metadata.moduleId=binding->module;return self.receiptValue(self.core->receiptById(actor->actor,"tx_"+id(request->receipt_id),metadata),*out);
    });}
    static EcStatus EC_CALL readOutbox(const EcPhase2OutboxRequest* request,EcPhase2OutboxBuffer* out)noexcept{return call([&](Impl& self)->EcStatus{if(!request||!valid(out)||request->reserved0||request->reserved1||out->reserved0||out->reserved1||(!out->data&&out->capacity)||request->limit<1||request->limit>100||request->after_event_id>static_cast<std::uint64_t>(INT64_MAX))return EC_INVALID_ARGUMENT;auto code=self.production(EC_P2_FEATURE_OUTBOX);if(code!=EC_OK)return code;auto cap=self.capabilities.find(id(request->meta.capability));if(cap==self.capabilities.end())return EC_DENIED;Capability* grant=nullptr;Binding* b=nullptr;Session* s=nullptr;code=self.authorize(request->meta,sizeof(*request),EC_P2_OP_OUTBOX_READ,0,cap->second.actorUuid,"",0,0,grant,b,s);if(code!=EC_OK)return code;std::vector<EcPhase2OutboxEvent> rows;auto entries=self.core->eventsAfter(static_cast<int64_t>(request->after_event_id),request->limit);out->next_event_id=request->after_event_id;for(const auto& event:entries){out->next_event_id=static_cast<uint64_t>(event.id);if(event.targetUuid!=s->actor.uuid()&&!self.core->hasRole(s->actor,Role::Owner)&&!self.core->hasRole(s->actor,Role::Admin))continue;auto target=self.core->player(event.targetUuid);if(!target.player)continue;auto payload=Json::parse(event.payload);auto row=dto<EcPhase2OutboxEvent>();row.event_id=static_cast<uint64_t>(event.id);row.receipt_id=parseId(std::string_view(event.txId).substr(3));row.target=playerId(target.player->playerId);row.type=event.type=="role_changed"?EC_P2_OUTBOX_ROLE_CHANGED:EC_P2_OUTBOX_ASSET_CHANGED;row.request_id=parseId(event.requestId);row.occurred_at_unix_ms=static_cast<uint64_t>(event.createdAt);if(row.type==EC_P2_OUTBOX_ROLE_CHANGED){row.asset=EC_P2_ASSET_NONE;row.account_revision=target.player->permissionRevision;}else{auto label=payload.value("asset","");if(label!="coin"&&label!="reputation")return EC_STORAGE_FAILURE;row.asset=label=="reputation"?EC_P2_ASSET_REPUTATION:EC_P2_ASSET_MONEY;auto currentBalance=self.core->balance(event.targetUuid,asset(row.asset));if(currentBalance.status!=Status::Ok)return status(currentBalance.status);row.authoritative_balance=currentBalance.amount;row.account_revision=currentBalance.revision;}rows.push_back(row);}out->required=static_cast<uint32_t>(rows.size());out->count=0;if(out->capacity<rows.size())return EC_BUFFER_TOO_SMALL;std::copy(rows.begin(),rows.end(),out->data);out->count=out->required;return EC_OK;});}
    static EcStatus EC_CALL bindModule(EcNativeBridgeToken nonce,const EcNativeModuleBindingRequest* request,EcNativeModuleBindingResult* out)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce);if(code!=EC_OK)return code;if(!valid(request)||!valid(out)||request->flags||request->reserved0||request->reserved1||(request->approved_module_capabilities&~EC_MODULE_CAP_ALL)||(request->approved_permissions&~EC_P2_PERMISSION_ALL)||!request->module_generation||!request->budget_period_ms||request->money_limit_per_request<0||request->reputation_limit_per_request<0||request->money_budget_per_period<0||request->reputation_budget_per_period<0)return EC_INVALID_ARGUMENT;auto module=text(request->module_id,64);if(!module||!moduleName(*module))return EC_INVALID_ARGUMENT;auto policy=self.policies.find(*module);if(policy==self.policies.end())return EC_DENIED;auto caps=policy->second&request->approved_module_capabilities;if(!caps)return EC_DENIED;if(self.bindings.size()>=128)return EC_LIMIT_EXCEEDED;if(self.generations[*module]>=request->module_generation)return EC_CONFLICT;auto key=self.createBinding(*module,request->module_generation,caps,request->approved_permissions,std::min<INT64>(request->money_limit_per_request,1000000),std::min<INT64>(request->reputation_limit_per_request,10000),std::min<INT64>(request->money_budget_per_period,6000000),std::min<INT64>(request->reputation_budget_per_period,60000),std::max<std::uint64_t>(request->budget_period_ms,60000));self.generations[*module]=request->module_generation;auto& b=*self.bindings.at(key);*out=dto<EcNativeModuleBindingResult>();out->scoped_api=&b.table;out->table_size=sizeof(b.table);out->caller_context=b.table.caller_context;out->caller_generation=b.generation;out->instance_epoch=self.epoch;return EC_OK;});}
    static EcStatus EC_CALL revokeModule(EcNativeBridgeToken nonce,const EcNativeModuleRevokeRequest* request)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce,true);if(code!=EC_OK)return code;if(!valid(request)||request->reserved||!text(request->reason,256))return EC_INVALID_ARGUMENT;auto binding=self.bindings.find(id(request->caller_context));if(binding==self.bindings.end())return EC_NOT_FOUND;if(binding->second->generation!=request->caller_generation)return EC_CONFLICT;binding->second->active=false;auto key=binding->first;std::erase_if(self.capabilities,[&](const auto& row){return row.second.binding==key;});return EC_OK;});}
    static EcStatus EC_CALL authenticate(EcNativeBridgeToken nonce,const EcNativePlayerIdentity* request,EcNativeJoinResult* out)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce);if(code!=EC_OK)return code;if(!valid(request)||!valid(out)||request->is_fully_authenticated!=1||request->is_simulated||!request->trusted_xuid||zero(request->trusted_uuid)||!equal(request->trusted_uuid,request->client_uuid))return EC_DENIED;auto display=text(request->display_name,128);if(!display)return EC_INVALID_UTF8;auto result=self.core->ensurePlayer({uuid(request->trusted_uuid),std::to_string(request->trusted_xuid),*display});if(result.status!=Status::Ok||!result.actor)return status(result.status);auto found=self.sessions.find(result.actor->uuid());if(found==self.sessions.end()){if(self.sessions.size()>=4096)return EC_LIMIT_EXCEEDED;found=self.sessions.emplace(result.actor->uuid(),Session{*result.actor,1,true}).first;}else{if(found->second.generation==UINT64_MAX)return EC_LIMIT_EXCEEDED;++found->second.generation;found->second.actor=*result.actor;found->second.online=true;}*out=dto<EcNativeJoinResult>();out->player_id=playerId(result.actor->playerId());out->session_token=randomId();out->identity_revision=result.actor->identityVersion();out->role_revision=self.core->permissionRevision(result.actor->uuid()).value_or(0);out->session_generation=found->second.generation;out->created=result.created;return EC_OK;});}
    static EcStatus EC_CALL disconnect(EcNativeBridgeToken nonce,const EcNativeDisconnectRequest* request)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce);if(code!=EC_OK)return code;if(!valid(request)||request->reserved||zero(request->client_uuid))return EC_INVALID_ARGUMENT;auto found=self.sessions.find(uuid(request->client_uuid));if(found==self.sessions.end())return EC_NOT_FOUND;if(request->expected_session_generation&&request->expected_session_generation!=found->second.generation)return EC_CONFLICT;found->second.online=false;return EC_OK;});}
    PlayerResult select(std::string_view selector,const Session& caller){if(selector=="self")return core->player(caller.actor.playerId());if(selector.starts_with("player_"))return core->player(selector);return core->findPlayerByDisplayName(selector);}
    bool readTarget(const Session& actor,const PlayerSnapshot& target){return actor.actor.uuid()==target.uuid||core->hasRole(actor.actor,Role::Owner)||core->hasRole(actor.actor,Role::Admin);}
    EcPhase2MutationRequest actionRequest(Session& session,EcRequestId requestId,std::uint32_t operation,std::uint32_t which,const PlayerSnapshot& target,const PlayerSnapshot* recipient,std::int64_t amount,std::uint64_t roleMask,std::string_view key,std::string_view reason){
        EcPhase2MutationRequest request{};request.meta.struct_size=sizeof(request);request.meta.struct_version=1;
        request.meta.request_id=requestId;request.meta.idempotency_key=keyId(key);request.target=playerId(target.playerId);
        if(recipient)request.recipient=playerId(recipient->playerId);request.operation=operation;request.asset=which;request.minor_units=amount;request.role_mask=roleMask;request.reason={reason.data(),static_cast<uint32_t>(reason.size()),0};
        if(nativeBinding.empty())return request;request.meta.caller_context=bindings.at(nativeBinding)->table.caller_context;
        request.meta.capability=parseId(issue(session,nativeBinding,operation,which,target.uuid,recipient?recipient->uuid:"",amount,roleMask));return request;
    }
    Cached result(EcStatus code,std::string content,uint32_t flags=0,EcNativePendingAction action={}){auto reply=dto<EcNativeReply>();reply.result=code;reply.flags=flags;reply.pending_action=action;return {{},std::move(content),reply,now()+300000,{},0,0};}
    Cached rejectedSelector(const EcNativeCommandRequest& request,const Session& actor,std::string_view operation,std::string_view selector,std::string_view kind,std::string_view value,std::string_view key,std::string_view reason,Status resultCode){
        std::int64_t amount=0;std::from_chars(value.data(),value.data()+value.size(),amount);if(amount==INT64_MIN)amount=0;
        std::uint32_t op=operation=="role"?(value=="on"?EC_P2_OP_ROLE_GRANT:EC_P2_OP_ROLE_REVOKE):operation=="transfer"?EC_P2_OP_TRANSFER:amount<0?EC_P2_OP_ASSET_DEDUCT:EC_P2_OP_ASSET_ADD;
        std::uint32_t which=operation=="role"?EC_P2_ASSET_NONE:kind=="reputation"?EC_P2_ASSET_REPUTATION:EC_P2_ASSET_MONEY;auto selectedRole=role(kind);auto roleMask=operation=="role"&&selectedRole?UINT64_C(1)<<static_cast<unsigned>(*selectedRole):0;
        auto payload=Json{{"operation",op},{"asset",which},{"minorUnits",amount<0?-amount:amount},{"roleMask",roleMask},{"target",id({})},{"recipient",id({})},{"expectedRevision",0},{"targetSelector",selector},{"reason",reason}}.dump();
        DomainRequestMetadata meta{"core",std::string(operation),id(request.request_id),core->permissionRevision(actor.actor.uuid()).value_or(0),payload};
        auto receipt=core->recordRejectedRequest(actor.actor,{},operation,payload,reason,id(keyId(key)),resultCode,meta);
        return result(status(receipt.status),receipt.receipt.empty()?"Player selector missing or ambiguous":receipt.receipt);
    }
    Cached execute(const EcNativeCommandRequest& request,std::string_view commandText){
        std::istringstream command{std::string(commandText)};std::string op;command>>op;
        if(op=="status"||op=="selfcheck"){std::string extra;if(command>>extra)return result(EC_INVALID_ARGUMENT,"Unexpected diagnostic arguments");return result(EC_OK,diagnostics());}
        if(!core)return result(EC_UNSUPPORTED,"Core health-only mode: private Owner configuration required");
        if(request.origin!=EC_NATIVE_ORIGIN_PLAYER)return result(EC_DENIED,"Server console cannot mint a player or Owner actor");
        Session* session=nullptr;auto code=extract(request.player,session);if(code!=EC_OK){audit(code,nullptr,"core",op,"");return result(code,"Authenticated live player session required");}
        if(nativeBinding.empty())return result(EC_DENIED,"Core native module capability policy denied");
        auto& binding=*bindings.at(nativeBinding);
        if(op=="identity"){
            std::string selector,extra;if(!(command>>selector)||command>>extra)return result(EC_INVALID_ARGUMENT,"identity self|player_id");auto target=select(selector,*session);if(!target.player)return result(status(target.status),"Player selector missing or ambiguous");if(!readTarget(*session,*target.player))return result(EC_DENIED,"Identity target scope denied");
            EcPhase2QueryRequest query{};query.meta.struct_size=sizeof(query);query.meta.struct_version=1;query.meta.caller_context=binding.table.caller_context;query.meta.capability=parseId(issue(*session,nativeBinding,EC_P2_OP_IDENTITY_READ,0,target.player->uuid,"",0,0));query.target=playerId(target.player->playerId);
            auto snapshot=dto<EcPhase2IdentitySnapshot>();std::array<char,129> display{};EcUtf8Buffer name{display.data(),static_cast<uint32_t>(display.size()),0};code=identity(query,snapshot,&name);if(code!=EC_OK)return result(code,"Identity scope denied");
            return result(EC_OK,Json{{"playerId",target.player->playerId},{"name",display.data()},{"identityVersion",snapshot.identity_revision},{"permissionRevision",snapshot.role_revision},{"sessionGeneration",snapshot.session_generation}}.dump());
        }
        if(op=="roles"){
            std::string selector,extra;if(!(command>>selector)||selector!="self"||command>>extra)return result(EC_INVALID_ARGUMENT,"roles self");if(!(binding.caps&EC_MODULE_CAP_PLAYER_READ))return result(EC_DENIED,"Module role-read capability denied");Json roles=Json::array();for(unsigned i=0;i<13;++i)if(core->hasRole(session->actor,static_cast<Role>(i)))roles.push_back(name(static_cast<Role>(i)));return result(EC_OK,Json{{"roles",roles},{"permissionRevision",core->permissionRevision(session->actor.uuid()).value_or(0)}}.dump());
        }
        if(op=="balance"){
            std::string selector,kind,extra;if(!(command>>selector>>kind)||command>>extra||(kind!="money"&&kind!="reputation"))return result(EC_INVALID_ARGUMENT,"balance self|player_id money|reputation");auto target=select(selector,*session);if(!target.player)return result(status(target.status),"Player selector missing or ambiguous");if(!readTarget(*session,*target.player)&&!core->hasRole(session->actor,Role::EconomyManager))return result(EC_DENIED,"Balance target scope denied");auto which=kind=="money"?EC_P2_ASSET_MONEY:EC_P2_ASSET_REPUTATION;
            EcPhase2AssetRequest query{};query.meta.struct_size=sizeof(query);query.meta.struct_version=1;query.meta.caller_context=binding.table.caller_context;query.meta.capability=parseId(issue(*session,nativeBinding,EC_P2_OP_ASSET_READ,which,target.player->uuid,"",0,0));query.target=playerId(target.player->playerId);query.asset=which;Capability* cap=nullptr;Binding* approved=nullptr;Session* subject=nullptr;code=authorize(query.meta,sizeof(query),EC_P2_OP_ASSET_READ,which,target.player->uuid,"",0,0,cap,approved,subject);if(code!=EC_OK)return result(code,"Balance scope denied");auto value=core->balance(target.player->uuid,asset(which));return result(status(value.status),Json{{"playerId",target.player->playerId},{"asset",kind},{"minorUnits",value.amount},{"revision",value.revision}}.dump());
        }
        if(op=="permission"){
            std::string selector,action,extra;if(!(command>>selector>>action)||selector!="self"||command>>extra)return result(EC_INVALID_ARGUMENT,"permission self money.adjust|money.transfer|reputation.adjust|role.manage");if(!(binding.caps&EC_MODULE_CAP_PERMISSION_CHECK))return result(EC_DENIED,"Module permission-check capability denied");Permission permission;
            if(action=="money.adjust")permission=Permission::MoneyAdjust;else if(action=="money.transfer")permission=Permission::MoneyTransfer;else if(action=="reputation.adjust")permission=Permission::ReputationAdjust;else if(action=="role.manage")permission=Permission::RoleManage;else return result(EC_INVALID_ARGUMENT,"Unknown permission action");bool allowed=core->hasPermission(session->actor,permission);return result(allowed?EC_OK:EC_DENIED,Json{{"allowed",allowed},{"action",action},{"permissionRevision",core->permissionRevision(session->actor.uuid()).value_or(0)}}.dump());
        }
        if(op=="receipt"){
            std::string key,extra;if(!(command>>key)||command>>extra||!textValid(key,160))return result(EC_INVALID_ARGUMENT,"receipt key");if(!(binding.caps&EC_MODULE_CAP_AUDIT))return result(EC_DENIED,"Module receipt capability denied");DomainRequestMetadata metadata;metadata.moduleId="core";auto receipt=core->receipt(session->actor,id(keyId(key)),metadata);return result(status(receipt.status),receipt.receipt.empty()?"Receipt not found":receipt.receipt);
        }
        if(op=="authority-test"){
            std::string extra;if(command>>extra)return result(EC_INVALID_ARGUMENT,"authority-test");if(!devValidation)return result(EC_UNSUPPORTED,"Development validation is disabled");if(pending.size()>=256)return result(EC_LIMIT_EXCEEDED,"Pending action limit reached");auto target=core->player(session->actor.playerId());if(!target.player)return result(EC_NOT_FOUND,"Player missing");
            const std::string reason="Native permission diagnostic test";auto requestDto=actionRequest(*session,request.request_id,EC_P2_OP_ASSET_ADD,EC_P2_ASSET_MONEY,*target.player,nullptr,100,0,"form_"+id(request.request_id),reason);requestDto.meta.expected_revision=core->balance(session->actor.uuid()).revision;capabilities.at(id(requestDto.meta.capability)).expectedRevision=requestDto.meta.expected_revision;
            Capability* cap=nullptr;Binding* b=nullptr;Session* subject=nullptr;code=authorize(requestDto.meta,sizeof(requestDto),requestDto.operation,requestDto.asset,target.player->uuid,"",requestDto.minor_units,0,cap,b,subject);
            if(code!=EC_OK){audit(code,session,"core","authority-test",session->actor.uuid());return result(code,"MoneyAdjust permission diagnostic denied");}
            auto token=randomId();Pending action{requestDto,reason,session->actor.uuid(),now()+60000,session->actor,binding.module,cap->roleRevision,requestDto.meta.expected_revision};action.request.reason={};pending.emplace(id(token),std::move(action));
            return result(EC_OK,Json{{"title","权限验证"},{"content","点击后再次验证当前身份、职业权限和账户版本。测试增加 1 两；60 秒内有效。"},{"buttons",Json::array({"执行测试","取消"})}}.dump(),EC_NATIVE_REPLY_SIMPLE_FORM|EC_NATIVE_REPLY_DEVELOPMENT_ONLY,token);
        }
        if(op!="role"&&op!="asset"&&op!="transfer")return result(EC_INVALID_ARGUMENT,"Unknown Core native command");
        std::string selector,kind,value,key,reason;if(op=="transfer"){if(!(command>>selector>>value>>key))return result(EC_INVALID_ARGUMENT,"transfer recipient positive_minor_units key reason");}else if(!(command>>selector>>kind>>value>>key))return result(EC_INVALID_ARGUMENT,op=="role"?"role target role on|off key reason":"asset target money|reputation signed_minor_units key reason");
        std::getline(command,reason);if(!reason.empty()&&reason.front()==' ')reason.erase(0,1);if(!textValid(reason,256)||!textValid(key,160))return result(EC_INVALID_UTF8,"Valid reason and idempotency key required");auto target=select(selector,*session);if(!target.player)return rejectedSelector(request,*session,op,selector,kind,value,key,reason,target.status);
        std::uint32_t which=0,operation=0;std::int64_t amount=0;std::uint64_t roleMask=0;std::optional<PlayerSnapshot> recipient;
        if(op=="role"){auto selected=role(kind);if(!selected||(value!="on"&&value!="off"))return result(EC_INVALID_ARGUMENT,"Unknown role or enabled state");operation=value=="on"?EC_P2_OP_ROLE_GRANT:EC_P2_OP_ROLE_REVOKE;roleMask=UINT64_C(1)<<static_cast<unsigned>(*selected);}
        else{auto end=std::from_chars(value.data(),value.data()+value.size(),amount);if(end.ec!=std::errc{}||end.ptr!=value.data()+value.size()||!amount||amount==INT64_MIN)return result(EC_INVALID_ARGUMENT,"Invalid integer amount");if(op=="transfer"){if(amount<0)return result(EC_INVALID_ARGUMENT,"Transfer amount must be positive");which=EC_P2_ASSET_MONEY;operation=EC_P2_OP_TRANSFER;recipient=*target.player;target=core->player(session->actor.playerId());}else{if(kind!="money"&&kind!="reputation")return result(EC_INVALID_ARGUMENT,"Unknown asset");which=kind=="money"?EC_P2_ASSET_MONEY:EC_P2_ASSET_REPUTATION;operation=amount<0?EC_P2_OP_ASSET_DEDUCT:EC_P2_OP_ASSET_ADD;if(amount<0)amount=-amount;}}
        auto requestDto=actionRequest(*session,request.request_id,operation,which,*target.player,recipient?&*recipient:nullptr,amount,roleMask,key,reason);auto answer=mutate(requestDto,true,code);
        return result(code,answer.receipt.empty()?"Core request rejected":answer.receipt,which?EC_NATIVE_REPLY_DEVELOPMENT_ONLY:0);
    }
    EcStatus deliver(const Cached& value,EcNativeReply* reply,EcUtf8Buffer* out){*reply=value.reply;auto code=buffer(out,value.content);return code==EC_OK?value.reply.result:code;}
    std::string cacheBucket(const EcNativePlayerIdentity& player,bool form,std::uint64_t& generation,std::uint64_t& revision){
        if(!core)return "player:"+uuid(player.client_uuid);Session* actor=nullptr;if(extract(player,actor)!=EC_OK)return {};
        generation=actor->generation;revision=core->permissionRevision(actor->actor.uuid()).value_or(0);
        return (core->hasRole(actor->actor,Role::Owner)?"owner:":form?"form:":"player:")+actor->actor.uuid();
    }
    EcStatus cachedReply(EcRequestId requestId,std::string fingerprint,std::string bucket,std::uint64_t generation,std::uint64_t revision,EcNativeReply* reply,EcUtf8Buffer* out,const std::function<Cached()>& run){
        auto key=id(requestId);auto previous=cache.find(key);if(previous!=cache.end()){if(previous->second.expires>=now()){if(previous->second.fingerprint!=fingerprint||previous->second.bucket!=bucket)return deliver(result(EC_CONFLICT,"Native request ID belongs to different input"),reply,out);if(previous->second.session!=generation||previous->second.roleRevision!=revision)return deliver(result(EC_REVOKED,"Native reply identity or permission binding revoked"),reply,out);return deliver(previous->second,reply,out);}cache.erase(previous);}
        purge();auto category=bucket.substr(0,bucket.find(':'));std::size_t perActor=category=="owner"?48:category=="console"?16:category=="form"?4:16;std::size_t total=category=="owner"?48:category=="console"?16:category=="form"?64:128;
        std::size_t actorCount=0,categoryCount=0;for(const auto& row:cache){if(row.second.bucket==bucket)++actorCount;if(row.second.bucket.starts_with(category+":"))++categoryCount;}
        if(actorCount>=perActor||categoryCount>=total||cache.size()>=256)return deliver(result(EC_LIMIT_EXCEEDED,"Native reply quota reached"),reply,out);
        auto value=run();value.fingerprint=std::move(fingerprint);value.bucket=std::move(bucket);value.session=generation;value.roleRevision=revision;
        if(core&&value.bucket!="console:server"){auto pos=value.bucket.find(':');if(pos!=std::string::npos)value.roleRevision=core->permissionRevision(value.bucket.substr(pos+1)).value_or(revision);}
        auto inserted=cache.emplace(key,std::move(value));return deliver(inserted.first->second,reply,out);
    }
    Result rejectPending(const Pending& action,EcStatus code){
        if(!action.attributedActor)return {Status::PermissionDenied,{},false,{}};
        DomainRequestMetadata meta{action.module,operationName(action.request.operation),id(action.request.meta.request_id),action.roleRevision,canonical(action.request,action.reason).dump()};
        auto target=core->player(playerKey(action.request.target));
        return core->recordRejectedRequest(*action.attributedActor,target.player?target.player->uuid:playerKey(action.request.target),meta.action,meta.canonicalRequestPayload,action.reason,id(action.request.meta.idempotency_key),denialStatus(code),meta);
    }
    static Json identityFingerprint(const EcNativePlayerIdentity& player){return Json{{"trustedUUID",uuid(player.trusted_uuid)},{"clientUUID",uuid(player.client_uuid)},{"xuid",player.trusted_xuid},{"name",player.display_name.data?std::string(player.display_name.data,player.display_name.length):""},{"authenticated",player.is_fully_authenticated},{"simulated",player.is_simulated},{"size",player.struct_size},{"version",player.struct_version}};}
    static EcStatus EC_CALL command(EcNativeBridgeToken nonce,const EcNativeCommandRequest* request,EcNativeReply* reply,EcUtf8Buffer* out)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce);if(code!=EC_OK&&code!=EC_UNSUPPORTED)return code;if(!valid(request)||!valid(reply)||!out||request->flags||request->reserved||zero(request->request_id)||(request->origin!=EC_NATIVE_ORIGIN_PLAYER&&request->origin!=EC_NATIVE_ORIGIN_SERVER_CONSOLE))return EC_INVALID_ARGUMENT;auto commandText=text(request->command_text,2048);if(!commandText)return EC_INVALID_UTF8;
        if(request->origin==EC_NATIVE_ORIGIN_SERVER_CONSOLE){auto& p=request->player;if(p.struct_size||p.struct_version||!zero(p.trusted_uuid)||!zero(p.client_uuid)||p.trusted_xuid||p.display_name.data||p.display_name.length||p.display_name.reserved||p.is_fully_authenticated||p.is_simulated)return EC_DENIED;}else if(!valid(&request->player)||!text(request->player.display_name,128))return EC_INVALID_ARGUMENT;
        std::uint64_t generation=0,revision=0;auto bucket=request->origin==EC_NATIVE_ORIGIN_SERVER_CONSOLE?std::string("console:server"):self.cacheBucket(request->player,false,generation,revision);if(bucket.empty())return EC_DENIED;
        auto fingerprint=Json{{"kind","command"},{"origin",request->origin},{"player",identityFingerprint(request->player)},{"text",*commandText}}.dump();return self.cachedReply(request->request_id,std::move(fingerprint),std::move(bucket),generation,revision,reply,out,[&]{return self.execute(*request,*commandText);});});}
    static EcStatus EC_CALL complete(EcNativeBridgeToken nonce,const EcNativeActionCompletionRequest* request,EcNativeReply* reply,EcUtf8Buffer* out)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce);if(code!=EC_OK)return code;if(!valid(request)||!valid(reply)||!out||zero(request->request_id)||zero(request->pending_action)||request->reserved0||request->reserved1||request->selected_button< -1||request->selected_button>1||!valid(&request->player)||!text(request->player.display_name,128))return EC_INVALID_ARGUMENT;
        std::uint64_t generation=0,revision=0;auto bucket=self.cacheBucket(request->player,true,generation,revision);if(bucket.empty())return EC_DENIED;
        auto fingerprint=Json{{"kind","completion"},{"player",identityFingerprint(request->player)},{"action",id(request->pending_action)},{"button",request->selected_button}}.dump();return self.cachedReply(request->request_id,std::move(fingerprint),std::move(bucket),generation,revision,reply,out,[&]{
            auto found=self.pending.find(id(request->pending_action));if(found==self.pending.end())return self.result(EC_REVOKED,"Pending action revoked or consumed");Session* actor=nullptr;auto verified=self.extract(request->player,actor);if(verified!=EC_OK||actor->actor.uuid()!=found->second.actorUuid){self.audit(EC_DENIED,actor,"core","form.complete",found->second.actorUuid);return self.result(EC_DENIED,"Form subject does not match authenticated player");}
            auto action=std::move(found->second);self.pending.erase(found);if(request->selected_button!=0){self.capabilities.erase(id(action.request.meta.capability));return self.result(EC_OK,"Diagnostic action cancelled");}
            if(action.expires<=self.now()){auto receipt=self.rejectPending(action,EC_EXPIRED);self.capabilities.erase(id(action.request.meta.capability));return self.result(EC_EXPIRED,receipt.receipt.empty()?"Pending action expired":receipt.receipt,EC_NATIVE_REPLY_DEVELOPMENT_ONLY);}
            auto account=self.core->balance(action.actorUuid,asset(action.request.asset));if(account.status!=Status::Ok||account.revision!=action.accountRevision){auto receipt=self.rejectPending(action,EC_CONFLICT);self.capabilities.erase(id(action.request.meta.capability));return self.result(EC_CONFLICT,receipt.receipt.empty()?"Pending account revision changed":receipt.receipt,EC_NATIVE_REPLY_DEVELOPMENT_ONLY);}
            action.request.reason={action.reason.data(),static_cast<uint32_t>(action.reason.size()),0};auto result=self.mutate(action.request,true,verified);if(verified!=EC_OK&&result.receipt.empty())result=self.rejectPending(action,verified);self.capabilities.erase(id(action.request.meta.capability));return self.result(verified,result.receipt.empty()?"Diagnostic action rejected":result.receipt,EC_NATIVE_REPLY_DEVELOPMENT_ONLY);
        });});}
    static EcStatus EC_CALL tick(EcNativeBridgeToken nonce,std::uint64_t)noexcept{return call([&](Impl& self)->EcStatus{auto code=self.bridge(nonce);if(code!=EC_OK)return code;if(self.noticePolled&&self.now()-self.lastNoticePoll<1000)return EC_OK;self.noticePolled=true;self.lastNoticePoll=self.now();self.purge();if(!self.host.publish_event)return EC_UNSUPPORTED;for(const auto& row:self.core->outboxFor("core.notice",100)){auto notice=dto<EcPhase2OutboxNotice>();notice.event_cursor=static_cast<uint64_t>(row.id);notice.instance_epoch=self.epoch;const std::string topic=EC_PHASE2_OUTBOX_TOPIC;EmEvent event{sizeof(event),1,{topic.data(),static_cast<uint32_t>(topic.size()),0},&notice,sizeof(notice),1,0};auto sent=self.host.publish_event(self.host.instance,&event);
            // Publishing is not an acknowledgement or delivery guarantee.
            auto retry=std::min<int64_t>(60000,INT64_C(1000)<<std::min<std::uint64_t>(row.retryCount,6));auto saved=self.core->recordOutboxAttempt("core.notice",row.id,sent==EM_OK?"Published advisory notice; consumer acknowledgement pending":"Host event publication failed",retry);if(saved!=Status::Ok)return status(saved);}
        return EC_OK;});}
};

Runtime::Runtime(std::filesystem::path config,std::filesystem::path data,TestClock clock):impl_(std::make_unique<Impl>(std::move(config),std::move(data),std::move(clock))){}
Runtime::~Runtime(){impl_->disable();}
bool Runtime::load(){return impl_->load();}
EmStatus Runtime::enable(const EmHostContext& host){try{return impl_->enable(host);}catch(...){impl_->disable();return EM_INTERNAL_ERROR;}}
EmStatus Runtime::disable()noexcept{return impl_->disable();}
const EternalCorePhase2Api* Runtime::phase2Api()const noexcept{return &impl_->shared;}
const EcNativeIngressApi* Runtime::nativeApi()const noexcept{return &impl_->ingress;}
std::string Runtime::diagnostics()const{return impl_->diagnostics();}
const std::string& Runtime::error()const noexcept{return impl_->failure;}
#ifdef ETERNAL_CORE_RUNTIME_TESTING
EcStatus Runtime::testEnableFeatures(std::uint64_t mask){auto code=impl_->gate();if(code!=EC_OK)return code;if(!impl_->core||mask&~EC_P2_FEATURE_ALL)return EC_INVALID_ARGUMENT;impl_->testFeatures=mask;return EC_OK;}
EcStatus Runtime::testIssueCapability(const EcNativePlayerIdentity& player,const EcPhase2MutationRequest& scope,EcCapability& token){
    auto code=impl_->gate();if(code!=EC_OK)return code;if(!impl_->core)return EC_UNSUPPORTED;Impl::Session* actor=nullptr;code=impl_->extract(player,actor);if(code!=EC_OK)return code;
    auto binding=impl_->bindings.find(id(scope.meta.caller_context));if(binding==impl_->bindings.end()||!binding->second->active)return EC_DENIED;
    if(!permissionBit(scope.operation))return EC_INVALID_ARGUMENT;auto target=impl_->core->player(playerKey(scope.target));if(!target.player)return EC_NOT_FOUND;
    auto recipient=zero(scope.recipient)?PlayerResult{}:impl_->core->player(playerKey(scope.recipient));if(!zero(scope.recipient)&&!recipient.player)return EC_NOT_FOUND;
    token=parseId(impl_->issue(*actor,binding->first,scope.operation,scope.asset,target.player->uuid,recipient.player?recipient.player->uuid:"",scope.minor_units,scope.role_mask));impl_->capabilities.at(id(token)).expectedRevision=scope.meta.expected_revision;return EC_OK;
}
#endif
}
