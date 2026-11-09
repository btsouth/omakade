#pragma once

#include "backup/BackupRecovery.h"

#include <functional>

class BackupStartup;
class OmarchyTheme;
class QGuiApplication;
class QQuickWindow;

// Finishes or undoes a library restore that an earlier run left part way, in its own window,
// before the library opens. False when the restore is still unresolved and the app must not
// open. `ready` lets the restore self-test drive the window; the app passes none.
bool runRestoreStartup(QGuiApplication& application, OmarchyTheme& theme,
                       const BackupRecovery::Paths& paths, bool couchMode,
                       const std::function<void(QQuickWindow*, BackupStartup*)>& ready = {});
