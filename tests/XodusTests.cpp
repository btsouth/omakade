#include "app/AppSettings.h"
#include "launch/GameLauncher.h"
#include "library/GameRoles.h"
#include "library/XodusGameModel.h"
#include "sources/xodus/XodusScanner.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {
constexpr auto kValor = "43802bcbb9d5e7ea";
constexpr auto kGears = "8474125f9e2af4b9";

QString pngDataUrl() {
  QImage image(4, 4, QImage::Format_ARGB32);
  image.fill(Qt::red);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QJsonObject game(const QString& id, const QString& title, const QString& state,
                 const QString& path) {
  return {{"id", id},
          {"title", title},
          {"publisher", "Lyrical Games"},
          {"storeId", "9P861V1542BL"},
          {"path", path},
          {"executable", "Game.exe"},
          {"installState", state},
          {"coverDataUrl", pngDataUrl()},
          {"heroUrl", "https://store-images.s-microsoft.com/image/hero"}};
}

// A Nexus profile with one ready game, plus entries the scan has to leave out.
void writeProfile(const QString& profile, const QString& dataRoot, const QJsonArray& extra = {}) {
  QDir().mkpath(profile);
  QJsonArray games{game(kValor, "Valor Mortis", "ready", dataRoot + "/games/Valor")};
  for (const QJsonValue& value : extra)
    games.append(value);
  const QJsonObject state{{"games", games},
                          {"downloads", QJsonArray{}},
                          {"settings", QJsonObject{{"runtimeRoot", dataRoot + "/runtimes"}}}};
  QFile file(profile + "/launcher-state.json");
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(state).toJson());
}

QString writeScript(const QString& profile, const QString& name, const QByteArray& marker) {
  QDir().mkpath(profile + "/shortcuts");
  QFile(profile).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner);
  QFile(profile + "/shortcuts")
      .setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  const QString path = profile + "/shortcuts/" + name;
  QFile file(path);
  file.open(QIODevice::WriteOnly | QIODevice::Truncate);
  file.write("#!/bin/sh\n" + marker + "\nexec /opt/Xodus-Nexus.AppImage --launch-game \"$@\"\n");
  file.close();
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  return path;
}
} // namespace

