#include "platform_tls.h"
#include <QScopeGuard>
#include <Security/Security.h>
#include <dispatch/dispatch.h>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace podlord {
namespace {
struct NativeTrustEvaluation {
    SecTrustRef trust;
    const std::shared_ptr<TrustCancellation> stop;
    dispatch_queue_t queue = dispatch_queue_create("berlin.yuna.podlord.trust", DISPATCH_QUEUE_SERIAL);
    bool done = false, trusted = false;
    NativeTrustEvaluation(SecTrustRef value, std::shared_ptr<TrustCancellation> cancellation)
        : trust(value), stop(std::move(cancellation)) { CFRetain(trust); }
    ~NativeTrustEvaluation() { if (queue) dispatch_release(queue); CFRelease(trust); }
    void complete(bool accepted) {
        { std::lock_guard lock(stop->mutex); trusted = accepted; done = true; }
        stop->ready.notify_all();
    }
};
}
bool platformTrustsPeer(const QList<QSslCertificate>& chain, const QString& peerName,
    const QList<QSslCertificate>& authorities, const QList<QSslError>& errors, std::shared_ptr<TrustCancellation> stop) {
    if (stop->isCanceled() || chain.isEmpty() || peerName.isEmpty() || peerName.contains(QChar::Null) || errors.isEmpty()) return false;
    for (const auto& error : errors) {
        switch (error.error()) {
        case QSslError::UnableToGetIssuerCertificate:
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain:
        case QSslError::UnableToGetLocalIssuerCertificate:
        case QSslError::UnableToVerifyFirstCertificate:
        case QSslError::CertificateUntrusted:
            break;
        default:
            return false;
        }
    }
    const auto appendCertificates = [](CFMutableArrayRef destination, const QList<QSslCertificate>& certificates) {
        for (const auto& certificate : certificates) {
            const auto der = certificate.toDer();
            const auto bytes = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(der.constData()), der.size());
            if (!bytes) return false;
            const auto parsed = SecCertificateCreateWithData(kCFAllocatorDefault, bytes);
            CFRelease(bytes);
            if (!parsed) return false;
            CFArrayAppendValue(destination, parsed);
            CFRelease(parsed);
        }
        return true;
    };
    const auto certificates = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!certificates) return false;
    const auto releaseCertificates = qScopeGuard([&] { CFRelease(certificates); });
    if (!appendCertificates(certificates, chain)) return false;
    const auto nameBytes = peerName.toUtf8();
    const auto name = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(nameBytes.constData()),
        nameBytes.size(), kCFStringEncodingUTF8, false);
    if (!name) return false;
    const auto releaseName = qScopeGuard([&] { CFRelease(name); });
    const auto policy = SecPolicyCreateSSL(true, name);
    if (!policy) return false;
    const auto releasePolicy = qScopeGuard([&] { CFRelease(policy); });
    SecTrustRef trust = nullptr;
    const auto releaseTrust = qScopeGuard([&] { if (trust) CFRelease(trust); });
    if (SecTrustCreateWithCertificates(certificates, policy, &trust) != errSecSuccess || !trust) return false;
    if (!authorities.isEmpty()) {
        const auto anchors = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        if (!anchors) return false;
        const auto releaseAnchors = qScopeGuard([&] { CFRelease(anchors); });
        if (!appendCertificates(anchors, authorities) || SecTrustSetAnchorCertificates(trust, anchors) != errSecSuccess
            || SecTrustSetAnchorCertificatesOnly(trust, true) != errSecSuccess) return false;
    }
    // With no explicit anchors, Apple's system and installed-user trust policies remain authoritative.
    const auto evaluation = std::make_shared<NativeTrustEvaluation>(trust, stop);
    if (!evaluation->queue) return false;
    dispatch_async(evaluation->queue, ^{
        const auto status = SecTrustEvaluateAsyncWithError(evaluation->trust, evaluation->queue,
            ^(SecTrustRef, bool accepted, CFErrorRef) { evaluation->complete(accepted); });
        if (status != errSecSuccess) evaluation->complete(false);
    });
    std::unique_lock lock(stop->mutex);
    stop->ready.wait(lock, [&] { return evaluation->done || stop->canceled; });
    return !stop->canceled && evaluation->trusted;
}
}
