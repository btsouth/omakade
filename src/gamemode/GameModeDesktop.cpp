#include "gamemode/GameModeDesktop.h"
#include "tracking/ProcFs.h"
#include <QFileInfo>
#include <limits>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr int kCommandTimeoutMs = 2500;

// Runs one command to completion. False when it is missing, stalls, or exits non-zero.
bool run(const QString& program, const QStringList& arguments, QByteArray* output = nullptr) {
  const QString executable = QStandardPaths::findExecutable(program);
  if (executable.isEmpty()) {
    return false;
  }
  QProcess process;
  process.start(executable, arguments);
  if (!process.waitForStarted(kCommandTimeoutMs)) {
    return false;
  }
  if (!process.waitForFinished(kCommandTimeoutMs)) {
    process.kill();
    process.waitForFinished(kCommandTimeoutMs);
    return false;
  }
  if (output != nullptr) {
    *output = process.readAllStandardOutput();
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void setError(QString* error, const QString& text) {
  if (error != nullptr) {
    *error = text;
  }
}
} // namespace

bool HyprlandGameModeCompositor::available() {
  int known = m_available.load();
  if (known < 0) {
    QByteArray output;
    const bool usable = !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() &&
                        run(QStringLiteral("hyprctl"),
                            {QStringLiteral("eval"), QStringLiteral("return 1")}, &output) &&
                        output.trimmed() == "ok";
    known = usable ? 1 : 0;
    m_available.store(known);
  }
  return known == 1;
}

QString HyprlandGameModeCompositor::workspaceSelector(const QJsonObject& workspace) {
  const QString name = workspace.value(QLatin1String("name")).toString();
  const int id = workspace.value(QLatin1String("id")).toInt();
  if (name.isEmpty()) {
    return {};
  }
  if (id > 0) {
    return QString::number(id);
  }
  if (name.startsWith(QLatin1String("special"))) {
    return name;
  }
  return QStringLiteral("name:") + name;
}

QVector<GameModeOutput> HyprlandGameModeCompositor::parseOutputs(const QByteArray& json,
                                                                 QString* error) {
  QVector<GameModeOutput> outputs;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    setError(error, parseError.errorString());
    return outputs;
  }
  for (const QJsonValue& value : document.array()) {
    const QJsonObject monitor = value.toObject();
    GameModeOutput output;
    output.name = monitor.value(QLatin1String("name")).toString();
    if (output.name.isEmpty()) {
      continue;
    }
    output.id = monitor.value(QLatin1String("id")).toInt(-1);
    output.description = monitor.value(QLatin1String("description")).toString();
    output.enabled = !monitor.value(QLatin1String("disabled")).toBool();
    output.focused = monitor.value(QLatin1String("focused")).toBool();
    output.width = monitor.value(QLatin1String("width")).toInt();
    output.height = monitor.value(QLatin1String("height")).toInt();
    output.scale = monitor.value(QLatin1String("scale")).toDouble(1);
    output.transform = monitor.value(QLatin1String("transform")).toInt();
    if (output.enabled) {
      output.workspace =
          workspaceSelector(monitor.value(QLatin1String("activeWorkspace")).toObject());
    }
    outputs.append(output);
  }
  return outputs;
}

QString HyprlandGameModeCompositor::placeholderTitle() {
  return QStringLiteral("Omakade Game Mode Placeholder");
}

