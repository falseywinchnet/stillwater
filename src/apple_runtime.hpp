#pragma once
// The sole Objective-C messaging seam. All source remains C++20.
#include <objc/message.h>
#include <objc/runtime.h>
#include <utility>
namespace stillwater::apple {
template <typename Return, typename... Arguments>
Return send(id receiver, const char* name, Arguments... arguments) {
    using Function = Return (*)(id, SEL, Arguments...);
    const Function invoke = reinterpret_cast<Function>(objc_msgSend);
    return invoke(receiver, sel_registerName(name), arguments...);
}
inline id type(const char* name) {
    return reinterpret_cast<id>(objc_getClass(name));
}
inline id string(const char* value) {
    return send<id>(type("NSString"), "stringWithUTF8String:", value);
}
inline id make(const char* name) {
    return send<id>(send<id>(type(name), "alloc"), "init");
}
inline void release(id value) {
    if (value != nil)
        send<void>(value, "release");
}
inline id retain(id value) {
    if (value != nil)
        send<id>(value, "retain");
    return value;
}
class Pool final {
  public:
    Pool() : value_(make("NSAutoreleasePool")) {}
    ~Pool() {
        release(value_);
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

  private:
    id value_{nil};
};
} // namespace stillwater::apple
