#pragma once

#include "input/VirtualPointer.h"

#include <QElapsedTimer>
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
  // Whether the controller can be a pointer in a launcher's window, see setPointerMode.
  Q_PROPERTY(bool pointerAvailable READ pointerAvailable CONSTANT)

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
  [[nodiscard]] bool pointerAvailable() const { return VirtualPointer::available(); }
  // While a launcher's window is shown over Game Mode and has focus, the controller is a
  // pointer in it: the left stick or D-pad moves it, A clicks, the right stick scrolls and
  // B asks for the library back. Steam's dialogs, such as a EULA, take nothing else.
  void setPointerMode(bool enabled);

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
  void pointerBackRequested();

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
  [[nodiscard]] bool pointerActive() const { return m_pointerMode && !m_inputEnabled; }
  void handlePointerButton(int button, bool pressed);
  void drivePointer();
  void resetPointer();
  bool ensurePointer();

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
  VirtualPointer m_pointer;
  bool m_pointerMode = false;
  bool m_pointerFailed = false;
  int m_pointerX = 0;
  int m_pointerY = 0;
  int m_pointerDpadX = 0;
  int m_pointerDpadY = 0;
  int m_scrollAxis = 0;
  double m_pointerRestX = 0;
  double m_pointerRestY = 0;
  double m_scrollRest = 0;
  QElapsedTimer m_pointerClock;
};
