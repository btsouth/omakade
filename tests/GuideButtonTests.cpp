#include "gamemode/GameModeGuideButton.h"
#include "guidebutton/GuideListener.h"
#include "guidebutton/GuidePress.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Key capability bitmaps as sysfs prints them on a 64-bit kernel.
// An Xbox 360 pad on xpad, read from a real controller, and Steam Input's pad made from it.
const QString kXpadKeys = QStringLiteral("7cdb000000000000 0 0 0 0");
const QString kXpadAxes = QStringLiteral("3003f");
// A virtual keyboard, read from a real one a streaming tool made, that declares every key code
// including the gamepad buttons.
const QString kEveryKey = QStringLiteral(
    "7fffffffffffffff ffffffffffffffff ffffffffffffffff ffffffffffffffff ffffffffffffffff "
    "ffffffffffffffff ffffffffffffffff ffffffffffffffff ffffffffffffffff ffffffffffffffff "
    "ffffffffffffffff fffffffffffffffe");
// The axes of a real keyboard's consumer control device: volume only.
const QString kVolumeAxis = QStringLiteral("100000000");
// The right Joy-Con reports its stick on the right-stick axes only.
const QString kRightStickAxes = QStringLiteral("18");
// A keyboard: ordinary keys, no gamepad buttons.
const QString kKeyboardKeys =
    QStringLiteral("10000 0 0 0 1007b00011007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe");
// A mouse: left, right, middle, side and extra.
const QString kMouseKeys = QStringLiteral("1f0000 0 0 0 0");
// A joystick with the mode button but no face buttons, such as some flight sticks.
const QString kModeOnlyKeys = QStringLiteral("1000000000000000 0 0 0 0");

constexpr qint64 kArmed = GuidePress::kArmDelayMs;

bool key(GuidePress& press, const QString& device, int code, int value, qint64 now) {
  return press.event(device, GuidePress::kEvKey, code, value, now);
}

// A device node backed by a FIFO, so the listener's real open, read and removal paths run
// without hardware. The writer is opened read-write so the reader never sees end of file
// until the test unplugs it.
struct FakeNode {
  QString path;
  int writer = -1;

  void send(int type, int code, int value) const {
    input_event event{};
    event.type = static_cast<__u16>(type);
    event.code = static_cast<__u16>(code);
    event.value = value;
    QVERIFY(::write(writer, &event, sizeof(event)) == sizeof(event));
  }
  void sync() const { send(EV_SYN, SYN_REPORT, 0); }
  void press(int code, int value) const {
    send(EV_KEY, code, value);
    sync();
  }
  void unplug() {
    QFile::remove(path);
    ::close(writer);
    writer = -1;
  }
};

class FakeInput {
public:
  FakeInput() {
    QDir(m_root.path()).mkpath(QStringLiteral("dev"));
    QDir(m_root.path()).mkpath(QStringLiteral("sys"));
    QDir(m_root.path()).mkpath(QStringLiteral("devices/virtual/input"));
  }
  [[nodiscard]] QString devDir() const { return m_root.filePath(QStringLiteral("dev")); }
  [[nodiscard]] QString sysDir() const { return m_root.filePath(QStringLiteral("sys")); }

  FakeNode add(const QString& node, const QString& name, const QString& keys,
               bool virtualDevice = false) {
    // sysfs links each event node to its device; virtual devices live under devices/virtual.
    const QString device = m_root.filePath(
        (virtualDevice ? QStringLiteral("devices/virtual/input/") : QStringLiteral("devices/"))
        + node);
    QDir().mkpath(device + QStringLiteral("/device/capabilities"));
    writeFile(device + QStringLiteral("/device/name"), name);
    writeFile(device + QStringLiteral("/device/capabilities/key"), keys);
    writeFile(device + QStringLiteral("/device/capabilities/abs"),
              keys == kXpadKeys ? kXpadAxes : QStringLiteral("0"));
    QFile::remove(sysDir() + QLatin1Char('/') + node);
    QFile::link(device, sysDir() + QLatin1Char('/') + node);
    FakeNode fake{.path = devDir() + QLatin1Char('/') + node};
    ::mkfifo(QFile::encodeName(fake.path).constData(), 0600);
    fake.writer = ::open(QFile::encodeName(fake.path).constData(), O_RDWR | O_NONBLOCK);
    return fake;
  }

private:
  static void writeFile(const QString& path, const QString& text) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8() + '\n');
  }
  QTemporaryDir m_root;
};

class FakeSystemctl {
public:
  QString loadState = QStringLiteral("loaded");
  bool enabled = false;
  bool active = false;
  bool refuse = false;
  QList<QStringList> calls;

