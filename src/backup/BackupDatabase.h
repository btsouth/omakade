#pragma once
#include "backup/BackupArchive.h"

class BackupDatabase {
public:
  enum class Mode { Merge, Replace };
  // The error a restore reports while omakade-sessiond holds the database.
  static constexpr const char* RecorderRunning =
      "Stop the play-session recorder before restoring, then retry.";
  // Internal restore primitive. The coordinator must own the app's startup gate,
  // create the recovery backup, and coordinate settings before calling this.
  // No source models may be live while their personal records are replaced.
  static bool restore(const QString& databasePath, const BackupPayload& payload, Mode mode,
                      QString* error = nullptr);
};
