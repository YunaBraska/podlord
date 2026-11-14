#include "ui_input.h"
#include <atomic>
#include <memory>
#import <UIKit/UIKit.h>

namespace podlord::test {
bool orientIosTestWindow(QQuickWindow* window) {
    const auto orientation = qEnvironmentVariable("PODLORD_TEST_ORIENTATION");
    if (orientation.isEmpty()) return true;
    if (!window || (orientation != "portrait" && orientation != "landscape")) return false;
    UIWindowScene* scene = nil;
    if (!QTest::qWaitFor([&] {
        for (UIScene* candidate in UIApplication.sharedApplication.connectedScenes)
            if ([candidate isKindOfClass:UIWindowScene.class] && candidate.activationState == UISceneActivationStateForegroundActive) {
                scene = static_cast<UIWindowScene*>(candidate);
                return true;
            }
        return false;
    }, 5000)) {
        qWarning("No active iOS window scene is available for the orientation test.");
        return false;
    }
    const bool landscape = orientation == "landscape";
    const auto failed = std::make_shared<std::atomic_bool>(false);
    UIWindowSceneGeometryPreferencesIOS* preferences = [[UIWindowSceneGeometryPreferencesIOS alloc]
        initWithInterfaceOrientations:landscape ? UIInterfaceOrientationMaskLandscapeRight : UIInterfaceOrientationMaskPortrait];
    [scene requestGeometryUpdateWithPreferences:preferences errorHandler:^(NSError* error) {
        qWarning("iOS denied orientation change: %s", error.localizedDescription.UTF8String);
        failed->store(true);
    }];
    [preferences release];
    if (!QTest::qWaitFor([&] { return failed->load() || (window->width() > window->height()) == landscape; }, 5000)
        || failed->load()) return false;
    qInfo("iOS confirmed %s geometry: %dx%d logical pixels.", qPrintable(orientation), window->width(), window->height());
    return true;
}
} // namespace podlord::test