  GameModeGuideButton::Result operator()(const QStringList& arguments) {
    calls.append(arguments);
    const QString verb = arguments.value(0);
    if (verb == QStringLiteral("show")) {
      return {.ok = true, .output = loadState};
    }
    if (verb == QStringLiteral("enable") || verb == QStringLiteral("disable")) {
      if (refuse) {
        return {.ok = false,
                .output = QStringLiteral("Created nothing.\nFailed to connect to bus")};
      }
      enabled = active = verb == QStringLiteral("enable");
      return {.ok = true, .output = {}};
    }
    if (verb == QStringLiteral("is-enabled")) {
      return {.ok = enabled, .output = enabled ? QStringLiteral("enabled")
                                               : QStringLiteral("disabled")};
    }
    if (verb == QStringLiteral("is-active")) {
      return {.ok = active,
              .output = active ? QStringLiteral("active") : QStringLiteral("failed")};
    }
    return {};
  }
};
} // namespace

class GuideButtonTests final : public QObject {
  Q_OBJECT

private slots:
  void recognizesControllersFromSysfs() {
    QVERIFY(GuidePress::isController(kXpadKeys, kXpadAxes));
    QVERIFY(GuidePress::isController(kXpadKeys, kRightStickAxes));
    QVERIFY(!GuidePress::isController(kXpadKeys, QStringLiteral("0")));
    QVERIFY(!GuidePress::isController(kXpadKeys, kVolumeAxis));
    QVERIFY(!GuidePress::isController(kEveryKey, QStringLiteral("0")));
    QVERIFY(!GuidePress::isController(kEveryKey, kXpadAxes));
    QVERIFY(!GuidePress::isController(kKeyboardKeys, kVolumeAxis));
    QVERIFY(!GuidePress::isController(kMouseKeys, QStringLiteral("3")));
    QVERIFY(!GuidePress::isController(kModeOnlyKeys, kXpadAxes));
    QVERIFY(!GuidePress::isController({}, {}));
    QVERIFY(!GuidePress::isController(QStringLiteral("not hex"), kXpadAxes));
    QVERIFY(GuidePress::hasBit(QStringLiteral("1 0"), 64));
    QVERIFY(!GuidePress::hasBit(QStringLiteral("1"), 64));
    QVERIFY(GuidePress::hasBit(QStringLiteral("80000000 0"), 63, 32));
  }

