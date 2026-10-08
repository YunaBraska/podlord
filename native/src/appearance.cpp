#include "appearance.h"
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace podlord {
namespace {
struct Theme final { QString name; QList<QColor> dark, light; };
const Result<QList<Theme>>& catalog() {
    static const Result<QList<Theme>> result = []() -> Result<QList<Theme>> {
        QFile file(":/podlord/theme-palettes.json");
        if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Shipped theme definitions are unavailable."};
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().isEmpty())
            return Failure{StoreError::InvalidData, "Invalid shipped theme definitions."};
        QList<Theme> themes;
        QSet<QString> names;
        for (const auto& entry : document.array()) {
            const auto object = entry.toObject();
            const QString name = object["name"].toString();
            if (object.size() != 3 || name.isEmpty() || name.trimmed() != name || names.contains(name))
                return Failure{StoreError::InvalidData, "Invalid or duplicate shipped theme."};
            Theme theme{name, {}, {}};
            for (const auto& variant : {QString("dark"), QString("light")}) {
                if (!object[variant].isArray() || object[variant].toArray().size() != 17)
                    return Failure{StoreError::InvalidData, "Incomplete shipped theme palette."};
                auto& colors = variant == "dark" ? theme.dark : theme.light;
                for (const auto& value : object[variant].toArray()) {
                    const auto text = value.toString();
                    const QColor color(text);
                    if (!value.isString() || text.size() != 7 || !text.startsWith('#') || !color.isValid())
                        return Failure{StoreError::InvalidData, "Invalid shipped theme color."};
                    colors.append(color);
                }
            }
            names.insert(name); themes.append(std::move(theme));
        }
        return themes;
    }();
    return result;
}
QColor mix(const QColor& base, const QColor& overlay, double weight) {
    const auto channel = [weight](int a, int b) { return static_cast<int>(std::nearbyint(a * (1 - weight) + b * weight)); };
    return QColor(channel(base.red(), overlay.red()), channel(base.green(), overlay.green()), channel(base.blue(), overlay.blue()));
}
} // namespace
QStringList themeNames() {
    QStringList names;
    if (const auto* themes = std::get_if<QList<Theme>>(&catalog())) for (const auto& theme : *themes) names.append(theme.name);
    return names;
}
bool validAppearance(const QString& name, const QString& variant) {
    return (variant == "dark" || variant == "light") && themeNames().contains(name);
}
Result<Appearance> makeAppearance(const QString& name, const QString& variant) {
    if (const auto* failure = std::get_if<Failure>(&catalog())) return *failure;
    if (!validAppearance(name, variant)) return Failure{StoreError::InvalidInput, "Choose a shipped theme and dark/light."};
    const auto& themes = std::get<QList<Theme>>(catalog());
    const auto selected = std::find_if(themes.cbegin(), themes.cend(), [&](const auto& theme) { return theme.name == name; });
    const auto& colors = variant == "light" ? selected->light : selected->dark;
    const QStringList keys{"app", "panel", "raised", "inset", "border", "strongBorder", "accent", "accentMuted", "accentGlow", "text", "muted", "success", "warning", "danger", "unknown", "radarShell", "radarGlass"};
    Appearance appearance{name, variant, {}, {}};
    for (int index = 0; index < colors.size(); ++index) appearance.colors.insert(keys[index], colors[index]);
    const QColor selection = mix(colors[3], colors[6], 0.28);
    appearance.colors.insert("selection", selection);
    appearance.colors.insert("hover", mix(colors[3], colors[7], 0.22));
    QColor glow = colors[8]; glow.setAlpha(72);
    appearance.colors.insert("glow", glow);
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        auto& palette = appearance.palette;
        const auto text = group == QPalette::Disabled ? colors[10] : colors[9];
        palette.setColor(group, QPalette::Window, colors[0]);
        palette.setColor(group, QPalette::WindowText, text);
        palette.setColor(group, QPalette::Base, colors[3]);
        palette.setColor(group, QPalette::PlaceholderText, colors[10]);
        palette.setColor(group, QPalette::AlternateBase, colors[1]);
        palette.setColor(group, QPalette::Text, text);
        palette.setColor(group, QPalette::Button, colors[2]);
        palette.setColor(group, QPalette::ButtonText, text);
        palette.setColor(group, QPalette::Highlight, selection);
        palette.setColor(group, QPalette::HighlightedText, colors[9]);
        palette.setColor(group, QPalette::ToolTipBase, colors[1]);
        palette.setColor(group, QPalette::ToolTipText, colors[9]);
        palette.setColor(group, QPalette::BrightText, colors[13]);
        palette.setColor(group, QPalette::Link, colors[6]);
        palette.setColor(group, QPalette::LinkVisited, colors[7]);
        palette.setColor(group, QPalette::Accent, colors[6]);
        palette.setColor(group, QPalette::Mid, colors[4]);
        palette.setColor(group, QPalette::Dark, colors[3]);
        palette.setColor(group, QPalette::Light, colors[2]);
        palette.setColor(group, QPalette::Shadow, QColor(0, 0, 0, variant == "light" ? 82 : 138));
    }
    QImage texture(16, 16, QImage::Format_RGBA8888);
    texture.fill(Qt::transparent);
    QColor grain = colors[9]; grain.setAlpha(10);
    const QColor scratch(0, 0, 0, 5);
    for (int y = 0; y < texture.height(); ++y) for (int x = 0; x < texture.width(); ++x) {
        const int position = (x * 13 + y * 7 + x * y * 3) % 17;
        if (position < 4) texture.setPixelColor(x, y, grain);
        else if (position == 9) texture.setPixelColor(x, y, scratch);
    }
    QByteArray bytes; QBuffer buffer(&bytes);
    if (!texture.save(&buffer, "PNG")) return Failure{StoreError::InvalidData, "Cannot prepare the static theme texture."};
    appearance.colors.insert("texture", "data:image/png;base64," + QString::fromLatin1(bytes.toBase64()));
    return appearance;
}
QColor appearanceIdentity(const Appearance& appearance, const QString& value) {
    if (value.trimmed().isEmpty() || value == "-") return QColor(Qt::transparent);
    quint32 hash = 17;
    for (const auto character : value) hash = hash * 31 + character.unicode();
    const QStringList keys{"accentGlow", "accent", "success", "muted", "strongBorder", "accentMuted"};
    return appearance.colors.value(keys[(hash & 0x7FFFFFFF) % keys.size()]).value<QColor>();
}
QColor appearanceStatus(const Appearance& appearance, const QString& status) {
    if (status.isEmpty() || status=="-") return QColor(Qt::transparent);
    static const QStringList healthy{"Available","Complete","Ready","Running","Succeeded","Observed"};
    static const QStringList warning{"Pending","Terminating","Suspended","Warning"};
    static const QStringList critical{"CrashLoopBackOff","CreateContainerConfigError","CreateContainerError","ErrImagePull","Error","Failed","ImagePullBackOff","NotReady","OOMKilled","Unavailable"};
    return appearance.colors.value(healthy.contains(status) ? "success" : warning.contains(status) ? "warning" : critical.contains(status) ? "danger" : "unknown").value<QColor>();
}
} // namespace podlord
