#pragma once

#include "launch/LaunchCommand.h"

#include <QList>
#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>
#include <QSqlDatabase>
#include <QVariantMap>

class SaveBackups;

class GameLauncher final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)

public:
  explicit GameLauncher(QObject* parent = nullptr);
  ~GameLauncher() override;
  void setRommLibraryRoot(const QString& root) { m_rommRoot = root; }
  void setSetupDatabase(const QString& path);
  Q_INVOKABLE QVariantMap inspect(const QVariantMap& installation) const;
  Q_INVOKABLE QStringList setupOptions(const QVariantMap& installation) const;
  Q_INVOKABLE bool saveSetup(const QVariantMap& installation, const QString& mode,
                             const QString& core, bool flatpak, const QString& path);
  Q_INVOKABLE bool resetSetup(const QVariantMap& installation);
  Q_INVOKABLE void copyLaunchDetails(const QVariantMap& installation) const;
  QHash<QString,QVariantMap> setupOverrides() const { return m_setups; }
  static bool contentAvailable(const QString& path, bool allowArchiveEntry = true);
  static QString setupKey(const QVariantMap& installation);

  [[nodiscard]] QString lastError() const;
  // True while a game process started by launch() is still alive. Games are started detached,
  // so this is polled from /proc rather than reported by QProcess.
  [[nodiscard]] bool gameRunning() const;
  // Starts a detached process and follows it through gameRunning() until it exits.
  bool startTracked(const LaunchCommand& command, const QString& workingDirectory = {});
  [[nodiscard]] static LaunchCommand lutrisCommand(const QString& id, bool flatpak);
  [[nodiscard]] static LaunchCommand heroicCommand(const QString& id, const QString& runner,
                                                   bool flatpak);
  [[nodiscard]] static LaunchCommand faugusCommand(const QString& id, bool flatpak);
  [[nodiscard]] static LaunchCommand retroArchCommand(const QString& contentPath,
                                                      const QString& corePath, bool flatpak);
  [[nodiscard]] static LaunchCommand resolvedCartridgeCommand(const QString& contentPath,
                                                              const QString& corePath, bool flatpak,
                                                              bool preferStandalone,
                                                              const QString& standaloneExecutable = {},
                                                              const QString& mappedCorePath = {},
                                                              bool retroArchAvailable = true);
  // A declared library system wins; a shared extension alone is not a console identity.
  [[nodiscard]] static QString cartridgeSystem(const QString& contentPath, const QString& system = {});
  // Resolve without starting a process, for launch and read-only diagnostics.
  [[nodiscard]] LaunchCommand plannedCartridgeCommand(const QString& contentPath,
      const QString& corePath, bool flatpak, const QString& system, QString* error) const;
  [[nodiscard]] static LaunchCommand pcsx2Command(const QString& id, bool isElf, bool flatpak);
  [[nodiscard]] static LaunchCommand ryujinxCommand(const QString& id,
                                                    const QString& nativeExecutable,
                                                    const QString& flatpakAppId =
                                                        QStringLiteral("io.github.ryubing.Ryujinx"));
  [[nodiscard]] static LaunchCommand shadps4Command(const QString& path,
                                                    const QString& nativeExecutable,
                                                    const QString& flatpakAppId = {});
  [[nodiscard]] static LaunchCommand cemuCommand(const QString& path, bool flatpak);
  [[nodiscard]] static LaunchCommand dolphinCommand(const QString& path, const QString& nativeExecutable,
                                                    bool flatpak);
  [[nodiscard]] static LaunchCommand battleNetCommand(const QString& id, const QString& prefix,
                                                      const QString& runner, bool flatpak);
  [[nodiscard]] static LaunchCommand gogCommand(const QString& id, const QString& installPath,
                                                const QString& winePrefix = {});
  Q_INVOKABLE bool launch(const QString& source, const QString& id, bool flatpak = false,
                          const QString& runner = {}, const QString& installPath = {},
                          const QString& launchTarget = {}, const QString& system = {});
  Q_INVOKABLE bool manage(const QString& source, const QString& id, bool flatpak = false,
                          const QString& runner = {}, const QString& launchTarget = {});
  Q_INVOKABLE bool install(const QString& source, const QString& id);
  void setPreferStandaloneEmulators(bool value);
  void setSaveBackups(SaveBackups* backups) { m_saveBackups = backups; }

signals:
  void lastErrorChanged();
  void setupChanged();
  void gameRunningChanged();

private:
  struct EmulatorPlan { LaunchCommand command; QString source, path, core, error; bool flatpak=false; };
  EmulatorPlan plannedEmulator(const QVariantMap& installation) const;
  bool launchPlannedEmulator(const QVariantMap& installation);
  QString storedSetupKey(const QVariantMap& installation) const;
  QString m_rommRoot;
  QSqlDatabase m_setupDatabase;
  QString m_setupConnection;
  QHash<QString,QVariantMap> m_setups;
  bool launchLutris(const QString& id, bool flatpak, bool manageOnly);
  bool launchHeroic(const QString& id, const QString& runner, bool flatpak, bool manageOnly);
  bool launchFaugus(const QString& id, bool flatpak, bool manageOnly);
  bool launchRetroArch(const QString& contentPath, const QString& corePath, bool flatpak,
                       bool manageOnly, const QString& system = {});
  bool launchPcsx2(const QString& id, bool isElf, bool flatpak, bool manageOnly);
  bool launchRyujinx(const QString& id, bool flatpak, const QString& flatpakAppId,
                     bool manageOnly);
  bool launchShadps4(const QString& path, bool flatpak, const QString& flatpakAppId,
                     bool manageOnly);
  bool launchCemu(const QString& path, bool flatpak, bool manageOnly);
  bool launchDolphin(const QString& path, bool flatpak, bool manageOnly);
  bool launchBattleNet(const QString& id, const QString& prefix, const QString& runner,
                       bool flatpak, bool manageOnly);
  bool launchGog(const QString& id, const QString& installPath, bool manageOnly);
  [[nodiscard]] QString flatpakError(const QString& appId, const QString& launcherName) const;
  void setError(const QString& error);
  bool startCommand(const LaunchCommand& command, bool track, const QString& workingDirectory = {},
                    const QProcessEnvironment& environment = QProcessEnvironment());
  void trackProcess(qint64 pid);
  void pollTrackedProcesses();
  struct TrackedProcess {
    qint64 pid = 0;
    qint64 startTime = -1;
  };
  SaveBackups* m_saveBackups = nullptr;
  QString m_lastError;
  bool m_preferStandaloneEmulators = false;
  QList<TrackedProcess> m_trackedProcesses;
  QTimer m_trackTimer;
};
