#pragma once
#include <QFont>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>

namespace podlord {
/** Formats the existing plain-text editor and paints only visible line numbers. */
class YamlPresentation : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QQuickItem* editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(qreal scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)
    Q_PROPERTY(int firstVisibleLine READ firstVisibleLine NOTIFY linesChanged)
public:
    explicit YamlPresentation(QQuickItem* parent = nullptr);
    QQuickTextDocument* document() const { return document_; }
    QQuickItem* editor() const { return editor_; }
    QFont font() const { return font_; }
    QColor color() const { return color_; }
    qreal scrollOffset() const { return offset_; }
    int firstVisibleLine() const { return firstLine_; }
    void setDocument(QQuickTextDocument* document);
    void setEditor(QQuickItem* editor);
    void setFont(const QFont& font);
    void setColor(const QColor& color);
    void setScrollOffset(qreal offset);
    void paint(QPainter* painter) override;
signals:
    void documentChanged();
    void editorChanged();
    void fontChanged();
    void colorChanged();
    void scrollOffsetChanged();
    void linesChanged();
private:
    bool scheduleLines();
    bool rebuildLines();
    QPointer<QQuickTextDocument> document_;
    QPointer<QQuickItem> editor_;
    QSyntaxHighlighter* highlighter_;
    QMetaObject::Connection contentConnection_, documentConnection_;
    QFont font_;
    QColor color_;
    qreal offset_ = 0;
    int firstLine_ = 1;
    bool queued_ = false;
    struct Line final { int number; QRectF rectangle; };
    QList<Line> lines_;
};
}
