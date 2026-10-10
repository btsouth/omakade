#pragma once

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

class AppSettings;
class UnifiedGameModel;

// Publishes Omakade and its library into Sunshine's app list so Moonlight clients can start
// them. Sunshine reads apps.json at startup and when its own web UI saves, so every change
// here needs a Sunshine restart before it shows up; `restartNeeded` tracks that.
//
// Omakade only touches entries it wrote. Each carries an "omakade" marker with the launch
// key, and anything without the marker is preserved exactly as Sunshine wrote it.
class SunshineIntegration final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool detected READ detected NOTIFY stateChanged)
  Q_PROPERTY(bool flatpak READ flatpak NOTIFY stateChanged)
  Q_PROPERTY(bool streaming READ streaming CONSTANT)
  Q_PROPERTY(QString appsPath READ appsPath NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(int exportedGames READ exportedGames NOTIFY stateChanged)
  Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)

public:
  // One game as the export worker sees it, copied off the model on the GUI thread.
  struct GameEntry {
    QString title;
    QString launchKey;
    QString coverSource;
  };
  struct SyncResult {
    bool okay = false;
    bool wrote = false;
    int games = 0;
    int entries = 0;
    QString status;
  };

  // `appsPath` and `imageRoot` override detection; tests point them at temporary files.
  explicit SunshineIntegration(UnifiedGameModel* games, AppSettings* settings,
                               const QString& appsPath = {}, const QString& imageRoot = {},
                               QObject* parent = nullptr);
  ~SunshineIntegration() override;

  [[nodiscard]] bool detected() const { return !m_appsPath.isEmpty(); }
  [[nodiscard]] bool flatpak() const { return m_flatpak; }
  [[nodiscard]] static bool streaming();
  [[nodiscard]] QString appsPath() const { return m_appsPath; }
  [[nodiscard]] QString statusText() const { return m_statusText; }
  [[nodiscard]] int exportedGames() const { return m_exportedGames; }
  [[nodiscard]] bool restartNeeded() const { return m_restartNeeded; }
  [[nodiscard]] bool busy() const { return m_busy; }
  void setIconSource(const QString& path) { m_iconSource = path; }

  // Starts rewriting the Omakade entries to match the preferences and library. The work
  // runs on a worker thread; `busy` clears and `stateChanged` fires when it is done.
  Q_INVOKABLE bool sync();
  Q_INVOKABLE void restartSunshine();

  [[nodiscard]] static QString serviceUnit();
  // Sunshine and Qt enumerate Wayland outputs in the same connector order. Honor an
  // explicit Sunshine output, or use its default first output when none is configured.
  [[nodiscard]] static QString configuredOutputName(const QString& configPath = {});
  [[nodiscard]] static int outputScreenIndex(const QString& configuredOutput,
                                             const QStringList& screenNames);
  [[nodiscard]] static QString commandArgument(const QString& value);
  [[nodiscard]] static QString commandPrefix(bool flatpakSunshine);
  [[nodiscard]] static bool isOmakadeEntry(const QJsonObject& entry);
  [[nodiscard]] static QJsonObject omakadeEntry(const QString& prefix, const QString& imagePath);
  [[nodiscard]] static QJsonObject gameEntry(const QString& title, const QString& launchKey,
                                             const QString& prefix, const QString& imagePath);
  // Keeps every foreign entry in order and replaces Omakade's with `ours`.
  [[nodiscard]] static QJsonObject mergeEntries(const QJsonObject& existing,
                                                const QJsonArray& ours);
  // Renders a cover or the app icon as 600x800 PNG box art under `imageRoot`. Returns the
  // target path, or an empty string when the source cannot be read.
  [[nodiscard]] static QString exportImage(const QString& imageRoot, const QString& sourcePath,
                                           const QString& name);

signals:
  void stateChanged();

private:
  void detect();
  void scheduleSync();
  void finishSync();
  // Asks systemd when Sunshine started and compares that with the last list Omakade wrote,
  // so the restart hint survives Omakade restarts without nagging after Sunshine reloaded.
  void refreshRestartState();
  void setStatus(const QString& text);
  [[nodiscard]] static SyncResult runSync(const QString& appsPath, const QString& imageRoot,
                                          const QString& prefix, bool includeOmakade,
                                          const QString& iconSource,
                                          const QVector<GameEntry>& games);

  UnifiedGameModel* m_games = nullptr;
  AppSettings* m_settings = nullptr;
  QString m_appsPath;
  QString m_imageRoot;
  QString m_iconSource;
  QString m_statusText;
  QTimer m_syncTimer;
  QFutureWatcher<SyncResult> m_syncWatcher;
  bool m_flatpak = false;
  bool m_restartNeeded = false;
  bool m_busy = false;
  bool m_syncPending = false;
  int m_exportedGames = 0;
};
