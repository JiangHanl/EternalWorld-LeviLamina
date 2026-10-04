#include <EternalSDK/Core/Phase2Client.hpp>
#include <EternalSDK/Core/native_ingress_abi.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

using eternal::sdk::Phase2Client;
using eternal::sdk::output;

namespace {
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
bool same(EcId128 a,EcId128 b)noexcept{return a.low==b.low&&a.high==b.high;}
bool nonzero(EcId128 value)noexcept{return value.low||value.high;}

// Synthetic provider only: these fixed fixture IDs are not production grants.
// Real authorization and native player/form integration belong to Core tests.
struct Provider {
    static inline Provider* active{};
    EternalCorePhase2Api api{};
    EmHostContext host{};
    uint64_t enabled=EC_P2_FEATURE_ALL;
    uint64_t implemented=EC_P2_FEATURE_ALL,development=0;
    uint32_t lifecycle=EC_LIFECYCLE_READY;
    uint64_t featureEpoch=9,featureReserved=0;
    uint64_t roleRevision=3,tokenRoleRevision=3,session=7,tokenSession=7;
    EcCapability issued{101,202},previous{};
    EcPlayerId target{303,404};
    uint32_t calls=0,commits=0,queryCount=0,tableSize=sizeof(api);
    uint32_t referenceSize=sizeof(EmServiceReference),referenceReserved=0;
    EmStatus queryResult=EM_OK;
    bool expired=false;
    int64_t balance=10000;
    uint64_t revision=1;
    EcPhase2RequestMeta captured{};
    EcPhase2MutationRequest completed{};
    EcPhase2Submission result{};