GameModeWindow HyprlandGameModeCompositor::parseWindow(const QByteArray& clientsJson,
                                                       const QVector<GameModeOutput>& outputs,
                                                       qint64 pid, bool placeholder) {
  GameModeWindow fallback;
  const QJsonDocument document = QJsonDocument::fromJson(clientsJson);
  if (pid <= 0 || !document.isArray()) {
    return fallback;
  }
  for (const QJsonValue& value : document.array()) {
    const QJsonObject client = value.toObject();
    if (client.value(QLatin1String("pid")).toVariant().toLongLong() != pid ||
        !client.value(QLatin1String("mapped")).toBool()) {
      continue;
    }
    // Qt adds the application name to the title, so it is matched by its start.
    if (client.value(QLatin1String("title")).toString().startsWith(placeholderTitle()) !=
        placeholder) {
      continue;
    }
    GameModeWindow window;
    window.address = client.value(QLatin1String("address")).toString();
    if (!validAddress(window.address)) {
      continue;
    }
    window.floating = client.value(QLatin1String("floating")).toBool();
    window.fullscreenMode = client.value(QLatin1String("fullscreen")).toInt();
    window.fullscreenClient = client.value(QLatin1String("fullscreenClient")).toInt();
    window.xwayland = client.value(QLatin1String("xwayland")).toBool();
    window.fullscreen = window.fullscreenMode != 0;
    window.workspace = workspaceSelector(client.value(QLatin1String("workspace")).toObject());
    const int monitor = client.value(QLatin1String("monitor")).toInt(-1);
    for (const GameModeOutput& output : outputs) {
      if (output.id == monitor) {
        window.output = output.name;
      }
    }
    if (client.value(QLatin1String("class")).toString().endsWith(QLatin1String("Omakade"))) {
      return window;
    }
    if (!fallback.valid()) {
      fallback = window;
    }
  }
  return fallback;
}

QString HyprlandGameModeCompositor::luaString(const QString& value) {
  QString quoted = QStringLiteral("\"");
  for (const QChar character : value) {
    const char16_t code = character.unicode();
    if (code == u'\\' || code == u'"') {
      quoted += QLatin1Char('\\');
      quoted += character;
    } else if (code < 0x20 || code == 0x7f) {
      quoted += QStringLiteral("\\%1").arg(static_cast<int>(code), 3, 10, QLatin1Char('0'));
    } else {
      quoted += character;
    }
  }
  return quoted + QLatin1Char('"');
}

bool HyprlandGameModeCompositor::validAddress(const QString& address) {
  static const QRegularExpression pattern(QStringLiteral("^0x[0-9a-fA-F]{1,16}$"));
  return pattern.match(address).hasMatch();
}

QString HyprlandGameModeCompositor::outputScript(const QString& name, bool enabled) {
  // Only the disabled flag is set, so a mode, position and scale the user configured for
  // this output stay as they are.
  return QStringLiteral("hl.monitor({ output = %1, disabled = %2 })")
      .arg(luaString(name), enabled ? QStringLiteral("false") : QStringLiteral("true"));
}

QString HyprlandGameModeCompositor::holdScript() {
  // A named rule replaces itself, so entering Game Mode again does not stack copies.
  // Hyprland matches the whole title, and Qt adds the application name after it.
  return QStringLiteral("hl.window_rule({ name = \"omakade-game-mode-placeholder\", "
                        "match = { title = %1 }, workspace = \"special:omakade silent\" })")
      .arg(luaString(QLatin1Char('^') + placeholderTitle() + QStringLiteral(".*")));
}

QString HyprlandGameModeCompositor::coldWindowScript() {
  // Only a temporary Game Mode root carries this initial title. Warm library
  // windows keep their ordinary desktop animation and placement rules.
  return QStringLiteral("hl.window_rule({ name = \"omakade-game-mode-startup\", "
                        "match = { initial_title = \"^Omakade Game Mode Startup.*\", "
                        "class = \"^io.github.tsouth89.Omakade$\" }, no_anim = true, "
                        "no_initial_focus = true, workspace = \"name:omakade silent\" })");
}

bool HyprlandGameModeCompositor::prepareColdWindow(QString* error) {
  return eval(coldWindowScript(), error);
}

