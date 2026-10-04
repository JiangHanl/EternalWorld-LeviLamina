#pragma once
#include "phase2_abi.h"
#include "../Types/Types.hpp"
#include "../Version/Version.hpp"

namespace eternal::sdk {
class Phase2Client final {
    const EternalCorePhase2Api* api_{};
    static bool nonzero(EcId128 id)noexcept{return id.low||id.high;}
    static bool equal(EcId128 a,EcId128 b)noexcept{return a.low==b.low&&a.high==b.high;}
    template<class Request> EcStatus prepare(Request& request,uint64_t feature)const noexcept {
        if(!api_)return EC_NOT_READY;
        auto info=output<EcPhase2FeatureInfo>();
        auto status=api_->get_phase2_features(&info);if(status!=EC_OK)return status;
        if(info.struct_size!=sizeof(info)||info.struct_version!=EC_PHASE2_STRUCT_VERSION||info.instance_epoch!=api_->instance_epoch||info.reserved[0]||info.reserved[1])return EC_ABI_MISMATCH;
        if(info.lifecycle!=EC_LIFECYCLE_READY)return EC_NOT_READY;
        // Development-only bits never make production endpoints available.
        if((info.enabled&feature)!=feature)return EC_UNSUPPORTED;
        if(!nonzero(api_->caller_context)||!api_->caller_generation||!api_->instance_epoch)return EC_NOT_READY;
        if(!nonzero(request.meta.capability))return EC_DENIED;
        if(nonzero(request.meta.caller_context)&&!equal(request.meta.caller_context,api_->caller_context))return EC_DENIED;
        request.meta.struct_size=sizeof(Request);request.meta.struct_version=EC_PHASE2_STRUCT_VERSION;
        request.meta.caller_context=api_->caller_context;return EC_OK;
    }
public:
    Phase2Client()=default;
    static EmStatus discover(const EmHostContext& host,Phase2Client& out)noexcept {
        out.reset();if(!accepts(host)||!host.instance||!host.query_service)return EM_ABI_MISMATCH;
        const EmServiceRequest request{sizeof(request),EM_STRUCT_VERSION,view(EC_PHASE2_SERVICE_ID),EC_PHASE2_API_MAJOR,EC_PHASE2_API_MINOR,0,0};
        auto reference=output<EmServiceReference>();auto status=host.query_service(host.instance,&request,&reference);if(status!=EM_OK)return status;
        if(reference.struct_size!=sizeof(reference)||reference.struct_version!=EM_STRUCT_VERSION||reference.reserved||!reference.table||reference.table_size<sizeof(EternalCorePhase2Api)||reference.api_major!=EC_PHASE2_API_MAJOR||reference.api_minor<EC_PHASE2_API_MINOR)return EM_ABI_MISMATCH;
        const auto* api=static_cast<const EternalCorePhase2Api*>(reference.table);
        if(api->struct_size!=sizeof(*api)||api->struct_version!=EC_PHASE2_STRUCT_VERSION||api->api_major!=EC_PHASE2_API_MAJOR||api->api_minor<EC_PHASE2_API_MINOR||
           api->v1_0.struct_size!=sizeof(EternalCoreApi)||api->v1_0.api_major!=EC_API_MAJOR||api->v1_0.api_minor!=EC_API_MINOR||api->v1_0.reserved0||
           !api->v1_0.get_version||!api->v1_0.get_features||!api->v1_0.read_identity||!api->v1_0.read_coin||!api->v1_0.submit_transfer||!api->v1_0.poll_receipt||
           !api->get_phase2_features||!api->read_identity_v2||!api->read_roles||!api->check_permission||!api->read_asset||!api->submit_mutation||!api->poll_receipt_v2||!api->read_outbox)return EM_ABI_MISMATCH;
        for(auto reserved:api->reserved)if(reserved)return EM_ABI_MISMATCH;
        for(auto reserved:api->v1_0.reserved)if(reserved)return EM_ABI_MISMATCH;
        out.api_=api;return EM_OK;
    }
    EcStatus features(EcPhase2FeatureInfo& out)const noexcept {
        if(!api_)return EC_NOT_READY;out=output<EcPhase2FeatureInfo>();return api_->get_phase2_features(&out);
    }
    EcStatus identity(EcPhase2QueryRequest request,EcPhase2IdentitySnapshot& out,EcUtf8Buffer& name)const noexcept {
        auto status=prepare(request,EC_P2_FEATURE_IDENTITY);if(status!=EC_OK)return status;
        out=output<EcPhase2IdentitySnapshot>();return api_->read_identity_v2(&request,&out,&name);
    }
    EcStatus roles(EcPhase2QueryRequest request,EcPhase2RoleSnapshot& out)const noexcept {
        auto status=prepare(request,EC_P2_FEATURE_ROLES);if(status!=EC_OK)return status;
        out=output<EcPhase2RoleSnapshot>();return api_->read_roles(&request,&out);
    }
    EcStatus permission(EcPhase2PermissionRequest request,EcPhase2PermissionDecision& out)const noexcept {
        auto status=prepare(request,EC_P2_FEATURE_PERMISSIONS);if(status!=EC_OK)return status;
        out=output<EcPhase2PermissionDecision>();return api_->check_permission(&request,&out);
    }
    EcStatus asset(EcPhase2AssetRequest request,EcPhase2AssetSnapshot& out)const noexcept {
        auto status=prepare(request,EC_P2_FEATURE_ASSET_READ);if(status!=EC_OK)return status;
        out=output<EcPhase2AssetSnapshot>();return api_->read_asset(&request,&out);
    }
    EcStatus submit(EcPhase2MutationRequest request,EcPhase2Submission& out)const noexcept {
        const auto feature=(request.operation==EC_P2_OP_ROLE_GRANT||request.operation==EC_P2_OP_ROLE_REVOKE)?EC_P2_FEATURE_ROLES:EC_P2_FEATURE_ASSET_MUTATION;
        auto status=prepare(request,feature);if(status!=EC_OK)return status;
        out=output<EcPhase2Submission>();return api_->submit_mutation(&request,&out);
    }
    EcStatus receipt(EcPhase2ReceiptRequest request,EcPhase2Receipt& out)const noexcept {
        auto status=prepare(request,EC_P2_FEATURE_RECEIPTS);if(status!=EC_OK)return status;
        out=output<EcPhase2Receipt>();return api_->poll_receipt_v2(&request,&out);
    }
    EcStatus outbox(EcPhase2OutboxRequest request,EcPhase2OutboxBuffer& out)const noexcept {
        auto status=prepare(request,EC_P2_FEATURE_OUTBOX);if(status!=EC_OK)return status;
        // Keep the caller's allocated pointer/capacity, initialize metadata only.
        out.struct_size=sizeof(out);out.struct_version=EC_PHASE2_STRUCT_VERSION;out.count=out.required=0;
        out.next_event_id=out.reserved0=out.reserved1=0;return api_->read_outbox(&request,&out);
    }
    void reset()noexcept{api_=nullptr;}
};
} // namespace eternal::sdk
