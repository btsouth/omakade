#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <SDL3/SDL_gamepad.h>

class QWindow;

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
  // True while the controller is the thing being used. The on screen keyboard follows this
  // rather than couch mode, so a pad plugged in for gaming does not make it appear on a mouse
  // click, and reaching a field with the pad on a desktop still gets a way to type.
  Q_PROPERTY(bool driving READ driving NOTIFY drivingChanged)

public:
  explicit ControllerInput(QObject* parent = nullptr);
  ~ControllerInput() override;

  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool driving() const { return m_driving; }
  // Real keyboard and mouse input puts the controller down.
  bool eventFilter(QObject* watched, QEvent* event) override;
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
  void drivingChanged();
  void favoriteRequested();
  void toolbarRequested();
  void startRequested();
  void focusDirectionRequested(int key);
  void acceptRequested();
  void backRequested();

private:
  struct InitResult {
    bool ready = false;
    QString error;
  };

  void pollEvents();
  void openAvailableControllers();
  void closeController(SDL_JoystickID id);
  [[nodiscard]] bool mirrorsAnotherController(SDL_JoystickID id) const;
  void handleButtonPressed(int button);
  [[nodiscard]] bool nintendoFaceButtons() const;
  void handleButtonReleased(int button);
  void setDpadPressed(int key, bool pressed);
  void emitDirection(int key);
  void updateAxisKey();
  void updateRepeatKey();
  [[nodiscard]] QString buttonLabel(SDL_GamepadButton button, const QString& fallback) const;

  QHash<SDL_JoystickID, SDL_Gamepad*> m_controllers;
  SDL_JoystickID m_activeController = 0;
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
  bool m_driving = false;
  void setDriving(bool driving);
  bool m_inputEnabled = true;
};