QString HyprlandGameModeCompositor::prepareScript(const QString& address, const QString& workspace,
                                                  const QString& output, const QString& placeholder) {
  const QString window = luaString(QStringLiteral("address:") + address);
  QString script = QStringLiteral(
      "local focused = hl.get_active_window()\n"
      "local monitor = hl.get_active_monitor()\n"
      "local desktop = hl.get_active_workspace()\n"
      "local targetDesktop = hl.get_active_workspace(%1)\n"
      "hl.window_rule({ name = \"omakade-game-mode-presentation\", "
      "match = { class = \"^io.github.tsouth89.Omakade$\", workspace = %2 }, "
      "no_anim = true, no_dim = true, opacity = \"1 override 1 override\" })\n"
      "hl.window_rule({ name = \"omakade-game-mode-frame\", enabled = true, "
      "match = { class = \"^io.github.tsouth89.Omakade$\", workspace = %2 }, "
      "render_unfocused = true })\n")
      .arg(luaString(output), luaString(workspace));
  if (!placeholder.isEmpty())
    script += QStringLiteral(
        "hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = 0, client = 0 }))\n"
        "hl.dispatch(hl.dsp.window.swap({ window = %1, target = %2 }))\n")
        .arg(window, luaString(QStringLiteral("address:") + placeholder));
  // Neither move follows the library. Restore any workspace.move side effect in
  // this same eval, before Hyprland can render or deliver a focus change to Qt.
  script += QStringLiteral(
      "hl.dispatch(hl.dsp.window.move({ window = %1, workspace = %2, follow = false }))\n"
      "hl.dispatch(hl.dsp.workspace.move({ workspace = %2, monitor = %3 }))\n"
      "%4"
      "hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = 2, client = 2 }))\n"
      "if targetDesktop and targetDesktop.config_name ~= %2 then\n"
      "  hl.dispatch(hl.dsp.focus({ workspace = targetDesktop.config_name }))\nend\n"
      "if desktop and desktop.config_name ~= %2 then\n"
      "  hl.dispatch(hl.dsp.focus({ workspace = desktop.config_name }))\nend\n"
      "if monitor then hl.dispatch(hl.dsp.focus({ monitor = monitor.name })) end\n"
      "if focused and focused.mapped and focused.workspace.config_name ~= %2 then\n"
      "  hl.dispatch(hl.dsp.focus({ window = \"address:\" .. focused.address }))\nend")
      .arg(window, luaString(workspace), luaString(output),
           // Trading with a hidden special-workspace node leaves the Wayland
           // root suspended in Hyprland 0.56. A zero resize clears suspension
           // without changing its layout, before the fullscreen configure.
           placeholder.isEmpty() ? QString{} : QStringLiteral(
               "hl.dispatch(hl.dsp.window.resize({ window = %1, x = 0, y = 0, relative = true }))\n")
               .arg(window));
  return script;
}

QString HyprlandGameModeCompositor::placeScript(const QString& address, const QString& workspace,
                                                const QString& output, const QString& placeholder) {
  const QString window = luaString(QStringLiteral("address:") + address);
  // Trading places puts the placeholder in the window's node of the layout tree, so the
  // other windows keep their size and position while the window is away.
  const QString trade =
      placeholder.isEmpty()
          ? QString{}
          : QStringLiteral("hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = "
                           "0, client = 0 }))\n"
                           "hl.dispatch(hl.dsp.window.swap({ window = %1, target = %2 }))\n")
                .arg(window, luaString(QStringLiteral("address:") + placeholder));
  // Focusing the output first makes a new workspace open there, not wherever focus was.
  // Finish the placeholder trade and fullscreen in one compositor transaction before
  // exposing the window, rather than waiting for the GUI completion callback.
  return trade + QStringLiteral("hl.dispatch(hl.dsp.focus({ monitor = %1 }))\n"
                                "hl.dispatch(hl.dsp.window.move({ window = %2, workspace = %3, follow = false }))\n"
                                "hl.dispatch(hl.dsp.window.fullscreen_state({ window = %2, internal = 2, client = 2 }))\n"
                                "hl.dispatch(hl.dsp.focus({ window = %2 }))")
                     .arg(luaString(output), window, luaString(workspace));
}

QString HyprlandGameModeCompositor::tradeScript(const QString& address,
                                                const QString& placeholder) {
  const QString window = luaString(QStringLiteral("address:") + address);
  // Hyprland refuses to swap a fullscreen window, and Couch Mode is fullscreen.
  return QStringLiteral(
             "hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = 0, client = 0 "
             "}))\n"
             "hl.dispatch(hl.dsp.window.swap({ window = %1, target = %2 }))")
      .arg(window, luaString(QStringLiteral("address:") + placeholder));
}

