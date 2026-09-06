#pragma once

#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVector>

class GameMetadata;

class UnifiedGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QStringList collectionNames READ collectionNames NOTIFY collectionsChanged)
  Q_PROPERTY(QStringList tagNames READ tagNames NOTIFY collectionsChanged)

public:
  explicit UnifiedGameModel(const QString& databasePath = {}, QObject* parent = nullptr);
  ~UnifiedGameModel() override;

  void setMetadata(GameMetadata* metadata);
  void addSourceModel(QAbstractItemModel* model);
  void setSourceEnabled(const QString& source, bool enabled);
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE bool setCustomCover(int row, const QUrl& sourceUrl);
  Q_INVOKABLE bool resetCustomCover(int row);
  Q_INVOKABLE QVariantList installations(int row) const;
  Q_INVOKABLE QVariantList linkCandidates(int row, const QString& search) const;
  Q_INVOKABLE bool recordLaunch(int row, const QString& source, const QString& runner,
                                const QString& appId);
  Q_INVOKABLE bool linkGames(int row, const QString& source, const QString& runner,
                             const QString& appId);
  Q_INVOKABLE bool unlinkGames(int row);
  Q_INVOKABLE bool setCompletionStatus(int row, const QString& status);
  // Keeps one game in the main library even when its system sits behind a console card.
  Q_INVOKABLE bool setPinned(int row, bool pinned);
  Q_INVOKABLE bool setTags(int row, const QString& tags);
  Q_INVOKABLE bool createCollection(const QString& name);
  Q_INVOKABLE bool deleteCollection(const QString& name);
  Q_INVOKABLE bool setCollectionMembership(int row, const QString& name, bool included);
  [[nodiscard]] QStringList collectionNames() const;
  [[nodiscard]] QStringList tagNames() const;

signals:
  void collectionsChanged();

private:
  struct SourceRow {
    QAbstractItemModel* model = nullptr;
    int row = -1;
  };
  [[nodiscard]] SourceRow mapRow(int row) const;
  [[nodiscard]] QString gameKey(const SourceRow& source) const;
  [[nodiscard]] SourceRow sourceForKey(const QString& key) const;
  [[nodiscard]] bool sourceEnabled(const SourceRow& source) const;
  [[nodiscard]] QVector<SourceRow> groupRows(const SourceRow& source) const;
  [[nodiscard]] QVariantMap gameMap(const SourceRow& source) const;
  void rebuildRows();
  bool openArtworkDatabase(const QString& path);
  void loadArtworkOverrides();
  void loadLinks();
  void loadLaunchActivity();
  void loadOrganization();
  void loadCollections();

  struct OrganizationState {
    QString status;
    QStringList tags;
    bool pinned = false;
  };

  GameMetadata* m_metadata = nullptr;
  QVector<QAbstractItemModel*> m_models;
  QSet<QString> m_disabledSources;
  QVector<SourceRow> m_rows;
  // Rebuilt with m_rows so linked-game lookups never walk every source model per role.
  QHash<QString, SourceRow> m_rowForKey;
  QHash<QString, QVector<SourceRow>> m_rowsForGroup;
  QSqlDatabase m_database;
  QString m_connectionName;
  QString m_databasePath;
  QString m_artworkRoot;
  QHash<QString, QString> m_coverOverrides;
  QHash<QString, QString> m_groupForGame;
  QHash<QString, QString> m_primaryForGroup;
  QHash<QString, qint64> m_lastLaunchForGame;
  QHash<QString, OrganizationState> m_organizationForGame;
  QHash<QString, QStringList> m_collectionsForGame;
  QStringList m_collectionNames;
};
