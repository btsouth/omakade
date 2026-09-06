#include "metadata/GameMetadata.h"
#include "library/ConsoleCatalog.h"
#include <algorithm>
#include <QSet>
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlQuery>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent>
#include <cmath>
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
constexpr auto fields = "fields "
                        "name,platforms,first_release_date,total_rating,total_rating_count,"
                        "aggregated_rating,aggregated_rating_count; ";
// IGDB allows four requests a second; 350 ms keeps a comfortable margin. SteamGridDB is not
// documented as precisely, so its calls are held a little further apart. With both providers
// paced at the request, the gap between games only has to yield to the event loop.
constexpr int kGridRequestGapMs = 250;
constexpr int kBetweenGamesMs = 100;

QString quoted(QString text) {
  text.replace('\\', "\\\\");
  text.replace('"', "\\\"");
  text.replace(QRegularExpression("[\\x00-\\x1f]"), " ");
  return '"' + text.left(200) + '"';
}
// One part of a parenthesised dump tag: a region, a language, a release flag, a revision, or a
// translation or hack note. Editions and subtitles are deliberately absent, so "(Director's Cut)"
// is never mistaken for dump metadata.
bool dumpTagPart(const QString& part) {
  static const QRegularExpression known(
      QStringLiteral(
          R"(^(?:NA|JP|EU|US|USA|EUR|JPN|Europe|Japan|World|Korea|China|Taiwan|Brazil|Australia|Asia|TW|KR|CN|AU|BR|CA|HK|RU|SP|FR|DE|ES|IT|NL|SE|PD|PE|U|E|J|W|UE|JU)$)"
          R"(|^(?:En|Ja|Fr|De|Es|It|Nl|Pt|Ko|Zh|Sv|Da|No|Fi|Ru|Pl)$)"
          R"(|^(?:Prototype|Proto|Beta|Alpha|Sample|Demo|Unl|Unlicensed|Pirate|Alt|Aftermarket|Homebrew|Enhanced Version|Virtual Console|Switch Online|Classic Mini)$)"
          R"(|^(?:NTSC|PAL)(?: |-)Conversion$)"
          R"(|^Rev(?:ision)?\.?(?: ?[0-9A-Za-z.]+)?$)"
          R"(|^(?:v|Version) ?[0-9][0-9A-Za-z.]*$)"
          R"(|Translat(?:ed|ion)|\bPatch\b|\bHack\b|\bFan ?Trans)"),
      QRegularExpression::CaseInsensitiveOption);
  return known.match(part.trimmed()).hasMatch();
}

// A group is dump metadata only when every comma or dash separated part is a known tag. That
// keeps "(Balloon Fight)" and other real subtitles, and removes "(NA, Rev 1)" whole.
bool dumpTagGroup(const QString& contents) {
  // A note about a translation or a patch is dump metadata however many clauses it runs to,
  // as in "(English Translated by Aeon Genesis, Rev 1.01 With Fixes by MTeam)".
  static const QRegularExpression romHack(
      QStringLiteral(R"(Translat(?:ed|ion)|\bPatch(?:ed)?\b|\bHack\b|\bFan ?Trans|\bFixes by\b)"),
      QRegularExpression::CaseInsensitiveOption);
  if (romHack.match(contents).hasMatch())
    return true;
  const QStringList parts =
      contents.split(QRegularExpression(QStringLiteral(",| - ")), Qt::SkipEmptyParts);
  if (parts.isEmpty())
    return false;
  for (const QString& part : parts)
    if (!dumpTagPart(part))
      return false;
  return true;
}