QString HyprlandGameModeCompositor::returnScript(const QString& address, const QString& workspace) {
  return QStringLiteral(
             "hl.dispatch(hl.dsp.window.move({ window = %1, workspace = %2, follow = false }))")
      .arg(luaString(QStringLiteral("address:") + address), luaString(workspace));
}

bool HyprlandGameModeCompositor::eval(const QString& script, QString* error) {
  QByteArray output;
  const bool ran = run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), script}, &output);
  if (ran && output.trimmed() == "ok") {
    return true;
  }
  setError(error, QString::fromUtf8(output).trimmed());
  return false;
}

QVector<GameModeOutput> HyprlandGameModeCompositor::outputs(QString* error) {
  QByteArray json;
  if (!run(QStringLiteral("hyprctl"),
           {QStringLiteral("-j"), QStringLiteral("monitors"), QStringLiteral("all")}, &json)) {
    setError(error, QStringLiteral("hyprctl did not answer"));
    return {};
  }
  return parseOutputs(json, error);
}

bool HyprlandGameModeCompositor::setOutputEnabled(const QString& name, bool enabled,
                                                  QString* error) {
  return !name.isEmpty() && eval(outputScript(name, enabled), error);
}

GameModeWindow HyprlandGameModeCompositor::windowForPid(qint64 pid) {
  QByteArray clients;
  if (!run(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("clients")},
           &clients)) {
    return {};
  }
  return parseWindow(clients, outputs(), pid);
}

GameModeWindow HyprlandGameModeCompositor::windowForClass(const QString& windowClass) {
  QByteArray clients;
  if (windowClass.isEmpty() ||
      !run(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("clients")}, &clients)) {
    return {};
  }
  for (const QJsonValue& value : QJsonDocument::fromJson(clients).array()) {
    const QJsonObject client = value.toObject();
    if (client.value(QLatin1String("class")).toString() != windowClass ||
        !client.value(QLatin1String("mapped")).toBool()) {
      continue;
    }
    const qint64 pid = client.value(QLatin1String("pid")).toVariant().toLongLong();
    const GameModeWindow window = parseWindow(clients, outputs(), pid);
    if (window.valid()) return window;
  }
  return {};
}

int HyprlandGameModeCompositor::otherWindowsOn(const QString& workspace, qint64 pid) {
  return otherWindowAddressesOn(workspace, pid).size();
}

QStringList HyprlandGameModeCompositor::otherWindowAddressesOn(const QString& workspace,
                                                               qint64 pid) {
  QByteArray clients;
  if (!run(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("clients")},
           &clients)) {
    return {};
  }
  return otherWindowAddresses(clients, workspace, pid);
}

QStringList HyprlandGameModeCompositor::otherWindowAddresses(const QByteArray& clientsJson,
                                                             const QString& workspace, qint64 pid) {
  QStringList addresses;
  for (const QJsonValue& value : QJsonDocument::fromJson(clientsJson).array()) {
    const QJsonObject client = value.toObject();
    if (!client.value(QLatin1String("mapped")).toBool() ||
        client.value(QLatin1String("pid")).toVariant().toLongLong() == pid ||
        workspaceSelector(client.value(QLatin1String("workspace")).toObject()) != workspace) {
      continue;
    }
    const QString address = client.value(QLatin1String("address")).toString();
    if (validAddress(address)) {
      addresses.append(address);
    }
  }
  return addresses;
}

int HyprlandGameModeCompositor::countOtherWindows(const QByteArray& clientsJson,
                                                  const QString& workspace, qint64 pid) {
  return otherWindowAddresses(clientsJson, workspace, pid).size();
}

GameModeWindow HyprlandGameModeCompositor::placeholderForPid(qint64 pid) {
  QByteArray clients;
  if (!run(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("clients")},
           &clients)) {
    return {};
  }
  return parseWindow(clients, {}, pid, true);
}

bool HyprlandGameModeCompositor::holdPlaceholder(QString* error) {
  return eval(holdScript(), error);
}

bool HyprlandGameModeCompositor::placeWindow(const QString& address, const QString& workspace,
                                             const QString& output, const QString& placeholder,
                                             QString* error) {
  return validAddress(address) && !workspace.isEmpty() && !output.isEmpty() &&
         (placeholder.isEmpty() || validAddress(placeholder)) &&
         eval(placeScript(address, workspace, output, placeholder), error);
}

