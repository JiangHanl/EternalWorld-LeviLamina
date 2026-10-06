#include "../../modules/EternalCore/runtime/Runtime.hpp"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(ETERNAL_CORE_RUNTIME_TESTING) || defined(ETERNAL_CORE_VALIDATION_BUILD)
#error ProductionRuntimeIsolationTests must use the actual production Runtime variant
#endif

using eternal::core::runtime::Runtime;
using Json=nlohmann::json;
namespace fs=std::filesystem;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T>T dto(){T value{};value.struct_size=sizeof(T);value.struct_version=1;return value;}
struct Fixture {
    static inline unsigned serial{};
    fs::path root=fs::temp_directory_path()/("EternalProductionIsolation_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(++serial));
    EmHostContext host=dto<EmHostContext>();
    std::unique_ptr<Runtime> runtime;
    Fixture(Json config){
        fs::create_directories(root/"config");std::ofstream(root/"config"/"core.json")<<config.dump();
        host.abi_major=EM_ABI_MAJOR;host.abi_minor=EM_ABI_MINOR;host.instance=this;host.publish_service=[](void*,const EmServiceOffer*)noexcept->EmStatus{return EM_OK;};
        runtime=std::make_unique<Runtime>(root/"config",root/"data");
    }
    ~Fixture(){runtime.reset();fs::remove_all(root);}
    static Json config(bool development=false,bool assets=false){auto names=Json::array({EC_MODULE_CAP_PLAYER_READ_NAME,EC_MODULE_CAP_PERMISSION_CHECK_NAME,EC_MODULE_CAP_MONEY_NAME,EC_MODULE_CAP_REPUTATION_NAME,EC_MODULE_CAP_AUDIT_NAME,EC_MODULE_CAP_EVENTS_NAME});return Json{{"ownerXuid","900000"},{"developmentValidation",development},{"validatedAssets",assets},{"moduleCapabilities",{{"core",names},{"test",names}}}};}
    const EcNativeIngressApi* ingress(){return runtime->nativeApi();}
    EcNativePlayerIdentity authenticate(){auto player=dto<EcNativePlayerIdentity>();player.trusted_xuid=900000;player.is_fully_authenticated=1;for(auto& byte:player.trusted_uuid.bytes)byte=0x77;player.client_uuid=player.trusted_uuid;player.display_name={"SyntheticProductionOwner",24,0};auto joined=dto<EcNativeJoinResult>();require(ingress()->authenticated_player(ingress()->bridge_nonce,&player,&joined)==EC_OK,"Synthetic trusted ingress");return player;}
    EcStatus command(const char* input,const EcNativePlayerIdentity& player,unsigned serial,std::string& text){auto request=dto<EcNativeCommandRequest>();request.origin=EC_NATIVE_ORIGIN_PLAYER;request.player=player;request.request_id={serial,10};request.command_text={input,static_cast<uint32_t>(std::strlen(input)),0};auto reply=dto<EcNativeReply>();std::array<char,16384> output{};EcUtf8Buffer buffer{output.data(),output.size(),0};auto status=ingress()->native_command(ingress()->bridge_nonce,&request,&reply,&buffer);text=output.data();require(!reply.pending_action.low&&!reply.pending_action.high,"Production never issues a diagnostic pending action");return status;}
};
template<class F>void test(const char* label,F run,unsigned& count){run();++count;std::cout<<"PASS "<<label<<'\n';}
}
int main(){try{unsigned groups=0;
    test("production rejects development flag before database creation",[]{Fixture f(Fixture::config(true));require(!f.runtime->load(),"Production development setting rejected");require(!fs::exists(f.root/"data"/"core.sqlite3"),"No database touched before rejection");},groups);
    test("production rejects validatedAssets flag before database creation",[]{Fixture f(Fixture::config(false,true));require(!f.runtime->load(),"Production cannot activate validation asset mode");require(!fs::exists(f.root/"data"/"core.sqlite3"),"No database touched before rejection");},groups);
    test("production rejects both flags and accepts ordinary Core config",[]{Fixture rejected(Fixture::config(true,true));require(!rejected.runtime->load(),"Both validation flags rejected");Fixture f(Fixture::config());require(f.runtime->load()&&f.runtime->enable(f.host)==EM_OK,"Production normal boot");auto features=dto<EcPhase2FeatureInfo>();require(f.runtime->phase2Api()->get_phase2_features(&features)==EC_OK&&!features.enabled&&!features.development_only,"No production or development capabilities enabled");},groups);
    test("production native asset and authority-test routes are absent",[]{Fixture f(Fixture::config());require(f.runtime->load()&&f.runtime->enable(f.host)==EM_OK,"Boot for isolation");auto player=f.authenticate();std::string reply;unsigned index=1;for(const auto* command:{"asset self money 100 test reason","transfer self 100 test reason","role self EconomyManager on test reason","authority-test"})require(f.command(command,player,index++,reply)==EC_INVALID_ARGUMENT,"Validation-only command not compiled into production dispatcher");require(f.command("balance self money",player,index++,reply)==EC_OK&&Json::parse(reply).at("minorUnits")==0,"No test command asset side effects");require(Json::parse(f.runtime->diagnostics()).at("transactions")==0,"No test transactions or rejection-only test flow created");},groups);
    test("production form completion cannot execute validation actions",[]{Fixture f(Fixture::config());require(f.runtime->load()&&f.runtime->enable(f.host)==EM_OK,"Boot for form isolation");auto request=dto<EcNativeActionCompletionRequest>();request.player=f.authenticate();request.request_id={1,1};request.pending_action={2,2};request.selected_button=0;auto reply=dto<EcNativeReply>();EcUtf8Buffer output{};require(f.ingress()->complete_action(f.ingress()->bridge_nonce,&request,&reply,&output)==EC_UNSUPPORTED,"No production diagnostic form execution body");},groups);
    test("production consumer refuses even real approved module context",[]{Fixture f(Fixture::config());require(f.runtime->load()&&f.runtime->enable(f.host)==EM_OK,"Boot without any online players");auto binding=dto<EcNativeModuleBindingRequest>();binding.module_id={"test",4,0};binding.module_generation=1;binding.approved_module_capabilities=EC_MODULE_CAP_EVENTS|EC_MODULE_CAP_AUDIT;binding.approved_permissions=EC_P2_PERMISSION_ALL;binding.budget_period_ms=60000;auto result=dto<EcNativeModuleBindingResult>();require(f.ingress()->bind_module(f.ingress()->bridge_nonce,&binding,&result)==EC_OK,"Actual module CallerContext");auto request=dto<EcPhase2ConsumerRegistrationRequest>();request.caller_context=result.caller_context;request.local_key={"projection",10,0};auto registered=dto<EcPhase2ConsumerRegistration>();require(result.scoped_api->register_consumer(&request,&registered)==EC_UNSUPPORTED,"Formal consumer remains production gated");},groups);
    std::cout<<"Production Runtime isolation: "<<groups<<" groups passed; synthetic temporary databases only, no BDS or real players.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
