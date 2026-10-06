#pragma once
#include <QQuickPaintedItem>

namespace podlord {
class KindGlyph : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(QColor fill READ fill WRITE setFill NOTIFY fillChanged)
    Q_PROPERTY(QColor stroke READ stroke WRITE setStroke NOTIFY strokeChanged)
public:
    explicit KindGlyph(QQuickItem* parent = nullptr);
    QString kind() const { return kind_; }
    QColor fill() const { return fill_; }
    QColor stroke() const { return stroke_; }
    void setKind(const QString& value);
    void setFill(const QColor& value);
    void setStroke(const QColor& value);
    void paint(QPainter* painter) override;
    static void draw(QPainter* painter, const QRectF& rect, const QString& kind, const QColor& fill, const QColor& stroke);
signals:
    void kindChanged();
    void fillChanged();
    void strokeChanged();
private:
    QString kind_;
    QColor fill_{Qt::green}, stroke_{QStringLiteral("#050806")};
};
}
