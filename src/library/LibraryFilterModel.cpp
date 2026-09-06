#include "library/LibraryFilterModel.h"

#include "library/ConsoleCatalog.h"
#include "library/PersonalDataRules.h"

#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"

#include <algorithm>
#include <QRandomGenerator>
#include <QUuid>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>

LibraryFilterModel::LibraryFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent), m_cardSystems(ConsoleCatalog::defaultCardSystems()) {
  setDynamicSortFilter(true);
  sort(0);
}

void LibraryFilterModel::setSourceModel(QAbstractItemModel* source) {
  if (sourceModel() != nullptr) {
    disconnect(sourceModel(), nullptr, this, nullptr);
  }
  clearSelection();
  QSortFilterProxyModel::setSourceModel(source);
  if (source != nullptr) {
    connect(source, &QAbstractItemModel::modelReset, this, &LibraryFilterModel::rebuildProxy);
    connect(source, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
      const QList<int> filters{GameRoles::Title, GameRoles::Subtitle, GameRoles::Source,
          GameRoles::System, GameRoles::IsPortal, GameRoles::LinkedSources, GameRoles::Hidden,
          GameRoles::Favorite, GameRoles::Recent, GameRoles::Installed,
          GameRoles::CompletionStatus, GameRoles::Collections, GameRoles::Tags};
      if (roles.isEmpty() || std::any_of(roles.cbegin(), roles.cend(), [&filters](int role) { return filters.contains(role); })) rebuildProxy();
    });
    connect(source, &QAbstractItemModel::rowsInserted, this, &LibraryFilterModel::rebuildProxy);
    connect(source, &QAbstractItemModel::rowsRemoved, this, &LibraryFilterModel::rebuildProxy);
  }
  rebuildProxy();
  if (auto* games = qobject_cast<UnifiedGameModel*>(source)) {
    connect(games, &QAbstractItemModel::modelReset, this, &LibraryFilterModel::reconcileSelection);
    connect(games, &UnifiedGameModel::savedFiltersChanged, this, &LibraryFilterModel::savedFiltersChanged);
    connect(games, &UnifiedGameModel::collectionsChanged, this, [this] {
      beginFilterChange();
      recountSystems();
      endFilterChange(Direction::Rows);
      emit organizationNamesChanged();
      emit savedFiltersChanged();
    });
  }
  emit organizationNamesChanged();
  emit savedFiltersChanged();
}

namespace {
QString selectionKey(const QVariantMap& game) {
  return game.value("source").toString() + QChar::Null + game.value("runner").toString()
      + QChar::Null + game.value("appId").toString();
}
}

bool LibraryFilterModel::isSelected(int row) const {
  if (row < 0 || row >= rowCount()) return false;
  for (const auto& member : installations(row)) if (m_selectedIdentities.contains(selectionKey(member.toMap()))) return true;
  return false;
}

void LibraryFilterModel::toggleSelection(int row) {
  if (row < 0 || row >= rowCount()) return;
  if (isSelected(row)) {
    for (const auto& member : installations(row)) m_selectedIdentities.remove(selectionKey(member.toMap()));
  } else m_selectedIdentities.insert(selectionKey(get(row)));
  ++m_selectionRevision;
  emit selectionChanged();
}

void LibraryFilterModel::selectAllFiltered() {
  for (int row = 0; row < rowCount(); ++row) if (!isSelected(row)) m_selectedIdentities.insert(selectionKey(get(row)));
  ++m_selectionRevision;
  emit selectionChanged();
}

void LibraryFilterModel::clearSelection() {
  m_selectedIdentities.clear();
  ++m_selectionRevision;
  emit selectionChanged();
}

void LibraryFilterModel::reconcileSelection() {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  if (games) for (int row = 0; row < games->rowCount(); ++row) {
    bool found = false;
    for (const auto& member : games->installations(row)) {
      const QString key = selectionKey(member.toMap());
      if (!m_selectedIdentities.contains(key)) continue;
      if (found) m_selectedIdentities.remove(key);
      found = true;
    }
  }
  ++m_selectionRevision;
  emit selectionChanged();
}

bool LibraryFilterModel::applyBulkChanges(const QVariantMap& changes) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const int count = selectionCount();
  const bool okay = games && games->bulkOrganize(m_selectedIdentities.values(), changes);
  m_bulkMessage = okay ? QStringLiteral("Updated %1 selected games").arg(count)
      : QStringLiteral("Nothing changed. Check the values and storage, or clear and reselect games if an entry is no longer available.");
  if (okay) clearSelection();
  emit bulkMessageChanged();
  return okay;
}

