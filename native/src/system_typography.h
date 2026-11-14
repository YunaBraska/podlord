#pragma once
#include <QObject>
#include <QVariantMap>

namespace podlord {
/** Requires QGuiApplication. Rendering reads this application-owned cached projection only. */
class SystemTypography final : public QObject {
    Q_OBJECT
public:
    explicit SystemTypography(QObject* parent = nullptr);
    QVariantMap sizes() const { return sizes_; }
    qreal scale() const { return sizes_.value("body").toReal() / 14; }
signals:
    void changed();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QVariantMap sizes_;
    void refresh();
};
}
