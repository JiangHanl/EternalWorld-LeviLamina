#include "Host.hpp"
#include "EternalSDK/Core/native_ingress_abi.h"
#include "../Fixtures/test_consumer_abi.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <process.h>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// REAL_DLL: actual Core/consumer DLLs + actual Host; identities are SYNTHETIC.
// No engine/player/client claim is made by this executable.
namespace {
using eternal::host::Host;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
EcUtf8View view(std::string_view value){return {value.data(),static_cast<uint32_t>(value.size()),0};}
struct Image {
    HMODULE handle{};
    explicit Image(const std::filesystem::path& path){handle=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);require(handle!=nullptr,"Consumer DLL could not load");}
    ~Image(){if(handle)FreeLibrary(handle);}
    template<class T>T symbol(const char* name){auto result=reinterpret_cast<T>(GetProcAddress(handle,name));require(result!=nullptr,"Consumer control export absent");return result;}
};
struct Scratch {
    std::filesystem::path path;
    explicit Scratch(std::filesystem::path existing={}):path(existing.empty()?std::filesystem::absolute("artifacts")/("consumer-module-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())):existing){}
    ~Scratch(){std::error_code error;std::filesystem::remove_all(path,error);}
};
struct Running {
    std::unique_ptr<Image> image;
    std::unique_ptr<Host> host;
    const EcNativeIngressApi* native{};
    TestConsumerControlFn consumer{};
    uint64_t serial{};
    EcNativePlayerIdentity identity{};
    explicit Running(const std::filesystem::path& root){
        host=std::make_unique<Host>();std::vector<eternal::host::Candidate> candidates;std::string error;
        require(Host::discover(root,{{"core","modules/EternalCore.dll",true,true},{"test-consumer","modules/EternalTestConsumer.dll",true,true}},candidates,error),error);
        require(host->load(candidates,error)&&host->enable(error),error);
        EmServiceRequest request{sizeof(request),EM_STRUCT_VERSION,{EC_NATIVE_INGRESS_SERVICE_ID,sizeof(EC_NATIVE_INGRESS_SERVICE_ID)-1,0},1,0,0,0};
        EmServiceReference reference{sizeof(reference),EM_STRUCT_VERSION};
        require(host->queryService(request,reference)==EM_OK,"Trusted test controller ingress missing");
        native=static_cast<const EcNativeIngressApi*>(reference.table);
        image=std::make_unique<Image>(root/"modules/EternalTestConsumer.dll");consumer=image->symbol<TestConsumerControlFn>("EternalTestConsumer_Control");
        identity.struct_size=sizeof(identity);identity.struct_version=EC_NATIVE_INGRESS_STRUCT_VERSION;
        identity.trusted_uuid.bytes[0]=0xaa;identity.trusted_uuid.bytes[15]=1;identity.client_uuid=identity.trusted_uuid;
        identity.trusted_xuid=1000;identity.display_name=view("SyntheticOwner");identity.is_fully_authenticated=1;
        EcNativeJoinResult joined{sizeof(joined),EC_NATIVE_INGRESS_STRUCT_VERSION};
        require(native->authenticated_player(native->bridge_nonce,&identity,&joined)==EC_OK,"Synthetic controller identity rejected");
    }
    ~Running(){if(host){std::string error;host->shutdown(error);host.reset();}image.reset();}
    TestConsumerState control(uint32_t action){TestConsumerState result{};result.struct_size=sizeof(result);require(consumer(action,&result)==EM_OK,"Consumer control failed");require(result.last_status==EC_OK,"Consumer SDK poll failed "+std::to_string(result.last_status));return result;}
    void grant(std::string_view key){const auto text="asset self money 100 "+std::string(key)+" consumer-test";
        EcNativeCommandRequest request{};request.struct_size=sizeof(request);request.struct_version=EC_NATIVE_INGRESS_STRUCT_VERSION;
        request.request_id={++serial,static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
        request.origin=EC_NATIVE_ORIGIN_PLAYER;request.player=identity;request.command_text=view(text);
        std::array<char,16384> output{};EcUtf8Buffer buffer{output.data(),static_cast<uint32_t>(output.size()),0};
        EcNativeReply reply{sizeof(reply),EC_NATIVE_INGRESS_STRUCT_VERSION};
        auto guard=host->guardTrustedDispatch();require(static_cast<bool>(guard),"Controller dispatch guard missing");
        require(native->native_command(native->bridge_nonce,&request,&reply,&buffer)==EC_OK&&reply.result==EC_OK,"Synthetic asset command failed "+std::string(output.data()));
    }
    void notice(){EcPhase2OutboxNotice hint{sizeof(hint),EC_PHASE2_STRUCT_VERSION,0,native->instance_epoch,0};
        EmEvent event{sizeof(event),EM_STRUCT_VERSION,{"core.outbox.changed",19,0},&hint,sizeof(hint),EC_PHASE2_STRUCT_VERSION,0};
        require(host->publishEvent(event)==EM_OK,"Formal EventBus publish failed");}
};
void prepare(const std::filesystem::path& root,const std::filesystem::path& core,const std::filesystem::path& consumer){
    std::filesystem::create_directories(root/"modules");std::filesystem::create_directories(root/"config/core");
    std::filesystem::copy_file(core,root/"modules/EternalCore.dll");std::filesystem::copy_file(consumer,root/"modules/EternalTestConsumer.dll");
    std::ofstream config(root/"config/core/core.json");
    config<<R"({"ownerXuid":"1000","developmentValidation":true,"validatedAssets":false,"moduleCapabilities":{"core":["core.player.read.v1","core.permission.check.v1","core.transaction.money.v1","core.transaction.reputation.v1","core.audit.write.v1","core.events.v1"],"test-consumer":["core.audit.write.v1","core.events.v1"]}})";
    require(static_cast<bool>(config),"Synthetic config failed");
}
uint64_t durableEffects(const std::filesystem::path& root){
    const auto filename=(root/"data/test-consumer/consumer.sqlite3").u8string();sqlite3* database{};
    require(sqlite3_open_v2(reinterpret_cast<const char*>(filename.c_str()),&database,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,"Consumer private projection not durable");
    sqlite3_stmt* statement{};const auto code=sqlite3_prepare_v2(database,"SELECT total=(SELECT COUNT(*) FROM applied),total FROM effects WHERE singleton=1",-1,&statement,nullptr);
    bool good=code==SQLITE_OK&&sqlite3_step(statement)==SQLITE_ROW&&sqlite3_column_int(statement,0)==1;
    const auto result=good?sqlite3_column_int64(statement,1):-1;sqlite3_finalize(statement);sqlite3_close(database);
    require(good&&result>=0,"Consumer side effects and dedup records diverged");return static_cast<uint64_t>(result);
}
int groups{};
void pass(const char* label){++groups;std::cout<<"PASS "<<label<<'\n';}
}
int main(int argc,char** argv){
    try{
        if(argc==3&&std::string_view(argv[1])=="--crash"){
            Running child(std::filesystem::absolute(argv[2]));child.grant("consumer-crash-event");child.control(TEST_CONSUMER_CRASH_AFTER_COMMIT);child.notice();
            throw std::runtime_error("Consumer failed to crash at durable effect/ACK boundary");
        }
        require(argc==4,"Usage: ConsumerModuleTests <Eternal root> <Validation Core DLL> <Consumer DLL>");
        Scratch scratch;prepare(scratch.path,std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]));
        {
            Running run(scratch.path);
            require(run.control(TEST_CONSUMER_STATE).effects==0,"Fresh consumer has effects");pass("REAL_DLL public SDK registry consumer registration");
            run.grant("consumer-normal-1");run.notice();auto state=run.control(TEST_CONSUMER_STATE);
            require(state.effects==1&&state.acknowledgements==1,"Normal consumption/ACK failed");pass("REAL_DLL normal EventBus delivery and ACK");
            run.notice();run.notice();require(run.control(TEST_CONSUMER_POLL).effects==1,"Repeated notice repeated effect");pass("REAL_DLL duplicate notifications cannot repeat committed effect");
            run.control(TEST_CONSUMER_FAIL_NEXT);run.grant("consumer-retry-1");run.notice();state=run.control(TEST_CONSUMER_STATE);
            require(state.effects==1&&state.failures==1,"Failure did not preserve pending event");
            require(run.control(TEST_CONSUMER_POLL).effects==2,"Scheduled retry did not consume");pass("REAL_DLL failure retry through formal Core API");
            run.control(TEST_CONSUMER_OFFLINE);run.grant("consumer-offline-1");run.notice();require(run.control(TEST_CONSUMER_STATE).effects==2,"Offline consumer received effect");pass("REAL_DLL offline consumer leaves durable backlog");
        }
        {
            Running run(scratch.path);auto state=run.control(TEST_CONSUMER_STATE);require(state.effects==3&&state.acknowledgements==1,"Core restart did not replay offline backlog");pass("REAL_DLL Core restart replays pending event to new module generation");
            run.grant("consumer-order-1");run.grant("consumer-order-2");run.control(TEST_CONSUMER_REVERSE);run.notice();state=run.control(TEST_CONSUMER_STATE);
            require(state.effects==5&&state.first_processed_event>state.last_processed_event,"Reverse-order delivery test did not run");pass("REAL_DLL out-of-order events retain independent dedup and ACK");
        }
        {
            Running run(scratch.path);auto state=run.control(TEST_CONSUMER_STATE);require(state.effects==5&&state.acknowledgements==0,"ACKed event redelivered after restart");pass("REAL_DLL ACK survives Core and consumer restart");
        }
        std::array<const char*,4> arguments{argv[0],"--crash",nullptr,nullptr};const auto path=scratch.path.string();arguments[2]=path.c_str();
        const auto exitCode=_spawnv(_P_WAIT,argv[0],arguments.data());require(exitCode==87,"Consumer crash child did not exit at intended boundary");
        require(durableEffects(scratch.path)==6,"Crash lost atomic consumer effect/dedup");pass("REAL_DLL real process crash after local commit before ACK");
        {
            Running run(scratch.path);auto state=run.control(TEST_CONSUMER_STATE);
            require(state.effects==6&&state.duplicates==1&&state.acknowledgements==1,"Crash replay repeated/lost effect or skipped ACK");pass("REAL_DLL crash recovery deduplicates replayed eventId then ACKs");
        }
        {
            Running run(scratch.path);auto state=run.control(TEST_CONSUMER_STATE);require(state.effects==6&&state.acknowledgements==0&&durableEffects(scratch.path)==6,"Recovered ACK not durable");pass("REAL_DLL recovered ACK prevents further replay after another restart");
        }
        std::cout<<"PASS ConsumerModuleTests: "<<groups<<" groups (REAL_DLL; synthetic identities; REAL_CLIENT NOT RUN)\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