namespace {
// A saved filter's sources: a list today, a single string in filters saved by earlier builds.
QStringList savedSources(const QVariantMap& state) {
  const QVariant value = state.value(QStringLiteral("source"));
  if (value.metaType().id() == QMetaType::QString) {
    const QString single = value.toString();
    return single.isEmpty() ? QStringList{} : QStringList{single};
  }
  QStringList sources;
  for (const QVariant& entry : value.toList()) {
    if (entry.metaType().id() != QMetaType::QString) return {};
    sources.append(entry.toString());
  }
  return value.metaType().id() == QMetaType::QStringList ? value.toStringList() : sources;
}
}  // namespace

void LibraryFilterModel::setSavedFilterMessage(const QString& value) {
  m_savedFilterMessage = value;
  emit savedFilterMessageChanged();
}

QVariantMap LibraryFilterModel::filterState() const {
  return {{"version", 1}, {"search", m_searchText}, {"mode", int(m_mode)},
      {"sort", int(m_sortMode)}, {"availability", int(m_availability)}, {"showHidden", m_showHidden},
      {"source", m_sourceFilters}, {"status", m_completionFilter},
      {"collection", m_collectionFilter}, {"tag", m_tagFilter}};
}

bool LibraryFilterModel::validFilterState(const QVariantMap& state) {
  if (state.size() != 10 || state.value("version").toInt() != 1) return false;
  for (const QString& key : {QStringLiteral("version"), QStringLiteral("mode"), QStringLiteral("sort"), QStringLiteral("availability")}) {
    const auto value = state.value(key);
    if (value.metaType().id() != QMetaType::Int && value.metaType().id() != QMetaType::LongLong && value.metaType().id() != QMetaType::Double) return false;
    if (value.toDouble() != value.toInt()) return false;
  }
  if (state.value("mode").toInt() < 0 || state.value("mode").toInt() > 3 ||
      state.value("sort").toInt() < 0 || state.value("sort").toInt() >= PersonalDataRules::kSortModeCount ||
      state.value("availability").toInt() < 0 || state.value("availability").toInt() > 2 ||
      state.value("showHidden").metaType().id() != QMetaType::Bool) return false;
  for (const QString& key : {QStringLiteral("search"), QStringLiteral("status"), QStringLiteral("collection"), QStringLiteral("tag")}) {
    const auto value = state.value(key);
    if (value.metaType().id() != QMetaType::QString || value.toString().size() > 4096 || value.toString().contains(QChar(0))) return false;
  }
  // Sources became a multi-select list. Accept a bare string too, so a filter saved by an
  // earlier build still applies instead of being reported as corrupt.
  const QVariant source = state.value("source");
  if (source.metaType().id() != QMetaType::QStringList && source.metaType().id() != QMetaType::QString &&
      source.metaType().id() != QMetaType::QVariantList) return false;
  const QStringList sources = savedSources(state);
  if (sources.size() > PersonalDataRules::kMaxSavedFilterSources) return false;
  for (const QString& name : sources) {
    if (name.size() > 4096 || name.contains(QChar(0))) return false;
  }
  return QStringList{"", "backlog", "playing", "completed", "abandoned"}.contains(state.value("status").toString());
}

QString LibraryFilterModel::filterWarning(const QVariantMap& state) const {
  if (!validFilterState(state)) return QStringLiteral("This saved filter has an unsupported format.");
  QStringList missing;
  const QString collection = state.value("collection").toString();
  const QString tag = state.value("tag").toString();
  if (!collection.isEmpty() && !collectionNames().contains(collection, Qt::CaseInsensitive)) missing << "collection: " + collection;
  if (!tag.isEmpty() && !tagNames().contains(tag, Qt::CaseInsensitive)) missing << "tag: " + tag;
  return missing.isEmpty() ? QString{} : QStringLiteral("Not currently available (%1). These criteria remain applied.").arg(missing.join(", "));
}

QVariantList LibraryFilterModel::savedFilters() const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  QVariantList result = games ? games->savedFilters() : QVariantList{};
  for (auto& value : result) {
    auto entry = value.toMap();
    entry.insert("warning", filterWarning(entry.value("state").toMap()));
    value = entry;
  }
  return result;
}

