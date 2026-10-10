#pragma once
#include <QProcessEnvironment>

namespace GuideEnvironment {
// Called on a background/reconnection path, never on guide input. Reads only the
// two compositor variables from the user manager, preserving service-local paths.
QProcessEnvironment resolve(const QProcessEnvironment& inherited);
}
