#pragma once
#include "library/ConsoleCatalog.h"
#include "library/PersonalDataRules.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
namespace SavedFilterRules {
inline bool integer(const QJsonValue& value, double minimum, double maximum) {
  return value.isDouble() && std::isfinite(value.toDouble()) &&
         value.toDouble() == std::floor(value.toDouble()) && value.toDouble() >= minimum &&
         value.toDouble() <= maximum;
}
inline bool text(const QJsonValue& value, int limit) {
  return value.isString() && value.toString().size() <= limit &&
         !value.toString().contains(QChar::Null);
}
inline bool valid(const QJsonObject& state) {
  if (!integer(state.value("version"), 1, 3) ||
      state.size() != (state.value("version").toInt() == 1 ? 10 : state.value("version").toInt() == 2 ? 14 : 15) ||
      !integer(state.value("mode"), 0, 3) ||
      !integer(state.value("sort"), 0, PersonalDataRules::kSortModeCount - 1) ||
      !integer(state.value("availability"), 0, 2) || !state.value("showHidden").isBool())
    return false;
  for (const QString& key : {QStringLiteral("search"), QStringLiteral("status"),
                             QStringLiteral("collection"), QStringLiteral("tag")})
    if (!text(state.value(key), 4096))
      return false;
  if (state.value("version").toInt() >= 2) {
    if (!text(state.value("genre"), 4096) || !text(state.value("platform"), 4096) ||
        !text(state.value("decade"), 4096))
      return false;
    if (!text(state.value("console"), 4096))
      return false;
    const QString console = state.value("console").toString();
    if (!console.isEmpty() && ConsoleCatalog::idFor(console) != console)
      return false;
    const QString decade = state.value("decade").toString();
    if (!decade.isEmpty() && !QRegularExpression("^[12][0-9]{2}0s$").match(decade).hasMatch())
      return false;
  }
  if (state.value("version").toInt() == 3 &&
      (!state.value("review").isString() ||
       !QStringList{"", "identification", "artwork", "either", "unavailable", "duplicates"}.contains(state.value("review").toString())))
    return false;
  // Sources are a multi-select list. A bare string is still accepted so a filter saved by an
  // earlier build exports instead of failing the whole archive.
  const QJsonValue sources = state.value("source");
  if (sources.isArray()) {
    if (sources.toArray().size() > PersonalDataRules::kMaxSavedFilterSources)
      return false;
    for (const auto& name : sources.toArray())
      if (!text(name, 4096))
        return false;
  } else if (!text(sources, 4096)) {
    return false;
  }
  return QStringList{"", "backlog", "playing", "completed", "abandoned"}.contains(
      state.value("status").toString());
}
} // namespace SavedFilterRules
