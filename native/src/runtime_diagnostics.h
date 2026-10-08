#pragma once
#include <QVariantList>

namespace podlord {
// Samples this process only; unavailable OS counters are reported explicitly.
QVariantList runtimeDiagnostics();
}
