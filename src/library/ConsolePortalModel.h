#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QVector>

class ConsolePortalModel final : public QAbstractListModel {
  Q_OBJECT

public:
  explicit ConsolePortalModel(QObject* parent = nullptr);

  void addRomModel(QAbstractItemModel* model);
  // Systems shown as cards; games of other systems stay library tiles.
  void setCardSystems(const QStringList& systems);
  [[nodiscard]] QStringList cardSystems() const { return m_cardSystems; }
  [[nodiscard]] int gameCount(const QString& systemId) const;
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
  struct Portal {
    QString systemId;
    QString title;
    int gameCount = 0;
    QStringList sources;
    qint64 lastPlayed = 0;
    int hours = 0;
    QColor accentStart;
    QColor accentEnd;
  };

  void rebuild();
  [[nodiscard]] QVariant valueForRole(const Portal& portal, int role) const;

  QVector<QAbstractItemModel*> m_models;
  QVector<Portal> m_portals;
  QStringList m_cardSystems;
};