QString cleanTitle(QString title) {
  // ROM sets tag dumps with the region, language, revision, release state and any translation
  // patch. IGDB knows none of that, so a tagged title never matches its catalogue entry.
  // Editions, remasters and subtitles are not tags and keep their names.
  static const QRegularExpression group(QStringLiteral(R"(\s*\(([^()]*)\))"));
  static const QRegularExpression squareTag(QStringLiteral(R"(\s*\[[^\[\]]*\])"));
  QString previous;
  while (previous != title) {
    previous = title;
    QString stripped;
    qsizetype at = 0;
    auto matches = group.globalMatch(title);
    while (matches.hasNext()) {
      const auto match = matches.next();
      if (!dumpTagGroup(match.captured(1)))
        continue;
      stripped += title.mid(at, match.capturedStart() - at);
      at = match.capturedEnd();
    }
    title = stripped + title.mid(at);
    // Square brackets only ever carry dump flags such as [!], [b1] or [T+Eng].
    title.remove(squareTag);
    title = title.simplified();
  }
  // "Legend of Zelda, The" is how a ROM set sorts a title, and the article can sit before a
  // subtitle as in "Legend of Zelda, The - Ocarina of Time". Put it back in front so the search
  // reads the way IGDB stores it.
  static const QRegularExpression sortedArticle(
      QStringLiteral(
          R"(^(.*?),\s*(The|A|An|Der|Die|Das|Le|La|Les|El|Los|Il|Lo)(\s*[-:].*)?$)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto article = sortedArticle.match(title);
  if (article.hasMatch())
    title = article.captured(2) + QLatin1Char(' ') + article.captured(1) + article.captured(3);
  return title.simplified();
}
} // namespace
QString GameMetadata::normalizedTitle(QString title) {
  title = cleanTitle(title);
  QString normalized = title.normalized(QString::NormalizationForm_KC)
                           .toCaseFolded()
                           .replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}\"]+")), " ")
                           .simplified();
  // A leading article is never what separates two games, and the catalogues disagree about it.
  static const QRegularExpression leadingArticle(QStringLiteral("^(?:the|a|an) (?=.)"));
  return normalized.remove(leadingArticle);
}
bool GameMetadata::wantsPortraitCover(const QString& system, const QString& source,
                                      const QString& sourceCover) {
  // Retro consoles get real box scans from the Libretro thumbnail server, and GameCube and Wii
  // covers come from GameTDB. That artwork is authentic and a fan-made portrait would replace
  // it, so those systems never ask for one. Switch, Wii U and PS4 dumps carry only a square
  // icon, which crops a logo off the card, so they may.
  const QString id = ConsoleCatalog::idFor(system);
  static const QSet<QString> iconOnly{QStringLiteral("switch"), QStringLiteral("wiiu"),
                                      QStringLiteral("ps4")};
  if (!id.isEmpty() && !iconOnly.contains(id))
    return false;
  // Steam ships an official 600x900 capsule for every game. It downloads on demand, so judging
  // by the file alone would hand a fan portrait to any game whose capsule had not arrived yet.
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0)
    return false;
  // Elsewhere, artwork that is already portrait shaped is the artwork to keep. Replacing a
  // publisher's cover with fan art is a downgrade, so a portrait only fills a gap or a bad shape.
  QString path = sourceCover;
  if (path.startsWith(QStringLiteral("file://")))
    path = QUrl(path).toLocalFile();
  if (path.isEmpty() || !QFileInfo::exists(path))
    return true;
  QImageReader reader(path);
  const QSize size = reader.size();
  if (!size.isValid() || size.height() <= 0)
    return true;
  return double(size.width()) / double(size.height()) > kPortraitAspectLimit;
}

int GameMetadata::platformId(const QString& system) {
  static const QHash<QString, int> ids{
      {"nes", 18}, {"snes", 19},    {"gb", 33},      {"gbc", 22},       {"gba", 24},
      {"n64", 4},  {"genesis", 29}, {"psx", 7},      {"dreamcast", 23}, {"gamecube", 21},
      {"wii", 5},  {"ps2", 8},      {"switch", 130}, {"wiiu", 41},      {"ps4", 48}};
  return ids.value(ConsoleCatalog::idFor(system), system.isEmpty() ? 6 : 0);
}
QByteArray GameMetadata::searchQuery(const QString& title, const QString& system) {
  const int platform = platformId(system);
  if (title.trimmed().isEmpty() || platform == 0)
    return {};
  return QByteArray(fields) + "search " + quoted(cleanTitle(title)).toUtf8() +
         "; where platforms = (" + QByteArray::number(platform) + "); limit 20;";
}
QVariantList GameMetadata::parseMatches(const QByteArray& data, int platform) {
  QVariantList result;
  const auto doc = QJsonDocument::fromJson(data);
  if (!doc.isArray())
    return result;
  for (const auto& value : doc.array()) {
    const auto obj = value.toObject();
    if (obj.value("id").toInteger() <= 0 || obj.value("name").toString().isEmpty())
      continue;
    if (platform > 0 && !obj.value("platforms").toArray().contains(platform))
      continue;
    QVariantMap match{{"id", obj.value("id").toInteger()}, {"title", obj.value("name").toString()}};
    const qint64 released = obj.value("first_release_date").toInteger();
    match["year"] = released > 0 ? QDateTime::fromSecsSinceEpoch(released).date().year() : 0;
    const auto rating = obj.value("total_rating");
    const int count = obj.value("total_rating_count").toInt();
    match["rating"] =
        rating.isDouble() && rating.toDouble() >= 0 && rating.toDouble() <= 100 && count > 0
            ? qRound(rating.toDouble())
            : -1;
    match["ratingCount"] = qMax(0, count);
    result.append(match);
  }
  return result;
}
bool GameMetadata::trustedImageUrl(const QUrl& url) {
  return url.scheme() == "https" && url.userInfo().isEmpty() && url.port(-1) == -1 &&
         (url.host() == "cdn2.steamgriddb.com" || url.host() == "cdn.steamgriddb.com");
}
QVariantList GameMetadata::parseCovers(const QByteArray& data) {
  QVariantList result;
  const auto object = QJsonDocument::fromJson(data).object();
  if (!object.value("success").toBool())
    return result;
  for (const auto& value : object.value("data").toArray()) {
    auto cover = value.toObject();
    if (cover.value("id").toInteger() <= 0 || cover.value("width").toInt() != 600 ||
        cover.value("height").toInt() != 900 || cover.value("nsfw").toBool() ||
        cover.value("humor").toBool() || !trustedImageUrl(QUrl(cover.value("url").toString())))
      continue;
    result.append(
        QVariantMap{{"id", cover.value("id").toInteger()},
                    {"url", cover.value("url").toString()},
                    {"author", cover.value("author").toObject().value("name").toString()}});
  }
  return result;
}
GameMetadata::GameMetadata(const QString& databasePath, GameInsightsService* insights,
                           QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), m_insights(insights),
      m_connection("omakade-metadata-" + QUuid::createUuid().toString()),
      m_cacheRoot(QFileInfo(databasePath).absolutePath() + "/portrait-covers"),
      m_network(network ? network : new QNetworkAccessManager(this)) {
  m_database = QSqlDatabase::addDatabase("QSQLITE", m_connection);
  m_database.setDatabaseName(databasePath);
  if (m_database.open()) {
    QSqlQuery query(m_database);
    query.exec("CREATE TABLE IF NOT EXISTS game_metadata (game_key TEXT PRIMARY KEY, payload TEXT "
               "NOT NULL)");
    if (query.exec("SELECT game_key,payload FROM game_metadata"))
      while (query.next())
        m_entries.insert(
            query.value(0).toString(),
            QJsonDocument::fromJson(query.value(1).toByteArray()).object().toVariantMap());
  }
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
    const QString portrait = it.value().value("portrait").toString();
    if (!portrait.isEmpty()) {
      QImageReader reader(portrait);
      if (reader.size() != QSize(600, 900) || !reader.canRead())
        it.value().remove("portrait");
    }
  }
  if (insights)
    connect(insights, &GameInsightsService::catalogFinished, this, &GameMetadata::matchResult);
  if (QFileInfo::exists(m_cacheRoot + "/configured"))
    secretOperation(0);
}
GameMetadata::~GameMetadata() {
  m_secrets.waitForFinished();
  m_gridKey.fill('\0');
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connection);
}
void GameMetadata::setLibrary(UnifiedGameModel* library) {
  m_library = library;
  if (m_library == nullptr)
    return;
  // Sources populate the library over the first few seconds, so wait for rows to arrive before
  // judging what artwork a game has. The review runs once and is cheap: only entries that
  // actually hold a portrait are examined.
  const auto settled = [this] {
    if (m_library == nullptr || m_library->rowCount() == 0)
      return;
    if (!m_reviewedPortraits) {
      m_reviewedPortraits = true;
      dropUnwantedPortraits();
    }
    // Sources arrive over several seconds. Wait for a quiet moment before queuing, so a
    // library still loading is not walked once per source.
    m_settle.start();
  };
  m_settle.setSingleShot(true);
  m_settle.setInterval(2000);
  connect(&m_settle, &QTimer::timeout, this, &GameMetadata::continueLibraryPass);
  connect(m_library, &QAbstractItemModel::rowsInserted, this, settled);
  connect(m_library, &QAbstractItemModel::modelReset, this, settled);
}

