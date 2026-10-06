#include "test_consumer_abi.h"
#include "EternalSDK/Core/Phase2Client.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

// Uses only the public SDK and its OWN projection database, never Core storage.
namespace {
using namespace eternal::sdk;
constexpr uint64_t caps=EC_MODULE_CAP_EVENTS|EC_MODULE_CAP_AUDIT;
EcUtf8View ecView(std::string_view text){return {text.data(),static_cast<uint32_t>(text.size()),0};}
const EmDependency dependencies[]{{sizeof(EmDependency),EM_STRUCT_VERSION,view("core"),{0,1,0,0},caps,0,0}};
const EmModuleDescriptor descriptor{sizeof(descriptor),EM_STRUCT_VERSION,view("test-consumer"),
    view("EternalTestConsumer (TEST ONLY)"),{0,1,0,0},EM_ABI_MAJOR,EM_ABI_MINOR,
    EM_HOST_LOGGING|EM_HOST_SERVICES|EM_HOST_EVENTS,0,0,dependencies,1,0,{0,0}};
EmHostContext host{};
Phase2Client client;
EcConsumerHandle consumer{};
sqlite3* database{};
bool loaded{},enabled{},offline{},busy{},failNext{},crashAfterCommit{},reverseBatch{};
uint64_t serial{},duplicates{},acks{},failures{},firstEvent{},lastEvent{};
EcStatus lastStatus=EC_NOT_READY;
void sql(const char* text){if(sqlite3_exec(database,text,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error("Consumer database operation failed");}
uint64_t count(){sqlite3_stmt* statement{};if(sqlite3_prepare_v2(database,"SELECT COUNT(*) FROM applied",-1,&statement,nullptr)!=SQLITE_OK)throw std::runtime_error("Consumer count failed");
    const auto code=sqlite3_step(statement);const auto result=code==SQLITE_ROW?sqlite3_column_int64(statement,0):-1;sqlite3_finalize(statement);
    if(result<0)throw std::runtime_error("Consumer count failed");return static_cast<uint64_t>(result);}
bool apply(const EcPhase2OutboxEvent& event){
    sql("BEGIN IMMEDIATE");sqlite3_stmt* statement{};
    try {
        if(sqlite3_prepare_v2(database,"INSERT OR IGNORE INTO applied(event_id,receipt_low,receipt_high) VALUES(?,?,?)",-1,&statement,nullptr)!=SQLITE_OK)throw std::runtime_error("Consumer insert failed");
        sqlite3_bind_int64(statement,1,static_cast<sqlite3_int64>(event.event_id));
        sqlite3_bind_int64(statement,2,static_cast<sqlite3_int64>(event.receipt_id.low));
        sqlite3_bind_int64(statement,3,static_cast<sqlite3_int64>(event.receipt_id.high));
        if(sqlite3_step(statement)!=SQLITE_DONE)throw std::runtime_error("Consumer insert failed");
        const bool inserted=sqlite3_changes(database)==1;sqlite3_finalize(statement);statement=nullptr;
        if(inserted)sql("UPDATE effects SET total=total+1 WHERE singleton=1");
        sql("COMMIT");return inserted;
    } catch (...) {if(statement)sqlite3_finalize(statement);sqlite3_exec(database,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
EcStatus poll(){
    if(!enabled)return EC_NOT_READY;
    if(offline||busy)return EC_OK;
    busy=true;struct Reset{~Reset(){busy=false;}} reset;
    std::array<EcPhase2ConsumerEvent,100> events{};
    EcPhase2ConsumerQueryRequest query{};query.consumer=consumer;query.limit=100;
    EcPhase2ConsumerBuffer buffer{};buffer.data=events.data();buffer.capacity=100;
    auto result=client.queryConsumer(query,buffer);if(result!=EC_OK)return result;
    if(reverseBatch)std::reverse(events.begin(),events.begin()+buffer.count);
    for(uint32_t index=0;index<buffer.count;++index){
        const auto& event=events[index];
        if(failNext){failNext=false;++failures;EcPhase2ConsumerRetryRequest retry{};
            retry.consumer=consumer;retry.delivery=event.delivery;retry.event_id=event.event.event_id;
            retry.request_id={++serial,UINT64_C(0xee02)};retry.error=ecView("synthetic-consumer-failure");retry.delay_ms=0;
            result=client.retryConsumer(retry);if(result!=EC_OK)return result;continue;}
        const bool inserted=apply(event.event);
        if(!inserted)++duplicates;
        if(!firstEvent)firstEvent=event.event.event_id;lastEvent=event.event.event_id;
        // Real process crash after a durable local effect, BEFORE Core ACK.
        if(crashAfterCommit){crashAfterCommit=false;std::_Exit(87);}
        EcPhase2ConsumerEventRequest ack{};ack.consumer=consumer;ack.delivery=event.delivery;
        ack.event_id=event.event.event_id;ack.request_id={++serial,UINT64_C(0xee01)};
        result=client.acknowledgeConsumer(ack);if(result!=EC_OK)return result;++acks;
    }
    return EC_OK;
}
void EM_CALL notice(void*,const EmEvent*)noexcept{try{lastStatus=poll();}catch(...){lastStatus=EC_INTERNAL_ERROR;}}
void close()noexcept{if(database){sqlite3_close(database);database=nullptr;}}
}
extern "C" EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor()noexcept{return &descriptor;}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext* context)noexcept{
    if(loaded)return EM_CONFLICT;
    if(!context||!accepts(*context)||!context->instance||!context->query_service||!context->subscribe||!context->log)return EM_ABI_MISMATCH;
    if((context->capabilities&descriptor.required_host_capabilities)!=descriptor.required_host_capabilities)return EM_UNSUPPORTED;
    host=*context;loaded=true;return EM_OK;
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable()noexcept{
    try{
        if(!loaded)return EM_NOT_READY;if(enabled)return EM_CONFLICT;
        const auto discovered=Phase2Client::discover(host,caps,client);if(discovered!=EM_OK)return discovered;
        client.setDevelopmentValidation(true);
        EcPhase2ConsumerRegistrationRequest registration{};registration.local_key=ecView("projection");
        EcPhase2ConsumerRegistration result{};
        if(client.registerConsumer(registration,result)!=EC_OK){client.reset();return EM_UNSUPPORTED;}consumer=result.consumer;
        auto directory=std::filesystem::u8path(std::string(host.data_directory.data,host.data_directory.length));
        std::filesystem::create_directories(directory);const auto path=(directory/"consumer.sqlite3").u8string();
        if(sqlite3_open_v2(reinterpret_cast<const char*>(path.c_str()),&database,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK)throw std::runtime_error("Consumer database open failed");
        sql("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS applied(event_id INTEGER PRIMARY KEY,receipt_low INTEGER NOT NULL,receipt_high INTEGER NOT NULL); CREATE TABLE IF NOT EXISTS effects(singleton INTEGER PRIMARY KEY CHECK(singleton=1),total INTEGER NOT NULL); INSERT OR IGNORE INTO effects VALUES(1,0); PRAGMA user_version=1;");
        EmSubscription subscription{sizeof(subscription),EM_STRUCT_VERSION,view("core.outbox.changed"),notice,nullptr,0};uint64_t token{};
        if(host.subscribe(host.instance,&subscription,&token)!=EM_OK)throw std::runtime_error("Consumer subscription failed");
        enabled=true;offline=busy=failNext=crashAfterCommit=reverseBatch=false;duplicates=acks=failures=firstEvent=lastEvent=0;
        lastStatus=poll();if(lastStatus!=EC_OK)throw std::runtime_error("Consumer initial replay failed");
        host.log(host.instance,EM_LOG_INFO,view("Test consumer enabled; SDK-only projection and ACK"));return EM_OK;
    }catch(...){enabled=false;client.reset();close();return EM_INTERNAL_ERROR;}
}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable()noexcept{enabled=false;client.reset();consumer={};close();return EM_OK;}
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload()noexcept{if(enabled)return EM_CONFLICT;loaded=false;host={};close();return EM_OK;}
extern "C" EM_EXPORT EmStatus EM_CALL EternalTestConsumer_Control(uint32_t action,TestConsumerState* state)noexcept{
    try{
        if(!state||state->struct_size!=sizeof(*state))return EM_INVALID_ARGUMENT;
        if(!enabled)return EM_NOT_READY;
        switch(action){case TEST_CONSUMER_POLL:lastStatus=poll();break;case TEST_CONSUMER_OFFLINE:offline=true;break;
            case TEST_CONSUMER_ONLINE:offline=false;break;case TEST_CONSUMER_FAIL_NEXT:failNext=true;break;
            case TEST_CONSUMER_CRASH_AFTER_COMMIT:crashAfterCommit=true;break;
            case TEST_CONSUMER_REVERSE:reverseBatch=true;firstEvent=lastEvent=0;break;
            case TEST_CONSUMER_STATE:break;default:return EM_INVALID_ARGUMENT;}
        *state={sizeof(*state),1,count(),duplicates,acks,failures,firstEvent,lastEvent,static_cast<int32_t>(lastStatus),0};return EM_OK;
    }catch(...){return EM_INTERNAL_ERROR;}
}
