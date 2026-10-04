#pragma once
#include "core_abi.h"
#include "../Module/Module.hpp"
namespace eternal::sdk {
class CoreClient final {
    const EternalCoreApi* api_{};
public:
    CoreClient()=default;
    static EmStatus discover(const EmHostContext& host,CoreClient& out)noexcept {
        out.reset();
        if(!accepts(host)||!host.query_service)return EM_ABI_MISMATCH;
        const EmServiceRequest request{sizeof(request),EM_STRUCT_VERSION,view(EM_CORE_SERVICE_ID),EC_API_MAJOR,EC_API_MINOR,0,0};
        auto reference=output<EmServiceReference>();
        auto status=host.query_service(host.instance,&request,&reference);if(status!=EM_OK)return status;
        if(!reference.table||reference.table_size<sizeof(EternalCoreApi))return EM_ABI_MISMATCH;
        const auto* table=static_cast<const EternalCoreApi*>(reference.table);
        if(table->struct_size<sizeof(*table)||table->api_major!=EC_API_MAJOR||table->api_minor<EC_API_MINOR||
           !table->get_version||!table->get_features||!table->read_identity||!table->read_coin||!table->submit_transfer||!table->poll_receipt)return EM_ABI_MISMATCH;
        out.api_=table;return EM_OK;
    }
    EcStatus version(EcVersionInfo& out)const noexcept {return api_?api_->get_version(&out):EC_NOT_READY;}
    EcStatus features(EcFeatureInfo& out)const noexcept {return api_?api_->get_features(&out):EC_NOT_READY;}
    EcStatus readCoin(const EcCoinRequest& request,EcCoinSnapshot& out)const noexcept {
        if(!api_)return EC_NOT_READY;auto feature=eternal::sdk::output<EcFeatureInfo>();
        auto status=api_->get_features(&feature);if(status!=EC_OK)return status;
        if(!(feature.enabled&EC_FEATURE_READ_COIN))return EC_UNSUPPORTED;
        return api_->read_coin(&request,&out);
    }
    void reset()noexcept{api_=nullptr;}
};
} // namespace eternal::sdk
