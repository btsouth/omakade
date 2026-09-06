#pragma once

#include <QChar>
#include <QString>

// Limits and character rules for personal data the user types in. The models
// apply them when data is written and BackupArchive applies the same rules when
// an archive is validated, so a library the app accepted always exports.
namespace PersonalDataRules {
constexpr int kMaxManualTitleLength = 200;
constexpr int kMaxManualArguments = 256;
constexpr int kMaxFilterNameLength = 100;
constexpr int kMaxSavedFilters = 500;
// Library sort modes: title, recent, playtime, rating, popularity. Keep in step with
// AppSettings' sort-mode names and the QML sort cycle.
constexpr int kSortModeCount = 5;
// A saved filter records the multi-select source chips, so it stores a list of source names.
constexpr int kMaxSavedFilterSources = 64;

inline bool hasControlCharacters(const QString& value) {
  for (const QChar character : value) {
    if (character.category() == QChar::Other_Control) {
      return true;
    }
  }
  return false;
}
}  // namespace PersonalDataRules