QString LibraryFilterModel::saveCurrentFilter(const QString& value) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const QString name = value.trimmed().normalized(QString::NormalizationForm_C);
  if (!games || savedFilters().size() >= PersonalDataRules::kMaxSavedFilters || name.isEmpty() ||
      name.size() > PersonalDataRules::kMaxFilterNameLength ||
      PersonalDataRules::hasControlCharacters(name) || !validFilterState(filterState())) {
    setSavedFilterMessage("Use a name of 1 to 100 characters; up to 500 filters can be saved."); return {};
  }
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!games->saveFilter(id, name, filterState())) {
    setSavedFilterMessage("Could not save. Choose a unique name and check available storage."); return {};
  }
  setSavedFilterMessage("Saved " + name);
  return id;
}

bool LibraryFilterModel::renameSavedFilter(const QString& id, const QString& value) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const QString name = value.trimmed().normalized(QString::NormalizationForm_C);
  if (games && !name.isEmpty() && name.size() <= PersonalDataRules::kMaxFilterNameLength &&
      !PersonalDataRules::hasControlCharacters(name)) {
    for (const auto& entry : games->savedFilters()) {
      const auto saved = entry.toMap();
      if (saved.value("id").toString() == id && games->saveFilter(id, name, saved.value("state").toMap())) {
        setSavedFilterMessage("Renamed " + name); return true;
      }
    }
  }
  setSavedFilterMessage("Could not rename. Use a unique name of 1 to 100 characters."); return false;
}

bool LibraryFilterModel::removeSavedFilter(const QString& id) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const bool removed = games && games->removeFilter(id);
  setSavedFilterMessage(removed ? "Saved filter deleted" : "Could not delete this saved filter");
  return removed;
}

bool LibraryFilterModel::applySavedFilter(const QString& id) {
  for (const auto& entry : savedFilters()) {
    const auto saved = entry.toMap();
    if (saved.value("id").toString() != id) continue;
    const auto state = saved.value("state").toMap();
    if (!validFilterState(state)) break;
    // Change the full query before invalidating so observers never see a partially applied view.
    m_searchText = state.value("search").toString();
    m_mode = Mode(state.value("mode").toInt());
    m_sortMode = SortMode(state.value("sort").toInt());
    m_availability = Availability(state.value("availability").toInt());
    m_showHidden = state.value("showHidden").toBool();
    m_sourceFilters = savedSources(state);
    m_completionFilter = state.value("status").toString();
    m_collectionFilter = state.value("collection").toString();
    m_tagFilter = state.value("tag").toString();
    invalidate();
    sort(0);
    emit searchTextChanged(); emit modeChanged(); emit sortModeChanged();
    emit availabilityChanged(); emit showHiddenChanged(); emit sourceFilterChanged();
    emit organizationFilterChanged();
    setSavedFilterMessage(saved.value("warning").toString());
    return true;
  }
  setSavedFilterMessage("This saved filter is missing or has an unsupported format.");
  return false;
}

int LibraryFilterModel::pickRandomGame() {
  QList<QPair<int, QString>> eligible;
  for (int row = 0; row < rowCount(); ++row) {
    if (!preferredInstallation(row).value(QStringLiteral("launchAvailable")).toBool()) continue;
    const QVariantMap game = get(row);
    const QString identity = QString::fromUtf8(QJsonDocument(QJsonArray{
        game.value(QStringLiteral("source")).toString(),
        game.value(QStringLiteral("runner")).toString(),
        game.value(QStringLiteral("appId")).toString()}).toJson(QJsonDocument::Compact));
    eligible.append({row, identity});
  }
  if (eligible.isEmpty()) return -1;
  if (eligible.size() > 1) {
    eligible.removeIf([this](const auto& entry) { return entry.second == m_lastRandomIdentity; });
  }
  const auto& chosen = eligible.at(QRandomGenerator::global()->bounded(int(eligible.size())));
  m_lastRandomIdentity = chosen.second;
  return chosen.first;
}

QVariant LibraryFilterModel::data(const QModelIndex& item, int role) const {
  const QModelIndex source = mapToSource(item);
  if (role == GameRoles::Subtitle && source.data(GameRoles::IsPortal).toBool()) {
    const int count = m_filteredSystemCounts.value(ConsoleCatalog::idFor(source.data(GameRoles::System).toString()));
    return count == 1 ? QStringLiteral("1 game") : QStringLiteral("%1 games").arg(count);
  }
  return QSortFilterProxyModel::data(item, role);
}

