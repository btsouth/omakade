#include "app/AppSettings.h"
#include "metadata/ProtonDbService.h"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

class Reply final : public QNetworkReply {
public:
  Reply(const QNetworkRequest& request, QByteArray body, int status, bool hold, QObject* parent)
      : QNetworkReply(parent), m_body(std::move(body)) {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
    setRawHeader("Retry-After", "120");
    open(QIODevice::ReadOnly);
    if (!hold)
      QTimer::singleShot(0, this, [this] {
        if (isFinished())
          return;
        emit readyRead();
        if (isFinished())
          return;
        setFinished(true);
        emit finished();
      });
  }
  void abort() override {
    if (isFinished())
      return;
    setError(OperationCanceledError, "Canceled");
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override {
    return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 max) override {
    const auto count = qMin(max, static_cast<qint64>(m_body.size()) - m_offset);
    if (!count)
      return -1;
    std::memcpy(data, m_body.constData() + m_offset, count);
    m_offset += count;
    return count;
  }

private:
  QByteArray m_body;
  qint64 m_offset = 0;
};

class Network final : public QNetworkAccessManager {
public:
  QByteArray body = R"({"tier":"gold","total":12})";
  int status = 200;
  bool hold = false;
  QList<QNetworkRequest> requests;

protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
    requests.append(request);
    return new Reply(request, body, status, hold, this);
  }
};

class ProtonDbTests : public QObject {
  Q_OBJECT
private slots:
  void filtersStoreIdentity() {
    QVERIFY(ProtonDbService::validAppId("Steam", "620"));
    for (const auto* id :
         {"0", "0620", "demo-0", "path:game", "2147483648", "18446744073709551615", "620/1"})
      QVERIFY(!ProtonDbService::validAppId("Steam", id));
    QVERIFY(!ProtonDbService::validAppId("Manual", "620"));
  }
  void validatesProviderData() {
    for (const auto* tier : {"gold", "silver", "platinum", "bronze", "borked", "native", "pending"})
      QVERIFY(!ProtonDbService::parseSummary(QByteArray("{\"tier\":\"") + tier + "\",\"total\":5}")
                   .isEmpty());
    for (const auto* body :
         {"[]", "{}", "{\"tier\":\"perfect\",\"total\":1}", "{\"tier\":\"gold\",\"total\":-1}",
          "{\"tier\":\"gold\",\"total\":0.5}", "{\"tier\":\"gold\",\"total\":\"12\"}"})
      QVERIFY(ProtonDbService::parseSummary(body).isEmpty());
    QVERIFY(ProtonDbService::parseSummary(QByteArray(65537, ' ')).isEmpty());
  }
  void optInCacheAndRestart() {
    QTemporaryDir dir;
    Network network;
    const auto path = dir.filePath("summaries.json");
    {
      ProtonDbService service(path, nullptr, &network);
      service.request("Steam", "620");
      QCOMPARE(network.requests.size(), 0);
      service.setEnabled(true);
      service.request("Steam", "620");
      service.request("Steam", "620");
      QTRY_COMPARE(service.summary("Steam", "620").value("tier").toString(), QString("gold"));
      QCOMPARE(network.requests.size(), 1);
      QCOMPARE(network.requests.first().url().host(), QString("www.protondb.com"));
      QVERIFY(network.requests.first().url().query().isEmpty());
      QCOMPARE(network.requests.first().rawHeader("User-Agent"),
               QByteArray("Omakade/development (optional ProtonDB badges)"));
      QCOMPARE(network.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
               int(QNetworkRequest::ManualRedirectPolicy));
      service.request("Steam", "620");
      QCOMPARE(network.requests.size(), 1);
      service.setEnabled(false);
      QVERIFY(service.summary("Steam", "620").isEmpty());
    }
    ProtonDbService restored(path, nullptr, &network);
    restored.setEnabled(true);
    QCOMPARE(restored.summary("Steam", "620").value("total").toInt(), 12);
    restored.request("Steam", "620");
    QCOMPARE(network.requests.size(), 1);
  }
  void transientFailureKeepsOldRating() {
    QTemporaryDir dir;
    QFile file(dir.filePath("cache"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QJsonObject old{{"tier", "silver"},
                          {"total", 10},
                          {"fetched", QDateTime::currentSecsSinceEpoch() - 8 * 86400}};
    file.write(QJsonDocument(QJsonObject{{"620", old}}).toJson());
    file.close();
    Network network;
    network.status = 500;
    ProtonDbService service(file.fileName(), nullptr, &network);
    service.setEnabled(true);
    service.request("Steam", "620");
    QTRY_VERIFY(service.revision() > 1);
    QCOMPARE(service.summary("Steam", "620").value("tier").toString(), QString("silver"));
    QVERIFY(service.summary("Steam", "620").value("stale").toBool());
    service.request("Steam", "620");
    QCOMPARE(network.requests.size(), 1);
  }
  void missingIsNotBorkedAndRateLimitStopsQueue() {
    QTemporaryDir dir;
    Network network;
    network.status = 404;
    ProtonDbService service(dir.filePath("cache"), nullptr, &network);
    service.setEnabled(true);
    service.request("Steam", "620");
    QTRY_COMPARE(service.summary("Steam", "620").value("tier").toString(), QString("pending"));
    network.status = 429;
    service.request("Steam", "400");
    service.request("Steam", "220");
    QTRY_COMPARE(network.requests.size(), 2);
    QTRY_VERIFY(service.revision() >= 3);
    QTest::qWait(1200);
    QCOMPARE(network.requests.size(), 2);
    QVERIFY(service.summary("Steam", "400").isEmpty());
  }
  void disableCancelsPendingWork() {
    QTemporaryDir dir;
    Network network;
    network.hold = true;
    ProtonDbService service(dir.filePath("cache"), nullptr, &network);
    service.setEnabled(true);
    service.request("Steam", "620");
    service.request("Steam", "220");
    service.setEnabled(false);
    QTest::qWait(1100);
    QCOMPARE(network.requests.size(), 1);
    QVERIFY(service.summary("Steam", "620").isEmpty());
  }
  void settingPersistsAndDefaultsOff() {
    QTemporaryDir dir;
    const auto path = dir.filePath("config.toml");
    AppSettings settings(path);
    QVERIFY(!settings.protonDbEnabled());
    settings.setProtonDbEnabled(true);
    AppSettings restored(path);
    QVERIFY(restored.protonDbEnabled());
    restored.setProtonDbEnabled(false);
    AppSettings off(path);
    QVERIFY(!off.protonDbEnabled());
    QVERIFY(!off.backupSettings().contains("protondb_enabled"));
  }
};
QTEST_GUILESS_MAIN(ProtonDbTests)
#include "ProtonDbTests.moc"
