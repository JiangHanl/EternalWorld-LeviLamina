#pragma once
#include "../Module/Module.hpp"
#include <utility>
namespace eternal::sdk {
template<class F> EmStatus boundary(F&& body)noexcept {try{return std::forward<F>(body)();}catch(...){return EM_INTERNAL_ERROR;}}
template<class T> T& moduleInstance(){static T instance;return instance;}
} // namespace eternal::sdk
/* Use in exactly one module translation unit. Implementation methods return
 * EmStatus, descriptor() returns a module-owned static const descriptor. */
#define EM_IMPLEMENT_MODULE(Type) \
extern "C" EM_EXPORT const EmModuleDescriptor* EM_CALL EternalModule_GetDescriptor() noexcept { \
    try{return Type::descriptor();}catch(...){return nullptr;} } \
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Load(const EmHostContext* context) noexcept { \
    return eternal::sdk::boundary([&]{if(!context)return EmStatus(EM_INVALID_ARGUMENT);return eternal::sdk::moduleInstance<Type>().load(*context);}); } \
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Enable() noexcept { \
    return eternal::sdk::boundary([]{return eternal::sdk::moduleInstance<Type>().enable();}); } \
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Disable() noexcept { \
    return eternal::sdk::boundary([]{return eternal::sdk::moduleInstance<Type>().disable();}); } \
extern "C" EM_EXPORT EmStatus EM_CALL EternalModule_Unload() noexcept { \
    return eternal::sdk::boundary([]{return eternal::sdk::moduleInstance<Type>().unload();}); }