void GameMetadata::setVisibleLibrary(QAbstractItemModel* visible) {
  m_visible = visible;
  if (m_visible == nullptr)
    return;
  // Changing the view changes what matters most. Reorder what is still pending rather than
  // starting again, so nothing already done is repeated.
  const auto viewChanged = [this] {
    promoteVisibleGames();
    if (!busy())
      m_settle.start();
  };
  connect(m_visible, &QAbstractItemModel::modelReset, this, viewChanged);
  connect(m_visible, &QAbstractItemModel::rowsInserted, this, viewChanged);
  connect(m_visible, &QAbstractItemModel::rowsRemoved, this, viewChanged);
}

void GameMetadata::continueLibraryPass() {
  // Stopping by hand means stopped, until the next launch or an explicit update.
  if (m_stoppedByHand || busy() || m_library == nullptr)
    return;
  if ((m_insights == nullptr || !m_insights->configured()) && !hasGridKey())
    return;
  m_cancelled = false;
  for (int row = 0; row < m_library->rowCount(); ++row) {
    QVariantMap game;
    const auto roles = m_library->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
      game.insert(QString::fromUtf8(it.value()),
                  m_library->data(m_library->index(row), it.key()));
    enqueue(game);
  }
  if (m_queue.isEmpty())
    return;
  promoteVisibleGames();
  next();
}

void GameMetadata::promoteVisibleGames() {
  if (m_queue.isEmpty() || m_visible == nullptr)
    return;
  QSet<QString> onScreen;
  for (int row = 0; row < m_visible->rowCount(); ++row)
    onScreen.insert(
        m_visible->data(m_visible->index(row, 0), GameRoles::MetadataKey).toString());
  if (onScreen.isEmpty())
    return;
  std::stable_partition(m_queue.begin(), m_queue.end(), [&onScreen](const QVariantMap& game) {
    return onScreen.contains(game.value("metadataKey").toString());
  });
}

