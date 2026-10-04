#pragma once
#include "../Module/Module.hpp"
namespace eternal::sdk {
class Events final {
    EmHostContext host_{};
public:
    explicit Events(const EmHostContext& host)noexcept:host_(host){}
    EmStatus subscribe(std::string_view topic,EmEventCallback callback,void* user,uint64_t& token)const noexcept {
        const EmSubscription s{sizeof(s),EM_STRUCT_VERSION,view(topic),callback,user,0};
        return host_.subscribe?host_.subscribe(host_.instance,&s,&token):EM_UNSUPPORTED;
    }
    EmStatus unsubscribe(uint64_t token)const noexcept {
        return host_.unsubscribe?host_.unsubscribe(host_.instance,token):EM_UNSUPPORTED;
    }
    EmStatus publish(std::string_view topic,const void* data,uint32_t bytes,uint32_t version=1)const noexcept {
        const EmEvent e{sizeof(e),EM_STRUCT_VERSION,view(topic),data,bytes,version,0};
        return host_.publish_event?host_.publish_event(host_.instance,&e):EM_UNSUPPORTED;
    }
};
} // namespace eternal::sdk