LibraryFilterModel::SortMode LibraryFilterModel::sortMode() const { return m_sortMode; }

void LibraryFilterModel::setSortMode(SortMode value) {
  if (m_sortMode == value) {
    return;
  }
  m_sortMode = value;
  const bool hadConsoleCards = hasConsoleCards();
  recountSystems();
  invalidate();
  if (hadConsoleCards != hasConsoleCards())
    emit consoleNavigationChanged();
  sort(0);
  emit sortModeChanged();
}

LibraryFilterModel::Availability LibraryFilterModel::availability() const { return m_availability; }

void LibraryFilterModel::setAvailability(Availability value) {
  if (m_availability == value) {
    return;
  }
  m_availability = value;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit availabilityChanged();
}

bool LibraryFilterModel::showHidden() const { return m_showHidden; }

void LibraryFilterModel::setShowHidden(bool value) {
  if (m_showHidden == value) {
    return;
  }
  m_showHidden = value;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit showHiddenChanged();
}

QStringList LibraryFilterModel::emulatorSources() {
  return {QStringLiteral("RetroArch"), QStringLiteral("Dolphin"), QStringLiteral("Ryujinx"),
          QStringLiteral("Cemu"), QStringLiteral("PCSX2"), QStringLiteral("shadPS4")};
}

QString LibraryFilterModel::sourceFilter() const {
  if (m_sourceFilters.isEmpty()) {
    return {};
  }
  if (sourcesSelected(emulatorSources()) && m_sourceFilters.size() == emulatorSources().size()) {
    return QStringLiteral("Emulated");
  }
  return m_sourceFilters.constFirst();
}

void LibraryFilterModel::setSourceFilter(const QString& value) {
  const QString normalized = value.trimmed();
  if (normalized.isEmpty()) {
    setSourceFilters({});
  } else if (normalized.compare(QStringLiteral("Emulated"), Qt::CaseInsensitive) == 0) {
    setSourceFilters(emulatorSources());
  } else {
    setSourceFilters({normalized});
  }
}

QStringList LibraryFilterModel::sourceFilters() const { return m_sourceFilters; }

bool LibraryFilterModel::sourceSelected(const QString& source) const {
  return std::any_of(m_sourceFilters.cbegin(), m_sourceFilters.cend(), [&source](const QString& selected) {
    return selected.compare(source, Qt::CaseInsensitive) == 0;
  });
}

bool LibraryFilterModel::sourcesSelected(const QStringList& sources) const {
  return !sources.isEmpty() && std::all_of(sources.cbegin(), sources.cend(),
                                           [this](const QString& source) { return sourceSelected(source); });
}

void LibraryFilterModel::toggleSource(const QString& source) {
  QStringList next = m_sourceFilters;
  if (sourceSelected(source)) {
    next.removeIf([&source](const QString& selected) { return selected.compare(source, Qt::CaseInsensitive) == 0; });
  } else {
    next.append(source.trimmed());
  }
  setSourceFilters(next);
}

void LibraryFilterModel::toggleSources(const QStringList& sources) {
  QStringList next = m_sourceFilters;
  const bool allSelected = sourcesSelected(sources);
  for (const QString& source : sources) {
    next.removeIf([&source](const QString& selected) { return selected.compare(source, Qt::CaseInsensitive) == 0; });
    if (!allSelected) {
      next.append(source);
    }
  }
  setSourceFilters(next);
}

void LibraryFilterModel::setSourceFilters(const QStringList& value) {
  QStringList normalized;
  for (const QString& entry : value) {
    const QString trimmed = entry.trimmed();
    if (!trimmed.isEmpty() && !normalized.contains(trimmed, Qt::CaseInsensitive)) {
      normalized.append(trimmed);
    }
  }
  if (m_sourceFilters == normalized) {
    return;
  }
  m_sourceFilters = normalized;
  const bool leftConsole = !m_consoleFilter.isEmpty();
  if (leftConsole) {
    m_consoleFilter.clear();
  }
  if (leftConsole) {
    rebuildProxy();
  } else {
    beginFilterChange();
    recountSystems();
    endFilterChange(Direction::Rows);
  }
  emit sourceFilterChanged();
  emit consoleNavigationChanged();
}

QString LibraryFilterModel::completionFilter() const { return m_completionFilter; }

