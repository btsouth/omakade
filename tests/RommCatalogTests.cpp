#include "sources/romm/RommCatalog.h"
#include "library/RommGameModel.h"
#include "library/UnifiedGameModel.h"
#include "library/GameRoles.h"
#include "app/AppSettings.h"
#include <QDir>
#include <atomic>
#include "sources/romm/RommCredentials.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <cstring>

struct Response {
  QByteArray body;
  int status = 200;
  QUrl redirect;
  bool hold = false;
};
class RommReply final : public QNetworkReply {
public:
  RommReply(const QNetworkRequest& request, const Response& response, QObject* parent)
      : QNetworkReply(parent), m_body(response.body) {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
    if (!response.redirect.isEmpty())
      setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
    open(QIODevice::ReadOnly);
    if (!response.hold)
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
    if (!isFinished()) {
      setError(OperationCanceledError, "Canceled");
      setFinished(true);
      emit finished();
    }
  }
  qint64 bytesAvailable() const override {
    return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 maximum) override {
    const auto size = qMin(maximum, qint64(m_body.size() - m_offset));
    if (!size)
      return -1;
    std::memcpy(data, m_body.constData() + m_offset, size);
    m_offset += size;
    return size;
  }

private:
  QByteArray m_body;
  qsizetype m_offset = 0;
};
class RommNetwork final : public QNetworkAccessManager {
public:
  QList<Response> responses;
  QList<QNetworkRequest> requests;

protected:
  QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                               QIODevice*) override {
    Q_ASSERT(operation == GetOperation);
    requests.append(request);
    return new RommReply(request, responses.isEmpty() ? Response{{}, 500} : responses.takeFirst(),
                         this);
  }
};
class RommCatalogTests : public QObject {
  Q_OBJECT
  const QUrl server{"https://romm.example/subpath"};
  const QByteArray token = "rmm_" + QByteArray(64, 'a');
  static QByteArray page(int id, int offset = 0, QJsonValue total = 1) {
    const QJsonArray items = id ? QJsonArray{QJsonObject{{"id", id},
                                                         {"name", "Game"},
                                                         {"platform_slug", "gba"},
                                                         {"fs_path", ""},
                                                         {"fs_name", "game.gba"}}}
                                : QJsonArray{};
    return QJsonDocument(
               QJsonObject{{"items", items}, {"offset", offset}, {"limit", 1}, {"total", total}})
        .toJson();
  }
  static void game(const QString& root) {
    QFile f(root + "/game.gba");
    if (f.open(QIODevice::WriteOnly))
      f.write("game");
  }
private slots:
  void modelConnectionOfflineMountAndPreferences() {
    QTemporaryDir dir;
    const QString mount=dir.filePath("mount");QVERIFY(QDir().mkpath(mount));game(mount);
    const QString settingsPath=dir.filePath("config.toml"),database=dir.filePath("catalog.sqlite");
    AppSettings settings(settingsPath);RommNetwork network;
    network.responses={{page(1)}};
    QByteArray saved;
    auto credentials=[&](const QUrl&,const QByteArray& value,bool store) {
      if(store)saved=value;
      return RommCredentialResult{true,saved,{}};
    };
    RommGameModel model(database,&settings,nullptr,nullptr,&network,credentials);
    UnifiedGameModel unified(dir.filePath("library.sqlite"));unified.addSourceModel(&model);
    QCOMPARE(network.requests.size(),0);
    QVERIFY(model.connectServer(server.toString(),mount,QString::fromLatin1(token)));
    QTRY_COMPARE(model.rowCount(),1);QTRY_VERIFY(!model.scanning());
    QVERIFY(model.index(0).data(GameRoles::Installed).toBool());
    const auto identity=model.index(0).data(GameRoles::AppId).toString();
    unified.toggleFavorite(0);QVERIFY(unified.index(0).data(GameRoles::Favorite).toBool());
    QVERIFY(QDir().rename(mount,mount+"-offline"));
    model.refresh();QTRY_VERIFY(!model.scanning());
    QCOMPARE(model.rowCount(),1);QVERIFY(!model.index(0).data(GameRoles::Installed).toBool());
    QVERIFY(model.statusText().contains("unavailable"));
    QVERIFY(QDir().rename(mount+"-offline",mount));
    network.responses={{QByteArray{},401}};model.refresh();QTRY_VERIFY(!model.scanning());
    QCOMPARE(model.rowCount(),1);QVERIFY(!model.errorText().isEmpty());
    QCOMPARE(model.index(0).data(GameRoles::AppId).toString(),identity);
    QVERIFY(unified.index(0).data(GameRoles::Favorite).toBool());
    model.disconnectServer();QCOMPARE(model.rowCount(),0);
    settings.setRommEnabled(true);QTRY_VERIFY(!model.scanning());
    QCOMPARE(model.rowCount(),1);
    model.disconnectServer(true);QTRY_VERIFY(!model.scanning());QVERIFY(saved.isEmpty());
    QFile config(settingsPath);QVERIFY(config.open(QIODevice::ReadOnly));QVERIFY(!config.readAll().contains(token));
    QFile cache(database);QVERIFY(cache.open(QIODevice::ReadOnly));QVERIFY(!cache.readAll().contains(token));
  }
  void obsoleteCredentialResultsCannotEnableDisconnectedSource() {
    QTemporaryDir dir;game(dir.path());AppSettings settings(dir.filePath("settings.toml"));
    RommNetwork network;std::atomic_bool release=false,started=false;
    auto credentials=[&](const QUrl&,const QByteArray&,bool) {
      started=true;while(!release.load())QThread::msleep(1);
      return RommCredentialResult{true,token,{}};
    };
    RommGameModel model(dir.filePath("catalog.sqlite"),&settings,nullptr,nullptr,&network,credentials);
    QVERIFY(model.connectServer(server.toString(),dir.path(),QString::fromLatin1(token)));
    QTRY_VERIFY(started.load());model.disconnectServer();release=true;
    QTest::qWait(30);QCOMPARE(network.requests.size(),0);QCOMPARE(model.rowCount(),0);QVERIFY(!model.scanning());
  }
  void validatesCredentialsWithoutTouchingKeyring() {
    QVERIFY(RommCatalog::validToken(token));
    QVERIFY(!RommCatalog::validToken(token + "\r\nHeader: bad"));
    QVERIFY(!RommCredentials::store(server, "invalid").success);
    QVERIFY(!RommCredentials::load(QUrl("https://user:pass@romm.example")).success);
    QVERIFY(RommCatalog::serverUrl(QUrl("https://romm.example/?token=bad")).isEmpty());
    QCOMPARE(RommCatalog::serverUrl(QUrl("https://romm.example/subpath/")), server);
  }
  void paginatesAndPersistsOnlyCompleteCatalog() {
    QTemporaryDir dir;
    game(dir.path());
    RommNetwork network;
    const QString database = dir.filePath("catalog.sqlite");
    {
      RommCatalog catalog(database, nullptr, &network);
      QSignalSpy done(&catalog, &RommCatalog::finished);
      network.responses = {{page(1, 0, 2)}, {page(2, 1, 2)}};
      QVERIFY(catalog.refresh(server, dir.path(), token));
      QTRY_COMPARE(done.size(), 1);
      QVERIFY(done.last().at(0).toBool());
      QCOMPARE(catalog.cached(server, dir.path()).size(), 2);
      QCOMPARE(network.requests.size(), 2);
      QCOMPARE(network.requests.first().url().path(), QString("/subpath/api/roms"));
      QCOMPARE(QUrlQuery(network.requests.last().url()).queryItemValue("offset"), QString("1"));
      QCOMPARE(network.requests.first().rawHeader("Authorization"), "Bearer " + token);
      QCOMPARE(network.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
               int(QNetworkRequest::ManualRedirectPolicy));
      network.responses = {{page(3, 0, 2)}, {{}, 401}};
      QVERIFY(catalog.refresh(server, dir.path(), token));
      QTRY_COMPARE(done.size(), 2);
      QVERIFY(!done.last().at(0).toBool());
      QCOMPARE(catalog.cached(server, dir.path()).first().appId, QString("1"));
      QVERIFY(!done.last().at(1).toString().contains(QString::fromLatin1(token)));
    }
    RommCatalog reopened(database);
    QCOMPARE(reopened.cached(server, dir.path()).size(), 2);
    QVERIFY(reopened.cached(QUrl("https://other.example"), dir.path()).isEmpty());
    QFile file(database);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains(token));
  }
  void rejectsPartialRepeatedAndOversizedPages() {
    QTemporaryDir dir;
    game(dir.path());
    RommNetwork network;
    RommCatalog catalog(dir.filePath("catalog.sqlite"), nullptr, &network);
    QSignalSpy done(&catalog, &RommCatalog::finished);
    network.responses = {{page(1)}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QTRY_COMPARE(done.size(), 1);
    const QList<QList<Response>> failures{{{page(2, 0, 2)}, {page(3, 0, 2)}},
                                          {{page(2, 0, 2)}, {page(2, 1, 2)}},
                                          {{page(2, 0, 2)}, {page(3, 1, 3)}},
                                          {{page(0, 0, 3)}},
                                          {{QByteArray(16 * 1024 * 1024 + 1, ' ')}},
                                          {{{}, 302, QUrl("https://evil.example/api/roms")}},
                                          {{{}, 302, QUrl("http://romm.example/api/roms")}}};
    for (const auto& responses : failures) {
      const int count = done.size();
      network.responses = responses;
      QVERIFY(catalog.refresh(server, dir.path(), token));
      QTRY_COMPARE(done.size(), count + 1);
      QVERIFY(!done.last().at(0).toBool());
      QCOMPARE(catalog.cached(server, dir.path()).first().appId, QString("1"));
    }
  }
  void sameOriginRedirectAndUnknownTotal() {
    QTemporaryDir dir;
    game(dir.path());
    RommNetwork network;
    RommCatalog catalog(dir.filePath("catalog.sqlite"), nullptr, &network);
    QSignalSpy done(&catalog, &RommCatalog::finished);
    network.responses = {{{}, 302, QUrl("/subpath/api/roms/")},
                         {page(1, 0, QJsonValue::Null)},
                         {page(0, 1, QJsonValue::Null)}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done.last().at(0).toBool());
    QCOMPARE(catalog.cached(server, dir.path()).size(), 1);
    QCOMPARE(network.requests.size(), 3);
  }
  void failedDatabaseWriteRollsBackCatalogReplacement() {
    QTemporaryDir dir;
    game(dir.path());
    RommNetwork network;
    const QString path = dir.filePath("catalog.sqlite");
    RommCatalog catalog(path, nullptr, &network);
    QSignalSpy done(&catalog, &RommCatalog::finished);
    network.responses = {{page(1)}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QTRY_COMPARE(done.size(), 1);
    {
      auto database = QSqlDatabase::addDatabase("QSQLITE", "romm-write-failure");
      database.setDatabaseName(path);
      QVERIFY(database.open());
      QSqlQuery query(database);
      QVERIFY(query.exec("CREATE TRIGGER refuse_new BEFORE INSERT ON romm_catalog BEGIN SELECT "
                         "RAISE(ABORT, 'fixture'); END"));
      database.close();
    }
    QSqlDatabase::removeDatabase("romm-write-failure");
    network.responses = {{page(2)}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QTRY_COMPARE(done.size(), 2);
    QVERIFY(!done.last().at(0).toBool());
    QCOMPARE(catalog.cached(server, dir.path()).first().appId, QString("1"));
  }
  void cancellationAndEmptyCatalog() {
    QTemporaryDir dir;
    game(dir.path());
    RommNetwork network;
    RommCatalog catalog(dir.filePath("catalog.sqlite"), nullptr, &network);
    QSignalSpy done(&catalog, &RommCatalog::finished);
    network.responses = {{page(1)}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QTRY_COMPARE(done.size(), 1);
    network.responses = {{{}, 200, {}, true}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QVERIFY(!catalog.refresh(server, dir.path(), token));
    catalog.cancel();
    QCOMPARE(done.size(), 2);
    QCOMPARE(catalog.cached(server, dir.path()).size(), 1);
    network.responses = {{page(0, 0, 0)}};
    QVERIFY(catalog.refresh(server, dir.path(), token));
    QTRY_COMPARE(done.size(), 3);
    QVERIFY(done.last().at(0).toBool());
    QVERIFY(catalog.cached(server, dir.path()).isEmpty());
  }
};
QTEST_GUILESS_MAIN(RommCatalogTests)
#include "RommCatalogTests.moc"
