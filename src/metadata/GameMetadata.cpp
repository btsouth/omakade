#include "metadata/GameMetadata.h"
#include "app/SecretService.h"
#include "library/ConsoleCatalog.h"
#include "library/CoverCachePolicy.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "metadata/RegionalMetadata.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlQuery>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
constexpr auto fields =
    "fields "
    "name,platforms,game_type,cover.image_id,first_release_date,total_rating,total_rating_count,"
    "release_dates.date,release_dates.human,release_dates.y,release_dates.platform,"
    "release_dates.release_region.region,"
    "aggregated_rating,aggregated_rating_count,genres.name,summary,"
    "involved_companies.company.name,involved_companies.developer,"
    "involved_companies.publisher,screenshots.image_id,screenshots.width,screenshots.height,screenshots.animated,"
    "alternative_names.name,alternative_names.comment,"
    "game_localizations.name,game_localizations.region.name,"
    "game_localizations.region.identifier,version_parent,version_title; ";

// IGDB allows four requests a second; 350 ms keeps a comfortable margin. SteamGridDB is not
// documented as precisely, so its calls are held a little further apart. With both providers
// paced at the request, the gap between games only has to yield to the event loop.
constexpr int kGridRequestGapMs = 250;
constexpr int kBetweenGamesMs = 100;
constexpr int kPayloadVersion = 7;

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
  title.replace(QLatin1Char('&'), QStringLiteral(" and "));
  QString normalized = title.normalized(QString::NormalizationForm_KC)
                           .toCaseFolded()
                           .replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}\"]+")), " ")
                           .simplified();
  // A leading article is never what separates two games, and the catalogues disagree about it.
  static const QRegularExpression leadingArticle(QStringLiteral("^(?:the|a|an) (?=.)"));
  normalized.remove(leadingArticle);
  // Accents are a spelling difference, not a different game: a cartridge labelled Pokemon
  // Stadium 2 is the catalogue's Pokémon Stadium 2.
  QString folded;
  folded.reserve(normalized.size());
  for (const QChar character : normalized.normalized(QString::NormalizationForm_D)) {
    if (character.category() != QChar::Mark_NonSpacing) {
      folded.append(character);
    }
  }
  return folded.normalized(QString::NormalizationForm_C);
}
bool GameMetadata::equivalentTitle(const QString& left, const QString& right) {
  // Catalogues split compounds and abbreviations differently: Clay Fighter/ClayFighter,
  // Dream T.V./Dream TV, and Goku-den/Gokuden. Preserve every letter and number.
  auto comparable = [](QString title) {
    title.replace(QLatin1Char('&'), QStringLiteral(" and "));
    title = normalizedTitle(title);
    static const QRegularExpression part(QStringLiteral(R"(\bpart (?=(?:[ivxlcdm]+|[0-9]+)\b))"));
    title.remove(part);
    return title;
  };
  QString a = comparable(left), b = comparable(right);
  if (a.isEmpty() || b.isEmpty()) return false;
  if (a == b) return true;
  // "1 2" is not "12", even though removing spaces would make them identical.
  const auto numbers = [](const QString& text) {
    QStringList result;
    static const QRegularExpression digits(QStringLiteral("[0-9]+"));
    auto matches = digits.globalMatch(text);
    while (matches.hasNext()) result.append(matches.next().captured());
    return result;
  };
  if (numbers(a) != numbers(b)) return false;
  return a.remove(QLatin1Char(' ')) == b.remove(QLatin1Char(' '));
}

bool GameMetadata::wantsPortraitCover(const QString& system, const QString& source,
                                      const QString& sourceCover) {
  Q_UNUSED(system);
  // Steam ships an official 600x900 capsule for every game. It downloads on demand, so judging
  // by the file alone would hand a fan portrait to any game whose capsule had not arrived yet.
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0)
    return false;
  // Everything else is decided by the shape of the artwork the game already has, not by which
  // system it came from. A physical box that was printed portrait, an NES box or a GameTDB
  // cover, already works as a cover and is authentic, so it is kept. A box that was printed
  // wide or square, an N64 carton or a Dreamcast case, cannot fill a card without being cropped
  // or letterboxed, and a portrait reads better there even when it is fan made. This rule
  // only decides whether a new portrait download is needed.
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

QString GameMetadata::withoutBrandPrefix(const QString& normalized) {
  // Normalising has already turned the apostrophe into a space, so the prefix reads "disney s ".
  // The length bound keeps this to a publisher or a person rather than most of the title.
  static const QRegularExpression brandPrefix(QStringLiteral("^[\\p{L}\\p{N} ]{2,24}? s "));
  QString stripped = normalized;
  stripped.remove(brandPrefix);
  return stripped.isEmpty() ? normalized : stripped;
}

qint64 GameMetadata::chooseGridMatch(const QVariantList& candidates, const QString& title,
                                     int year, const QString& system) {
  // The title still has to match. Accepting a subtitle as well would turn "The Lion King" into
  // "The Lion King III: Timon & Pumbaa", which is a different game with different artwork, so a
  // near miss is left for someone to confirm by hand.
  //
  // The release year is a tie-breaker rather than a gate. IGDB and SteamGridDB disagree about
  // it constantly for older games, because one is dating the arcade original or the Japanese
  // release and the other the cartridge that was actually dumped: Contra is 1987 to one and
  // 1988 to the other, Aero the Acro-Bat 2 is 1993 and 1994. Requiring them to agree threw
  // away covers the catalogue plainly had.
  // Preserve explicit Japanese long vowels before accent folding. Do not contract every
  // "ou" or "uu": that would make unrelated unaccented names equivalent.
  const auto longVowelTitle = [](QString name) {
    name = name.normalized(QString::NormalizationForm_C).toCaseFolded();
    name.replace(QStringLiteral("ō"), QStringLiteral("ou"));
    name.replace(QStringLiteral("ū"), QStringLiteral("uu"));
    return normalizedTitle(name);
  };
  const QString longWanted = longVowelTitle(title);
  const QString wanted = normalizedTitle(title);
  if (wanted.isEmpty())
    return 0;
  // Two passes. The publisher prefix is only ignored once matching on the name as written has
  // found nothing, so "Kirby's Dream Land" is never allowed to become "Dream Land" while a
  // Kirby's Dream Land sits in the results.
  for (const bool ignoreBrand : {false, true}) {
    const QString target = ignoreBrand ? withoutBrandPrefix(wanted) : wanted;
    if (ignoreBrand && target == wanted)
      break;
    const QString longTarget = ignoreBrand ? withoutBrandPrefix(longWanted) : longWanted;
    qint64 best = 0;
    int bestDistance = kGridYearTolerance + 1;
    bool tied = false;
    for (const QVariant& item : candidates) {
      const QVariantMap candidate = item.toMap();
      const qint64 id = candidate.value("id").toLongLong();
      if (id <= 0)
        continue;
      QString candidateTitle = candidate.value("title").toString();
      // SteamGridDB distinguishes the SNES Alien vs. Predator from the Atari and
      // Capcom games with a catalogue qualifier. Only remove known qualifiers for
      // the actual ROM platform; arbitrary parentheses can identify a different game.
      if (system.compare(QStringLiteral("snes"), Qt::CaseInsensitive) == 0) {
        static const QRegularExpression snesQualifier(
            QStringLiteral(R"(\s*\((?:Nintendo|SNES|Super Nintendo|Super Famicom)\)$)"),
            QRegularExpression::CaseInsensitiveOption);
        candidateTitle = candidateTitle.trimmed().remove(snesQualifier);
      }
      const QString name = normalizedTitle(candidateTitle);
      const QString longName = longVowelTitle(candidateTitle);
      const bool normalMatch = equivalentTitle(name, target) ||
                               (ignoreBrand && equivalentTitle(withoutBrandPrefix(name), target));
      const bool longMatch = equivalentTitle(longName, longTarget) ||
                             (ignoreBrand && equivalentTitle(withoutBrandPrefix(longName), longTarget));
      if (!normalMatch && !longMatch)
        continue;
      // A year missing on either side is not evidence against a match, only the absence of
      // evidence for one, so it ranks behind an exact year and ahead of one that disagrees.
      const int candidateYear = candidate.value("year").toInt();
      const int distance = year == 0 || candidateYear == 0 ? 1 : qAbs(candidateYear - year);
      if (distance > kGridYearTolerance)
        continue;
      if (distance < bestDistance) {
        bestDistance = distance;
        best = id;
        tied = false;
      } else if (distance == bestDistance) {
        tied = true;
      }
    }
    // Two entries the same distance away are two different games with one name, such as the
    // three Paperboys. Guessing between them puts the wrong cover on a card, so neither is
    // taken, and the looser pass is not allowed to rescue a genuinely ambiguous name either.
    if (tied)
      return 0;
    if (best > 0)
      return best;
  }
  return 0;
}