void LibraryFilterModel::setCompletionFilter(const QString& value) {
  const QString normalized = value.trimmed().toLower();
  if (m_completionFilter == normalized) {
    return;
  }
  m_completionFilter = normalized;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit organizationFilterChanged();
}

QString LibraryFilterModel::collectionFilter() const { return m_collectionFilter; }

void LibraryFilterModel::setCollectionFilter(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_collectionFilter.compare(normalized, Qt::CaseInsensitive) == 0) {
    return;
  }
  m_collectionFilter = normalized;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit organizationFilterChanged();
}

QString LibraryFilterModel::tagFilter() const { return m_tagFilter; }

void LibraryFilterModel::setTagFilter(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_tagFilter.compare(normalized, Qt::CaseInsensitive) == 0) {
    return;
  }
  m_tagFilter = normalized;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit organizationFilterChanged();
}

QStringList LibraryFilterModel::collectionNames() const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  return games == nullptr ? QStringList{} : games->collectionNames();
}

QStringList LibraryFilterModel::cardSystems() const { return m_cardSystems; }

void LibraryFilterModel::setCardSystems(const QStringList& value) {
  QStringList normalized;
  for (const QString& system : value) {
    const QString id = ConsoleCatalog::idFor(system);
    if (!id.isEmpty() && !normalized.contains(id)) {
      normalized.append(id);
    }
  }
  if (m_cardSystems == normalized) {
    return;
  }
  m_cardSystems = normalized;
  if (!m_consoleFilter.isEmpty() && !m_cardSystems.contains(m_consoleFilter)) {
    m_consoleFilter.clear();
  }
  recountSystems();
  rebuildProxy();
  emit consoleNavigationChanged();
}

QStringList LibraryFilterModel::fixedCardSystems() const { return m_fixedCardSystems; }

void LibraryFilterModel::setFixedCardSystems(const QStringList& value) {
  if (m_fixedCardSystems == value) return;
  m_fixedCardSystems = value;
  rebuildProxy();
  emit consoleNavigationChanged();
}

bool LibraryFilterModel::expandConsoles() const { return m_expandConsoles; }

void LibraryFilterModel::setExpandConsoles(bool value) {
  if (m_expandConsoles == value) {
    return;
  }
  m_expandConsoles = value;
  rebuildProxy();
  emit consoleNavigationChanged();
}

int LibraryFilterModel::consoleExpandLimit() const { return m_consoleExpandLimit; }

void LibraryFilterModel::setConsoleExpandLimit(int value) {
  value = qMax(1, value);
  if (m_consoleExpandLimit == value) {
    return;
  }
  m_consoleExpandLimit = value;
  if (m_expandConsoles) {
    rebuildProxy();
  }
  emit consoleNavigationChanged();
}

bool LibraryFilterModel::hasConsoleCards() const {
  return m_singleSourceSystem.isEmpty() && m_consolePortalsEnabled && m_consoleFilter.isEmpty() &&
         std::any_of(m_systemCounts.keyBegin(), m_systemCounts.keyEnd(),
                     [this](const QString& system) { return m_systemCounts.value(system, 0) > 0; });
}

bool LibraryFilterModel::setPinned(int row, bool pinned) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setPinned(mapToSource(index(row, 0)).row(), pinned);
}

// Counts are based on visible members, including the active library filters.
void LibraryFilterModel::recountSystems() {
  m_portalSystems.clear();
  m_systemCounts.clear();
  m_filteredSystemCounts.clear();
  m_singleSourceSystem.clear();
  QSet<QString> selectedSourceSystems;
  const QAbstractItemModel* source = sourceModel();
  if (source == nullptr) {
    return;
  }
  for (int row = 0; row < source->rowCount(); ++row) {
    const QModelIndex game = source->index(row, 0);
    if (game.data(GameRoles::IsPortal).toBool()) {
      m_portalSystems.insert(ConsoleCatalog::idFor(game.data(GameRoles::System).toString()));
      continue;
    }
    const QString system = ConsoleCatalog::idFor(game.data(GameRoles::System).toString());
    if (!system.isEmpty()) {
      // Count the source's systems before search/favorites filtering, so those
      // filters cannot unexpectedly flatten a multi-system source.
      if (m_sourceFilters.size() == 1 &&
          (sourceSelected(game.data(GameRoles::Source).toString()) ||
           game.data(GameRoles::LinkedSources).toString().split(QStringLiteral(" + "))
               .contains(m_sourceFilters.first(), Qt::CaseInsensitive)))
        selectedSourceSystems.insert(system);
      m_systemCounts[system] += 1;
      if (matchesGameFilters(game)) m_filteredSystemCounts[system] += 1;
    }
  }
  if (selectedSourceSystems.size() == 1)
    m_singleSourceSystem = *selectedSourceSystems.cbegin();
}

