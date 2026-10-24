#pragma once

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QString>

// An Omakade key on Omarchy: Super + Ctrl + G for Game Mode and Ctrl + Print to clip a
// highlight, both of which Omarchy leaves unbound. Omakade never claims a key on its own.
// The user adds the binding from Settings, and it is one line in
// ~/.config/hypr/bindings.lua that they can read, change or delete.
class GameModeShortcut final : public QObject {
  Q_OBJECT
  // False outside Omarchy, where there is no bindings file in this format to add to.
  Q_PROPERTY(bool available READ available NOTIFY changed)
  // The binding is in the bindings file.
  Q_PROPERTY(bool bound READ bound NOTIFY changed)
  // What the key is already used for when it is not free. Empty when it is.
  Q_PROPERTY(QString takenBy READ takenBy NOTIFY changed)
  // The key of the binding in the file, or the one that would be added.
  Q_PROPERTY(QString keyLabel READ keyLabel NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
  // One key Omakade can add: what it is, what it runs and how it is recognized.
  struct Binding {
    // As bindings.lua writes it, such as "SUPER + CTRL + G".
    QString key;
    QString description;
    // The command Omakade writes. Any of `commands` in a binding counts as Omakade's own.
    QString command;
    QStringList commands;
    QString comment;
    // How `hyprctl -j binds` reports the key: Hyprland's modifier bits and key name.
    int modmask = 0;
    QString keyName;
  };
  [[nodiscard]] static Binding gameMode();
  [[nodiscard]] static Binding clip();

  // `omarchy` is false when the desktop is not Omarchy. A null `bindingsPath` disables
  // the object, which is what isolated test runs use.
  explicit GameModeShortcut(const QString& bindingsPath, bool omarchy, QObject* parent = nullptr,
                            Binding binding = gameMode());
  ~GameModeShortcut() override;

  [[nodiscard]] bool available() const { return m_available; }
  [[nodiscard]] bool bound() const { return !m_boundKey.isEmpty(); }
  [[nodiscard]] QString takenBy() const { return m_takenBy; }
  [[nodiscard]] QString keyLabel() const;
  [[nodiscard]] bool busy() const { return m_watcher.isRunning(); }
  [[nodiscard]] QString statusText() const { return m_statusText; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void add();
  Q_INVOKABLE void remove();

  [[nodiscard]] static QString defaultKey(const Binding& binding = gameMode());
  [[nodiscard]] static QString command(const Binding& binding = gameMode());
  // The `o.bind` line Omakade adds.
  [[nodiscard]] static QString bindingLine(const Binding& binding = gameMode());
  // What the running Hyprland is given to bind the key now. It leaves exactly one binding.
  [[nodiscard]] static QString liveBindingScript(const Binding& binding = gameMode());
  // The key of the first live binding that runs the command, such as "SUPER + CTRL + G".
  // Empty when there is none. Commented lines do not count.
  [[nodiscard]] static QString boundKey(const QString& contents, const Binding& binding = gameMode());
  [[nodiscard]] static QString withBinding(const QString& contents, const Binding& binding = gameMode());
  // Removes every live binding of the command, and the comment Omakade wrote above its own.
  [[nodiscard]] static QString withoutBinding(const QString& contents, const Binding& binding = gameMode());
  // Unbinds every key removed from the file, including custom keys.
  [[nodiscard]] static QString removalScript(const QString& contents, const Binding& binding = gameMode());
  // Reads `hyprctl -j binds`. The description of whatever else holds the default key, or
  // empty when the key is free or already runs the command.
  [[nodiscard]] static QString takenBy(const QByteArray& bindsJson, const Binding& binding = gameMode());
  // "SUPER + CTRL + G" as it is written on a keyboard: "Super + Ctrl + G".
  [[nodiscard]] static QString displayKey(const QString& key);

signals:
  void changed();

private:
  struct Outcome {
    bool available = false;
    QString boundKey;
    QString takenBy;
    QString statusText;
  };
  enum class Action { Refresh, Add, Remove };
  void start(Action action);
  [[nodiscard]] static Outcome run(const QString& path, bool omarchy, Action action, const Binding& binding);

  Binding m_binding;
  QString m_path;
  bool m_omarchy = false;
  bool m_available = false;
  QString m_boundKey;
  QString m_takenBy;
  QString m_statusText;
  QFutureWatcher<Outcome> m_watcher;
};