void GameMetadata::dropUnwantedPortraits() {
  if (m_library == nullptr)
    return;
  int dropped = 0;
  for (int row = 0; row < m_library->rowCount(); ++row) {
    const QModelIndex game = m_library->index(row);
    const QString id = game.data(GameRoles::MetadataKey).toString();
    if (id.isEmpty())
      continue;
    auto value = entry(id);
    if (!value.contains("portrait"))
      continue;
    if (wantsPortraitCover(game.data(GameRoles::System).toString(),
                           game.data(GameRoles::Source).toString(),
                           game.data(GameRoles::SourceCoverPath).toString()))
      continue;
    // A portrait the user chose is stored as a custom cover, which outranks this and stays.
    value.remove("portrait");
    value.remove("gridCoverId");
    persist(id, value);
    ++dropped;
  }
  if (dropped > 0) {
    m_status = QStringLiteral("Restored artwork on %1 %2")
                   .arg(dropped)
                   .arg(dropped == 1 ? "game" : "games");
    emit changed();
  }
}
void GameMetadata::persist(const QString& id, const QVariantMap& value) {
  if (id.isEmpty())
    return;
  QSqlQuery query(m_database);
  query.prepare("INSERT OR REPLACE INTO game_metadata(game_key,payload) VALUES(?,?)");
  query.addBindValue(id);
  query.addBindValue(
      QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
  if (!query.exec()) {
    m_status = "Could not save game metadata";
    emit changed();
    return;
  }
  m_entries.insert(id, value);
  emit entryChanged(id);
  emit changed();
}
void GameMetadata::inspect(const QVariantMap& game) {
  m_selected = game;
  m_candidates.clear();
  m_covers.clear();
  emit changed();
}
bool GameMetadata::needsIdentifying(const QVariantMap& saved, qint64 now) {
  // An answer produced by older rules is stale however recently it was written. Without this a
  // matching fix would reach existing libraries only as each entry aged out, which for a shelf
  // marked "Needs identification" means a month of looking broken.
  if (saved.value("matchVersion").toInt() < kMatchVersion)
    return true;
  return saved.value("updated").toLongLong() <= now - kRatingFreshnessSeconds;
}

void GameMetadata::enqueue(const QVariantMap& game) {
  if (game.value("isPortal").toBool() || game.value("metadataKey").toString().isEmpty())
    return;
  const auto saved = entry(game.value("metadataKey").toString());
  if (saved.value("rejected").toBool())
    return;
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const bool ratings = m_insights && m_insights->configured() && needsIdentifying(saved, now);
  const bool portrait = hasGridKey() &&
                        wantsPortraitCover(game.value("system").toString(),
                                           game.value("source").toString(),
                                           game.value("sourceCoverPath").toString()) &&
                        !QFileInfo::exists(saved.value("portrait").toString()) &&
                        saved.value("coverAttempt").toLongLong() <= now - 86400;
  if (!ratings && !portrait)
    return;
  m_queue.enqueue(game);
}
void GameMetadata::refreshLibrary() {
  if (busy() || !m_library)
    return;
  if ((!m_insights || !m_insights->configured()) && !hasGridKey()) {
    finish("Connect IGDB or SteamGridDB in settings first");
    return;
  }
  m_cancelled = false;
  m_stoppedByHand = false;
  m_queue.clear();
  for (int i = 0; i < m_library->rowCount(); ++i) {
    QVariantMap game;
    const auto roles = m_library->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
      game.insert(QString::fromUtf8(it.value()), m_library->data(m_library->index(i), it.key()));
    enqueue(game);
  }
  promoteVisibleGames();
  next();
}
void GameMetadata::next() {
  // Pending games keep the public busy state active during the request delay.
  if (m_busy || m_secrets.isRunning() || m_cancelled)
    return;
  if (m_queue.isEmpty()) {
    // Say how many games could not be identified confidently, so the exceptions are a known
    // quantity rather than something to discover one game at a time.
    int unidentified = 0;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
      if (it.value().value("matchStatus").toString() == "Needs identification")
        ++unidentified;
    m_status = unidentified == 0
                   ? QStringLiteral("Library metadata is up to date")
                   : QStringLiteral("Library metadata is up to date. %1 %2 identification; open a "
                                    "game's details to choose its match.")
                         .arg(unidentified)
                         .arg(unidentified == 1 ? "game needs" : "games need");
    emit changed();
    return;
  }
  m_active = m_queue.dequeue();
  m_manual = false;
  m_busy = true;
  m_igdbStage = "games";
  auto saved = entry(key());
  if (saved.value("updated").toLongLong() > QDateTime::currentSecsSinceEpoch() - 30 * 86400) {
    gridSearch();
    return;
  }
  if (saved.value("igdbId").toLongLong() > 0 && m_insights && m_insights->configured()) {
    const QByteArray query = QByteArray(fields) + "where id = " +
                             QByteArray::number(saved.value("igdbId").toLongLong()) + "; limit 1;";
    requestIgdb(query, "games", "games");
    return;
  } else if (m_insights && m_insights->configured()) {
    if (m_active.value("source").toString() == "Steam") {
      const auto mapping = IgdbApi::steamMappingQuery(m_active.value("appId").toString());
      if (!mapping.isEmpty()) {
        requestIgdb(mapping, "external_games", "mapping");
        return;
      }
    }
    const auto query =
        searchQuery(m_active.value("title").toString(), m_active.value("system").toString());
    if (!query.isEmpty()) {
      requestIgdb(query, "games", "games");
      return;
    }
  }
  if (m_insights && m_insights->busy()) {
    m_busy = false;
    m_queue.prepend(m_active);
    QTimer::singleShot(kBetweenGamesMs, this, &GameMetadata::next);
    return;
  }
  gridSearch();
}
void GameMetadata::search(const QString& title) {
  if (busy() || !m_insights || !m_insights->configured() || m_selected.isEmpty())
    return;
  m_cancelled = false;
  m_igdbStage = "games";
  m_active = m_selected;
  m_manual = true;
  m_candidateProvider = "igdb";
  m_candidates.clear();
  m_covers.clear();
  const auto query = searchQuery(title, m_active.value("system").toString());
  if (query.isEmpty()) {
    finish("This platform is unsupported");
    return;
  }
  m_queue.clear();
  m_busy = true;
  requestIgdb(query, "games", "games");
  m_status = "Searching IGDB";
  emit changed();
}
void GameMetadata::matchResult(const QByteArray& data, const QString& error) {
  if (!m_busy)
    return;
  if (m_cancelled) {
    finish("Metadata update stopped");
    return;
  }
  if (error.isEmpty() && QJsonDocument::fromJson(data).isArray())
    m_queryCache.insert(m_queryKey, data);
  if (m_igdbStage == "popularity") {
    auto value = entry(key());
    if (error.isEmpty() && QJsonDocument::fromJson(data).isArray()) {
      value["popularity"] = -1.0;
      for (const auto& item : QJsonDocument::fromJson(data).array()) {
        const auto obj = item.toObject();
        const double score = obj.value("value").toDouble(-1);
        if (obj.value("game_id").toInteger() == value.value("igdbId").toLongLong() &&
            std::isfinite(score) && score >= 0) {
          value["popularity"] = score;
          break;
        }
      }
      persist(key(), value);
    }
    m_igdbStage.clear();
    gridSearch();
    return;
  }
  if (!error.isEmpty()) {
    m_queue.clear();
    finish(error);
    return;
  }
  if (m_igdbStage == "mapping") {
    qint64 id = 0;
    const bool mapped = IgdbApi::parseSteamMapping(data, &id);
    m_igdbStage = mapped ? "mappedGame" : "games";
    requestIgdb(mapped ? QByteArray(fields) + "where id = " + QByteArray::number(id) + "; limit 1;"
                       : searchQuery(m_active.value("title").toString(),
                                     m_active.value("system").toString()),
                "games", m_igdbStage);
    return;
  }
  if (!QJsonDocument::fromJson(data).isArray()) {
    m_queue.clear();
    finish("IGDB returned invalid data. Cached metadata is unchanged.");
    return;
  }
  const auto matches = parseMatches(data, platformId(m_active.value("system").toString()));
  if (m_manual) {
    m_candidates = matches;
    m_candidateProvider = "igdb";
    finish(matches.isEmpty() ? "No IGDB matches. Try another title."
                             : "Choose the matching game and edition");
    return;
  }
  const auto saved = entry(key());
  // A match the user chose stays chosen. Refreshing its rating must never hand the game to a
  // different catalogue entry, however well another one scores.
  const bool userChose = saved.value("manualMatch").toBool() && saved.value("igdbId").toLongLong() > 0;
  QVariantList exact;
  for (const auto& match : matches) {
    if (userChose) {
      if (saved.value("igdbId").toLongLong() == match.toMap().value("id").toLongLong())
        exact.append(match);
      continue;
    }
    if (m_igdbStage == "mappedGame" ||
        saved.value("igdbId").toLongLong() == match.toMap().value("id").toLongLong() ||
        normalizedTitle(match.toMap().value("title").toString()) ==
            normalizedTitle(m_active.value("title").toString()))
      exact.append(match);
  }
  if (exact.size() == 1)
    acceptMatch(exact.first().toMap());
  else if (exact.size() > 1) {
    // Several catalogue entries carry the same name on the same platform: usually a regional
    // duplicate or a compilation beside the game. The entry people actually rated is the one
    // to keep, so pick the most rated and fall back to the lowest id for a stable answer.
    QVariantMap best;
    for (const auto& candidate : exact) {
      const auto map = candidate.toMap();
      if (best.isEmpty() ||
          map.value("ratingCount").toInt() > best.value("ratingCount").toInt() ||
          (map.value("ratingCount").toInt() == best.value("ratingCount").toInt() &&
           map.value("id").toLongLong() < best.value("id").toLongLong()))
        best = map;
    }
    acceptMatch(best);
  } else {
    auto value = saved;
    value["matchStatus"] = "Needs identification";
    value["updated"] = QDateTime::currentSecsSinceEpoch();
    value["matchVersion"] = kMatchVersion;
    persist(key(), value);
    gridSearch();
  }
}
void GameMetadata::chooseMatch(int index) {
  if (busy() || m_candidateProvider != "igdb" || index < 0 || index >= m_candidates.size() ||
      m_active.value("metadataKey") != m_selected.value("metadataKey"))
    return;
  m_cancelled = false;
  m_queue.clear();
  m_manual = true;
  m_busy = true;
  acceptMatch(m_candidates.at(index).toMap());
}
void GameMetadata::acceptMatch(const QVariantMap& match) {
  auto value = entry(key());
  if (value.value("igdbId") != match.value("id"))
    value = {};
  value["igdbId"] = match.value("id");
  value["title"] = match.value("title");
  value["year"] = match.value("year");
  value["rating"] = match.value("rating");
  value["ratingCount"] = match.value("ratingCount");
  value["matchStatus"] = "Matched to IGDB";
  value["rejected"] = false;
  value["updated"] = QDateTime::currentSecsSinceEpoch();
  value["ratingProvider"] = "igdb";
  value["ratingField"] = "total_rating";
  value["platform"] = m_active.value("system");
  value["localTitle"] = m_active.value("title");
  value["manualMatch"] = m_manual || value.value("manualMatch").toBool();
  value["matchVersion"] = kMatchVersion;
  persist(key(), value);
  m_candidates.clear();
  requestIgdb("fields game_id,value; where game_id = " +
                  QByteArray::number(value.value("igdbId").toLongLong()) +
                  " & popularity_type = 1; limit 1;",
              "popularity_primitives", "popularity");
}
void GameMetadata::rejectMatch() {
  if (busy() || m_selected.isEmpty())
    return;
  persist(m_selected.value("metadataKey").toString(),
          {{"rejected", true}, {"matchStatus", "Automatic matching disabled"}});
  m_candidates.clear();
  m_covers.clear();
  m_status = "Match removed. Search to identify this game again.";
  emit changed();
}
void GameMetadata::findCovers() {
  if (busy() || m_selected.isEmpty())
    return;
  m_cancelled = false;
  m_active = m_selected;
  m_manual = true;
  m_busy = true;
  m_candidates.clear();
  m_covers.clear();
  gridSearch();
}
void GameMetadata::gridSearch() {
  if (!hasGridKey()) {
    finish("IGDB data saved. Connect SteamGridDB for portrait covers.");
    return;
  }
  if (!m_manual && !wantsPortraitCover(m_active.value("system").toString(),
                                       m_active.value("source").toString(),
                                       m_active.value("sourceCoverPath").toString())) {
    // An earlier run may have downloaded a portrait over artwork that should have been kept.
    // Drop it so the game shows its own art again. A portrait the user picked is stored as a
    // custom cover, which outranks this and is untouched. The file stays for the ordinary
    // cache trim to reclaim.
    auto value = entry(key());
    if (value.contains("portrait")) {
      value.remove("portrait");
      value.remove("gridCoverId");
      persist(key(), value);
    }
    finish("IGDB data saved. This game keeps the artwork its source provides.");
    return;
  }
  auto value = entry(key());
  if (!m_manual && QFileInfo::exists(value.value("portrait").toString())) {
    finish("Cached portrait kept");
    return;
  }
  value["coverAttempt"] = QDateTime::currentSecsSinceEpoch();
  persist(key(), value);
  if (value.value("gridId").toLongLong() > 0) {
    gridCovers(value.value("gridId").toLongLong());
    return;
  }
  QString title = value.value("title", m_active.value("title")).toString();
  get(QUrl("https://www.steamgriddb.com/api/v2/search/autocomplete/" +
           QString::fromLatin1(QUrl::toPercentEncoding(title))),
      "search");
}
void GameMetadata::gridCovers(qint64 id) {
  if (id <= 0) {
    finish("No matching SteamGridDB game");
    return;
  }
  get(QUrl(QStringLiteral("https://www.steamgriddb.com/api/v2/grids/game/"
                          "%1?dimensions=600x900&types=static&nsfw=false&humor=false")
               .arg(id)),
      "covers");
}
void GameMetadata::chooseGridGame(int index) {
  if (busy() || m_candidateProvider != "grid" || index < 0 || index >= m_candidates.size() ||
      m_active.value("metadataKey") != m_selected.value("metadataKey"))
    return;
  m_cancelled = false;
  m_queue.clear();
  auto value = entry(key());
  value["gridId"] = m_candidates.at(index).toMap().value("id");
  value.remove("portrait");
  persist(key(), value);
  m_candidates.clear();
  m_busy = true;
  gridCovers(value.value("gridId").toLongLong());
}
void GameMetadata::chooseCover(int index) {
  if (busy() || index < 0 || index >= m_covers.size() ||
      m_active.value("metadataKey") != m_selected.value("metadataKey"))
    return;
  const auto cover = m_covers.at(index).toMap();
  const QUrl url(cover.value("url").toString());
  if (!trustedImageUrl(url))
    return;
  m_cancelled = false;
  m_queue.clear();
  m_downloadId = cover.value("id").toLongLong();
  m_busy = true;
  get(url, "image");
}
void GameMetadata::get(const QUrl& url, const QString& stage) {
  emit changed();
  // Hold SteamGridDB's API calls apart. Image downloads come from a CDN and are slow enough on
  // their own. Without this the queue would burst three calls per game back to back.
  if (stage != "image" && m_sinceGridRequest.isValid()) {
    const qint64 waited = m_sinceGridRequest.elapsed();
    if (waited < kGridRequestGapMs) {
      QTimer::singleShot(kGridRequestGapMs - waited, this,
                         [this, url, stage] { get(url, stage); });
      return;
    }
  }
  if (stage != "image")
    m_sinceGridRequest.restart();
  QNetworkRequest request(url);
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  if (stage != "image")
    request.setRawHeader("Authorization", "Bearer " + m_gridKey);
  auto* reply = m_network->get(request);
  auto buffer = std::make_shared<QByteArray>();
  const qsizetype limit = stage == "image" ? 12 * 1024 * 1024 : 1024 * 1024;
  connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, limit] {
    buffer->append(reply->read(limit - buffer->size() + 1));
    if (buffer->size() > limit)
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, stage, limit] {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok =
        reply->error() == QNetworkReply::NoError && status == 200 && buffer->size() <= limit;
    reply->deleteLater();
    if (!ok) {
      m_queue.clear();
      finish(status == 401   ? "SteamGridDB rejected the API key"
             : status == 429 ? "SteamGridDB rate limit reached. Try again later."
                             : "SteamGridDB unavailable. Cached artwork is unchanged.");
      return;
    }
    response(*buffer, stage);
  });
}
void GameMetadata::response(const QByteArray& data, const QString& stage) {
  if (m_cancelled) {
    finish("Metadata update stopped");
    return;
  }
  if (stage == "image") {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (size != QSize(600, 900)) {
      finish("Portrait has unexpected dimensions");
      return;
    }
    const QImage image = reader.read();
    if (image.isNull()) {
      finish("Could not decode portrait");
      return;
    }
    QDir().mkpath(m_cacheRoot);
    const QString path =
        m_cacheRoot + '/' +
        QString::fromLatin1(
            QCryptographicHash::hash(key().toUtf8(), QCryptographicHash::Sha256).toHex()) +
        '-' + QString::number(m_downloadId) + ".png";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
      finish("Could not save portrait");
      return;
    }
    if (m_manual && m_library) {
      for (int row = 0; row < m_library->rowCount(); ++row)
        if (m_library->data(m_library->index(row), GameRoles::MetadataKey).toString() == key()) {
          if (!m_library->setCustomCover(row, QUrl::fromLocalFile(path))) {
            finish("Could not apply the selected cover");
            return;
          }
          break;
        }
    }
    auto value = entry(key());
    value["portrait"] = path;
    value["gridCoverId"] = m_downloadId;
    value["portraitUpdated"] = QDateTime::currentSecsSinceEpoch();
    persist(key(), value);
    trimPortraitCache();
    const bool selectedManually = m_manual;
    const QString selectedKey = key();
    if (selectedManually)
      m_covers.clear();
    finish("Portrait saved from SteamGridDB");
    if (selectedManually)
      emit portraitSelected(selectedKey);
    return;
  }
  const auto object = QJsonDocument::fromJson(data).object();
  if (!object.value("success").toBool() || !object.value("data").isArray()) {
    m_queue.clear();
    finish("SteamGridDB returned invalid data");
    return;
  }
  if (stage == "test") {
    finish("SteamGridDB connection working");
    return;
  }
  if (stage == "search") {
    QVariantList matches, exact;
    const auto saved = entry(key());
    const QString title = normalizedTitle(saved.value("title", m_active.value("title")).toString());
    for (const auto& item : object.value("data").toArray()) {
      const auto game = item.toObject();
      if (game.value("id").toInteger() <= 0 || game.value("name").toString().isEmpty())
        continue;
      const QVariantMap match{{"id", game.value("id").toInteger()},
                              {"title", game.value("name").toString()}};
      matches.append(match);
      const qint64 released = game.value("release_date").toInteger();
      const int year = released > 0 ? QDateTime::fromSecsSinceEpoch(released).date().year() : 0;
      if (normalizedTitle(match.value("title").toString()) == title &&
          (saved.value("year").toInt() == 0 || year == 0 || saved.value("year").toInt() == year))
        exact.append(match);
    }
    if (!m_manual && exact.size() == 1 && saved.value("igdbId").toLongLong() > 0) {
      auto value = saved;
      value["gridId"] = exact.first().toMap().value("id");
      persist(key(), value);
      gridCovers(value.value("gridId").toLongLong());
    } else if (m_manual) {
      m_candidates = matches;
      m_candidateProvider = "grid";
      finish("Choose the matching SteamGridDB game");
    } else
      finish("Portrait needs a confirmed match. Open game details to choose.");
  } else {
    m_covers = parseCovers(data);
    if (!m_manual && !m_covers.isEmpty()) {
      const auto cover = m_covers.first().toMap();
      m_downloadId = cover.value("id").toLongLong();
      get(QUrl(cover.value("url").toString()), "image");
    } else
      finish(m_covers.isEmpty() ? "No portrait covers found" : "Choose a portrait cover");
  }
}
void GameMetadata::finish(const QString& message) {
  m_busy = false;
  m_status = message;
  emit changed();
  if (!m_queue.isEmpty())
    QTimer::singleShot(kBetweenGamesMs, this, &GameMetadata::next);
}
void GameMetadata::storeGridKey(QString key) {
  if (busy())
    return;
  key = key.trimmed();
  if (!QRegularExpression("^[A-Za-z0-9]{20,128}$").match(key).hasMatch()) {
    finish("That SteamGridDB API key is invalid");
    return;
  }
  QByteArray bytes = key.toLatin1();
  key.fill(QChar::Null);
  secretOperation(1, bytes);
  bytes.fill('\0');
}
void GameMetadata::removeGridKey() {
  if (!busy())
    secretOperation(2);
}
void GameMetadata::secretOperation(int action, QByteArray value) {
  disconnect(&m_secrets, nullptr, this, nullptr);
  connect(&m_secrets, &QFutureWatcher<InsightsSecretResult>::finished, this, [this, action] {
    auto result = m_secrets.future().takeResult();
    if (!result.success) {
      result.secret.fill('\0');
      finish("Secret Service could not update the SteamGridDB key");
      return;
    }
    m_gridKey.fill('\0');
    m_gridKey = result.secret;
    QDir().mkpath(m_cacheRoot);
    if (action == 2)
      QFile::remove(m_cacheRoot + "/configured");
    else if (!m_gridKey.isEmpty()) {
      QSaveFile marker(m_cacheRoot + "/configured");
      if (marker.open(QIODevice::WriteOnly)) {
        marker.write("1");
        marker.commit();
      }
    }
    finish(action == 2 ? "SteamGridDB disconnected. Cached covers are kept."
                       : "SteamGridDB key available");
  });
  m_secrets.setFuture(QtConcurrent::run([action, value]() mutable {
    InsightsSecretResult result;
    SecretSchema* schema =
        secret_schema_new("io.github.tsouth89.Omakade.SteamGridDB", SECRET_SCHEMA_NONE, "service",
                          SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
    GError* error = nullptr;
    if (action == 1)
      secret_password_store_sync(schema, SECRET_COLLECTION_DEFAULT, "Omakade SteamGridDB API key",
                                 value.constData(), nullptr, &error, "service", "api-key", nullptr);
    if (action == 2)
      secret_password_clear_sync(schema, nullptr, &error, "service", "api-key", nullptr);
    else if (!error) {
      gchar* password =
          secret_password_lookup_sync(schema, nullptr, &error, "service", "api-key", nullptr);
      if (password) {
        result.secret = password;
        result.found = true;
        secret_password_free(password);
      }
    }
    result.success = error == nullptr;
    if (error)
      g_error_free(error);
    secret_schema_unref(schema);
    value.fill('\0');
    return result;
  }));
  value.fill('\0');
  emit changed();
}

void GameMetadata::cancel() {
  m_queue.clear();
  m_cancelled = true;
  m_stoppedByHand = true;
  m_settle.stop();
  m_status = m_busy ? "Stopping after the current request" : "Metadata update stopped";
  emit changed();
}
void GameMetadata::requestIgdb(QByteArray query, QString endpoint, QString stage) {
  emit changed();
  m_igdbStage = stage;
  m_queryKey = endpoint.toUtf8() + ':' + query;
  if (m_queryCache.contains(m_queryKey)) {
    const auto cached = m_queryCache.value(m_queryKey);
    QTimer::singleShot(0, this, [this, cached] { matchResult(cached, {}); });
    return;
  }
  QTimer::singleShot(350, this, [this, query, endpoint, stage] {
    if (m_cancelled) {
      finish("Metadata update stopped");
      return;
    }
    if (!m_insights || !m_insights->configured() || query.isEmpty()) {
      gridSearch();
      return;
    }
    if (!m_insights->requestCatalog(query, endpoint))
      requestIgdb(query, endpoint, stage);
  });
}

void GameMetadata::testGridConnection() {
  if (busy() || !hasGridKey())
    return;
  m_queue.clear();
  m_cancelled = false;
  m_busy = true;
  get(QUrl("https://www.steamgriddb.com/api/v2/search/autocomplete/Mario"), "test");
}
void GameMetadata::clearPortraitCache() {
  if (busy())
    return;
  const QDir cache(m_cacheRoot);
  for (const auto& file : cache.entryList({"*.png"}, QDir::Files))
    QFile::remove(cache.filePath(file));
  const auto keys = m_entries.keys();
  for (const auto& id : keys) {
    auto value = entry(id);
    if (value.remove("portrait")) {
      value.remove("coverAttempt");
      persist(id, value);
    }
  }
  finish("Downloaded portraits cleared. Your chosen covers are kept.");
}

void GameMetadata::setCacheLimitMb(int megabytes) {
  m_cacheLimitBytes = qBound(1, megabytes, 4096) * 1024LL * 1024;
  trimPortraitCache();
}
void GameMetadata::trimPortraitCache() {
  const QDir cache(m_cacheRoot);
  qint64 kept = 0;
  QSet<QString> removed;
  for (const auto& file : cache.entryInfoList({"*.png"}, QDir::Files, QDir::Time)) {
    if (kept + file.size() <= m_cacheLimitBytes)
      kept += file.size();
    else if (QFile::remove(file.absoluteFilePath()))
      removed.insert(file.absoluteFilePath());
  }
  if (removed.isEmpty())
    return;
  for (const auto& id : m_entries.keys()) {
    auto value = entry(id);
    if (removed.contains(value.value("portrait").toString())) {
      value.remove("portrait");
      persist(id, value);
    }
  }
}
