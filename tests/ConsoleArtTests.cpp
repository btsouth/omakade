#include "library/ConsoleCatalog.h"
#include "library/ConsolePortalModel.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSet>
#include <QtTest>

namespace {
// Catalog consoles with no master artwork yet. Their cards show the generated gradient.
const QSet<QString> kNoArtYet = {QStringLiteral("ps5")};
// The whole bundle, so art for unsupported consoles or full-size masters cannot creep back in.
constexpr qint64 kBudgetBytes = 12 * 1024 * 1024;
const QSize kCardSize(768, 1152);
} // namespace

class ConsoleArtTests final : public QObject {
  Q_OBJECT

private slots:
  void everyCatalogConsoleHasDecodableArt() {
    for (const ConsoleDefinition& console : ConsoleCatalog::all()) {
      const QString url = ConsolePortalModel::coverUrl(console.id);
      if (kNoArtYet.contains(console.id)) {
        QVERIFY2(url.isEmpty(),
                 qPrintable(console.id + QStringLiteral(" has art now; drop it from kNoArtYet")));
        continue;
      }
      QCOMPARE(url, QStringLiteral("qrc:/consoles/%1.webp").arg(console.id));
      QImageReader reader(url.mid(3));
      QCOMPARE(reader.format(), QByteArrayLiteral("webp"));
      QCOMPARE(reader.size(), kCardSize);
      const QImage image = reader.read();
      QVERIFY2(!image.isNull(),
               qPrintable(console.id + QStringLiteral(": ") + reader.errorString()));
    }
  }

  void onlyCatalogConsolesAreBundledWithinBudget() {
    QSet<QString> catalog;
    for (const ConsoleDefinition& console : ConsoleCatalog::all())
      catalog.insert(console.id);

    qint64 total = 0;
    qsizetype files = 0;
    QDirIterator it(QStringLiteral(":/consoles"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QFileInfo file(it.next());
      QCOMPARE(file.suffix(), QStringLiteral("webp"));
      QVERIFY2(catalog.contains(file.completeBaseName()), qPrintable(file.fileName()));
      total += file.size();
      ++files;
    }
    QCOMPARE(files, catalog.size() - kNoArtYet.size());
    QVERIFY2(total <= kBudgetBytes, qPrintable(QStringLiteral("%1 bytes").arg(total)));
  }
};

QTEST_GUILESS_MAIN(ConsoleArtTests)
#include "ConsoleArtTests.moc"
