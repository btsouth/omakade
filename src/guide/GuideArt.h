#pragma once

#include <QNetworkAccessManager>
#include <QSet>
#include <QVariantMap>

class GuideArt final : public QObject {
  Q_OBJECT
public:
  explicit GuideArt(QObject* parent = nullptr) : QObject(parent) {}
  void prefetch(const QString& source, const QVariantMap& metadata);
  static QString cacheRoot();
  static QString appId(const QString& source, const QVariantMap& metadata);
  static bool needsHero(const QString& hero);
  static bool validImage(const QString& path, bool hero);
  static QVariantMap select(const QString& source, const QVariantMap& metadata, const QString& root);
signals:
  void ready(const QString& appId);
private:
  QNetworkAccessManager m_network;
  QSet<QString> m_pending;
};