QList<int> GameMetadata::platformIds(const QString& system) {
  // IGDB catalogues a Japanese release under its own machine, so a Super Famicom cartridge is
  // platform 58 rather than the Super Nintendo's 19. Filtering on the western platform alone
  // threw away games IGDB had already returned by name, which was the single largest reason a
  // shelf of imports stayed unidentified.
  static const QHash<QString, QList<int>> ids{
      {"nes", {18, 99}},   {"snes", {19, 58}}, {"gb", {33}},      {"gbc", {22}},
      {"gba", {24}},       {"n64", {4}},       {"genesis", {29}}, {"psx", {7}},
      {"dreamcast", {23}}, {"gamecube", {21}}, {"wii", {5}},      {"ps2", {8}},
      {"switch", {130}},   {"wiiu", {41}},     {"ps4", {48}}};
  const QString id = ConsoleCatalog::idFor(system);
  if (ids.contains(id))
    return ids.value(id);
  return system.isEmpty() ? QList<int>{6} : QList<int>{};
}

QStringList GameMetadata::platformNames(const QVariantList& ids) {
  static const QHash<qint64, QString> names{
      {3, "Linux"},
      {4, "Nintendo 64"},
      {5, "Wii"},
      {6, "PC"},
      {7, "PlayStation"},
      {8, "PlayStation 2"},
      {9, "PlayStation 3"},
      {11, "Xbox"},
      {12, "Xbox 360"},
      {14, "Mac"},
      {18, "NES"},
      {19, "Super Nintendo"},
      {20, "Nintendo DS"},
      {21, "GameCube"},
      {22, "Game Boy Color"},
      {23, "Dreamcast"},
      {24, "Game Boy Advance"},
      {29, "Sega Genesis"},
      {33, "Game Boy"},
      {37, "Nintendo 3DS"},
      {38, "PSP"},
      {41, "Wii U"},
      {46, "PS Vita"},
      {48, "PlayStation 4"},
      {49, "Xbox One"},
      {130, "Switch"},
      {167, "PlayStation 5"},
      {169, "Xbox Series X|S"},
  };
  QStringList result;
  for (const auto& id : ids) {
    const QString name = names.value(id.toLongLong());
    if (!name.isEmpty() && !result.contains(name))
      result.append(name);
  }
  return result;
}
QByteArray GameMetadata::searchQuery(const QString& title, const QString& system, bool cleanRomTags) {
  const QList<int> platforms = platformIds(system);
  if (title.trimmed().isEmpty() || platforms.isEmpty())
    return {};
  QByteArrayList numbers;
  for (int platform : platforms)
    numbers.append(QByteArray::number(platform));
  return QByteArray(fields) + "search " + quoted(cleanRomTags ? cleanTitle(title) : title).toUtf8() +
         "; where platforms = (" + numbers.join(',') + "); limit 20;";
}
QByteArray GameMetadata::aliasSearchQuery(const QString& title, const QString& system) {
  const auto platforms = platformIds(system);
  if (platforms.isEmpty() || cleanTitle(title).isEmpty())
    return {};
  QList<QByteArray> numbers;
  for (int platform : platforms)
    numbers.append(QByteArray::number(platform));
  const auto name = quoted(cleanTitle(title)).toUtf8();
  return QByteArray(fields) + "where platforms = (" + numbers.join(',') + ") & (name ~ " + name +
         " | alternative_names.name ~ " + name + " | game_localizations.name ~ " + name +
         "); limit 20;";
}
QByteArray GameMetadata::discoveryQuery(const QString& title, const QString& system) {
  const auto platforms = platformIds(system);
  if (platforms.isEmpty()) return {};
  auto words = normalizedTitle(title).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  std::stable_sort(words.begin(), words.end(), [](const QString& a, const QString& b) {
    return a.size() > b.size();
  });
  QByteArrayList conditions, ids;
  QStringList fragments;
  for (const auto& word : words) {
    if (word.size() < 4) continue;
    const QString fragment = word.left(3);
    if (fragments.contains(fragment)) continue;
    fragments.append(fragment);
    const auto literal = quoted(fragment).toUtf8();
    conditions.append("(name ~ *" + literal + "* | alternative_names.name ~ *" + literal +
                      "* | game_localizations.name ~ *" + literal + "*)");
    if (conditions.size() == 2) break;
  }
  if (conditions.isEmpty()) return {};
  for (int platform : platforms) ids.append(QByteArray::number(platform));
  // This only discovers candidates. The complete title and platform still have to match.
  return QByteArray(fields) + "where platforms = (" + ids.join(',') + ") & " +
         conditions.join(" & ") + "; limit 100;";
}