    Provider() {
        active=this;
        api.v1_0={sizeof(EternalCoreApi),EC_API_MAJOR,EC_API_MINOR,0,
            version,legacyFeatures,legacyIdentity,legacyCoin,legacyTransfer,legacyReceipt,{}};
        api.struct_size=sizeof(api);api.struct_version=EC_PHASE2_STRUCT_VERSION;
        api.api_major=EC_PHASE2_API_MAJOR;api.api_minor=EC_PHASE2_API_MINOR;
        api.caller_context={11,22};api.caller_generation=1;api.instance_epoch=9;
        api.get_phase2_features=features;api.read_identity_v2=identity;api.read_roles=roles;
        api.check_permission=permission;api.read_asset=asset;api.submit_mutation=submit;
        api.poll_receipt_v2=receipt;api.read_outbox=outbox;
        host=output<EmHostContext>();host.abi_major=EM_ABI_MAJOR;host.abi_minor=EM_ABI_MINOR;
        host.instance=this;host.query_service=query;
    }
    Phase2Client client() {
        Phase2Client value;require(Phase2Client::discover(host,value)==EM_OK,"valid discovery");return value;
    }
    EcPhase2MutationRequest mutation()const {
        EcPhase2MutationRequest request{};
        request.meta.capability=issued;request.meta.request_id={1,2};request.meta.idempotency_key={3,4};
        request.target=target;request.operation=EC_P2_OP_ASSET_ADD;request.asset=EC_P2_ASSET_MONEY;
        request.minor_units=100;request.reason={"synthetic contract",18,0};return request;
    }
    EcStatus authorize(const EcPhase2RequestMeta& meta,uint32_t size)noexcept {
        ++calls;captured=meta;
        if(meta.struct_size!=size||meta.struct_version!=EC_PHASE2_STRUCT_VERSION||meta.reserved)return EC_ABI_MISMATCH;
        if(!same(meta.caller_context,api.caller_context))return EC_DENIED;
        if(nonzero(previous)&&same(meta.capability,previous))return EC_REVOKED;
        if(!same(meta.capability,issued))return EC_DENIED;
        if(expired)return EC_EXPIRED;
        if(roleRevision!=tokenRoleRevision||session!=tokenSession)return EC_REVOKED;
        return EC_OK;
    }
    static EmStatus EM_CALL query(void* instance,const EmServiceRequest* request,EmServiceReference* out)noexcept {
        auto& p=*static_cast<Provider*>(instance);++p.queryCount;
        const auto name=eternal::sdk::text(request->id);
        if(name.starts_with("core.native."))return EM_UNSUPPORTED;
        if(name!=EC_PHASE2_SERVICE_ID)return EM_NOT_FOUND;
        if(p.queryResult!=EM_OK)return p.queryResult;
        *out=output<EmServiceReference>();out->struct_size=p.referenceSize;out->reserved=p.referenceReserved;
        out->table=&p.api;out->table_size=p.tableSize;out->api_major=p.api.api_major;
        out->api_minor=p.api.api_minor;return EM_OK;
    }
    static EcStatus EC_CALL version(EcVersionInfo* out)noexcept {
        *out=output<EcVersionInfo>();out->api_major=1;out->lifecycle=EC_LIFECYCLE_READY;return EC_OK;
    }
    static EcStatus EC_CALL legacyFeatures(EcFeatureInfo* out)noexcept {
        *out=output<EcFeatureInfo>();out->enabled=0;return EC_OK;
    }
    static EcStatus EC_CALL legacyIdentity(const EcIdentityRequest*,EcIdentitySnapshot*,EcUtf8Buffer*)noexcept{return EC_UNSUPPORTED;}
    static EcStatus EC_CALL legacyCoin(const EcCoinRequest*,EcCoinSnapshot*)noexcept{return EC_UNSUPPORTED;}
    static EcStatus EC_CALL legacyTransfer(const EcTransferRequest*,EcTransferSubmission*)noexcept{return EC_UNSUPPORTED;}
    static EcStatus EC_CALL legacyReceipt(const EcReceiptRequest*,EcTransferReceipt*)noexcept{return EC_UNSUPPORTED;}
    static EcStatus EC_CALL features(EcPhase2FeatureInfo* out)noexcept {
        auto& p=*active;*out=output<EcPhase2FeatureInfo>();out->enabled=p.enabled;
        out->implemented=p.implemented;out->development_only=p.development;
        out->instance_epoch=p.featureEpoch;out->lifecycle=p.lifecycle;
        out->domain_thread_model=EC_THREAD_DOMAIN_GAME_THREAD;out->reserved[0]=p.featureReserved;return EC_OK;
    }
    static EcStatus EC_CALL identity(const EcPhase2QueryRequest* request,EcPhase2IdentitySnapshot* out,EcUtf8Buffer* name)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        if(!same(request->target,p.target))return EC_DENIED;
        name->required=7;if(!name->data||name->capacity<7)return EC_BUFFER_TOO_SMALL;
        std::memcpy(name->data,"player",7);out->player_id=p.target;out->identity_revision=1;return EC_OK;
    }
    static EcStatus EC_CALL roles(const EcPhase2QueryRequest* request,EcPhase2RoleSnapshot* out)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        out->player_id=p.target;out->role_mask=EC_P2_ROLE_ECONOMY_MANAGER;out->role_revision=p.roleRevision;return EC_OK;
    }
    static EcStatus EC_CALL permission(const EcPhase2PermissionRequest* request,EcPhase2PermissionDecision* out)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        out->subject=request->subject;out->allowed=1;out->result=EC_OK;out->role_revision=p.roleRevision;return EC_OK;
    }
    static EcStatus EC_CALL asset(const EcPhase2AssetRequest* request,EcPhase2AssetSnapshot* out)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        if(!same(request->target,p.target))return EC_DENIED;
        out->player_id=p.target;out->asset=request->asset;out->minor_units=p.balance;out->account_revision=p.revision;return EC_OK;
    }
    static EcStatus EC_CALL submit(const EcPhase2MutationRequest* request,EcPhase2Submission* out)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        if(!same(request->target,p.target))return EC_DENIED;
        if(!nonzero(request->meta.request_id)||!nonzero(request->meta.idempotency_key))return EC_INVALID_ARGUMENT;
        if(p.commits&&same(request->meta.idempotency_key,p.completed.meta.idempotency_key)) {
            if(request->minor_units!=p.completed.minor_units||request->operation!=p.completed.operation)return EC_CONFLICT;
            *out=p.result;out->flags|=EC_P2_SUBMISSION_REPLAYED;return EC_OK;
        }
        if(request->meta.expected_revision&&request->meta.expected_revision!=p.revision)return EC_CONFLICT;
        if(request->operation==EC_P2_OP_ROLE_GRANT||request->operation==EC_P2_OP_ROLE_REVOKE) {
            const auto role=request->role_mask;
            if(!role||(role&(role-1))||(role&~EC_P2_ROLE_MUTABLE_ALL)||request->minor_units)return EC_INVALID_ARGUMENT;
        } else {
            if(request->minor_units<=0)return EC_INVALID_ARGUMENT;
            if(request->minor_units>1000)return EC_BUDGET_EXCEEDED;
            p.balance+=request->minor_units;++p.revision;
        }
        ++p.commits;p.completed=*request;
        p.result=output<EcPhase2Submission>();p.result.receipt_id={77,88};p.result.request_id=request->meta.request_id;
        p.result.state=EC_RECEIPT_COMMITTED;p.result.result=EC_OK;*out=p.result;return EC_OK;
    }
    static EcStatus EC_CALL receipt(const EcPhase2ReceiptRequest* request,EcPhase2Receipt* out)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        if(!p.commits||!same(request->receipt_id,p.result.receipt_id))return EC_NOT_FOUND;
        out->receipt_id=p.result.receipt_id;out->request_id=p.completed.meta.request_id;
        out->state=EC_RECEIPT_COMMITTED;out->result=EC_OK;out->target=p.target;out->target_balance=p.balance;return EC_OK;
    }
    static EcStatus EC_CALL outbox(const EcPhase2OutboxRequest* request,EcPhase2OutboxBuffer* out)noexcept {
        auto& p=*active;auto status=p.authorize(request->meta,sizeof(*request));if(status!=EC_OK)return status;
        if(out->struct_size!=sizeof(*out)||out->struct_version!=1||out->count||out->required||out->reserved0||out->reserved1)return EC_ABI_MISMATCH;
        out->required=1;if(!out->data||!out->capacity)return EC_BUFFER_TOO_SMALL;
        out->data[0]=output<EcPhase2OutboxEvent>();out->data[0].event_id=10;out->data[0].target=p.target;
        out->data[0].authoritative_balance=p.balance;out->count=1;out->next_event_id=10;return EC_OK;
    }
};
template<class F> void test(const char* name,F run,int& count) {
    run();++count;std::cout<<"PASS "<<name<<'\n';
}
}

