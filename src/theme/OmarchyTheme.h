#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

class OmarchyTheme final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool omarchyAvailable READ omarchyAvailable NOTIFY themeChanged)
  Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)
  Q_PROPERTY(QString mode READ mode NOTIFY themeChanged)
  Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY themeChanged)
  Q_PROPERTY(int cornerRadius READ cornerRadius NOTIFY themeChanged)
  Q_PROPERTY(int gapsOut READ gapsOut NOTIFY themeChanged)
  Q_PROPERTY(qreal surfaceAlpha READ surfaceAlpha NOTIFY themeChanged)
  Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
  Q_PROPERTY(QColor selection READ selection NOTIFY themeChanged)
  Q_PROPERTY(QColor muted READ muted NOTIFY themeChanged)
  Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
  Q_PROPERTY(QColor darkBackground READ darkBackground NOTIFY themeChanged)
  Q_PROPERTY(QColor darkerBackground READ darkerBackground NOTIFY themeChanged)
  Q_PROPERTY(QColor lighterBackground READ lighterBackground NOTIFY themeChanged)
  Q_PROPERTY(QColor foreground READ foreground NOTIFY themeChanged)
  Q_PROPERTY(QColor darkForeground READ darkForeground NOTIFY themeChanged)
  Q_PROPERTY(QColor lightForeground READ lightForeground NOTIFY themeChanged)
  Q_PROPERTY(QColor brightForeground READ brightForeground NOTIFY themeChanged)
  Q_PROPERTY(QColor mutedText READ mutedText NOTIFY themeChanged)
  Q_PROPERTY(QColor red READ red NOTIFY themeChanged)
  Q_PROPERTY(QColor yellow READ yellow NOTIFY themeChanged)
  Q_PROPERTY(QColor green READ green NOTIFY themeChanged)
  Q_PROPERTY(QColor cyan READ cyan NOTIFY themeChanged)
  Q_PROPERTY(QColor blue READ blue NOTIFY themeChanged)
  Q_PROPERTY(QColor magenta READ magenta NOTIFY themeChanged)

public:
  explicit OmarchyTheme(QObject* parent = nullptr);
  OmarchyTheme(QString stateHome, QString configHome, QObject* parent = nullptr);
  ~OmarchyTheme() override;

  [[nodiscard]] bool omarchyAvailable() const;
  [[nodiscard]] QString themeName() const;
  [[nodiscard]] QString mode() const;
  [[nodiscard]] QString fontFamily() const;
  [[nodiscard]] int cornerRadius() const;
  [[nodiscard]] int gapsOut() const;
  [[nodiscard]] qreal surfaceAlpha() const;

  [[nodiscard]] QColor accent() const;
  [[nodiscard]] QColor selection() const;
  [[nodiscard]] QColor muted() const;
  [[nodiscard]] QColor background() const;
  [[nodiscard]] QColor darkBackground() const;
  [[nodiscard]] QColor darkerBackground() const;
  [[nodiscard]] QColor lighterBackground() const;
  [[nodiscard]] QColor foreground() const;
  [[nodiscard]] QColor darkForeground() const;
  [[nodiscard]] QColor lightForeground() const;
  [[nodiscard]] QColor brightForeground() const;
  [[nodiscard]] QColor mutedText() const;
  [[nodiscard]] QColor red() const;
  [[nodiscard]] QColor yellow() const;
  [[nodiscard]] QColor green() const;
  [[nodiscard]] QColor cyan() const;
  [[nodiscard]] QColor blue() const;
  [[nodiscard]] QColor magenta() const;

  Q_INVOKABLE void reload();

signals:
  void themeChanged();

private:
  using Values = QHash<QString, QString>;

  [[nodiscard]] QString currentRoot() const;
  [[nodiscard]] QString themeRoot() const;
  [[nodiscard]] static Values readSimpleToml(const QString& path);
  [[nodiscard]] static Values resolvedValues(Values values);
  [[nodiscard]] static qreal readSectionAlpha(const QString& path, const QString& section,
                                              const QString& key, qreal fallback);
  [[nodiscard]] static QColor parsedColor(const Values& values, const QString& key,
                                          const QColor& fallback);
  [[nodiscard]] static QString resolvedMonospaceFamily();
  [[nodiscard]] static double contrastRatio(const QColor& first, const QColor& second);
  void applyFallback();
  void applyValues(const Values& values);
  void refreshHyprlandMetrics();
  void refreshWatchPaths();
  void scheduleReload();

  QString m_stateHome;
  QString m_configHome;
  QFileSystemWatcher m_watcher;
  QTimer m_reloadTimer;

  bool m_omarchyAvailable = false;
  QString m_themeName = QStringLiteral("Omakade Dark");
  QString m_mode = QStringLiteral("dark");
  QString m_fontFamily = QStringLiteral("monospace");
  int m_cornerRadius = 10;
  int m_gapsOut = 5;
  qreal m_surfaceAlpha = 0.82;

  QColor m_accent;
  QColor m_selection;
  QColor m_muted;
  QColor m_background;
  QColor m_darkBackground;
  QColor m_darkerBackground;
  QColor m_lighterBackground;
  QColor m_foreground;
  QColor m_darkForeground;
  QColor m_lightForeground;
  QColor m_brightForeground;
  QColor m_mutedText;
  QColor m_red;
  QColor m_yellow;
  QColor m_green;
  QColor m_cyan;
  QColor m_blue;
  QColor m_magenta;
};
