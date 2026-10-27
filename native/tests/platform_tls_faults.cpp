#include <Security/Security.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace {
std::atomic<bool> armed{false};
std::atomic<unsigned> arrays{0};
const char* fault = nullptr;

bool fail(const char* operation, const void* caller) {
    if (!armed.load(std::memory_order_acquire)) return false;
    Dl_info image{};
    if (!dladdr(caller, &image) || !image.dli_fname) return false;
    const auto* filename = std::strrchr(image.dli_fname, '/');
    if (!filename || std::strcmp(filename + 1, "platform-tls-test") != 0) return false;
    if (std::strcmp(operation, "arrays") == 0)
        operation = ++arrays == 1 ? "fault_certificates" : "fault_anchors";
    return fault && std::strcmp(fault, operation) == 0 && armed.exchange(false);
}
CFDataRef failingData(CFAllocatorRef allocator, const UInt8* bytes, CFIndex length) {
    return fail("fault_data", __builtin_return_address(0)) ? nullptr : CFDataCreate(allocator, bytes, length);
}
CFMutableArrayRef failingArray(CFAllocatorRef allocator, CFIndex capacity, const CFArrayCallBacks* callbacks) {
    return fail("arrays", __builtin_return_address(0)) ? nullptr : CFArrayCreateMutable(allocator, capacity, callbacks);
}
CFStringRef failingName(CFAllocatorRef allocator, const UInt8* bytes, CFIndex length, CFStringEncoding encoding, Boolean external) {
    return fail("fault_name", __builtin_return_address(0)) ? nullptr : CFStringCreateWithBytes(allocator, bytes, length, encoding, external);
}
SecPolicyRef failingPolicy(Boolean server, CFStringRef name) {
    return fail("fault_policy", __builtin_return_address(0)) ? nullptr : SecPolicyCreateSSL(server, name);
}
OSStatus failingTrust(CFTypeRef certificates, CFTypeRef policies, SecTrustRef* trust) {
    if (fail("fault_trust", __builtin_return_address(0))) return errSecAllocate;
    if (fail("fault_null_trust", __builtin_return_address(0))) { *trust = nullptr; return errSecSuccess; }
    return SecTrustCreateWithCertificates(certificates, policies, trust);
}
OSStatus failingAnchors(SecTrustRef trust, CFArrayRef anchors) {
    return fail("fault_set_anchors", __builtin_return_address(0)) ? errSecAllocate : SecTrustSetAnchorCertificates(trust, anchors);
}
OSStatus failingExclusiveAnchors(SecTrustRef trust, Boolean exclusive) {
    return fail("fault_exclusive_anchors", __builtin_return_address(0)) ? errSecAllocate : SecTrustSetAnchorCertificatesOnly(trust, exclusive);
}

// Only the isolated CLI test loads this image. System frameworks and Qt startup remain untouched.
struct InterposedFunction { const void* replacement; const void* original; };
__attribute__((used, section("__DATA,__interpose"))) const InterposedFunction replacements[] = {
    {reinterpret_cast<const void*>(&failingData), reinterpret_cast<const void*>(&CFDataCreate)},
    {reinterpret_cast<const void*>(&failingArray), reinterpret_cast<const void*>(&CFArrayCreateMutable)},
    {reinterpret_cast<const void*>(&failingName), reinterpret_cast<const void*>(&CFStringCreateWithBytes)},
    {reinterpret_cast<const void*>(&failingPolicy), reinterpret_cast<const void*>(&SecPolicyCreateSSL)},
    {reinterpret_cast<const void*>(&failingTrust), reinterpret_cast<const void*>(&SecTrustCreateWithCertificates)},
    {reinterpret_cast<const void*>(&failingAnchors), reinterpret_cast<const void*>(&SecTrustSetAnchorCertificates)},
    {reinterpret_cast<const void*>(&failingExclusiveAnchors), reinterpret_cast<const void*>(&SecTrustSetAnchorCertificatesOnly)}
};
}

extern "C" void podlord_test_arm_platform_fault() {
    fault = std::getenv("PODLORD_TEST_PLATFORM_FAULT"); arrays = 0;
    armed.store(true, std::memory_order_release);
}