class XodusTests final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

  void scannerImportsReadyGamesWithArtworkAndPrefix() {
    QTemporaryDir directory;
    const QString profile = directory.path() + "/xodus-nexus";
    const QString data = directory.path() + "/data";
    QVERIFY(QDir().mkpath(data + "/compatdata/" + kValor + "/pfx"));
    QVERIFY(QDir().mkpath(data + "/prefixes/" + kGears));
    QJsonObject http = game("aaaaaaaaaaaaaaaa", "Plain Hero", "ready", data + "/games/Plain");
    http.insert("heroUrl", "http://store-images.s-microsoft.com/image/hero");
    writeProfile(profile, data,
                 {game(kGears, "Gears of War: E-Day", "ready", data + "/games/Gears"),
                  game("bbbbbbbbbbbbbbbb", "Half Installed", "downloading", data + "/games/Half"),
                  game("../escape", "Bad Id", "ready", data + "/games/Bad"),
                  game(kValor, "Duplicate", "ready", data + "/games/Duplicate"),
                  game("cccccccccccccccc", "Relative", "ready", "games/Relative"), http});

    const XodusScanResult result = XodusScanner::scan({profile}, directory.path() + "/cache");
    QVERIFY(!result.incomplete);
    QCOMPARE(result.roots, QStringList{profile});
    QCOMPARE(result.games.size(), 3);
    const XodusGameRecord& valor = result.games.at(0);
    QCOMPARE(valor.gameId, QString::fromLatin1(kValor));
    QCOMPARE(valor.title, QStringLiteral("Valor Mortis"));
    QCOMPARE(valor.publisher, QStringLiteral("Lyrical Games"));
    QCOMPARE(valor.installPath, data + "/games/Valor");
    QCOMPARE(valor.winePrefix, data + "/compatdata/" + kValor + "/pfx");
    QCOMPARE(valor.coverPath, directory.path() + "/cache/" + kValor + "-cover.png");
    QVERIFY(!QImage(valor.coverPath).isNull());
    QCOMPARE(valor.heroUrl, QUrl("https://store-images.s-microsoft.com/image/hero"));
    // A game Nexus has not run yet has no prefix; an older one uses the legacy layout.
    QCOMPARE(result.games.at(1).winePrefix, data + "/prefixes/" + kGears);
    QVERIFY(result.games.at(2).winePrefix.isEmpty());
    QVERIFY(result.games.at(2).heroUrl.isEmpty());

    // An unchanged cover is left as it is on the next scan.
    const QDateTime written = QFileInfo(valor.coverPath).lastModified();
    QTest::qWait(20);
    XodusScanner::scan({profile}, directory.path() + "/cache");
    QCOMPARE(QFileInfo(valor.coverPath).lastModified(), written);
  }

  void malformedStateIsIncomplete() {
    QTemporaryDir directory;
    const QString profile = directory.path() + "/xodus-nexus";
    QDir().mkpath(profile);
    QFile file(profile + "/launcher-state.json");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"games\": ");
    file.close();
    const XodusScanResult result = XodusScanner::scan({profile});
    QVERIFY(result.incomplete);
    QVERIFY(result.games.isEmpty());
    QVERIFY(result.warnings.first().startsWith("Could not parse"));
  }

  void storeCatalogGivesPosterAndHero() {
    const QUrl url =
        XodusScanner::catalogUrl({"9P861V1542BL", "bad id", "9P861V1542BL", "9N4PT8HGCDHQ"});
    QCOMPARE(url.host(), QStringLiteral("displaycatalog.mp.microsoft.com"));
    QVERIFY(url.query().contains("bigIds=9P861V1542BL,9N4PT8HGCDHQ&"));
    QVERIFY(XodusScanner::catalogUrl({"lowercase123", ""}).isEmpty());

    const auto images = [](const QJsonArray& list) {
      return QJsonObject{{"LocalizedProperties", QJsonArray{QJsonObject{{"Images", list}}}}};
    };
    QJsonObject valor = images(
        {QJsonObject{{"ImagePurpose", "BoxArt"}, {"Uri", "//store-images.s-microsoft.com/box"}},
         QJsonObject{{"ImagePurpose", "Poster"}, {"Uri", "//store-images.s-microsoft.com/poster"}},
         QJsonObject{{"ImagePurpose", "TitledHeroArt"},
                     {"Uri", "//store-images.s-microsoft.com/titled"}},
         QJsonObject{{"ImagePurpose", "SuperHeroArt"},
                     {"Uri", "//store-images.s-microsoft.com/super"}}});
    valor.insert("ProductId", "9P861V1542BL");
    // Only titled hero art, and a poster on a host that is not the store's.
    QJsonObject gears =
        images({QJsonObject{{"ImagePurpose", "Poster"}, {"Uri", "https://example.com/poster"}},
                QJsonObject{{"ImagePurpose", "TitledHeroArt"},
                            {"Uri", "https://store-images.s-microsoft.com/titled"}}});
    gears.insert("ProductId", "9N4PT8HGCDHQ");
    QJsonObject bad = images(
        {QJsonObject{{"ImagePurpose", "Poster"}, {"Uri", "//store-images.s-microsoft.com/x"}}});
    bad.insert("ProductId", "../escape");
    const QByteArray json =
        QJsonDocument(QJsonObject{{"Products", QJsonArray{valor, gears, bad}}}).toJson();

    const QHash<QString, XodusStoreArt> art = XodusScanner::parseCatalog(json);
    QCOMPARE(art.size(), 2);
    QCOMPARE(art.value("9P861V1542BL").poster,
             QUrl("https://store-images.s-microsoft.com/poster?w=600"));
    QCOMPARE(art.value("9P861V1542BL").hero,
             QUrl("https://store-images.s-microsoft.com/super?w=1920"));
    QVERIFY(art.value("9N4PT8HGCDHQ").poster.isEmpty());
    QCOMPARE(art.value("9N4PT8HGCDHQ").hero,
             QUrl("https://store-images.s-microsoft.com/titled?w=1920"));
    QVERIFY(XodusScanner::parseCatalog("not json").isEmpty());
  }

  void launchScriptMustBeNexusOwnAndPrivate() {
    QTemporaryDir directory;
    const QString profile = directory.path() + "/xodus-nexus";
    const QString script =
        writeScript(profile, "launch-game", "# X-Xodus-Nexus-Profile=af01308aff70");
    QCOMPARE(XodusScanner::launchScript(profile), script);
    QVERIFY(XodusScanner::launchScript(profile, false).isEmpty());
    QVERIFY(XodusScanner::launchScript("relative/profile").isEmpty());

    QFile(script).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner | QFileDevice::WriteGroup);
    QVERIFY(XodusScanner::launchScript(profile).isEmpty());
    QFile(script).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QVERIFY(XodusScanner::launchScript(profile).isEmpty());

    writeScript(profile, "launch-game", "# written by someone else");
    QVERIFY(XodusScanner::launchScript(profile).isEmpty());

    // A linked profile is followed, as dotfile managers set it up.
    writeScript(profile, "launch-game", "# X-Xodus-Nexus-Profile=af01308aff70");
    QVERIFY(QFile::link(profile, directory.path() + "/linked-profile"));
    QCOMPARE(XodusScanner::launchScript(directory.path() + "/linked-profile"), script);

    const QString elsewhere = writeScript(directory.path() + "/other", "launch-game",
                                          "# X-Xodus-Nexus-Profile=af01308aff70");
    QFile::remove(script);
    QVERIFY(QFile::link(elsewhere, script));
    QVERIFY(XodusScanner::launchScript(profile).isEmpty());
  }

  void launcherBuildsSafeCommands() {
    QTemporaryDir directory;
    const QString profile = directory.path() + "/xodus-nexus";
    const QString game =
        writeScript(profile, "launch-game", "# X-Xodus-Nexus-Profile=af01308aff70");
    const QString nexus =
        writeScript(profile, "launch-nexus", "# X-Xodus-Nexus-Profile=af01308aff70");

    const LaunchCommand play = GameLauncher::xodusCommand(profile, QString::fromLatin1(kValor));
    QCOMPARE(play.program, game);
    QCOMPARE(play.arguments, QStringList{QString::fromLatin1(kValor)});
    const LaunchCommand manage = GameLauncher::xodusCommand(profile);
    QCOMPARE(manage.program, nexus);
    QVERIFY(manage.arguments.isEmpty());
    QVERIFY(!GameLauncher::xodusCommand(profile, "../escape").isValid());
    QVERIFY(!GameLauncher::xodusCommand(profile, "43802BCBB9D5E7EA").isValid());
    QVERIFY(!GameLauncher::xodusCommand(profile, "").isValid());
    QVERIFY(!GameLauncher::xodusCommand(directory.path() + "/missing", QString::fromLatin1(kValor))
                 .isValid());
  }

  void modelIsRepeatableAndKeepsLocalState() {
    QTemporaryDir directory;
    const QString profile = directory.path() + "/xodus-nexus";
    const QString data = directory.path() + "/data";
    QVERIFY(QDir().mkpath(data + "/compatdata/" + kValor + "/pfx"));
    writeProfile(profile, data);
    const QString database = directory.path() + "/omakade.sqlite3";
    const QString cache = directory.path() + "/cache";
    {
      XodusGameModel model(database);
      model.setArtworkDownloadsEnabled(false);
      model.refreshFromRoots({profile}, cache);
      QVERIFY(model.xodusDetected());
      QCOMPARE(model.rowCount(), 1);
      const QModelIndex row = model.index(0);
      QCOMPARE(model.data(row, GameRoles::Source).toString(), QStringLiteral("Xodus Nexus"));
      QCOMPARE(model.data(row, GameRoles::Title).toString(), QStringLiteral("Valor Mortis"));
      QCOMPARE(model.data(row, GameRoles::Subtitle).toString(),
               QStringLiteral("Xodus Nexus · Lyrical Games"));
      QCOMPARE(model.data(row, GameRoles::AppId).toString(), QString::fromLatin1(kValor));
      QVERIFY(model.data(row, GameRoles::Runner).toString().isEmpty());
      QCOMPARE(model.data(row, GameRoles::LaunchTarget).toString(),
               data + "/compatdata/" + kValor + "/pfx");
      QCOMPARE(model.data(row, GameRoles::InstallPath).toString(), data + "/games/Valor");
      QCOMPARE(model.data(row, GameRoles::CoverPath).toString(),
               QUrl::fromLocalFile(cache + "/" + kValor + "-cover.png").toString());
      QVERIFY(model.data(row, GameRoles::HeroPath).toString().isEmpty());
      // The unified library reads the prefix through this role name to stop a game.
      QCOMPARE(model.roleNames().value(GameRoles::LaunchTarget), QByteArray("launchTarget"));
      model.toggleFavorite(0);
      model.toggleHidden(0);
      model.refreshFromRoots({profile}, cache);
      QCOMPARE(model.rowCount(), 1);
      QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
      QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
    }
    XodusGameModel reloaded(database);
    QCOMPARE(reloaded.rowCount(), 1);
    QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
    QCOMPARE(reloaded.statusText(), QStringLiteral("Loaded cached Xodus Nexus library"));

    // A removed game leaves the library on the next scan.
    QFile state(profile + "/launcher-state.json");
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(R"({"games": [], "settings": {}})");
    state.close();
    reloaded.setArtworkDownloadsEnabled(false);
    reloaded.refreshFromRoots({profile}, cache);
    QCOMPARE(reloaded.rowCount(), 0);
  }

  void settingSurvivesSaveAndBackupRestore() {
    QTemporaryDir directory;
    AppSettings settings(directory.path() + "/settings.toml");
    QVERIFY(settings.xodusEnabled());
    settings.setXodusEnabled(false);
    AppSettings reloaded(directory.path() + "/settings.toml");
    QVERIFY(!reloaded.xodusEnabled());

    QJsonObject saved = reloaded.backupSettings();
    QCOMPARE(saved.value("xodus_enabled"), QJsonValue(false));
    AppSettings restored(directory.path() + "/restored.toml");
    QVERIFY(restored.applyBackupSettings(saved, true));
    QVERIFY(!restored.xodusEnabled());
    // A backup from before Xodus Nexus support leaves the current choice alone.
    saved.remove("xodus_enabled");
    QVERIFY(restored.applyBackupSettings(saved, true));
    QVERIFY(!restored.xodusEnabled());
  }

  void malformedStateKeepsCachedGames() {
    QTemporaryDir directory;
    const QString profile = directory.path() + "/xodus-nexus";
    writeProfile(profile, directory.path() + "/data");
    XodusGameModel model(directory.path() + "/omakade.sqlite3");
    model.setArtworkDownloadsEnabled(false);
    model.refreshFromRoots({profile});
    QCOMPARE(model.rowCount(), 1);
    QFile state(profile + "/launcher-state.json");
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write("not json");
    state.close();
    model.refreshFromRoots({profile});
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.statusText().startsWith(QStringLiteral("Xodus Nexus scan interrupted")));
    // Nexus being uninstalled entirely also keeps what was cached.
    model.refreshFromRoots({});
    QCOMPARE(model.rowCount(), 1);
  }
};

QTEST_GUILESS_MAIN(XodusTests)
#include "XodusTests.moc"
