#pragma once

#include "ask/AskAgent.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QVariantMap>

#include <functional>

// Runs the guide's questions on omakade-sessiond's guide thread. The guide saves a picture
// of the paused game and asks; this runs the user's default agent on it, one question at a
// time, and keeps the answer for the game it is about until another game is in view.
class AskService final : public QObject {
  Q_OBJECT

public:
  // Everything outside this class it touches, so tests can stand in for each.
  struct Environment {
    // Where Omarchy keeps the default agent: <home>/.config/omarchy/defaults/agent.
    QString home;
    // Omakade's config.toml, for ask = true.
    QString configPath;
    // A private runtime folder: the guide's pictures in shots/, the agent's empty working
    // folder in work/.
    QString folder;
    // The agent runs with this environment and is found on its PATH.
    QProcessEnvironment process;
    std::function<qint64()> now = [] { return QDateTime::currentSecsSinceEpoch(); };
    // An answer that takes longer than this is given up on.
    int timeoutMs = 60000;
    // An agent that has answered gets this long to exit on its own.
    int exitGraceMs = 5000;
    // Whether the game Ask follows still runs, once a snapshot stops naming it.
    std::function<bool(qint64 pid, qint64 procStart)> alive;
  };

  struct Game {
    qint64 pid = 0;
    qint64 procStart = -1;
    QString title;
    QString platform;
    [[nodiscard]] bool sameProcess(const Game& other) const {
      return pid == other.pid && procStart == other.procStart;
    }
  };

  explicit AskService(Environment environment, QObject* parent = nullptr);
  ~AskService() override;

  [[nodiscard]] static Environment defaultEnvironment();
  // The game a guide snapshot describes, or one with pid 0 when it names none.
  [[nodiscard]] static Game gameFrom(const QVariantMap& session, const QVariantMap& metadata);

  // Where the guide saves the picture it asks about. Only files directly inside are taken.
  [[nodiscard]] QString shotFolder() const;
  // A new snapshot. Another game, or the end of this one, drops the last answer and its
  // picture. Re-reads the setting and the default agent, which the user can change at any
  // time.
  void follow(const Game& game);
  // mode "next" asks about image, a picture in shotFolder(); "lookup" follows up the last
  // answer with a web search, on the same picture. False when it can't ask now; a picture
  // that is not taken is deleted.
  bool ask(const QString& mode, const QString& image = {});
  // What the guide card shows, or empty without Ask: {agent, state, mode, text, started, at,
  // lookup, shots}. state is "ready", "thinking", "answer" or "failed".
  [[nodiscard]] QJsonObject guideStatus() const;

signals:
  void statusChanged();

private:
  void reload();
  void reset();
  void finish(const AskAgent::Reply& reply, const QString& fallbackError);
  // Lets go of the agent: now stops it at once, otherwise it gets a moment to exit.
  void stopProcess(bool now);
  // Signals the agent's process group, which outlives the QProcess if the agent's own
  // process exits first.
  void signalLater(qint64 group, int signal, int delayMs);
  // Deletes every picture, including any the guide took but never asked about.
  void dropImage();
  [[nodiscard]] bool available() const;
  [[nodiscard]] QString acceptedImage(const QString& path) const;
  [[nodiscard]] QString program() const;

  Environment m_environment;
  bool m_enabled = false;
  AskAgent::Agent m_agent;
  Game m_game;
  QString m_state = QStringLiteral("ready");
  AskAgent::Mode m_mode = AskAgent::Mode::Next;
  QString m_text, m_image;
  qint64 m_started = 0, m_at = 0;
  QPointer<QProcess> m_process;
  QByteArray m_output, m_errors;
  QTimer m_deadline;
};