bool HyprlandGameModeCompositor::prepareWindow(const QString& address, const QString& workspace,
                                              const QString& output, const QString& placeholder,
                                              QString* error) {
  return validAddress(address) && !workspace.isEmpty() && !output.isEmpty() &&
         (placeholder.isEmpty() || validAddress(placeholder)) &&
         eval(prepareScript(address, workspace, output, placeholder), error);
}

bool HyprlandGameModeCompositor::returnWindow(const QString& address, const QString& workspace,
                                              const QString& placeholder, QString* error) {
  if (!validAddress(address) || workspace.isEmpty()) {
    return false;
  }
  if (placeholder.isEmpty()) {
    return eval(returnScript(address, workspace), error);
  }
  return validAddress(placeholder) && eval(tradeScript(address, placeholder), error);
}

bool HyprlandGameModeCompositor::focusWindow(const QString& address, QString* error) {
  return validAddress(address) && eval(QStringLiteral("hl.dispatch(hl.dsp.focus({ window = %1 }))")
                                           .arg(luaString(QStringLiteral("address:") + address)),
                                       error);
}

bool HyprlandGameModeCompositor::presentWindow(const QString& address, QString* error) {
  // The final focus and stopping hidden rendering share one compositor turn.
  return validAddress(address) && eval(QStringLiteral(
      "hl.dispatch(hl.dsp.focus({ window = %1 }))\n"
      "hl.window_rule({ name = \"omakade-game-mode-frame\", enabled = false })")
      .arg(luaString(QStringLiteral("address:") + address)), error);
}

bool HyprlandGameModeCompositor::setWindowMode(const QString& address, int mode, int clientMode,
                                               QString* error) {
  if (!validAddress(address) || mode < 0 || mode > 3 || clientMode < 0 || clientMode > 3)
    return false;
  return eval(QStringLiteral("hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = "
                             "%2, client = %3 }))")
                  .arg(luaString(QStringLiteral("address:") + address))
                  .arg(mode)
                  .arg(clientMode),
              error);
}

QString HyprlandGameModeCompositor::focusGameScript(const GameModeGameWindow& game,
                                                    const QString& ownerAddress) {
  if (!validAddress(game.address) || (!ownerAddress.isEmpty() && !validAddress(ownerAddress)) ||
      game.address == ownerAddress || game.fullscreen < 0 || game.fullscreen > 3 ||
      game.fullscreenClient < 0 || game.fullscreenClient > 3)
    return {};
  QString script;
  if (!ownerAddress.isEmpty())
    script =
        QStringLiteral(
            "hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = 0, client = 0 "
            "}))\n")
            .arg(luaString(QStringLiteral("address:") + ownerAddress));
  // Qt remapping/fullscreen can replace a game's compositor fullscreen on this workspace.
  // Restore the modes captured at park, including windowed (0), in the same eval as focus.
  script += QStringLiteral(
                "hl.dispatch(hl.dsp.window.fullscreen_state({ window = %1, internal = %2, client = "
                "%3 }))\n"
                "hl.dispatch(hl.dsp.focus({ window = %1 }))")
                .arg(luaString(QStringLiteral("address:") + game.address))
                .arg(game.fullscreen)
                .arg(game.fullscreenClient);
  return script;
}

bool HyprlandGameModeCompositor::focusGameWindow(const GameModeGameWindow& game, qint64 ownerPid,
                                                 QString* error) {
  if (ownerPid < 0 || game.process.pid == ownerPid || !processAlive(game.process)) {
    setError(error, QStringLiteral("The retained game's identity is no longer valid."));
    return false;
  }
  const auto owner = ownerPid > 0 ? windowForPid(ownerPid) : GameModeWindow{};
  const auto script = focusGameScript(game, owner.address);
  if (script.isEmpty()) {
    setError(error, QStringLiteral("The retained game's presentation is invalid."));
    return false;
  }
  return eval(script, error);
}

bool HyprlandGameModeCompositor::focusWorkspace(const QString& workspace, QString* error) {
  return !workspace.isEmpty() &&
         eval(QStringLiteral("hl.dispatch(hl.dsp.focus({ workspace = %1 }))")
                  .arg(luaString(workspace)),
              error);
}

