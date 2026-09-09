#pragma once
#include <QObject>
#include <QVariantList>
#include <functional>

// Local, single-file SRAM protection. Unsupported layouts are deliberately skipped.
class SaveBackups final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY changed)
  Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
public:
  explicit SaveBackups(QObject* parent = nullptr);
  SaveBackups(QString home, QString config, QString root,
              std::function<bool()> emulatorRunning, QObject* parent = nullptr);
  void setEnabled(bool enabled) { m_enabled = enabled; }
  int revision() const { return m_revision; }
  QVariantList versions() const { return m_versions; }
  QString message() const { return m_message; }
  // Returns the supported existing save path, or empty without guessing.
  QString discover(const QString& game, const QString& core, bool allowMissing = false) const;
  bool protect(const QString& game, const QString& core, bool flatpak = false);
  Q_INVOKABLE int count(const QString& game) const;
  Q_INVOKABLE void selectGame(const QString& game);
  Q_INVOKABLE bool restore(const QString& version);
signals:
  void changed();
  void warning(const QString& message);
private:
  QString gameRoot(const QString& game) const;
  QVariantList list(const QString& game) const;
  bool snapshot(const QString& game, const QString& core, const QString& source, QString* error);
  void report(const QString& message, bool warn = false);
  QString m_home, m_config, m_root, m_game, m_message;
  std::function<bool()> m_running;
  bool m_enabled = true;
  int m_revision = 0;
  QVariantList m_versions;
};
