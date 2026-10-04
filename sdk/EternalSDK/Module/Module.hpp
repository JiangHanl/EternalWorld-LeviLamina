#pragma once
#include "module_abi.h"
#include "../Types/Types.hpp"
#include "../Version/Version.hpp"
#include <filesystem>
#include <stdexcept>
namespace eternal::sdk {
class ModuleContext final {
    EmHostContext context_{};
public:
    explicit ModuleContext(const EmHostContext& value):context_(value) {
        if(!accepts(value)||!value.instance)throw std::invalid_argument("Invalid Host context");
    }
    const EmHostContext& abi()const noexcept{return context_;}
    EmStatus log(uint32_t level,std::string_view message)const noexcept {
        return context_.log?context_.log(context_.instance,level,view(message)):EM_UNSUPPORTED;
    }
    EmStatus publish(const EmServiceOffer& offer)const noexcept {
        return context_.publish_service?context_.publish_service(context_.instance,&offer):EM_UNSUPPORTED;
    }
    EmStatus query(const EmServiceRequest& request,EmServiceReference& result)const noexcept {
        return context_.query_service?context_.query_service(context_.instance,&request,&result):EM_UNSUPPORTED;
    }
    std::filesystem::path configFile(std::string_view basename)const {
        if(basename.empty()||basename=="."||basename==".."||basename.find_first_of("/\\:\0",0,4)!=std::string_view::npos)
            throw std::invalid_argument("Configuration filename must be a basename");
        const auto directory=text(context_.config_directory);
        if(directory.empty())throw std::invalid_argument("Module configuration directory is absent");
        return std::filesystem::u8path(directory)/std::filesystem::u8path(basename);
    }
};
} // namespace eternal::sdk
