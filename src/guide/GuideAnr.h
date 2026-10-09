#pragma once
#include <QProcessEnvironment>
#include <QString>

// Compositor-owned leases serialize concurrent guards. Either pipe endpoint can
// release the same token after the other endpoint dies.
namespace GuideAnr {
bool acquire(const QString& token, const QProcessEnvironment& environment);
void release(const QString& token, const QProcessEnvironment& environment);
}
