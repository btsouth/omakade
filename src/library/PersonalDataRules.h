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

inline bool hasControlCharacters(const QString& value) {
  for (const QChar character : value) {
    if (character.category() == QChar::Other_Control) {
      return true;
    }
  }
  return false;
}
}  // namespace PersonalDataRules