  void shortPressToggles() {
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed));
    QVERIFY(key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 120));
    // Pressing again later toggles again.
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed + 3000));
    QVERIFY(key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 3100));
  }

  void otherButtonsNeverToggle() {
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    for (const int code : {BTN_SOUTH, BTN_START, BTN_SELECT, BTN_THUMBL, KEY_HOMEPAGE}) {
      QVERIFY(!key(press, QStringLiteral("event1"), code, 1, kArmed + 10));
      QVERIFY(!key(press, QStringLiteral("event1"), code, 0, kArmed + 50));
    }
    QVERIFY(!press.event(QStringLiteral("event1"), GuidePress::kEvAbs, ABS_X, 32000,
                         kArmed + 60));
  }

  void longHoldDoesNotToggle() {
    // Holding the Xbox button turns the controller off, and holds mean something else on others.
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 2, kArmed + 500));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0,
                 kArmed + GuidePress::kMaxHoldMs + 1));
  }

  void chordsDoNotToggle() {
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_START, 1, kArmed + 40));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_START, 0, kArmed + 80));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 120));

    // A d-pad direction is a chord too, on pads that report it as a hat.
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed + 5000));
    QVERIFY(!press.event(QStringLiteral("event1"), GuidePress::kEvAbs, ABS_HAT0Y, -1,
                         kArmed + 5040));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 5100));

    // A chord on one controller does not cancel a press on another.
    press.opened(QStringLiteral("event2"), 0);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed + 9000));
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_SOUTH, 1, kArmed + 9020));
    QVERIFY(key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 9100));
  }

  void buttonsAlreadyHeldMakeAChord() {
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    // The other button lands a few milliseconds before Guide.
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_TR, 1, kArmed));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed + 5));
    QVERIFY(!press.holding(QStringLiteral("event1")));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_TR, 0, kArmed + 90));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 100));

    // A button held since before the controller was opened.
    press.opened(QStringLiteral("event2"), 0, {BTN_TL, BTN_MODE});
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 1, kArmed + 5000));
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 0, kArmed + 5100));
    // Once it is let go, Guide alone works.
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_TL, 0, kArmed + 6000));
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 1, kArmed + 7000));
    QVERIFY(press.holding(QStringLiteral("event2")));
    QVERIFY(key(press, QStringLiteral("event2"), BTN_MODE, 0, kArmed + 7100));

    // A dropped report forgets what was held, rather than blocking Guide forever.
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_SOUTH, 1, kArmed + 9000));
    press.dropped(QStringLiteral("event2"));
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 1, kArmed + 11000));
    QVERIFY(key(press, QStringLiteral("event2"), BTN_MODE, 0, kArmed + 11100));
  }

  void pulledTriggersMakeAChord() {
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    press.setTrigger(QStringLiteral("event1"), ABS_RZ, 191);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed));
    QVERIFY(!press.event(QStringLiteral("event1"), GuidePress::kEvAbs, ABS_RZ, 255,
                         kArmed + 30));
    QVERIFY(!press.event(QStringLiteral("event1"), GuidePress::kEvAbs, ABS_RZ, 0, kArmed + 60));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 100));

    // A light touch, or a right stick reported on the same axis resting at its centre, is not.
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed + 3000));
    QVERIFY(!press.event(QStringLiteral("event1"), GuidePress::kEvAbs, ABS_RZ, 128,
                         kArmed + 3030));
    QVERIFY(key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 3100));

    // An axis with no known range never counts.
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed + 6000));
    QVERIFY(!press.event(QStringLiteral("event1"), GuidePress::kEvAbs, ABS_Z, 255,
                         kArmed + 6030));
    QVERIFY(key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 6100));
  }

  void pressesFromBeforeTheControllerOpenedDoNotToggle() {
    GuidePress press;
    // The press that switched the controller on, finished just after it connected.
    press.opened(QStringLiteral("event1"), 10'000);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, 10'100));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0, 10'300));
    // A button held at connection, whose press was never seen.
    press.opened(QStringLiteral("event2"), 0);
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 0, kArmed + 100));
    // Events from a device that was never opened, or has closed.
    QVERIFY(!key(press, QStringLiteral("event9"), BTN_MODE, 1, kArmed));
    QVERIFY(!key(press, QStringLiteral("event9"), BTN_MODE, 0, kArmed + 100));
    press.closed(QStringLiteral("event2"));
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 1, kArmed + 5000));
    QVERIFY(!key(press, QStringLiteral("event2"), BTN_MODE, 0, kArmed + 5100));
  }

  void onePressSeenTwiceTogglesOnce() {
    // Steam Input mirrors the controller on a virtual pad, so one press arrives twice.
    GuidePress press;
    press.opened(QStringLiteral("physical"), 0);
    press.opened(QStringLiteral("steam"), 0);
    QVERIFY(!key(press, QStringLiteral("physical"), BTN_MODE, 1, kArmed));
    QVERIFY(!key(press, QStringLiteral("steam"), BTN_MODE, 1, kArmed + 4));
    QVERIFY(key(press, QStringLiteral("physical"), BTN_MODE, 0, kArmed + 110));
    QVERIFY(!key(press, QStringLiteral("steam"), BTN_MODE, 0, kArmed + 116));

    // Either copy may arrive first.
    QVERIFY(!key(press, QStringLiteral("steam"), BTN_MODE, 1, kArmed + 4000));
    QVERIFY(!key(press, QStringLiteral("physical"), BTN_MODE, 1, kArmed + 4003));
    QVERIFY(key(press, QStringLiteral("steam"), BTN_MODE, 0, kArmed + 4100));
    QVERIFY(!key(press, QStringLiteral("physical"), BTN_MODE, 0, kArmed + 4104));

    // A chord seen on one copy cancels the other copy too.
    QVERIFY(!key(press, QStringLiteral("physical"), BTN_MODE, 1, kArmed + 8000));
    QVERIFY(!key(press, QStringLiteral("physical"), BTN_SOUTH, 1, kArmed + 8030));
    QVERIFY(!key(press, QStringLiteral("steam"), BTN_MODE, 1, kArmed + 8002));
    QVERIFY(!key(press, QStringLiteral("physical"), BTN_MODE, 0, kArmed + 8100));
    QVERIFY(!key(press, QStringLiteral("steam"), BTN_MODE, 0, kArmed + 8105));
  }

  void twoPlayersPressingTogetherToggleOnce() {
    GuidePress press;
    press.opened(QStringLiteral("player1"), 0);
    press.opened(QStringLiteral("player2"), 0);
    QVERIFY(!key(press, QStringLiteral("player1"), BTN_MODE, 1, kArmed));
    QVERIFY(!key(press, QStringLiteral("player2"), BTN_MODE, 1, kArmed + 200));
    QVERIFY(key(press, QStringLiteral("player1"), BTN_MODE, 0, kArmed + 300));
    QVERIFY(!key(press, QStringLiteral("player2"), BTN_MODE, 0, kArmed + 500));
    // Well after, either player toggles again.
    QVERIFY(!key(press, QStringLiteral("player2"), BTN_MODE, 1, kArmed + 3000));
    QVERIFY(key(press, QStringLiteral("player2"), BTN_MODE, 0, kArmed + 3100));
  }

  void droppedEventsForgetTheHeldButton() {
    GuidePress press;
    press.opened(QStringLiteral("event1"), 0);
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 1, kArmed));
    press.dropped(QStringLiteral("event1"));
    QVERIFY(!key(press, QStringLiteral("event1"), BTN_MODE, 0, kArmed + 100));
  }

  void listenerWatchesOnlyControllers() {
    FakeInput input;
    FakeNode pad = input.add(QStringLiteral("event3"), QStringLiteral("Microsoft X-Box 360 pad"),
                             kXpadKeys);
    FakeNode steam = input.add(QStringLiteral("event4"),
                               QStringLiteral("Microsoft X-Box 360 pad 0"), kXpadKeys, true);
    FakeNode keyboard = input.add(QStringLiteral("event5"), QStringLiteral("AT keyboard"),
                                  kKeyboardKeys);
    FakeNode mouse = input.add(QStringLiteral("event6"), QStringLiteral("Mouse"), kMouseKeys);

    const QList<GuideListener::Controller> found =
        GuideListener::scan(input.devDir(), input.sysDir());
    QCOMPARE(found.size(), 2);
    QCOMPARE(found.at(0).node, QStringLiteral("event3"));
    QVERIFY(!found.at(0).virtualDevice);
    QCOMPARE(found.at(1).node, QStringLiteral("event4"));
    QCOMPARE(found.at(1).name, QStringLiteral("Microsoft X-Box 360 pad 0"));
    QVERIFY(found.at(1).virtualDevice);

    GuideListener listener(input.devDir(), input.sysDir());
    listener.start();
    QStringList open = listener.openNodes();
    open.sort();
    QCOMPARE(open, (QStringList{QStringLiteral("event3"), QStringLiteral("event4")}));
  }

  void listenerTogglesOnceForAPressSeenOnTwoDevices() {
    FakeInput input;
    FakeNode pad = input.add(QStringLiteral("event3"), QStringLiteral("pad"), kXpadKeys);
    FakeNode steam = input.add(QStringLiteral("event4"), QStringLiteral("pad 0"), kXpadKeys, true);
    GuideListener listener(input.devDir(), input.sysDir());
    QSignalSpy pressed(&listener, &GuideListener::pressed);
    QSignalSpy preparing(&listener, &GuideListener::preparing);
    listener.start();
    QTest::qWait(int(kArmed) + 100);

    // Motion and other buttons around the press change nothing.
    pad.send(EV_ABS, ABS_X, 12000);
    pad.sync();
    pad.press(BTN_SOUTH, 1);
    pad.press(BTN_SOUTH, 0);
    pad.press(BTN_MODE, 1);
    steam.press(BTN_MODE, 1);
    QTRY_COMPARE(preparing.size(), 2);
    QCOMPARE(pressed.size(), 0);
    pad.press(BTN_MODE, 0);
    steam.press(BTN_MODE, 0);
    QTRY_COMPARE(pressed.size(), 1);
    QCOMPARE(pressed.at(0).at(0).toString(), QStringLiteral("event3"));
    QTest::qWait(100);
    QCOMPARE(pressed.size(), 1);
  }

  void listenerFollowsHotplugAndIgnoresDroppedReports() {
    FakeInput input;
    GuideListener listener(input.devDir(), input.sysDir());
    QSignalSpy pressed(&listener, &GuideListener::pressed);
    QSignalSpy changed(&listener, &GuideListener::devicesChanged);
    listener.start();
    QVERIFY(listener.openNodes().isEmpty());

    FakeNode pad = input.add(QStringLiteral("event7"), QStringLiteral("pad"), kXpadKeys);
    QTRY_COMPARE(listener.openNodes(), QStringList{QStringLiteral("event7")});
    QTest::qWait(int(kArmed) + 100);

    // The kernel's queue overflowed while the button was down, so its release is not trusted.
    pad.press(BTN_MODE, 1);
    pad.send(EV_SYN, SYN_DROPPED, 0);
    pad.send(EV_KEY, BTN_MODE, 0);
    pad.sync();
    QTest::qWait(100);
    QCOMPARE(pressed.size(), 0);

    pad.press(BTN_MODE, 1);
    pad.press(BTN_MODE, 0);
    QTRY_COMPARE(pressed.size(), 1);

    pad.unplug();
    QTRY_VERIFY(listener.openNodes().isEmpty());

    // A new controller reusing the node name is a new device, and has to arm again.
    FakeNode replacement = input.add(QStringLiteral("event7"), QStringLiteral("other pad"),
                                     kXpadKeys);
    QTRY_COMPARE(listener.openNodes(), QStringList{QStringLiteral("event7")});
    replacement.press(BTN_MODE, 1);
    replacement.press(BTN_MODE, 0);
    QTest::qWait(100);
    QCOMPARE(pressed.size(), 1);
    QVERIFY(changed.size() >= 3);
  }

  void listenerRetriesAControllerItCannotOpenYet() {
    if (::geteuid() == 0) {
      QSKIP("root can open the node regardless of its permissions");
    }
    FakeInput input;
    FakeNode pad = input.add(QStringLiteral("event8"), QStringLiteral("pad"), kXpadKeys);
    // udev grants access a moment after the node appears.
    ::chmod(QFile::encodeName(pad.path).constData(), 0);
    GuideListener listener(input.devDir(), input.sysDir());
    listener.start();
    QVERIFY(listener.openNodes().isEmpty());
    ::chmod(QFile::encodeName(pad.path).constData(), 0600);
    QTRY_COMPARE(listener.openNodes(), QStringList{QStringLiteral("event8")});
  }

  void settingFollowsSystemd() {
    FakeSystemctl systemctl;
    auto run = [&systemctl](GameModeGuideButton::Action action) {
      return GameModeGuideButton::run(
          [&systemctl](const QStringList& arguments) { return systemctl(arguments); }, action);
    };

    GameModeGuideButton::Outcome outcome = run(GameModeGuideButton::Action::Refresh);
    QVERIFY(outcome.available);
    QVERIFY(!outcome.enabled);
    QVERIFY(outcome.statusText.isEmpty());

    outcome = run(GameModeGuideButton::Action::Enable);
    QVERIFY(outcome.enabled);
    QVERIFY(outcome.running);
    QVERIFY(systemctl.calls.contains(
        QStringList{QStringLiteral("enable"), QStringLiteral("--now"),
                    QStringLiteral("omakade-guide-button.service")}));

    outcome = run(GameModeGuideButton::Action::Disable);
    QVERIFY(!outcome.enabled);
    QVERIFY(!outcome.running);
    QVERIFY(systemctl.calls.contains(
        QStringList{QStringLiteral("disable"), QStringLiteral("--now"),
                    QStringLiteral("omakade-guide-button.service")}));

    // A failure is reported, and the switch shows what systemd actually holds.
    systemctl.refuse = true;
    outcome = run(GameModeGuideButton::Action::Enable);
    QVERIFY(!outcome.enabled);
    QCOMPARE(outcome.statusText,
             QStringLiteral("Could not turn on the controller button: Failed to connect to bus"));

    // Enabled, but the service has stopped.
    systemctl.refuse = false;
    systemctl.enabled = true;
    systemctl.active = false;
    outcome = run(GameModeGuideButton::Action::Refresh);
    QVERIFY(outcome.enabled);
    QVERIFY(!outcome.running);
    QVERIFY(outcome.statusText.contains(QStringLiteral("journalctl --user")));

    // Not installed: the setting is hidden and nothing is changed.
    systemctl.loadState = QStringLiteral("not-found");
    systemctl.calls.clear();
    outcome = run(GameModeGuideButton::Action::Enable);
    QVERIFY(!outcome.available);
    QCOMPARE(systemctl.calls.size(), 1);

    // No user session or no systemctl.
    outcome = GameModeGuideButton::run([](const QStringList&) { return GameModeGuideButton::Result{}; },
                                       GameModeGuideButton::Action::Refresh);
    QVERIFY(!outcome.available);
  }

  void isolatedRunsNeverTouchServices() {
    GameModeGuideButton button(false);
    QSignalSpy changed(&button, &GameModeGuideButton::changed);
    button.enable();
    QVERIFY(!button.busy());
    QCOMPARE(changed.size(), 0);
    QVERIFY(!button.available());
  }
};

QTEST_GUILESS_MAIN(GuideButtonTests)
#include "GuideButtonTests.moc"
