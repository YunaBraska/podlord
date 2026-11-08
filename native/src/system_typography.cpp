#include "system_typography.h"
#include <QEvent>
#include <QFontInfo>
#include <QGuiApplication>
#include <algorithm>
#include <array>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

namespace podlord {
#ifdef Q_OS_ANDROID
namespace {
void typographyConfigurationChanged(JNIEnv*, jclass) {
    if (auto* app = QCoreApplication::instance())
        QCoreApplication::postEvent(app, new QEvent(QEvent::ApplicationFontChange));
}
}
#endif
#ifdef Q_OS_IOS
qreal appleTextSize(qreal base);
#endif
SystemTypography::SystemTypography(QObject* parent) : QObject(parent) {
#ifdef Q_OS_ANDROID
    const JNINativeMethod methods[] = {{const_cast<char*>("notifyTypographyChanged"), const_cast<char*>("()V"), reinterpret_cast<void*>(typographyConfigurationChanged)}};
    QJniEnvironment environment;
    if (!environment.registerNativeMethods("berlin/yuna/podlord/PodlordActivity", methods, 1))
        qWarning("Cannot observe Android system text configuration changes.");
#endif
    refresh();
    qGuiApp->installEventFilter(this);
    connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, this, &SystemTypography::refresh);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) refresh();
    });
}
void SystemTypography::refresh() {
    constexpr std::array<std::pair<const char*, int>, 8> styles{{
        {"caption",11}, {"small",12}, {"cell",13}, {"body",14},
        {"heading",16}, {"subheading",18}, {"emptyTitle",22}, {"title",24}}};
#ifdef Q_OS_ANDROID
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto resources = context.callObjectMethod("getResources", "()Landroid/content/res/Resources;");
    const auto metrics = resources.callObjectMethod("getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    const auto density = metrics.getField<jfloat>("density");
#elif !defined(Q_OS_IOS)
    const auto scale = std::max(qreal(1), QFontInfo(QGuiApplication::font()).pixelSize() / qreal(14));
#endif
    QVariantMap next;
    for (const auto& [name, base] : styles) {
#ifdef Q_OS_ANDROID
        // Native SP conversion includes Android 14's nonlinear curve. Qt uses DIPs.
        const auto pixels = QJniObject::callStaticMethod<jfloat>("android/util/TypedValue", "applyDimension",
            "(IFLandroid/util/DisplayMetrics;)F", 2, jfloat(base), metrics.object<jobject>());
        const qreal size = pixels / density;
#elif defined(Q_OS_IOS)
        const qreal size = appleTextSize(base);
#else
        const qreal size = base * scale;
#endif
        next.insert(QString::fromLatin1(name), size);
    }
    if (sizes_ == next) return;
    sizes_ = std::move(next);
    emit changed();
}
bool SystemTypography::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::ApplicationFontChange) refresh();
    return QObject::eventFilter(watched, event);
}
}
