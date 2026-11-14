#include "workspace.h"
#include <QFile>

namespace podlord {
QString Workspace::applicationLicense() const {
    QFile resource(":/podlord/license.txt");
    return resource.open(QIODevice::ReadOnly) ? QString::fromUtf8(resource.readAll()) : QString("The bundled application license could not be read.");
}
QString Workspace::dependencyNotices() const {
    QString notices;
    for (const auto* name : {"THIRD-PARTY-NOTICES.txt", "LGPL-3.0.txt", "GPL-3.0.txt", "LGPL-2.1.txt", "OpenSSL-LICENSE.txt", "yaml-cpp-LICENSE.txt", "libvterm-LICENSE.txt"}) {
        QFile resource(QStringLiteral(":/podlord/licenses/") + QString::fromLatin1(name));
        if (!resource.open(QIODevice::ReadOnly)) return QStringLiteral("The bundled dependency notice could not be read: ") + QString::fromLatin1(name);
        notices += QString::fromLatin1(name) + QStringLiteral("\n\n") + QString::fromUtf8(resource.readAll()) + QStringLiteral("\n\n");
    }
    return notices;
}
}