QVariantList GameMetadata::parseMatches(const QByteArray& data, const QList<int>& platforms) {
  QVariantList result;
  const auto doc = QJsonDocument::fromJson(data);
  if (!doc.isArray())
    return result;
  for (const auto& value : doc.array()) {
    const auto obj = value.toObject();
    if (obj.value("id").toInteger() <= 0 || obj.value("name").toString().isEmpty())
      continue;
    if (!platforms.isEmpty()) {
      const auto listed = obj.value("platforms").toArray();
      bool onPlatform = false;
      for (int platform : platforms)
        onPlatform = onPlatform || listed.contains(platform);
      if (!onPlatform)
        continue;
    }
    QVariantMap match{{"id", obj.value("id").toInteger()}, {"title", obj.value("name").toString()}};
    QStringList aliases;
    QVariantList aliasEvidence, localizations;
    for (const auto& item : obj.value("alternative_names").toArray()) {
      const auto alias = item.toObject();
      const QString name = alias.value("name").toString().simplified();
      if (name.isEmpty())
        continue;
      aliases.append(name);
      aliasEvidence.append(
          QVariantMap{{"name", name}, {"comment", alias.value("comment").toString()}});
    }
    for (const auto& item : obj.value("game_localizations").toArray()) {
      const auto localization = item.toObject();
      const auto region = localization.value("region").toObject();
      const QString name = localization.value("name").toString().simplified();
      if (name.isEmpty())
        continue;
      aliases.append(name);
      localizations.append(
          QVariantMap{{"name", name},
                      {"region", region.value("name").toString()},
                      {"regionIdentifier", region.value("identifier").toString()}});
    }
    aliases.removeDuplicates();
    match["aliases"] = aliases;
    match["gameType"] = obj.value("game_type").toInt(-1);
    match["alternativeNames"] = aliasEvidence;
    match["localizations"] = localizations;
    match["versionParent"] = obj.value("version_parent").toInteger();
    match["edition"] = obj.value("version_title").toString();
    QVariantList releases;
    for (const auto& row : obj.value("release_dates").toArray()) {
      const auto release = row.toObject();
      releases.append(QVariantMap{
          {"date", release.value("date").toInteger()},
          {"human", release.value("human").toString()},
          {"year", release.value("y").toInt()},
          {"platform", release.value("platform").toInt()},
          {"region", release.value("release_region").toObject().value("region").toString()}});
    }
    match["releaseDates"] = releases;
    QStringList releaseRegions;
    for (const auto& row : releases) {
      const auto release = row.toMap();
      if (platforms.contains(release.value("platform").toInt()) &&
          !release.value("region").toString().isEmpty()) {
        QString region = release.value("region").toString();
        region.replace('_', ' ');
        releaseRegions.append(region);
      }
    }
    releaseRegions.removeDuplicates();
    match["releaseRegions"] = releaseRegions;
    const qint64 released = obj.value("first_release_date").toInteger();
    match["year"] =
        released > 0 ? QDateTime::fromSecsSinceEpoch(released, QTimeZone::UTC).date().year() : 0;
    if (released > 0) {
      match["releaseText"] =
          QLocale(QLocale::English)
              .toString(QDateTime::fromSecsSinceEpoch(released, QTimeZone::UTC).date(),
                        "MMMM d, yyyy");
    }
    const auto rating = obj.value("total_rating");
    const int count = obj.value("total_rating_count").toInt();
    match["rating"] =
        rating.isDouble() && rating.toDouble() >= 0 && rating.toDouble() <= 100 && count > 0
            ? qRound(rating.toDouble())
            : -1;
    match["ratingCount"] = qMax(0, count);
    QStringList genres;
    for (const auto& genre : obj.value("genres").toArray())
      if (!genre.toObject().value("name").toString().isEmpty())
        genres.append(genre.toObject().value("name").toString());
    if (!genres.isEmpty())
      match["genres"] = genres;
    const QString summary = obj.value("summary").toString().simplified();
    if (!summary.isEmpty())
      match["summary"] = summary.left(1500);
    QStringList developers, publishers;
    QVariantList platformIds;
    for (const auto& platformId : obj.value("platforms").toArray())
      platformIds.append(platformId.toInteger());
    for (const auto& involved : obj.value("involved_companies").toArray()) {
      const auto company = involved.toObject();
      const QString name = company.value("company").toObject().value("name").toString();
      if (name.isEmpty())
        continue;
      if (company.value("developer").toBool() && !developers.contains(name))
        developers.append(name);
      if (company.value("publisher").toBool() && !publishers.contains(name))
        publishers.append(name);
    }
    if (!developers.isEmpty())
      match["developers"] = developers;
    if (!publishers.isEmpty())
      match["publishers"] = publishers;
    if (!platformIds.isEmpty())
      match["platformIds"] = platformIds;
    // Provider image IDs are identifiers, never arbitrary URLs or paths.
    static const QRegularExpression imageId(QStringLiteral("^[A-Za-z0-9_]+$"));
    const QString coverId = obj.value("cover").toObject().value("image_id").toString();
    if (imageId.match(coverId).hasMatch())
      match["igdbCoverUrl"] = QStringLiteral("https://images.igdb.com/igdb/image/upload/t_cover_big_2x/%1.jpg").arg(coverId);

    // Artwork includes advertisements, box scans, and unrelated promotional illustrations.
    // Only use landscape screenshots for an automatic backdrop. Keep ordering deterministic
    // when the provider returns the same candidates in a different order.
    QString bestId;
    qint64 bestArea = 0;
    int bestWidth = 0, bestHeight = 0;
    for (const auto& candidate : obj.value("screenshots").toArray()) {
      const auto image = candidate.toObject();
      const QString id = image.value("image_id").toString();
      const int width = image.value("width").toInt();
      const int height = image.value("height").toInt();
      if (!imageId.match(id).hasMatch() || image.value("animated").toBool() ||
          width < 160 || height < 144 || width > 32768 || height > 32768)
        continue;
      const double aspect = double(width) / height;
      if (aspect < 1.0 || aspect > 2.4)
        continue;
      // Resolution is capped at the downloaded size, rather than preferring huge originals.
      const qint64 area = qint64(std::min(width, 1920)) * std::min(height, 1080);
      if (area > bestArea || (area == bestArea && (bestId.isEmpty() || id < bestId))) {
        bestId = id;
        bestArea = area;
        bestWidth = width;
        bestHeight = height;
      }
    }
    if (!bestId.isEmpty()) {
      match["heroUrl"] = QStringLiteral("https://images.igdb.com/igdb/image/upload/t_1080p/%1.jpg").arg(bestId);
      match["heroKind"] = "screenshot";
      match["heroWidth"] = bestWidth;
      match["heroHeight"] = bestHeight;
    }
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
  QSet<qint64> ids;
  QSet<QString> urls;
  for (const auto& value : object.value("data").toArray()) {
    auto cover = value.toObject();
    if (cover.value("id").toInteger() <= 0 || cover.value("width").toInt() != 600 ||
        cover.value("height").toInt() != 900 || cover.value("nsfw").toBool() ||
        cover.value("humor").toBool() || !trustedImageUrl(QUrl(cover.value("url").toString())))
      continue;
    const auto id = cover.value("id").toInteger();
    const auto url = cover.value("url").toString();
    if (ids.contains(id) || urls.contains(url)) continue;
    ids.insert(id);
    urls.insert(url);
    result.append(
        QVariantMap{{"id", cover.value("id").toInteger()},
                    {"url", cover.value("url").toString()},
                    {"score", cover.value("score").toDouble()},
                    {"author", cover.value("author").toObject().value("name").toString()}});
  }
  // Preserve provider order for ties and missing scores. Score is community preference,
  // not evidence that an image is official or belongs to a particular edition.
  std::stable_sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
    return a.toMap().value("score").toDouble() > b.toMap().value("score").toDouble();
  });
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
  if (insights) {
    connect(insights, &GameInsightsService::catalogFinished, this, &GameMetadata::matchResult);
    // Credentials are read from the keyring on a worker thread, so the library can settle
    // before they arrive. Without this the pass looked once, found no connection, and never
    // looked again, leaving the whole library unidentified until something else changed.
    connect(insights, &GameInsightsService::changed, this, [this] {
      if (!m_stoppedByHand && !m_editing) {
        queueSelected();
        m_settle.start();
      }
    });
  }
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
  const auto settled = [this] {
    if (m_library == nullptr || m_library->rowCount() == 0)
      return;
    // Sources arrive over several seconds. Wait for a quiet moment before queuing, so a
    // library still loading is not walked once per source.
    m_settle.start();
  };
  m_settle.setSingleShot(true);
  m_settle.setInterval(2000);
  connect(&m_settle, &QTimer::timeout, this, &GameMetadata::continueLibraryPass);
  connect(m_library, &QAbstractItemModel::rowsInserted, this, settled);
  connect(m_library, &QAbstractItemModel::modelReset, this, settled);
  // Sources that load straight from their own database have already filled the library by the
  // time this runs, and those rows arrived before anything was listening. Waiting only for the
  // next change then waits forever, and nothing is ever identified.
  settled();
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
  // Opening a console swaps the whole view in one pass and reports it as a layout change, not
  // as rows coming and going. Without this, walking into a console never reordered anything and
  // its games waited behind the rest of the library.
  connect(m_visible, &QAbstractItemModel::layoutChanged, this, viewChanged);
}

void GameMetadata::setEditing(bool editing) {
  if (m_editing == editing)
    return;
  m_editing = editing;
  if (editing) {
    // Stand the queue down and hold it, rather than dropping it, so nothing already worked out
    // is repeated when the person is done.
    m_pausedQueue = m_queue;
    m_queue.clear();
    m_settle.stop();
    const auto saved = entry(m_selected.value("metadataKey").toString());
    const QString state = saved.value("matchStatus").toString();
    m_status = state.isEmpty() ? QStringLiteral("Not identified yet") : state;
    emit changed();
    return;
  }
  m_queue = m_pausedQueue;
  m_pausedQueue.clear();
  if (!m_queue.isEmpty())
    next();
  else
    m_settle.start();
  emit changed();
}

