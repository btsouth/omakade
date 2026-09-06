#pragma once

#include <QHash>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QUrl>
#include <QSet>

class LibraryFilterModel final : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
  Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY selectionChanged)
  Q_PROPERTY(QString bulkMessage READ bulkMessage NOTIFY bulkMessageChanged)
  Q_PROPERTY(QVariantList savedFilters READ savedFilters NOTIFY savedFiltersChanged)
  Q_PROPERTY(QString savedFilterMessage READ savedFilterMessage NOTIFY savedFilterMessageChanged)
  Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
  Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
  Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
  Q_PROPERTY(
      Availability availability READ availability WRITE setAvailability NOTIFY availabilityChanged)
  Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
  Q_PROPERTY(
      QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY sourceFilterChanged)
  Q_PROPERTY(QStringList sourceFilters READ sourceFilters WRITE setSourceFilters NOTIFY
                 sourceFilterChanged)
  Q_PROPERTY(QStringList emulatorSources READ emulatorSources CONSTANT)
  Q_PROPERTY(QStringList cardSystems READ cardSystems WRITE setCardSystems NOTIFY
                 consoleNavigationChanged)
  Q_PROPERTY(QStringList fixedCardSystems READ fixedCardSystems WRITE setFixedCardSystems NOTIFY consoleNavigationChanged)
  Q_PROPERTY(bool expandConsoles READ expandConsoles WRITE setExpandConsoles NOTIFY
                 consoleNavigationChanged)
  Q_PROPERTY(int consoleExpandLimit READ consoleExpandLimit WRITE setConsoleExpandLimit NOTIFY
                 consoleNavigationChanged)
  Q_PROPERTY(bool hasConsoleCards READ hasConsoleCards NOTIFY consoleNavigationChanged)
  Q_PROPERTY(QString completionFilter READ completionFilter WRITE setCompletionFilter NOTIFY
                 organizationFilterChanged)
  Q_PROPERTY(QString collectionFilter READ collectionFilter WRITE setCollectionFilter NOTIFY
                 organizationFilterChanged)
  Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter NOTIFY organizationFilterChanged)
  Q_PROPERTY(QStringList collectionNames READ collectionNames NOTIFY organizationNamesChanged)
  Q_PROPERTY(QStringList tagNames READ tagNames NOTIFY organizationNamesChanged)
  Q_PROPERTY(bool consolePortalsEnabled READ consolePortalsEnabled WRITE setConsolePortalsEnabled
                 NOTIFY consoleNavigationChanged)
  Q_PROPERTY(QString consoleFilter READ consoleFilter WRITE setConsoleFilter NOTIFY
                 consoleNavigationChanged)
  Q_PROPERTY(QString consoleTitle READ consoleTitle NOTIFY consoleNavigationChanged)