bool LibraryFilterModel::systemExpanded(const QString& system) const {
  return (!m_singleSourceSystem.isEmpty() && system == m_singleSourceSystem) ||
         (m_expandConsoles && !m_fixedCardSystems.contains(system));
}

bool LibraryFilterModel::consolePortalsEnabled() const { return m_consolePortalsEnabled; }

void LibraryFilterModel::setConsolePortalsEnabled(bool value) {
  if (m_consolePortalsEnabled == value) {
    return;
  }
  m_consolePortalsEnabled = value;
  if (!value) {
    m_consoleFilter.clear();
  }
  rebuildProxy();
  emit consoleNavigationChanged();
}

QString LibraryFilterModel::consoleFilter() const { return m_consoleFilter; }

void LibraryFilterModel::setConsoleFilter(const QString& value) {
  const QString normalized = ConsoleCatalog::idFor(value.trimmed());
  if (m_consoleFilter == normalized) {
    return;
  }
  m_consoleFilter = normalized;
  rebuildProxy();
  emit consoleNavigationChanged();
}

void LibraryFilterModel::rebuildProxy() {
  if (sourceModel() == nullptr) {
    return;
  }
  // Entering or leaving a console swaps more than a thousand rows at once.
  // beginFilterChange/endFilterChange would replay that as one insert or
  // remove per row and freeze the grid, so drop the mapping and let the proxy
  // rebuild it in one pass. This is a single layout change for the view, with
  // no detach from the source model in between.
  const bool hadConsoleCards = hasConsoleCards();
  recountSystems();
  invalidate();
  if (hadConsoleCards != hasConsoleCards())
    emit consoleNavigationChanged();
}

QString LibraryFilterModel::consoleTitle() const {
  return m_consoleFilter.isEmpty() ? QString{} : ConsoleCatalog::displayNameFor(m_consoleFilter);
}

QStringList LibraryFilterModel::tagNames() const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  return games == nullptr ? QStringList{} : games->tagNames();
}

QString LibraryFilterModel::searchText() const { return m_searchText; }

void LibraryFilterModel::setSearchText(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_searchText == normalized) {
    return;
  }

  m_searchText = normalized;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit searchTextChanged();
}

LibraryFilterModel::Mode LibraryFilterModel::mode() const { return m_mode; }

void LibraryFilterModel::setMode(Mode value) {
  if (m_mode == value) {
    return;
  }

  m_mode = value;
  beginFilterChange();
  recountSystems();
  endFilterChange(Direction::Rows);
  emit modeChanged();
}

QVariantMap LibraryFilterModel::get(int row) const {
  if (row < 0 || row >= rowCount()) {
    return {};
  }

  QVariantMap result;
  const auto roles = sourceModel()->roleNames();
  for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
    result.insert(QString::fromUtf8(iterator.value()), data(index(row, 0), iterator.key()));
  }
  return result;
}

int LibraryFilterModel::indexOf(const QString& source, const QString& runner,
                                const QString& appId) const {
  if (source.isEmpty() || appId.isEmpty()) {
    return -1;
  }
  const QString normalizedRunner = runner.isNull() ? QStringLiteral("") : runner;
  for (int row = 0; row < rowCount(); ++row) {
    const QModelIndex game = index(row, 0);
    if (game.data(GameRoles::AppId).toString() != appId ||
        game.data(GameRoles::Source).toString() != source) {
      if (game.data(GameRoles::Linked).toBool()) {
        for (const QVariant& value : installations(row)) {
          const QVariantMap installation = value.toMap();
          if (installation.value(QStringLiteral("source")).toString() == source &&
              installation.value(QStringLiteral("runner")).toString() == normalizedRunner &&
              installation.value(QStringLiteral("appId")).toString() == appId) return row;
        }
      }
      continue;
    }
    const QString gameRunner = game.data(GameRoles::Runner).toString();
    if ((gameRunner.isNull() ? QStringLiteral("") : gameRunner) == normalizedRunner) {
      return row;
    }
  }
  return -1;
}