void GameMetadata::continueLibraryPass() {
  // Stopping by hand means stopped, until the next launch or an explicit update.
  if (m_stoppedByHand || m_editing || m_library == nullptr)
    return;
  // Reading keys from the keyring is work that finishes on its own, so look again shortly
  // rather than waiting to be told. Wanting credentials is different: that waits for an event.
  if (busy()) {
    m_settle.start();
    return;
  }
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

QList<QVariantMap> GameMetadata::orderForIdentification(const QList<QVariantMap>& games,
                                                        const QSet<QString>& onScreen) {
  QList<QVariantMap> visible;
  // Everything else is grouped by system and then taken a system at a time in turn. Walking the
  // library in order meant a small system sat behind every ROM of a large one, which is why
  // eight Nintendo 64 games could go unidentified while a 1,387 game shelf worked through.
  QList<QString> order;
  QHash<QString, QList<QVariantMap>> bySystem;
  for (const QVariantMap& game : games) {
    if (onScreen.contains(game.value("metadataKey").toString())) {
      visible.append(game);
      continue;
    }
    const QString system = game.value("system").toString();
    if (!bySystem.contains(system))
      order.append(system);
    bySystem[system].append(game);
  }
  QList<QVariantMap> rest;
  for (bool moved = true; moved;) {
    moved = false;
    for (const QString& system : order) {
      auto& remaining = bySystem[system];
      if (remaining.isEmpty())
        continue;
      rest.append(remaining.takeFirst());
      moved = true;
    }
  }
  return visible + rest;
}

void GameMetadata::promoteVisibleGames() {
  if (m_queue.isEmpty())
    return;
  QSet<QString> onScreen;
  if (m_visible != nullptr)
    for (int row = 0; row < m_visible->rowCount(); ++row)
      onScreen.insert(
          m_visible->data(m_visible->index(row, 0), GameRoles::MetadataKey).toString());
  const QList<QVariantMap> ordered =
      orderForIdentification(QList<QVariantMap>(m_queue.cbegin(), m_queue.cend()), onScreen);
  m_queue.clear();
  for (const QVariantMap& game : ordered)
    m_queue.enqueue(game);
  const QString selected = m_selected.value("metadataKey").toString();
  for (qsizetype i = 0; i < m_queue.size(); ++i) {
    if (m_queue.at(i).value("metadataKey").toString() == selected) {
      m_queue.move(i, 0);
      break;
    }
  }
}

bool GameMetadata::persist(const QString& id, const QVariantMap& value) {
  if (id.isEmpty())
    return false;
  QSqlQuery query(m_database);
  query.prepare("INSERT OR REPLACE INTO game_metadata(game_key,payload) VALUES(?,?)");
  query.addBindValue(id);
  query.addBindValue(
      QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
  if (!query.exec()) {
    m_pendingWrites.insert(id, value);
    m_queue.clear();
    finish("Could not save game metadata. Retry when storage is available.");
    return false;
  }
  m_pendingWrites.remove(id);
  const auto previous = m_entries.value(id);
  m_entries.insert(id, value);
  emit entryChanged(id, previous);
  emit changed();
  return true;
}
void GameMetadata::inspect(const QVariantMap& game) {
  m_selected = game;
  m_candidates.clear();
  m_covers.clear();
  // Someone looking at a game's details is waiting on that game, so it goes to the front rather
  // than taking its turn behind the rest of the library.
  const QString key = game.value("metadataKey").toString();
  if (!key.isEmpty()) {
    for (qsizetype at = 0; at < m_queue.size(); ++at) {
      if (m_queue.at(at).value("metadataKey").toString() != key)
        continue;
      if (at > 0)
        m_queue.move(at, 0);
      break;
    }
  }
  queueSelected();
  emit changed();
}

QVariantMap GameMetadata::current() const {
  auto value = entry(m_selected.value("metadataKey").toString());
  QString filename;
  if (!m_selected.value("system").toString().isEmpty()) {
    filename = m_selected.value("installPath").toString();
    if (filename.isEmpty())
      filename = m_selected.value("title").toString();
  }
  return RegionalMetadata::details(value, filename,
                                   platformIds(m_selected.value("system").toString()));
}

bool GameMetadata::selectedBusy() const {
  const QString selected = m_selected.value("metadataKey").toString();
  if (selected.isEmpty()) return false;
  if (m_busy && key() == selected) return true;
  for (const auto& game : m_queue)
    if (game.value("metadataKey").toString() == selected) return true;
  return false;
}

QString GameMetadata::selectedStatus() const {
  if (m_selected.isEmpty()) return {};
  if (m_pendingWrites.contains(m_selected.value("metadataKey").toString()))
    return QStringLiteral("Game metadata could not be saved. Retry when storage is available.");
  if (selectedBusy()) return QStringLiteral("Loading game details…");
  const QString selected = m_selected.value("metadataKey").toString();
  if (m_detailErrors.contains(selected)) return QStringLiteral("Couldn't refresh game details. Try again.");
  if (current().value("identityAmbiguous").toBool())
    return QStringLiteral("Multiple editions match. Identify this game to confirm its details.");
  if (current().value("v").toInt() >= kPayloadVersion) return {};
  if (!m_insights || !m_insights->configured()) return QStringLiteral("Connect IGDB in Settings to load game details.");
  if (current().value("igdbId").toLongLong() <= 0) return QStringLiteral("Identify this game to find its details.");
  return QStringLiteral("Game details are waiting to refresh.");
}

void GameMetadata::queueSelected(bool force) {
  const QString selected = m_selected.value("metadataKey").toString();
  if (selected.isEmpty() || m_editing || m_stoppedByHand || selectedBusy()) return;
  if (!m_insights || !m_insights->configured()) return;
  if (!force && m_detailAttempts.value(selected, 0) > QDateTime::currentSecsSinceEpoch() - 60) return;
  QVariantMap game = m_selected;
  if (force) game["refreshDetails"] = true;
  const auto before = m_queue.size();
  enqueue(game);
  if (m_queue.size() == before) return;
  m_detailAttempts[selected] = QDateTime::currentSecsSinceEpoch();
  m_detailErrors.remove(selected);
  m_queue.move(m_queue.size() - 1, 0);
  next();
}

void GameMetadata::refreshSelected() {
  const QString selected = m_selected.value("metadataKey").toString();
  if (m_pendingWrites.contains(selected)) {
    if (busy())
      return;
    const auto pending = m_pendingWrites.value(selected);
    if (persist(selected, pending))
      finish("Game metadata saved");
    return;
  }
  m_stoppedByHand = false;
  m_cancelled = false;
  queueSelected(true);
  emit changed();
}

QByteArray GameMetadata::matchingRulesFingerprint() {
  // Representative of every rule: dump tags, sorted articles, accents, brand prefixes,
  // catalogue numbers, editions that must survive, and the platforms each system searches.
  static const QStringList titles{
      QStringLiteral("Chrono Trigger (USA)"),
      QStringLiteral("Star Fox (EU, Rev 1)"),
      QStringLiteral("Legend of Zelda, The - A Link to the Past (NA)"),
      QStringLiteral("Slayers (English Translated by Dynamic Designs, Rev 1.01)"),
      QStringLiteral("Mega Man X3 (Prototype - NTSC Conversion)"),
      QStringLiteral("Pokémon Stadium 2"),
      QStringLiteral("Disney's DuckTales"),
      QStringLiteral("1636 - Pokemon Fire Red"),
      QStringLiteral("Sonic 3 (& Knuckles)"),
      QStringLiteral("Persona 3 Reload (Digital Deluxe Edition)")};
  QByteArray material;
  for (const QString& title : titles)
    material += normalizedTitle(title).toUtf8() + '\n';
  for (const QString& system :
       {QStringLiteral("snes"), QStringLiteral("nes"), QStringLiteral("n64"),
        QStringLiteral("dreamcast"), QStringLiteral("switch"), QString{}}) {
    material += system.toUtf8() + '=';
    for (int platform : platformIds(system))
      material += QByteArray::number(platform) + ',';
    material += '\n';
  }
  material += QByteArray::number(equivalentTitle("Clay Fighter 2", "ClayFighter 2"));
  material += QByteArray::number(equivalentTitle("Super Back to the Future, Part II", "Super Back to the Future II"));
  material += QByteArray::number(equivalentTitle("Game 1 2", "Game 12"));
  return QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex().left(16);
}

bool GameMetadata::needsIdentifying(const QVariantMap& saved, qint64 now) {
  // An answer produced by older rules is stale however recently it was written. Without this a
  // matching fix would reach existing libraries only as each entry aged out, which for a shelf
  // marked "Needs identification" means a month of looking broken.
  if (saved.value("matchVersion").toInt() < kMatchVersion)
    return true;
  return saved.value("updated").toLongLong() <= now - kRatingFreshnessSeconds;
}

bool GameMetadata::needsCoverAttempt(const QVariantMap& saved, qint64 now) {
  // Same reasoning as needsIdentifying: an answer the current rules would no longer give is
  // stale however recently it was written, so a rules change is not left waiting out the
  // backoff on every entry it would now decide differently.
  if (saved.value("coverRules").toInt() < kCoverRulesVersion)
    return true;
  return saved.value("coverAttempt").toLongLong() <= now - kCoverAttemptBackoffSeconds;
}

void GameMetadata::enqueue(const QVariantMap& game) {
  if (game.value("isPortal").toBool() || game.value("metadataKey").toString().isEmpty())
    return;
  if (m_pendingWrites.contains(game.value("metadataKey").toString()))
    return;
  const auto saved = entry(game.value("metadataKey").toString());
  if (saved.value("rejected").toBool())
    return;
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  // Entries saved before the richer IGDB payload carry no version; refresh them
  // once so release, credits, genres, and summary arrive without waiting for the
  // regular freshness cycle.
  const bool enrich =
      saved.value("igdbId").toLongLong() > 0 && saved.value("v").toInt() < kPayloadVersion;
  const bool ratings = m_insights && m_insights->configured() &&
                       (game.value("refreshDetails").toBool() || enrich || needsIdentifying(saved, now));
  const bool portrait = (hasGridKey() || (!saved.value("igdbCoverUrl").toString().isEmpty() &&
                                               !QFileInfo::exists(saved.value("fallbackCover").toString()))) &&
                        wantsPortraitCover(game.value("system").toString(),
                                           game.value("source").toString(),
                                           game.value("sourceCoverPath").toString()) &&
                        !QFileInfo::exists(saved.value("portrait").toString()) &&
                        (game.value("refreshDetails").toBool() || needsCoverAttempt(saved, now));
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
      if (!it.value().value("rejected").toBool() &&
          it.value().value("matchStatus").toString().startsWith("Needs identification"))
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
  if (key() == m_selected.value("metadataKey").toString() && m_insights && m_insights->configured())
    m_detailAttempts[key()] = QDateTime::currentSecsSinceEpoch();
  m_manual = false;
  m_numberedRetryTitle.clear();
  m_aliasRetried = false;
  m_discoveryRetried = false;
  m_brandRetryTitle.clear();
  m_artworkTitles.clear();
  m_artworkTitleIndex = 0;
  m_manualSearchTitle.clear();
  m_pendingGridId = 0;
  m_busy = true;
  m_igdbStage = "games";
  auto saved = entry(key());
  // The same rule that decided this game was worth queuing decides whether it is identified
  // again. Judging freshness by the timestamp alone here meant every game queued because the
  // rules had changed was dequeued, sent straight to artwork, and never re-identified, so its
  // recorded rule version never moved and the whole library stayed on old answers forever.
  // Games identified before the richer payload arrived also come back once so the new
  // fields land without waiting out the regular cycle.
  const bool enriched =
      saved.value("igdbId").toLongLong() <= 0 || saved.value("v").toInt() >= kPayloadVersion;
  if (!m_active.value("refreshDetails").toBool() &&
      !needsIdentifying(saved, QDateTime::currentSecsSinceEpoch()) && enriched) {
    gridSearch();
    return;
  }
  if (saved.value("igdbId").toLongLong() > 0 &&
      (saved.value("manualMatch").toBool() ||
       (!saved.value("identityAmbiguous").toBool() &&
        saved.value("matchVersion").toInt() >= kMatchVersion)) &&
      m_insights && m_insights->configured()) {
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
QString GameMetadata::searchTitle(const QString& title) const { return cleanTitle(title); }

void GameMetadata::search(const QString& title) {
  if (busy() || !m_insights || !m_insights->configured() || m_selected.isEmpty())
    return;
  m_cancelled = false;
  m_igdbStage = "games";
  m_active = m_selected;
  m_manual = true;
  m_numberedRetryTitle.clear();
  m_aliasRetried = false;
  m_discoveryRetried = false;
  m_brandRetryTitle.clear();
  m_artworkTitles.clear();
  m_artworkTitleIndex = 0;
  m_manualSearchTitle.clear();
  m_pendingGridId = 0;
  m_candidateProvider = "igdb";
  m_candidates.clear();
  m_covers.clear();
  const auto query = searchQuery(title, m_active.value("system").toString(), false);
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
      if (!persist(key(), value))
        return;
    }
    m_igdbStage.clear();
    gridSearch();
    return;
  }
  if (!error.isEmpty()) {
    m_detailErrors[key()] = error;
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
    m_detailErrors[key()] = "Invalid provider response";
    m_queue.clear();
    finish("IGDB returned invalid data. Cached metadata is unchanged.");
    return;
  }
  const auto matches = parseMatches(data, platformIds(m_active.value("system").toString()));
  if (m_manual) {
    m_candidates = matches;
    m_candidateProvider = "igdb";
    finish(matches.isEmpty() ? "No IGDB matches. Try another title."
                             : "Choose the matching game and edition");
    return;
  }
  const auto saved = entry(key());
  // A licensed game is often catalogued with its publisher in front, as Disney's DuckTales for
  // a cartridge labelled DuckTales. Comparing with that prefix removed as well recognises it.
  static const QRegularExpression brandPrefix(QStringLiteral("^[\\p{L}\\p{N} ]{2,24}? s "));
  const auto sameGame = [](const QString& candidate, const QString& local) {
    if (equivalentTitle(candidate, local))
      return true;
    QString withoutBrand = candidate;
    withoutBrand.remove(brandPrefix);
    return !withoutBrand.isEmpty() && equivalentTitle(withoutBrand, local);
  };
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
    const QString local = normalizedTitle(
        m_numberedRetryTitle.isEmpty() ? m_active.value("title").toString() : m_numberedRetryTitle);
    bool aliasMatches = false;
    for (const auto& alias : match.toMap().value("aliases").toStringList())
      aliasMatches = aliasMatches || equivalentTitle(alias, local);
    if (aliasMatches || m_igdbStage == "mappedGame" ||
        (!saved.value("identityAmbiguous").toBool() &&
         saved.value("matchVersion").toInt() >= kMatchVersion &&
         saved.value("igdbId").toLongLong() == match.toMap().value("id").toLongLong()) ||
        sameGame(normalizedTitle(match.toMap().value("title").toString()),
                 normalizedTitle(m_numberedRetryTitle.isEmpty() ? m_active.value("title").toString()
                                                                : m_numberedRetryTitle)))
      exact.append(match);
  }
  if (!userChose && exact.size() > 1) {
    const QString localTitle = m_active.value("title").toString();
    const bool originalNamed = std::any_of(exact.cbegin(), exact.cend(), [&](const QVariant& item) {
      const auto candidate = item.toMap();
      return candidate.value("gameType", -1).toInt() >= 0 &&
             candidate.value("gameType").toInt() != 5 &&
             equivalentTitle(candidate.value("title").toString(), localTitle);
    });
    if (originalNamed) {
      exact.erase(std::remove_if(exact.begin(), exact.end(), [&](const QVariant& item) {
        const auto candidate = item.toMap();
        return candidate.value("gameType", -1).toInt() == 5 &&
               !equivalentTitle(candidate.value("title").toString(), localTitle);
      }), exact.end());
    }
  }
  if (!userChose && exact.size() > 1 && ConsoleCatalog::idFor(m_active.value("system").toString()) == "snes") {
    const auto regions = RegionalMetadata::romTags(m_active.value("title").toString())
                             .value("regions").toStringList();
    const int preferredPlatform = regions.size() != 1 ? 0 :
        regions.first() == "Japan" ? 58 :
        (regions.first() == "North America" || regions.first() == "Europe") ? 19 : 0;
    const auto hasPlatform = [](const QVariant& item, int platform) {
      for (const auto& id : item.toMap().value("platformIds").toList())
        if (id.toInt() == platform) return true;
      return false;
    };
    // A dump region can distinguish separate SNES/Super Famicom records, but
    // does not distinguish two editions listed for the same machine.
    if (preferredPlatform && std::any_of(exact.cbegin(), exact.cend(), [&](const QVariant& item) {
          return hasPlatform(item, preferredPlatform);
        })) {
      exact.erase(std::remove_if(exact.begin(), exact.end(), [&](const QVariant& item) {
        return !hasPlatform(item, preferredPlatform) && hasPlatform(item, preferredPlatform == 19 ? 58 : 19);
      }), exact.end());
    }
  }
  const int resultLimit = m_igdbStage == "discovery" ? 100 : 20;
  const bool truncated = QJsonDocument::fromJson(data).array().size() >= resultLimit && !userChose;
  if (!userChose && !m_aliasRetried && (truncated || exact.size() > 1)) {
    // Broad search pages can be filled by sequels, hacks, or punctuation lookalikes.
    // Ask the catalogue for the exact title/aliases before declaring an edition conflict.
    const auto query = aliasSearchQuery(
        m_numberedRetryTitle.isEmpty() ? m_active.value("title").toString() : m_numberedRetryTitle,
        m_active.value("system").toString());
    if (!query.isEmpty()) {
      m_aliasRetried = true;
      requestIgdb(query, "games", "aliases");
      return;
    }
  }
  if (exact.size() == 1 && !truncated)
    acceptMatch(exact.first().toMap());
  else if (exact.size() > 1 || truncated) {
    // Popularity cannot distinguish regional releases, compilations, or editions.
    // Preserve the last payload and artwork until the user resolves the identity.
    auto value = saved;
    value["matchStatus"] = "Needs identification: multiple matching editions";
    value["identityAmbiguous"] = true;
    value["updated"] = QDateTime::currentSecsSinceEpoch();
    value["matchVersion"] = kMatchVersion;
    if (!persist(key(), value))
      return;
    m_candidates = exact;
    m_candidateProvider = "igdb";
    finish("Multiple editions match. Identify this game to choose the correct one.");
  } else {
    // Some ROM sets number their files, as "1636 - Pokemon Fire Red". Searching for the number
    // finds nothing. Trying again without it only after the title as written has failed means a
    // game that really begins with a number, 1080 Snowboarding or 1942, is never mangled.
    static const QRegularExpression catalogueNumber(QStringLiteral("^\\d{2,5}\\s*-\\s*(?=\\S)"));
    const QString written = m_active.value("title").toString();
    if (!m_manual && m_numberedRetryTitle.isEmpty() &&
        catalogueNumber.match(written).hasMatch()) {
      QString withoutNumber = written;
      withoutNumber.remove(catalogueNumber);
      const QByteArray retry = searchQuery(withoutNumber, m_active.value("system").toString());
      if (!retry.isEmpty()) {
        m_numberedRetryTitle = withoutNumber;
        requestIgdb(retry, "games", "games");
        return;
      }
    }
    if (!userChose && !m_aliasRetried && m_insights && m_insights->configured()) {
      const auto query =
          aliasSearchQuery(m_numberedRetryTitle.isEmpty() ? m_active.value("title").toString()
                                                          : m_numberedRetryTitle,
                           m_active.value("system").toString());
      if (!query.isEmpty()) {
        m_aliasRetried = true;
        requestIgdb(query, "games", "aliases");
        return;
      }
    }
    if (!userChose && !m_discoveryRetried && m_insights && m_insights->configured()) {
      const auto query = discoveryQuery(m_numberedRetryTitle.isEmpty()
                                          ? m_active.value("title").toString() : m_numberedRetryTitle,
                                      m_active.value("system").toString());
      if (!query.isEmpty()) {
        m_discoveryRetried = true;
        requestIgdb(query, "games", "discovery");
        return;
      }
    }
    auto value = saved;
    value["matchStatus"] = "Needs identification";
    value["updated"] = QDateTime::currentSecsSinceEpoch();
    value["matchVersion"] = kMatchVersion;
    if (!persist(key(), value))
      return;
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
  m_detailErrors.remove(key());
  auto value = entry(key());
  if (value.value("igdbId") != match.value("id")) {
    QVariantMap artwork;
    for (const auto& field : {"portrait", "gridCoverId", "portraitUpdated"})
      if (value.contains(field))
        artwork.insert(field, value.value(field));
    value = artwork;
  }
  value["igdbId"] = match.value("id");
  value["title"] = match.value("title");
  value["year"] = match.value("year");
  value["rating"] = match.value("rating");
  value["ratingCount"] = match.value("ratingCount");
  // A successful provider response replaces its own fields, including removals.
  // User identity/artwork choices elsewhere in the payload remain untouched.
  for (const char* field : {"releaseText", "summary", "genres", "developers", "publishers",
                            "platformIds", "igdbCoverUrl", "heroUrl", "heroKind", "heroWidth", "heroHeight", "aliases", "alternativeNames",
                            "localizations", "versionParent", "edition", "releaseDates"}) {
    value.remove(QLatin1String(field));
    if (match.contains(QLatin1String(field)))
      value[QLatin1String(field)] = match.value(QLatin1String(field));
  }
  value["platform"] = m_active.value("system");
  const QString platform = value.value("platform").toString();
  const QString platformText =
      platform.isEmpty()
          ? platformNames(value.value("platformIds").toList()).join(QStringLiteral(", "))
          : ConsoleCatalog::displayNameFor(platform);
  value.remove("platformText");
  if (!platformText.isEmpty())
    value["platformText"] = platformText;
  value.remove("identityAmbiguous");
  value["matchStatus"] = "Matched to IGDB";
  value["rejected"] = false;
  value["updated"] = QDateTime::currentSecsSinceEpoch();
  value["v"] = kPayloadVersion;
  value["ratingProvider"] = "igdb";
  value["ratingField"] = "total_rating";
  value["localTitle"] = m_active.value("title");
  if (!m_active.value("system").toString().isEmpty()) {
    const QString path = m_active.value("installPath").toString();
    if (!path.isEmpty())
      value["romFilename"] = QFileInfo(path).fileName();
  }
  value["manualMatch"] = m_manual || value.value("manualMatch").toBool();
  value["matchVersion"] = kMatchVersion;
  if (!persist(key(), value))
    return;
  m_candidates.clear();
  requestIgdb("fields game_id,value; where game_id = " +
                  QByteArray::number(value.value("igdbId").toLongLong()) +
                  " & popularity_type = 1; limit 1;",
              "popularity_primitives", "popularity");
}
void GameMetadata::rejectMatch() {
  if (busy() || m_selected.isEmpty())
    return;
  if (!persist(m_selected.value("metadataKey").toString(),
               {{"rejected", true}, {"matchStatus", "Automatic matching disabled"}}))
    return;
  m_candidates.clear();
  m_covers.clear();
  m_status = "Match removed. Search to identify this game again.";
  emit changed();
}
void GameMetadata::findCovers() { beginCoverSearch({}); }

void GameMetadata::searchCovers(const QString& title) { beginCoverSearch(title.trimmed()); }

void GameMetadata::beginCoverSearch(const QString& typedTitle) {
  if (busy() || m_selected.isEmpty())
    return;
  m_cancelled = false;
  m_active = m_selected;
  m_manual = true;
  m_numberedRetryTitle.clear();
  m_aliasRetried = false;
  m_discoveryRetried = false;
  m_brandRetryTitle.clear();
  m_artworkTitles.clear();
  m_artworkTitleIndex = 0;
  m_manualSearchTitle = typedTitle;
  m_pendingGridId = 0;
  m_busy = true;
  m_candidates.clear();
  m_covers.clear();
  gridSearch();
}

void GameMetadata::clearGridSelection() {
  if (busy() || m_selected.isEmpty())
    return;
  const QString id = m_selected.value("metadataKey").toString();
  if (id.isEmpty())
    return;
  auto value = entry(id);
  const bool had = value.contains("gridId") || value.contains("portrait");
  value.remove("gridId");
  value.remove("portrait");
  value.remove("gridCoverId");
  // Look again on the next pass rather than waiting out the day-long backoff, which is what
  // someone clearing a wrong cover is asking for.
  value.remove("coverAttempt");
  value.remove("coverRules");
  if (!persist(id, value))
    return;
  m_pendingGridId = 0;
  m_candidates.clear();
  m_covers.clear();
  finish(had ? "Cover cleared. This game will be looked up again."
             : "This game has no downloaded cover to clear.");
}

QStringList GameMetadata::artworkSearchTitles(const QVariantMap& entry) {
  QStringList result;
  QSet<QString> seen;
  auto append = [&](const QString& title) {
    const QString normalized = normalizedTitle(title);
    if (!normalized.isEmpty() && !seen.contains(normalized)) {
      seen.insert(normalized);
      result.append(title);
    }
  };
  append(entry.value("title").toString());
  for (const auto& item : entry.value("alternativeNames").toList()) {
    const auto alias = item.toMap();
    const QString comment = alias.value("comment").toString().toLower();
    if (comment.contains("abbreviat") || comment.contains("acronym")) continue;
    append(alias.value("name").toString());
    if (result.size() >= 8) break;
  }
  const auto originals = result;
  for (const auto& title : originals) append(withoutBrandPrefix(normalizedTitle(title)));
  return result;
}

bool GameMetadata::canSharePortrait(const QVariantMap& target, const QVariantMap& donor) {
  return target.value("igdbId").toLongLong() > 0 &&
      target.value("igdbId") == donor.value("igdbId") &&
      !target.value("identityAmbiguous").toBool() && !donor.value("identityAmbiguous").toBool() &&
      !target.value("rejected").toBool() && !donor.value("rejected").toBool() &&
      !target.value("platform").toString().isEmpty() &&
      target.value("platform") == donor.value("platform") &&
      target.value("edition") == donor.value("edition") &&
      donor.value("gridId").toLongLong() > 0 && !donor.value("portrait").toString().isEmpty();
}

void GameMetadata::gridSearch() {
  const auto fallback = entry(key());
  const QString sourcePath = m_active.value("sourceCoverPath").toString();
  const QString localSource = sourcePath.startsWith("file://") ? QUrl(sourcePath).toLocalFile() : sourcePath;
  const QUrl fallbackUrl(fallback.value("igdbCoverUrl").toString());
  static const QRegularExpression fallbackPath(
      QStringLiteral(R"(^/igdb/image/upload/t_cover_big_2x/[A-Za-z0-9_]+\.jpg$)"));
  if (!m_manual && fallback.value("igdbId").toLongLong() > 0 &&
      !fallback.value("identityAmbiguous").toBool() && !fallback.value("rejected").toBool() &&
      !QFileInfo::exists(localSource) &&
      !QFileInfo::exists(fallback.value("portrait").toString()) &&
      !QFileInfo::exists(fallback.value("fallbackCover").toString()) &&
      fallback.value("igdbCoverAttempt").toLongLong() < QDateTime::currentSecsSinceEpoch() - 30 &&
      fallbackUrl.scheme() == "https" && fallbackUrl.host() == "images.igdb.com" &&
      fallbackUrl.userInfo().isEmpty() && fallbackUrl.port(-1) == -1 &&
      fallbackUrl.query().isEmpty() && fallbackPath.match(fallbackUrl.path()).hasMatch()) {
    auto value = fallback;
    value["igdbCoverAttempt"] = QDateTime::currentSecsSinceEpoch();
    if (!persist(key(), value)) return;
    get(fallbackUrl, "igdb-cover");
    return;
  }
  if (!m_manual && entry(key()).value("identityAmbiguous").toBool()) {
    finish("Identify this game before downloading new artwork.");
    return;
  }
  static const QRegularExpression unlicensedTag(
      QStringLiteral(R"(\([^)]*\b(?:TW|Taiwan|Pirate|Unlicensed|Unl)\b[^)]*\))"),
      QRegularExpression::CaseInsensitiveOption);
  if (!m_manual && !QFileInfo::exists(fallback.value("portrait").toString()) &&
      unlicensedTag.match(m_active.value("title").toString()).hasMatch()) {
    // Bootlegs can reuse an official game's exact name. SteamGridDB's search
    // response has no platform evidence to distinguish them. IGDB's identified
    // cover above is safe; a title-only grid selection needs confirmation.
    finish("Choose SteamGridDB artwork for this unlicensed ROM.");
    return;
  }
  if (!hasGridKey()) {
    finish("Game identified. Connect SteamGridDB to find covers.");
    return;
  }
  if (!m_manual && !wantsPortraitCover(m_active.value("system").toString(),
                                       m_active.value("source").toString(),
                                       m_active.value("sourceCoverPath").toString())) {
    // Source artwork can avoid a new download, but must not replace an existing portrait.
    finish("IGDB data saved. Existing artwork kept.");
    return;
  }
  auto value = entry(key());
  if (!m_manual && QFileInfo::exists(value.value("portrait").toString())) {
    finish("Cached portrait kept");
    return;
  }
  // Share only an existing provider download for the same identified platform/edition.
  // Custom artwork remains a separate per-installation override.
  if (!value.contains("gridId")) {
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
      if (!canSharePortrait(value, it.value()) ||
          !QFileInfo::exists(it.value().value("portrait").toString())) continue;
      value["gridId"] = it.value().value("gridId");
      value["coverRules"] = kCoverRulesVersion;
      if (!m_manual) {
        for (const auto& field : {"portrait", "gridCoverId", "portraitUpdated"})
          value[field] = it.value().value(field);
        value["coverRules"] = kCoverRulesVersion;
        if (persist(key(), value)) finish("Cover found from another installation of this game");
        return;
      }
      break;
    }
  }
  // A stored grid game with no portrait to show for it was never confirmed by anyone: either an
  // older rule settled on it, or someone opened a candidate to look at it. Trusting one of those
  // is how a game ends up wearing another game's box art, so on a rules change it is dropped and
  // looked up again. A selection that did produce a portrait is left alone.
  if (value.value("coverRules").toInt() < kCoverRulesVersion && !value.contains("portrait")) {
    value.remove("gridId");
  }
  value["coverAttempt"] = QDateTime::currentSecsSinceEpoch();
  value["coverRules"] = kCoverRulesVersion;
  if (!persist(key(), value))
    return;
  if (m_manualSearchTitle.isEmpty() && value.value("gridId").toLongLong() > 0) {
    gridCovers(value.value("gridId").toLongLong());
    return;
  }
  if (m_artworkTitles.isEmpty()) {
    m_artworkTitles = m_manualSearchTitle.isEmpty() ? artworkSearchTitles(value)
                                                  : QStringList{m_manualSearchTitle};
    if (m_artworkTitles.isEmpty()) m_artworkTitles.append(m_active.value("title").toString());
  }
  const QString title = m_artworkTitles.value(m_artworkTitleIndex);
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
  // Held rather than stored. Opening a candidate to see what artwork it has used to pin the game
  // to it permanently, because the background pass short circuits on a stored id and would then
  // fetch that game's covers on its own. It is written once a cover is actually taken.
  m_pendingGridId = m_candidates.at(index).toMap().value("id").toLongLong();
  m_candidates.clear();
  m_busy = true;
  gridCovers(m_pendingGridId);
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
  const bool imageRequest = stage == "image" || stage == "igdb-cover";
  // Hold SteamGridDB's API calls apart. Image downloads come from a CDN and are slow enough on
  // their own. Without this the queue would burst three calls per game back to back.
  if (!imageRequest && m_sinceGridRequest.isValid()) {
    const qint64 waited = m_sinceGridRequest.elapsed();
    if (waited < kGridRequestGapMs) {
      QTimer::singleShot(kGridRequestGapMs - waited, this,
                         [this, url, stage] { get(url, stage); });
      return;
    }
  }
  if (!imageRequest)
    m_sinceGridRequest.restart();
  QNetworkRequest request(url);
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  if (!imageRequest)
    request.setRawHeader("Authorization", "Bearer " + m_gridKey);
  auto* reply = m_network->get(request);
  auto buffer = std::make_shared<QByteArray>();
  const qsizetype limit = imageRequest ? 12 * 1024 * 1024 : 1024 * 1024;
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
    if (!ok && stage == "igdb-cover") {
      if (m_cancelled) finish("Metadata update stopped");
      else gridSearch();
      return;
    }
    if (!ok) {
      // A refusal is not an answer about this game. The attempt was recorded before the request
      // went out, so leaving it there would hold the game back for a day over a rate limit or a
      // dropped connection, and a library large enough to be rate limited is exactly the one
      // that loses the most covers to it. Take the record back so the next pass tries again.
      if (stage != "test") {
        auto value = entry(key());
        if (value.remove("coverAttempt") > 0) {
          value.remove("coverRules");
          if (!persist(key(), value))
            return;
        }
      }
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
  if (stage == "igdb-cover") {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (size.width() < 32 || size.height() < 32 || size.width() > 4096 || size.height() > 4096) {
      gridSearch();
      return;
    }
    const QImage image = reader.read();
    if (!image.isNull()) {
      QDir().mkpath(m_cacheRoot);
      const QString path = m_cacheRoot + '/' + QString::fromLatin1(
          QCryptographicHash::hash(key().toUtf8(), QCryptographicHash::Sha256).toHex()) +
          "-igdb-" + QString::number(entry(key()).value("igdbId").toLongLong()) + ".jpg";
      QSaveFile file(path);
      if (file.open(QIODevice::WriteOnly) && image.convertToFormat(QImage::Format_RGB32).save(&file, "JPG", 92) && file.commit()) {
        auto value = entry(key());
        value["fallbackCover"] = path;
        if (!persist(key(), value)) return;
      }
    }
    gridSearch();
    return;
  }
  if (stage == "image") {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (size != QSize(600, 900)) {
      finish("Downloaded cover has unexpected dimensions");
      return;
    }
    const QImage image = reader.read();
    if (image.isNull()) {
      finish("Could not read downloaded cover");
      return;
    }
    QDir().mkpath(m_cacheRoot);
    // Covers are photographs and illustrations, and lossless storage was costing about 750 KB
    // each. A library of a thousand games would have filled the artwork cache on its own and
    // then spent the rest of its life evicting and downloading the same covers. JPEG at this
    // quality is indistinguishable on a card and roughly a fifth of the size.
    const QString path =
        m_cacheRoot + '/' +
        QString::fromLatin1(
            QCryptographicHash::hash(key().toUtf8(), QCryptographicHash::Sha256).toHex()) +
        '-' + QString::number(m_downloadId) + ".jpg";
    QSaveFile file(path);
    const QImage opaque = image.convertToFormat(QImage::Format_RGB32);
    if (!file.open(QIODevice::WriteOnly) || !opaque.save(&file, "JPG", 92) || !file.commit()) {
      finish("Could not save downloaded cover");
      return;
    }
    QString selectedCoverPath;
    if (m_manual && m_library) {
      for (int row = 0; row < m_library->rowCount(); ++row)
        if (m_library->data(m_library->index(row), GameRoles::MetadataKey).toString() == key()) {
          if (!m_library->setCustomCover(row, QUrl::fromLocalFile(path))) {
            finish("Could not apply the selected cover");
            return;
          }
          selectedCoverPath = m_library->data(m_library->index(row), GameRoles::CoverPath).toString();
          break;
        }
    }
    auto value = entry(key());
    value["selectedCoverPath"] = selectedCoverPath;
    value["portrait"] = path;
    value["gridCoverId"] = m_downloadId;
    value["portraitUpdated"] = QDateTime::currentSecsSinceEpoch();
    // Taking a cover from a grid game chosen by hand is what confirms that choice, so it is
    // written here rather than on the click that opened it.
    if (m_pendingGridId > 0) {
      value["gridId"] = m_pendingGridId;
      value["coverRules"] = kCoverRulesVersion;
      m_pendingGridId = 0;
    }
    if (!persist(key(), value))
      return;
    trimPortraitCache();
    const bool selectedManually = m_manual;
    const QString selectedKey = key();
    if (selectedManually)
      m_covers.clear();
    finish("Cover saved from SteamGridDB");
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
    QVariantList matches;
    const auto saved = entry(key());
    const QString title = saved.value("title", m_active.value("title")).toString();
    for (const auto& item : object.value("data").toArray()) {
      const auto game = item.toObject();
      if (game.value("id").toInteger() <= 0 || game.value("name").toString().isEmpty())
        continue;
      const qint64 released = game.value("release_date").toInteger();
      matches.append(QVariantMap{
          {"id", game.value("id").toInteger()},
          {"title", game.value("name").toString()},
          {"year", released > 0
                       ? QDateTime::fromSecsSinceEpoch(released, QTimeZone::UTC).date().year()
                       : 0}});
    }
    const qint64 chosen = saved.value("igdbId").toLongLong() <= 0 || !m_manualSearchTitle.isEmpty()
                              ? 0 : chooseGridMatch(matches, m_artworkTitles.value(m_artworkTitleIndex, title),
                                                    saved.value("year").toInt(), m_active.value("system").toString());
    if (chosen == 0 && m_manualSearchTitle.isEmpty() && m_artworkTitleIndex + 1 < m_artworkTitles.size()) {
      ++m_artworkTitleIndex;
      gridSearch();
      return;
    }
    if (chosen > 0) {
      if (m_manual) {
        m_pendingGridId = chosen;
      } else {
        auto value = saved;
        value["gridId"] = chosen;
        if (!persist(key(), value)) return;
      }
      gridCovers(chosen);
    } else if (m_manual) {
      m_candidates = matches;
      m_candidateProvider = "grid";
      finish("Choose the matching SteamGridDB game");
    } else
      finish("No confident cover match. Open Game & Artwork to choose a matching game.");
  } else {
    m_covers = parseCovers(data);
    if (!m_manual && !m_covers.isEmpty()) {
      const auto cover = m_covers.first().toMap();
      m_downloadId = cover.value("id").toLongLong();
      get(QUrl(cover.value("url").toString()), "image");
    } else
      finish(m_covers.isEmpty() ? "No suitable covers found" : "Choose a cover");
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
      // Ratings do not need this key, so a failure here must not end the pass.
      if (!m_stoppedByHand && !m_editing) {
        queueSelected();
        m_settle.start();
      }
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
    // The key arriving can be the thing that makes work possible, so look again.
    if (!m_stoppedByHand && !m_editing && !busy())
      m_settle.start();
  });
  m_secrets.setFuture(QtConcurrent::run([action, value]() mutable {
    QMutexLocker keyring(&secretServiceLock());
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
  if (m_active.value("refreshDetails").toBool())
    m_queryCache.remove(m_queryKey);
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
  if (!m_pendingWrites.isEmpty()) {
    finish("Retry unsaved game metadata before clearing portraits.");
    return;
  }
  if (busy())
    return;
  const QDir cache(m_cacheRoot);
  // Portraits have been saved as JPEG since they stopped being stored losslessly, so clearing
  // only PNGs emptied the database of them and left every file on disk. trimPortraitCache has
  // always looked for both; this has to agree with it.
  for (const auto& file : cache.entryList({"*.jpg", "*.png"}, QDir::Files))
    QFile::remove(cache.filePath(file));
  const auto keys = m_entries.keys();
  for (const auto& id : keys) {
    auto value = entry(id);
    const bool removedPortrait = value.remove("portrait") > 0;
    const bool removedFallback = value.remove("fallbackCover") > 0;
    if (removedPortrait || removedFallback) {
      value.remove("igdbCoverAttempt");
      value.remove("coverAttempt");
      // Without this the games just cleared would wait out the backoff before anything could
      // be downloaded again, so clearing appeared to do nothing for a day.
      value.remove("coverRules");
      if (!persist(id, value))
        return;
    }
  }
  finish("Downloaded portraits cleared. Your chosen covers are kept.");
}

void GameMetadata::setCacheLimitMb(int megabytes) {
  m_cacheLimitBytes = qBound(1, megabytes, 4096) * 1024LL * 1024;
  trimPortraitCache();
}
void GameMetadata::trimPortraitCache() {
  QSet<QString> referenced;
  for (const auto& value : std::as_const(m_entries)) {
    referenced.insert(value.value("portrait").toString());
    referenced.insert(value.value("fallbackCover").toString());
  }
  for (const auto& value : std::as_const(m_pendingWrites)) {
    referenced.insert(value.value("portrait").toString());
    referenced.insert(value.value("fallbackCover").toString());
  }
  CoverCachePolicy::prune(m_cacheRoot, m_cacheRoot, m_cacheLimitBytes, referenced);
}

void GameMetadata::reloadReviewEntry(const QString& key) {
  QSqlQuery query(m_database);
  query.prepare("SELECT payload FROM game_metadata WHERE game_key=?");
  query.addBindValue(key);
  if (!query.exec()) return;
  const auto previous = m_entries.value(key);
  m_entries[key] = query.next() ? QJsonDocument::fromJson(query.value(0).toByteArray()).toVariant().toMap() : QVariantMap{};
  emit entryChanged(key, previous);
  emit changed();
}

void GameMetadata::retryReviewGames(const QVariantList& games) {
  if (!reviewWritable() || m_editing) return;
  if ((!m_insights || !m_insights->configured()) && !hasGridKey()) {
    finish("Connect IGDB or SteamGridDB in settings first");
    return;
  }
  m_cancelled = false;
  // This explicit selection must not resume the automatic pass over other games.
  m_stoppedByHand = true;
  m_settle.stop();
  for (const auto& item : games.mid(0, 100)) {
    auto game = item.toMap();
    game.insert("refreshDetails", true);
    enqueue(game);
  }
  next();
  emit changed();
}
