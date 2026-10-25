#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Ask: the guide's "What now?" sends a picture of the paused game to the coding agent
// Omarchy has as its default (`omarchy default agent`) and shows the answer on the card.
// This is the part the app's settings and omakade-sessiond share: which agent that is,
// whether Ask can drive it, the command that asks it one question with nothing that can
// change the computer, and reading its answer.
namespace AskAgent {

struct Agent {
  // Omarchy's name for it, as in ~/.config/omarchy/defaults/agent: "codex", "claude", ...
  QString id;
  // What the user knows it as: "Codex", "Claude Code".
  QString name;
  // Ask can drive it: it takes a picture on the command line and runs one turn headless,
  // with nothing that can read files.
  bool supported = false;
  // Look it up: it can search the web with nothing that can read files.
  bool canLookUp = false;
  [[nodiscard]] bool valid() const { return !id.isEmpty(); }
  bool operator==(const Agent&) const = default;
};

// The agent Omarchy's default names, or an invalid one when none is chosen.
[[nodiscard]] Agent defaultAgent(const QString& home);
[[nodiscard]] Agent named(const QString& id);

// ask = true in Omakade's config.toml. Off unless turned on, because Ask sends a picture
// of the game to the agent's provider.
[[nodiscard]] bool enabled(const QString& configContents);
[[nodiscard]] bool enabledIn(const QString& configPath);

enum class Mode { Next, Lookup };

struct Question {
  QString title;
  QString platform;
  Mode mode = Mode::Next;
  // The guide's screenshot of the paused game.
  QString imagePath;
  // The answer a Lookup follows up.
  QString earlier;
};

[[nodiscard]] QString prompt(const Question& question);

struct Command {
  QString program;
  QStringList arguments;
  // Written to the agent's input, which is then closed.
  QByteArray input;
};

// One question for the agent at program, run in workDir. Empty for an unsupported agent.
[[nodiscard]] Command command(const Agent& agent, const QString& program, const QString& workDir,
                              const Question& question);

struct Reply {
  // The agent has finished its turn. It may take a moment longer to exit.
  bool done = false;
  QString answer;
  QString error;
};

// Reads everything the agent has printed so far.
[[nodiscard]] Reply read(const Agent& agent, const QByteArray& output);

// Plain text for the card: markdown links become their text, emphasis marks go and
// whitespace collapses. Long answers end at a sentence near the limit.
[[nodiscard]] QString plain(const QString& text);

} // namespace AskAgent