bool HyprlandGameModeCompositor::focusOutput(const QString& name, QString* error) {
  return !name.isEmpty() &&
         eval(QStringLiteral("hl.dispatch(hl.dsp.focus({ monitor = %1 }))").arg(luaString(name)),
              error);
}

bool PactlGameModeAudio::available() {
  return run(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")});
}

QVector<GameModeSink> PactlGameModeAudio::parseSinks(const QByteArray& json, QString* error) {
  QVector<GameModeSink> sinks;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    setError(error, parseError.errorString());
    return sinks;
  }
  for (const QJsonValue& value : document.array()) {
    const QJsonObject object = value.toObject();
    GameModeSink sink;
    sink.name = object.value(QLatin1String("name")).toString();
    if (sink.name.isEmpty()) {
      continue;
    }
    sink.description = object.value(QLatin1String("description")).toString();
    sinks.append(sink);
  }
  return sinks;
}

QVector<GameModeSink> PactlGameModeAudio::sinks(QString* error) {
  QByteArray json;
  if (!run(QStringLiteral("pactl"),
           {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"),
            QStringLiteral("sinks")},
           &json)) {
    setError(error, QStringLiteral("pactl did not answer"));
    return {};
  }
  return parseSinks(json, error);
}

QString PactlGameModeAudio::defaultSink() {
  QByteArray output;
  if (!run(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")}, &output)) {
    return {};
  }
  return QString::fromUtf8(output).trimmed();
}

bool PactlGameModeAudio::setDefaultSink(const QString& name, QString* error) {
  if (name.isEmpty() || name.startsWith(QLatin1Char('-'))) {
    return false;
  }
  if (run(QStringLiteral("pactl"), {QStringLiteral("set-default-sink"), name})) {
    return true;
  }
  setError(error, QStringLiteral("pactl could not select %1").arg(name));
  return false;
}

bool OmarchyGameModeNotifications::available() {
  return !QStandardPaths::findExecutable(QStringLiteral("omarchy-shell")).isEmpty();
}

bool OmarchyGameModeNotifications::silenced(bool* silenced) {
  QByteArray output;
  if (!run(QStringLiteral("omarchy-shell"),
           {QStringLiteral("notifications"), QStringLiteral("dndState")}, &output)) {
    return false;
  }
  const QByteArray state = output.trimmed();
  if (state != "on" && state != "off") {
    return false;
  }
  if (silenced != nullptr) {
    *silenced = state == "on";
  }
  return true;
}

bool OmarchyGameModeNotifications::setSilenced(bool silenced) {
  if (!run(QStringLiteral("omarchy-shell"),
           {QStringLiteral("notifications"), QStringLiteral("setDnd"),
            silenced ? QStringLiteral("true") : QStringLiteral("false")})) {
    return false;
  }
  // The bar's indicator reads the state on request, the same way Omarchy's own toggle
  // refreshes it.
  run(QStringLiteral("omarchy-shell"),
      {QStringLiteral("-q"), QStringLiteral("omarchy.indicators"), QStringLiteral("refresh")});
  return true;
}

// Retained-session queries distinguish a failed scan from an empty workspace.
bool HyprlandGameModeCompositor::gameWindows(const QString& workspace, qint64 owner,
                                             QVector<GameModeGameWindow>* windows, QString* error) {
  QByteArray json;
  if (!windows || !run(QStringLiteral("hyprctl"), {"-j", "clients"}, &json)) {
    setError(error, QStringLiteral("Hyprland could not inspect game windows."));
    return false;
  }
  const auto document = QJsonDocument::fromJson(json);
  if (!document.isArray()) {
    setError(error, QStringLiteral("Hyprland returned invalid game windows."));
    return false;
  }
  const auto processes = ProcFs::listProcesses(true);
  windows->clear();
  for (const auto& value : document.array()) {
    const auto client = value.toObject();
    const qint64 pid = client.value("pid").toVariant().toLongLong();
    if (pid == owner || !client.value("mapped").toBool() ||
        workspaceSelector(client.value("workspace").toObject()) != workspace)
      continue;
    const QString address = client.value("address").toString();
    if (!validAddress(address)) {
      setError(error, QStringLiteral("A game window has no usable address."));
      return false;
    }
    bool found = false;
    for (const auto& process : processes) {
      if (process.pid != pid)
        continue;
      found = true;
      // These are excluded as witnesses, never expanded into their descendants.
      const QString executable = QFileInfo(process.exePath).fileName().toLower();
      const QString comm = process.comm.toLower();
      const QStringList launchers{
          "steam",     "steamwebhelper", "heroic",       "lutris",       "faugus-launcher",
          "legendary", "battle.net.exe", "explorer.exe", "services.exe", "wineserver"};
      if (launchers.contains(comm) || launchers.contains(executable))
        break;
      const GameModeProcess identity{pid, process.procStart, process.steamAppId, process.winePrefix,
                                     process.flatpakAppId};
      if (!identity.valid() || !ProcFs::processAlive(pid, process.procStart)) {
        setError(error, QStringLiteral("A game process changed during discovery."));
        return false;
      }
      const int fullscreen = client.value("fullscreen").toInt();
      const int fullscreenClient = client.value("fullscreenClient").toInt();
      if (fullscreen < 0 || fullscreen > 3 || fullscreenClient < 0 || fullscreenClient > 3) {
        setError(error, QStringLiteral("A game window has an invalid fullscreen mode."));
        return false;
      }
      windows->append({address, identity, fullscreen, fullscreenClient});
      break;
    }
    if (!found) {
      setError(error, QStringLiteral("A mapped game's process identity could not be read."));
      return false;
    }
  }
  return true;
}

bool HyprlandGameModeCompositor::processAlive(const GameModeProcess& process) {
  return process.valid() && ProcFs::processAlive(process.pid, process.procStart);
}

bool HyprlandGameModeCompositor::desktopFocus(GameModeDesktopFocus* focus, QString* error) {
  QByteArray active;
  QString outputError;
  const auto monitors = outputs(&outputError);
  if (!focus || monitors.isEmpty() || !outputError.isEmpty() ||
      !run(QStringLiteral("hyprctl"), {"-j", "activewindow"}, &active)) {
    setError(error, QStringLiteral("Hyprland could not capture desktop focus."));
    return false;
  }
  const auto document = QJsonDocument::fromJson(active);
  if (!document.isObject()) {
    setError(error, QStringLiteral("Hyprland returned invalid desktop focus."));
    return false;
  }
  *focus = {};
  for (const auto& monitor : monitors) {
    if (monitor.focused && monitor.enabled) {
      focus->output = monitor.name;
      focus->workspace = monitor.workspace;
    }
  }
  const auto window = document.object();
  const QString address = window.value("address").toString();
  if (validAddress(address))
    focus->address = address;
  if (focus->output.isEmpty() || focus->workspace.isEmpty()) {
    setError(error, QStringLiteral("The focused desktop workspace is unknown."));
    return false;
  }
  return true;
}

QString HyprlandGameModeCompositor::moveWorkspaceScript(const QString& workspace,
                                                        const QString& output) {
  // Verified in LuaBindingsDispatchers.cpp: workspace.move takes workspace and monitor.
  // There is no follow parameter. Move a non-visible workspace to avoid following it.
  return QStringLiteral("hl.dispatch(hl.dsp.workspace.move({ workspace = %1, monitor = %2 }))")
      .arg(luaString(workspace), luaString(output));
}

bool HyprlandGameModeCompositor::moveWorkspace(const QString& workspace, const QString& output,
                                               QString* error) {
  GameModeDesktopFocus before;
  if (workspace.isEmpty() || output.isEmpty() || !desktopFocus(&before, error))
    return false;
  QString script = moveWorkspaceScript(workspace, output);
  if (before.workspace != workspace) {
    script += QStringLiteral("\nhl.dispatch(hl.dsp.focus({ monitor = %1 }))")
                  .arg(luaString(before.output));
    script += QStringLiteral("\nhl.dispatch(hl.dsp.focus({ workspace = %1 }))")
                  .arg(luaString(before.workspace));
    if (!before.address.isEmpty())
      script += QStringLiteral("\nhl.dispatch(hl.dsp.focus({ window = %1 }))")
                    .arg(luaString(QStringLiteral("address:") + before.address));
  }
  if (!eval(script, error))
    return false;
  QByteArray json;
  if (!run(QStringLiteral("hyprctl"), {"-j", "workspaces"}, &json)) {
    setError(error, QStringLiteral("Workspace relocation could not be verified."));
    return false;
  }
  const auto document = QJsonDocument::fromJson(json);
  if (!document.isArray()) {
    setError(error, QStringLiteral("Hyprland returned invalid workspace placement."));
    return false;
  }
  for (const auto& value : document.array()) {
    const auto entry = value.toObject();
    if (workspaceSelector(entry) == workspace && entry.value("monitor").toString() == output)
      return true;
  }
  setError(error, QStringLiteral("The game workspace did not reach the requested display."));
  return false;
}

bool PactlGameModeAudio::parseStreams(const QByteArray& json, QVector<GameModeStream>* streams,
                                      QString* error) {
  const auto document = QJsonDocument::fromJson(json);
  if (!streams || !document.isArray()) {
    setError(error, QStringLiteral("pactl returned invalid sink inputs."));
    return false;
  }
  streams->clear();
  for (const auto& value : document.array()) {
    const auto entry = value.toObject();
    const auto properties = entry.value("properties").toObject();
    bool indexOk = false;
    const qulonglong index = entry.value("index").toVariant().toString().toULongLong(&indexOk);
    if (!indexOk || index > std::numeric_limits<quint32>::max()) {
      setError(error, QStringLiteral("pactl returned an invalid sink input index."));
      return false;
    }
    GameModeStream stream;
    stream.index = static_cast<quint32>(index);
    stream.process.pid = properties.value("application.process.id").toVariant().toLongLong();
    stream.token = properties.value("object.serial").toVariant().toString();
    stream.muted = entry.value("mute").toBool();
    streams->append(stream);
  }
  return true;
}

bool PactlGameModeAudio::streams(QVector<GameModeStream>* streams, QString* error) {
  QByteArray json;
  if (!run(QStringLiteral("pactl"), {"-f", "json", "list", "sink-inputs"}, &json)) {
    setError(error, QStringLiteral("pactl could not inspect game audio streams."));
    return false;
  }
  if (!parseStreams(json, streams, error))
    return false;
  const auto processes = ProcFs::listProcesses(true);
  for (auto& stream : *streams) {
    for (const auto& process : processes) {
      if (process.pid == stream.process.pid &&
          ProcFs::processAlive(process.pid, process.procStart)) {
        stream.process.procStart = process.procStart;
        stream.process.steamAppId = process.steamAppId;
        stream.process.winePrefix = process.winePrefix;
        stream.process.flatpakAppId = process.flatpakAppId;
        break;
      }
    }
  }
  return true;
}

bool PactlGameModeAudio::setStreamMuted(const GameModeStream& stream, bool muted, QString* error) {
  QVector<GameModeStream> current;
  if (!stream.process.valid() || stream.token.isEmpty() || !streams(&current, error))
    return false;
  for (const auto& candidate : current) {
    if (!candidate.sameStream(stream))
      continue;
    // Revalidate process start immediately before the indexed write.
    if (!ProcFs::processAlive(stream.process.pid, stream.process.procStart))
      return false;
    if (run(QStringLiteral("pactl"),
            {"set-sink-input-mute", QString::number(stream.index), muted ? "1" : "0"}))
      return true;
    setError(error, QStringLiteral("pactl could not change the game's mute state."));
    return false;
  }
  setError(error, QStringLiteral("The game audio stream changed before muting."));
  return false;
}

bool GameModeCompositor::moveLibraryToDesktop(qint64 pid, const GameModeDesktopFocus& desktop,
                                              QString* error) {
  const auto window = windowForPid(pid);
  if (!window.valid() || desktop.workspace.isEmpty() || desktop.output.isEmpty()) return false;
  if (window.workspace != desktop.workspace &&
      !returnWindow(window.address, desktop.workspace, {}, error)) return false;
  return focusWindow(window.address, error);
}