int main() {
    try {
        int count=0;
        test("service discovery and immutable legacy prefix",[]{
            Provider p;auto c=p.client();EcFeatureInfo f{};
            require(p.api.v1_0.get_features(&f)==EC_OK&&f.enabled==0,"legacy remains disabled");
            EcPhase2FeatureInfo f2{};require(c.features(f2)==EC_OK&&f2.struct_size==64,"phase2 feature discovery");
        },count);
        test("missing service resets previous client",[]{
            Provider p;auto c=p.client();p.queryResult=EM_NOT_FOUND;
            require(Phase2Client::discover(p.host,c)==EM_NOT_FOUND,"missing service");
            EcPhase2FeatureInfo f{};require(c.features(f)==EC_NOT_READY,"old pointer discarded");
        },count);
        test("reference and function table validation",[]{
            Provider p;Phase2Client c;
            p.tableSize=sizeof(p.api)-1;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"short reference");
            p.tableSize=sizeof(p.api);p.referenceReserved=1;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"reserved reference");
            p.referenceReserved=0;p.referenceSize=0;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"invalid reference header");
            p.referenceSize=sizeof(EmServiceReference);p.api.api_major=2;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"wrong major");
            p.api.api_major=1;p.api.api_minor=0;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"old minor");
            p.api.api_minor=1;p.api.v1_0.struct_size=104;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"legacy layout changed");
            p.api.v1_0.struct_size=96;p.api.reserved[0]=1;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"reserved table");
            p.api.reserved[0]=0;p.api.submit_mutation=nullptr;require(Phase2Client::discover(p.host,c)==EM_ABI_MISMATCH,"missing method");
        },count);
        test("production zero never falls back to development",[]{
            Provider p;auto c=p.client();p.enabled=0;p.development=EC_P2_FEATURE_ALL;
            EcPhase2Submission out{};require(c.submit(p.mutation(),out)==EC_UNSUPPORTED,"development is not production");
            require(p.calls==0&&p.commits==0,"no provider mutation");
        },count);
        test("shared table cannot create a caller binding",[]{
            Provider p;p.api.caller_context={};auto c=p.client();EcPhase2Submission out{};
            require(c.submit(p.mutation(),out)==EC_NOT_READY&&p.calls==0,"unbound caller rejected");
        },count);
        test("caller context and complete DTO size",[]{
            Provider p;auto c=p.client();auto r=p.mutation();EcPhase2Submission out{};
            require(c.submit(r,out)==EC_OK,"valid scoped request");
            require(p.captured.struct_size==sizeof(r)&&p.captured.struct_size!=sizeof(r.meta),"complete request size");
            require(same(p.captured.caller_context,p.api.caller_context)&&same(p.captured.capability,p.issued),"issued context preserved");
            require(!nonzero(r.meta.caller_context)&&r.meta.struct_size==0,"caller request unchanged");
            r.meta.caller_context={900,901};auto before=p.calls;
            require(c.submit(r,out)==EC_DENIED&&p.calls==before,"self reported context rejected locally");
        },count);
        test("missing and forged capabilities",[]{
            Provider p;auto c=p.client();auto r=p.mutation();EcPhase2Submission out{};
            r.meta.capability={};require(c.submit(r,out)==EC_DENIED&&p.calls==0,"no public mint");
            r.meta.capability={999,998};require(c.submit(r,out)==EC_DENIED&&p.commits==0,"forged capability denied by provider");
        },count);
        test("old action role revocation prevents commit",[]{
            Provider p;auto c=p.client();auto opened=p.mutation();++p.roleRevision;EcPhase2Submission out{};
            require(c.submit(opened,out)==EC_REVOKED&&p.commits==0&&p.balance==10000,"old action revoked");
        },count);
        test("session replacement and expiry prevent commit",[]{
            Provider p;auto c=p.client();EcPhase2Submission out{};++p.session;
            require(c.submit(p.mutation(),out)==EC_REVOKED&&p.commits==0,"session revoked");
            p.session=p.tokenSession;p.expired=true;
            require(c.submit(p.mutation(),out)==EC_EXPIRED&&p.commits==0,"expired scope");
        },count);
        test("target scope budget and account revision",[]{
            Provider p;auto c=p.client();auto r=p.mutation();EcPhase2Submission out{};
            r.target={1,1};require(c.submit(r,out)==EC_DENIED,"unauthorized target");
            r=p.mutation();r.minor_units=1001;require(c.submit(r,out)==EC_BUDGET_EXCEEDED,"budget");
            r=p.mutation();r.meta.expected_revision=2;require(c.submit(r,out)==EC_CONFLICT,"account revision");
            require(p.commits==0&&p.balance==10000,"rejections have no effects");
        },count);
        test("idempotent submission and receipt forwarding",[]{
            Provider p;auto c=p.client();auto r=p.mutation();EcPhase2Submission out{};
            require(c.submit(r,out)==EC_OK&&c.submit(r,out)==EC_OK,"retry succeeds");
            require(p.commits==1&&p.balance==10100&&(out.flags&EC_P2_SUBMISSION_REPLAYED),"one commit");
            const auto receiptId=out.receipt_id;
            r.minor_units=200;require(c.submit(r,out)==EC_CONFLICT&&p.commits==1,"changed retry conflicts");
            EcPhase2ReceiptRequest query{};query.meta.capability=p.issued;query.receipt_id=receiptId;EcPhase2Receipt receipt{};
            require(c.receipt(query,receipt)==EC_OK&&receipt.target_balance==10100,"receipt");
        },count);
        test("immutable Owner Player and single role mutation",[]{
            Provider p;auto c=p.client();auto r=p.mutation();r.operation=EC_P2_OP_ROLE_GRANT;r.asset=0;r.minor_units=0;
            EcPhase2Submission out{};
            for(auto role:{EC_P2_ROLE_OWNER,EC_P2_ROLE_PLAYER,EC_P2_ROLE_BUILDER|EC_P2_ROLE_MODERATOR}) {
                r.role_mask=role;require(c.submit(r,out)==EC_INVALID_ARGUMENT&&p.commits==0,"role mutation invalid");
            }
            r.role_mask=EC_P2_ROLE_BUILDER;require(c.submit(r,out)==EC_OK,"single mutable role");
        },count);
        test("identity roles permission and asset metadata",[]{
            Provider p;auto c=p.client();EcPhase2QueryRequest q{};q.meta.capability=p.issued;q.target=p.target;
            EcPhase2IdentitySnapshot identity{};EcUtf8Buffer name{};
            require(c.identity(q,identity,name)==EC_BUFFER_TOO_SMALL&&name.required==7,"caller name sizing");
            char storage[7];name={storage,sizeof(storage),0};require(c.identity(q,identity,name)==EC_OK&&!std::strcmp(storage,"player"),"caller name ownership");
            EcPhase2RoleSnapshot roles{};require(c.roles(q,roles)==EC_OK&&roles.struct_size==64,"roles output initialized");
            EcPhase2PermissionRequest permission{};permission.meta.capability=p.issued;permission.subject=p.target;
            EcPhase2PermissionDecision decision{};require(c.permission(permission,decision)==EC_OK&&decision.allowed,"permission forwarding");
            EcPhase2AssetRequest a{};a.meta.capability=p.issued;a.target=p.target;a.asset=EC_P2_ASSET_MONEY;
            EcPhase2AssetSnapshot asset{};require(c.asset(a,asset)==EC_OK&&asset.minor_units==10000&&asset.struct_size==56,"minor units snapshot");
        },count);
        test("outbox keeps caller storage and initializes metadata",[]{
            Provider p;auto c=p.client();EcPhase2OutboxRequest r{};r.meta.capability=p.issued;r.limit=1;
            EcPhase2OutboxEvent row{};EcPhase2OutboxBuffer out{};out.data=&row;out.capacity=1;
            out.count=99;out.required=99;out.reserved0=99;out.reserved1=99;
            require(c.outbox(r,out)==EC_OK&&out.data==&row&&out.capacity==1&&out.count==1&&row.event_id==10,"caller buffer preserved");
        },count);
        test("lifecycle feature epoch and reset guards",[]{
            Provider p;auto c=p.client();EcPhase2Submission out{};p.lifecycle=EC_LIFECYCLE_STOPPED;
            require(c.submit(p.mutation(),out)==EC_NOT_READY&&p.calls==0,"stopped provider");
            p.lifecycle=EC_LIFECYCLE_READY;++p.featureEpoch;
            require(c.submit(p.mutation(),out)==EC_ABI_MISMATCH,"epoch mismatch");
            p.featureEpoch=p.api.instance_epoch;p.featureReserved=1;
            require(c.submit(p.mutation(),out)==EC_ABI_MISMATCH,"feature reserved");
            c.reset();require(c.submit(p.mutation(),out)==EC_NOT_READY,"disabled consumer reset");
        },count);
        test("new generation rediscovery rejects prior token",[]{
            Provider p;auto c=p.client();auto oldRequest=p.mutation();c.reset();
            p.previous=p.issued;p.issued={501,502};p.api.caller_context={33,44};++p.api.caller_generation;
            ++p.api.instance_epoch;p.featureEpoch=p.api.instance_epoch;require(Phase2Client::discover(p.host,c)==EM_OK,"new generation discover");
            EcPhase2Submission out{};require(c.submit(oldRequest,out)==EC_REVOKED&&p.commits==0,"old grant cannot cross generation");
            require(c.submit(p.mutation(),out)==EC_OK,"newly issued scope");
        },count);
        std::cout<<"Synthetic SDK contracts: "<<count<<" groups passed; no Core DLL, native player, or BDS validation performed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
