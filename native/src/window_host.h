#pragma once
#include "window_layout.h"
#include <QFuture>
#include <QObject>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

namespace podlord {
class Workspace;
/** One QML engine; each native window has its own context and workspace projection. */
class WindowHost final : public QObject {
    Q_OBJECT
public:
    explicit WindowHost(Workspace& primary, QObject* parent = nullptr);
    ~WindowHost() override;
    bool open();
    bool detach(Workspace& source, const QString& session);
    bool focus(Workspace& workspace);
    QList<QQuickWindow*> windows() const;
    bool applicationClosing() const;
    bool restorationReady() const { return layoutRestored_; }
    bool layoutCloseNeedsDecision(const Workspace& workspace) const;
    QString layoutCloseError() const { return layoutCloseError_; }
    bool confirmLayoutClose(Workspace& workspace, bool discard);
    bool cancelClose();
signals:
    void windowOpened(QQuickWindow* window);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    enum class ClosePhase { Idle, Waiting, Closing };
    ClosePhase closePhase_ = ClosePhase::Idle;
    struct Window final {
        Workspace* workspace;
        QPointer<QQuickWindow> window;
        QQmlContext* context;
        bool owned;
        QString id;
        QRect normalGeometry;
        bool maximized = false;
    };
    Workspace& primary_;
    QQmlEngine engine_;
    QQmlComponent component_;
    QList<Window> windows_;
    std::optional<Result<WindowLayout>> storedLayout_;
    QFuture<Result<WindowLayout>> layoutFuture_;
    QPointer<Workspace> layoutDecision_;
    QString layoutCloseError_;
    QTimer retryQuit_;
    QMetaObject::Connection restoreReady_;
    bool layoutRestored_ = false, layoutSaving_ = false, skipLayoutSave_ = false;
    bool create(Workspace& workspace, bool owned, const Workspace* source = nullptr,
        const WindowPlacement* placement = nullptr);
    bool restore();
    WindowLayout layout() const;
    bool retire(Workspace& workspace);
};
}
