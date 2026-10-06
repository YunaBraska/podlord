#pragma once
#include <QtCore/qglobal.h>

namespace podlord {
/** Configure the image atlas before creating a GUI, preserving explicit Qt overrides. */
inline bool configureGraphics() {
    for (const auto* name : {"QSG_ATLAS_WIDTH", "QSG_ATLAS_HEIGHT"})
        if (!qEnvironmentVariableIsSet(name) && !qputenv(name, "1024")) return false;
    return true;
}
}
