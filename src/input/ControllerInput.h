#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <SDL3/SDL_gamepad.h>

class ControllerInput final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool inputEnabled READ inputEnabled NOTIFY inputEnabledChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY controllerChanged)
  Q_PROPERTY(QString name READ name NOTIFY controllerChanged)
  Q_PROPERTY(int controllerCount READ controllerCount NOTIFY controllerChanged)
  Q_PROPERTY(QString primaryGlyph READ primaryGlyph NOTIFY controllerChanged)
  Q_PROPERTY(QString backGlyph READ backGlyph NOTIFY controllerChanged)
  Q_PROPERTY(QString favoriteGlyph READ favoriteGlyph NOTIFY controllerChanged)
  Q_PROPERTY(QString toolbarGlyph READ toolbarGlyph NOTIFY controllerChanged)
  Q_PROPERTY(bool focusNavigation READ focusNavigation WRITE setFocusNavigation NOTIFY
                 focusNavigationChanged)

public:
  explicit ControllerInput(QObject* parent = nullptr);
  ~ControllerInput() override;

  [[nodiscard]] bool connected() const;
  [[nodiscard]] QString name() const;
  [[nodiscard]] int controllerCount() const;
  [[nodiscard]] QString primaryGlyph() const;
  [[nodiscard]] QString backGlyph() const;
  [[nodiscard]] QString favoriteGlyph() const;
  [[nodiscard]] QString toolbarGlyph() const;
  [[nodiscard]] bool focusNavigation() const;
  void setFocusNavigation(bool enabled);
  void setInputEnabled(bool enabled);
  bool inputEnabled() const { return m_inputEnabled; }
  void setWindowFocused(bool focused);
  void start();

signals:
  void controllerChanged();
  void inputEnabledChanged();
  void focusNavigationChanged();
  void favoriteRequested();
  void toolbarRequested();
  void focusDirectionRequested(int key);
  void keyRequested(int key, int modifiers);

private:
  struct InitResult {
    bool ready = false;
    QString error;
  };

  void pollEvents();
  void openAvailableControllers();
  void closeController(SDL_JoystickID id);
  void handleButtonPressed(int button);
  void handleButtonReleased(int button);
  void setDpadPressed(int key, bool pressed);
  void emitDirection(int key);
  void updateAxisKey();
  void updateRepeatKey();
  [[nodiscard]] QString buttonLabel(SDL_GamepadButton button, const QString& fallback) const;

  QHash<SDL_JoystickID, SDL_Gamepad*> m_controllers;
  QFutureWatcher<InitResult> m_initWatcher;
  QTimer m_pollTimer;
  QTimer m_repeatTimer;
  int m_axisX = 0;
  int m_axisY = 0;
  int m_axisKey = 0;
  QList<int> m_dpadKeys;
  int m_repeatKey = 0;
  bool m_sdlReady = false;
  bool m_focusNavigation = false;
  bool m_inputEnabled = true;
};