public:
  enum class Mode { All = 0, Favorites, Recent, Hidden };
  Q_ENUM(Mode)
  enum class SortMode { Title = 0, RecentlyPlayed, Playtime, Rating, Popularity };
  Q_ENUM(SortMode)
  enum class Availability { Installed = 0, AllGames, ReadyToInstall };
  Q_ENUM(Availability)

  explicit LibraryFilterModel(QObject* parent = nullptr);
  QVariant data(const QModelIndex& index, int role) const override;
  void setSourceModel(QAbstractItemModel* sourceModel) override;

  [[nodiscard]] QString searchText() const;
  void setSearchText(const QString& value);

  [[nodiscard]] Mode mode() const;
  void setMode(Mode value);
  [[nodiscard]] SortMode sortMode() const;
  void setSortMode(SortMode value);
  [[nodiscard]] Availability availability() const;
  void setAvailability(Availability value);
  [[nodiscard]] bool showHidden() const;
  void setShowHidden(bool value);
  // One source, several sources, or the "Emulated" group; an empty list means every source.
  [[nodiscard]] QString sourceFilter() const;
  void setSourceFilter(const QString& value);
  [[nodiscard]] QStringList sourceFilters() const;
  void setSourceFilters(const QStringList& value);
  Q_INVOKABLE void toggleSource(const QString& source);
  Q_INVOKABLE void toggleSources(const QStringList& sources);
  Q_INVOKABLE bool sourceSelected(const QString& source) const;
  Q_INVOKABLE bool sourcesSelected(const QStringList& sources) const;
  [[nodiscard]] static QStringList emulatorSources();
  // Systems that sit behind a console card instead of appearing as tiles.
  [[nodiscard]] QStringList cardSystems() const;
  void setCardSystems(const QStringList& value);
  // Spreads console cards into their games, except explicit console overrides.
  [[nodiscard]] QStringList fixedCardSystems() const;
  void setFixedCardSystems(const QStringList& value);
  [[nodiscard]] bool expandConsoles() const;
  void setExpandConsoles(bool value);
  [[nodiscard]] int consoleExpandLimit() const;
  void setConsoleExpandLimit(int value);
  [[nodiscard]] bool hasConsoleCards() const;
  Q_INVOKABLE bool setPinned(int row, bool pinned);
  [[nodiscard]] QString completionFilter() const;
  void setCompletionFilter(const QString& value);
  [[nodiscard]] QString collectionFilter() const;
  void setCollectionFilter(const QString& value);
  [[nodiscard]] QString tagFilter() const;
  void setTagFilter(const QString& value);
  [[nodiscard]] QStringList collectionNames() const;
  [[nodiscard]] QStringList tagNames() const;
  [[nodiscard]] bool consolePortalsEnabled() const;
  void setConsolePortalsEnabled(bool value);
  [[nodiscard]] QString consoleFilter() const;
  void setConsoleFilter(const QString& value);
  [[nodiscard]] QString consoleTitle() const;

  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE int pickRandomGame();
  int selectionCount() const { return m_selectedIdentities.size(); }
  int selectionRevision() const { return m_selectionRevision; }
  QString bulkMessage() const { return m_bulkMessage; }
  Q_INVOKABLE bool isSelected(int row) const;
  Q_INVOKABLE void toggleSelection(int row);
  Q_INVOKABLE void selectAllFiltered();
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE bool applyBulkChanges(const QVariantMap& changes);
  QVariantList savedFilters() const;
  QString savedFilterMessage() const { return m_savedFilterMessage; }
  Q_INVOKABLE QString saveCurrentFilter(const QString& name);
  Q_INVOKABLE bool renameSavedFilter(const QString& id, const QString& name);
  Q_INVOKABLE bool removeSavedFilter(const QString& id);
  Q_INVOKABLE bool applySavedFilter(const QString& id);
  QVariantMap filterState() const;
  Q_INVOKABLE int indexOf(const QString& source, const QString& runner, const QString& appId) const;
  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE bool setCustomCover(int row, const QUrl& sourceUrl);
  Q_INVOKABLE bool resetCustomCover(int row);
  Q_INVOKABLE bool setCustomArtwork(int row, const QString& kind, const QUrl& sourceUrl);
  Q_INVOKABLE bool resetCustomArtwork(int row, const QString& kind);
  Q_INVOKABLE QVariantList installations(int row) const;
  Q_INVOKABLE QVariantMap preferredInstallation(int row) const;
  Q_INVOKABLE bool setPreferredInstallation(int row, const QString& source, const QString& runner,
                                            const QString& appId);
  Q_INVOKABLE QVariantList linkCandidates(int row, const QString& search) const;
  Q_INVOKABLE bool recordLaunch(int row, const QString& source, const QString& runner,
                                const QString& appId);
  Q_INVOKABLE bool linkGames(int row, const QString& source, const QString& runner,
                             const QString& appId);
  Q_INVOKABLE bool unlinkGames(int row);
  Q_INVOKABLE bool setCompletionStatus(int row, const QString& status);
  Q_INVOKABLE bool setTags(int row, const QString& tags);
  Q_INVOKABLE bool createCollection(const QString& name);
  Q_INVOKABLE bool deleteCollection(const QString& name);
  Q_INVOKABLE bool setCollectionMembership(int row, const QString& name, bool included);

signals:
  void selectionChanged();
  void bulkMessageChanged();
  void savedFiltersChanged();
  void savedFilterMessageChanged();
  void searchTextChanged();
  void modeChanged();
  void sortModeChanged();
  void availabilityChanged();
  void showHiddenChanged();
  void sourceFilterChanged();
  void organizationFilterChanged();
  void organizationNamesChanged();
  void consoleNavigationChanged();

protected:
  [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                      const QModelIndex& sourceParent) const override;
  [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
  void rebuildProxy();
  void recountSystems();
  bool matchesGameFilters(const QModelIndex& game) const;
  [[nodiscard]] bool systemExpanded(const QString& system) const;

  void reconcileSelection();
  QSet<QString> m_selectedIdentities;
  int m_selectionRevision = 0;
  QString m_bulkMessage;
  void setSavedFilterMessage(const QString& value);
  QString filterWarning(const QVariantMap& state) const;
  static bool validFilterState(const QVariantMap& state);
  QString m_savedFilterMessage;
  QString m_lastRandomIdentity;
  QString m_searchText;
  Mode m_mode = Mode::All;
  SortMode m_sortMode = SortMode::Title;
  Availability m_availability = Availability::Installed;
  bool m_showHidden = false;
  QStringList m_sourceFilters;
  QStringList m_cardSystems;
  QStringList m_fixedCardSystems;
  bool m_expandConsoles = false;
  int m_consoleExpandLimit = 200;
  QHash<QString, int> m_systemCounts;
  QHash<QString, int> m_filteredSystemCounts;
  QSet<QString> m_portalSystems;
  QString m_singleSourceSystem;
  QString m_completionFilter;
  QString m_collectionFilter;
  QString m_tagFilter;
  bool m_consolePortalsEnabled = true;
  QString m_consoleFilter;
};