void LibraryFilterModel::toggleFavorite(int row) {
  if (row < 0 || row >= rowCount()) {
    return;
  }

  QMetaObject::invokeMethod(sourceModel(), "toggleFavorite",
                            Q_ARG(int, mapToSource(index(row, 0)).row()));
}

void LibraryFilterModel::toggleHidden(int row) {
  if (row < 0 || row >= rowCount()) {
    return;
  }
  QMetaObject::invokeMethod(sourceModel(), "toggleHidden",
                            Q_ARG(int, mapToSource(index(row, 0)).row()));
}

bool LibraryFilterModel::setCustomCover(int row, const QUrl& sourceUrl) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->setCustomCover(mapToSource(index(row, 0)).row(), sourceUrl);
}

bool LibraryFilterModel::resetCustomCover(int row) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->resetCustomCover(mapToSource(index(row, 0)).row());
}

bool LibraryFilterModel::setCustomArtwork(int row, const QString& kind, const QUrl& sourceUrl) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games && row >= 0 && row < rowCount() &&
         games->setCustomArtwork(mapToSource(index(row, 0)).row(), kind, sourceUrl);
}
bool LibraryFilterModel::resetCustomArtwork(int row, const QString& kind) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games && row >= 0 && row < rowCount() &&
         games->resetCustomArtwork(mapToSource(index(row, 0)).row(), kind);
}

QVariantList LibraryFilterModel::installations(int row) const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return {};
  }
  return games->installations(mapToSource(index(row, 0)).row());
}

QVariantMap LibraryFilterModel::preferredInstallation(int row) const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount()
             ? games->preferredInstallation(mapToSource(index(row, 0)).row())
             : QVariantMap{};
}

bool LibraryFilterModel::setPreferredInstallation(int row, const QString& source,
                                                  const QString& runner, const QString& appId) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setPreferredInstallation(mapToSource(index(row, 0)).row(), source, runner, appId);
}

QVariantList LibraryFilterModel::linkCandidates(int row, const QString& search) const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return {};
  }
  return games->linkCandidates(mapToSource(index(row, 0)).row(), search);
}

bool LibraryFilterModel::linkGames(int row, const QString& source, const QString& runner,
                                   const QString& appId) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->linkGames(mapToSource(index(row, 0)).row(), source, runner, appId);
}

bool LibraryFilterModel::recordLaunch(int row, const QString& source, const QString& runner,
                                      const QString& appId) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->recordLaunch(mapToSource(index(row, 0)).row(), source, runner, appId);
}

bool LibraryFilterModel::unlinkGames(int row) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->unlinkGames(mapToSource(index(row, 0)).row());
}

bool LibraryFilterModel::setCompletionStatus(int row, const QString& status) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setCompletionStatus(mapToSource(index(row, 0)).row(), status);
}

bool LibraryFilterModel::setTags(int row, const QString& tags) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setTags(mapToSource(index(row, 0)).row(), tags);
}

bool LibraryFilterModel::createCollection(const QString& name) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && games->createCollection(name);
}

bool LibraryFilterModel::deleteCollection(const QString& name) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || !games->deleteCollection(name)) {
    return false;
  }
  if (m_collectionFilter.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
    setCollectionFilter({});
  }
  return true;
}

bool LibraryFilterModel::setCollectionMembership(int row, const QString& name, bool included) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setCollectionMembership(mapToSource(index(row, 0)).row(), name, included);
}

bool LibraryFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);

  const bool isPortal = sourceIndex.data(GameRoles::IsPortal).toBool();
  const QString system = ConsoleCatalog::idFor(sourceIndex.data(GameRoles::System).toString());
  // A system is either spread across the library as tiles or kept behind a
  // console card. Pinned games stay in the library either way, and the expand
  // toggle opens every following system, regardless of collection size.
  const bool cardGame = !isPortal && !system.isEmpty() && m_cardSystems.contains(system) && m_portalSystems.contains(system);
  if (m_consolePortalsEnabled) {
    if (m_consoleFilter.isEmpty()) {
      if (isPortal && systemExpanded(system)) {
        return false;
      }
      if (cardGame && !systemExpanded(system) && !sourceIndex.data(GameRoles::Pinned).toBool()) {
        return false;
      }
    } else if (isPortal || system != m_consoleFilter) {
      return false;
    }
  } else if (isPortal) {
    return false;
  }

  if (isPortal) return m_filteredSystemCounts.value(system) > 0;
  return matchesGameFilters(sourceIndex);
}

bool LibraryFilterModel::matchesGameFilters(const QModelIndex& sourceIndex) const {
  const QString primarySource = sourceIndex.data(GameRoles::Source).toString();
  const QVariant installedValue = sourceIndex.data(GameRoles::Installed);
  const bool installed = !installedValue.isValid() || installedValue.toBool();
  if ((m_availability == Availability::Installed && !installed) ||
      (m_availability == Availability::ReadyToInstall && installed)) {
    return false;
  }

  if (!m_sourceFilters.isEmpty()) {
    const QStringList linkedSources =
        sourceIndex.data(GameRoles::LinkedSources).toString().split(QStringLiteral(" + "));
    if (!sourceSelected(primarySource) &&
        std::none_of(linkedSources.cbegin(), linkedSources.cend(),
                     [this](const QString& source) { return sourceSelected(source); })) {
      return false;
    }
  }

  const bool hidden = sourceIndex.data(GameRoles::Hidden).toBool();
  if (m_mode == Mode::Hidden && !hidden) {
    return false;
  }
  if (m_mode != Mode::Hidden && !m_showHidden && hidden) {
    return false;
  }

  if (m_mode == Mode::Favorites && !sourceIndex.data(GameRoles::Favorite).toBool()) {
    return false;
  }
  if (m_mode == Mode::Recent && !sourceIndex.data(GameRoles::Recent).toBool()) {
    return false;
  }

  if (!m_completionFilter.isEmpty() &&
      sourceIndex.data(GameRoles::CompletionStatus).toString() != m_completionFilter) {
    return false;
  }
  const auto containsCaseInsensitive = [](const QStringList& values, const QString& expected) {
    return std::any_of(values.cbegin(), values.cend(), [&expected](const QString& value) {
      return value.compare(expected, Qt::CaseInsensitive) == 0;
    });
  };
  if (!m_collectionFilter.isEmpty() &&
      !containsCaseInsensitive(sourceIndex.data(GameRoles::Collections).toStringList(),
                               m_collectionFilter)) {
    return false;
  }
  if (!m_tagFilter.isEmpty() &&
      !containsCaseInsensitive(sourceIndex.data(GameRoles::Tags).toStringList(), m_tagFilter)) {
    return false;
  }

  if (m_searchText.isEmpty()) {
    return true;
  }

  const QString title = sourceIndex.data(GameRoles::Title).toString();
  const QString subtitle = sourceIndex.data(GameRoles::Subtitle).toString();
  const QString tags = sourceIndex.data(GameRoles::Tags).toStringList().join(QLatin1Char(' '));
  return title.contains(m_searchText, Qt::CaseInsensitive) ||
         subtitle.contains(m_searchText, Qt::CaseInsensitive) ||
         tags.contains(m_searchText, Qt::CaseInsensitive);
}

bool LibraryFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  if (m_sortMode == SortMode::Popularity) {
    const double a = left.data(GameRoles::Popularity).isValid() ? left.data(GameRoles::Popularity).toDouble() : -1;
    const double b = right.data(GameRoles::Popularity).isValid() ? right.data(GameRoles::Popularity).toDouble() : -1;
    if (a != b) return a > b;
  }
  if (m_sortMode == SortMode::Rating) {
    const int a = left.data(GameRoles::Rating).isValid() ? left.data(GameRoles::Rating).toInt() : -1;
    const int b = right.data(GameRoles::Rating).isValid() ? right.data(GameRoles::Rating).toInt() : -1;
    if (a != b) return a > b;
  }
  if (m_sortMode == SortMode::RecentlyPlayed) {
    const qint64 leftPlayed = left.data(GameRoles::LastPlayed).toLongLong();
    const qint64 rightPlayed = right.data(GameRoles::LastPlayed).toLongLong();
    if (leftPlayed != rightPlayed) {
      return leftPlayed > rightPlayed;
    }
  }
  if (m_sortMode == SortMode::Playtime) {
    const int leftHours = left.data(GameRoles::Hours).toInt();
    const int rightHours = right.data(GameRoles::Hours).toInt();
    if (leftHours != rightHours) {
      return leftHours > rightHours;
    }
  }
  const int titleOrder = left.data(GameRoles::Title).toString().localeAwareCompare(right.data(GameRoles::Title).toString());
  if (titleOrder != 0) return titleOrder < 0;
  return left.data(GameRoles::MetadataKey).toString() < right.data(GameRoles::MetadataKey).toString();
}
