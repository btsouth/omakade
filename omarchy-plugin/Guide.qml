import QtQuick
import QtQuick.Window
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "components"
import "GuideProtocol.js" as Protocol
import "GuideFocus.js" as Focus
import "Legibility.js" as Legibility

// The in-game guide: one card over the paused game, built like Omarchy's
// panels. A hero with the game and the time, its readings, action tiles,
// achievements and sound, then Resume and Quit (components/GuideCard.qml).
// Input arrives as actions (up, down, left, right, a, b, y, guide) from the
// keyboard or, for controllers, from Omakade's service over the guide socket;
// input() maps them to cursor moves and to actions.
Item {
  id: root

  property bool opened: false
  // The surface has its real size and the card is on screen.
  property bool presented: false
  property bool fixtureMode: false
  // A preview's own pad family, which wins over the fixture's.
  property string fixturePad: ""
  property string outputName: ""
  property var backend: null
  property var backendQueue: []
  property var model: ({})
  property string family: "keyboard"
  property string cursor: ""
  // What the card body shows: the rows, the game's achievements, or the quit
  // question. The header and hint line stay.
  property string view: "main"
  readonly property bool confirming: root.view === "confirm"
  property int confirmChoice: 0
  property int achIndex: 0
  // Quit was confirmed and Omakade is waiting for the game to exit.
  property bool quitting: false
  // A second B straight after backing out of a view must not also close.
  property double backAt: 0
  // The card steps aside while a screenshot is taken.
  property bool capturing: false
  property string captureKind: ""
  property bool hiddenFrame: false
  property bool captureDelayPassed: false
  property date now: new Date()
  // Couch scale from the payload (`scale`, set by Omakade in couch mode): one
  // multiplier on every size and gap of the card. 1 is exactly the Omarchy
  // menu. `zoom` is what is drawn: the requested scale, lowered only as far as
  // the card needs to fit the screen without scrolling.
  property real couchScale: 1
  property real zoom: 1
  function sized(v) { return v > 0 ? Math.max(1, Math.round(v * root.zoom)) : 0 }

  readonly property var game: root.model.game || null
  readonly property bool forceReady: !!(root.game && root.game.forceReady)
  readonly property var performance: root.model.performance || ({})
  readonly property var stats: root.fixtureMode ? (root.model.stats || {}) : capture.status
  readonly property var recording: root.fixtureMode ? ((root.model.capture || {}).recording || null) : capture.recording
  readonly property var replay: root.fixtureMode ? ((root.model.capture || {}).replay || null) : capture.replay
  readonly property bool volumeAvailable: root.fixtureMode ? (root.model.audio || {}).volume !== undefined : audio.available
  readonly property real volume: root.fixtureMode ? Number((root.model.audio || {}).volume || 0) : audio.volume
  readonly property bool muted: root.fixtureMode ? !!(root.model.audio || {}).muted : audio.muted
  readonly property var outputs: root.fixtureMode ? ((root.model.audio || {}).outputs || []) : audio.outputs
  readonly property var currentOutput: root.outputs.filter(function(o) { return o.current })[0] || root.outputs[0] || null
  readonly property var achievements: (root.game && root.game.achievements) || null
  readonly property bool hasAchievements: !!(root.achievements && root.achievements.total > 0)
  // Omakade sends changed fields after the initial payload; the list and the rows are
  // only rebuilt when what they show changed, so scrolling and hover hold.
  property var achievementItems: []
  property string achievementsJson: "[]"
  onAchievementsChanged: {
    var items = (root.achievements && root.achievements.items) || []
    var json = JSON.stringify(items)
    if (json !== root.achievementsJson) { root.achievementsJson = json; root.achievementItems = items }
  }
  readonly property var padLevels: root.fixtureMode ? (root.model.pads || []) : pads.levels

  // The card's controls as rows of keys, top to bottom (GuideFocus.js): the
  // tiles, achievements, volume, sound output, then Resume and Quit. Every
  // control shown takes the cursor; `rows` is all of them.
  readonly property var grid: Focus.grid({game: !!root.game, replay: !!root.replay, achievements: root.hasAchievements,
                                          volume: root.volumeAvailable, outputs: root.outputs.length})
  readonly property var rows: [].concat.apply([], root.grid)
  // Where across the card the cursor is (0..1), kept through one-item rows.
  property real focusAnchor: Focus.homeAnchor
  // The card's width in Omarchy units at scale 1: room for five tiles.
  readonly property int cardWidth: 480
  readonly property int replaySeconds: (root.replay && root.replay.seconds) || 30

  // ------------------------------------------------------------ colours and type

  readonly property string fontFamily: Style.font.menuFamily
  readonly property color text: Commons.Color.menu.text
  readonly property color background: Commons.Color.menu.background
  // The menu's colours, held to the release bar: 4.5:1 for text and 3:1 for
  // the focus edge and the recording mark, over a dark and a light frame.
  // Secondary text is Omarchy's 0.52, raised only where a theme needs it.
  readonly property var cardGrounds: Legibility.grounds(Commons.Color.menu.background)
  readonly property var fillGrounds: Legibility.grounds(Commons.Color.menu.selectedBackground, root.cardGrounds)
  readonly property color quiet: root.solid(Legibility.fade(root.text, root.background, root.cardGrounds, 0.52, 4.6))
  readonly property color selectedInk: root.solid(Legibility.legibleInk(Commons.Color.menu.selectedText, root.text, root.fillGrounds, 4.6))
  readonly property color urgentInk: root.solid(Legibility.legibleInk(Commons.Color.urgent, root.text, root.fillGrounds, 4.6))
  readonly property color recordingInk: root.solid(Legibility.legibleInk(Commons.Color.bar.active, root.text, root.cardGrounds.concat(root.fillGrounds), 3))
  // Where the selected fill alone is too close to the card to see at 3:1, the
  // row also takes a hairline in the theme's focus colour.
  readonly property bool needsEdge: Legibility.weakest(root.fillGrounds, root.cardGrounds) < 3
  readonly property color focusEdge: root.solid(Legibility.fade(Style.focusStateColor(root.text, Commons.Color.accent, Commons.Color.urgent), root.background, root.cardGrounds, 0.25, 3))

  function solid(c) { return Qt.rgba(c.r, c.g, c.b, 1) }

  // Material Design glyphs from the Nerd Font: one set, one weight.
  readonly property var icons: ({
    resume: "\u{f040a}", desktop: "\u{f0379}", library: "\u{f0570}", screenshot: "\u{f0100}",
    record: "\u{f044a}", replay: "\u{f02da}", achievements: "\u{f0538}", volume: "\u{f057e}",
    volumeOff: "\u{f0581}", speaker: "\u{f04c3}", headphones: "\u{f02cb}", quit: "\u{f0343}",
    stop: "\u{f04db}", gamepad: "\u{f0297}"
  })

  // The readings the designs show as labelled numbers; missing ones are left out.
  function known(v) { return v !== undefined && v !== null && isFinite(Number(v)) }
  function loadReadout(label, load, temp) {
    if (!root.known(load) && !root.known(temp)) return null
    if (!root.known(load)) return {label: label, value: Math.round(temp) + "°", unit: ""}
    return {label: label, value: Math.round(load) + "%", unit: root.known(temp) ? " " + Math.round(temp) + "°" : ""}
  }
  readonly property var readouts: [
    root.known(root.performance.fps) ? {label: "FPS", value: String(Math.round(root.performance.fps)), unit: ""} : null,
    root.known(root.performance.frametime) ? {label: "FRAME", value: Number(root.performance.frametime).toFixed(1), unit: " ms"} : null,
    root.loadReadout("CPU", root.stats.cpu, root.stats.cpuTemp),
    root.loadReadout("GPU", root.stats.gpu, root.stats.gpuTemp)
  ].filter(function(r) { return r !== null })

  // ------------------------------------------------------------ backend socket

  Socket {
    id: service
    parser: SplitParser {
      onRead: function(line) {
        try {
          var m = JSON.parse(line)
          if (m.type === "input") {
            root.input(m.action)
            root.notify("input-ack", {receivedNs: m.receivedNs, cursor: root.cursor})
          } else if (m.type === "update") root.update(JSON.stringify(m.payload))
          else if (m.type === "toast") root.notice(m.title || "", m.detail || "")
        } catch (e) { console.warn("omakade.guide: socket message:", e) }
      }
    }
    path: root.backend ? root.backend.socket : ""
    connected: !!root.backend
    onConnectedChanged: {
      if (connected) {
        root.backendQueue.forEach(function(message) { service.write(message) })
        service.flush()
        root.backendQueue = []
      } else if (root.backend) {
        root.backend = null
        root.backendQueue = []
        root.model = ({})
        if (root.opened) root.close()
      }
    }
  }

  function notify(name, value) {
    if (!root.backend) return
    var message = JSON.stringify({version: 1, token: root.backend.token, action: name, value: value}) + "\n"
    if (service.connected) { service.write(message); service.flush() }
    else root.backendQueue.push(message)
  }

  // Omakade's messages for the player go to Omarchy's notifications.
  function notice(title, detail) {
    if (!title) return
    Quickshell.execDetached(["omarchy-notification-send", title].concat(detail ? [detail] : []))
  }

  function update(json) {
    var p = Protocol.update(json, root.model)
    if (!p) return "invalid"
    if (root.backend && p.backend && root.backend.token !== p.backend.token) return "stale"
    root.fixtureMode = false
    fixtureFile.path = ""
    root.model = p.data
    root.family = p.pad
    root.outputName = p.output
    if (p.scale !== undefined) root.setCouchScale(p.scale)
    return "ok"
  }

  // Ready means seen and usable: the surface has its real size and the card
  // has finished fading in.
  function ready() {
    return root.presented && window.width > 1 && window.height > 1 && card.opacity === 1 && !fadeIn.running ? "ready" : "waiting"
  }

  function state(unused) {
    return JSON.stringify({scale: root.couchScale, zoom: root.zoom, opened: root.opened, opening: root.opened && !root.presented, presented: root.presented, ready: root.ready(),
      token: root.backend ? root.backend.token : "", socket: root.backend ? root.backend.socket : "",
      output: window.screen ? window.screen.name : "", surface: [window.width, window.height],
      cursor: root.cursor, rows: root.rows, view: root.view, confirming: root.confirming, confirmChoice: root.confirmChoice, achIndex: root.achIndex, pad: root.family, data: root.model,
      geometry: root.geometry(), palette: {text: String(Commons.Color.menu.text), quiet: String(root.quiet),
        background: String(Commons.Color.menu.background), selectedBackground: String(Commons.Color.menu.selectedBackground),
        selectedText: String(Commons.Color.menu.selectedText), urgent: String(Commons.Color.urgent), border: String(Commons.Color.menu.border),
        recording: String(root.recordingInk), selectedInk: String(root.selectedInk), urgentInk: String(root.urgentInk),
        focusEdge: root.needsEdge ? String(root.focusEdge) : ""}})
  }

  // Where the card and its rows are on the surface, for layout checks.
  function geometry() {
    var box = function(item) {
      if (!item || !item.visible) return null
      var p = item.mapToItem(surface, 0, 0)
      return [p.x, p.y, item.width, item.height]
    }
    var rowBoxes = {}, iconBoxes = {}, truncated = []
    Object.keys(root.controls).forEach(function(key) {
      var item = root.controls[key]
      if (!item || !item.visible) return
      rowBoxes[key] = box(item)
      iconBoxes[key] = box(item.iconItem)
      if (item.truncated) truncated.push(item.label || key)
    })
    if (content.titleTruncated) truncated.push("title")
    return {card: box(card), rows: rowBoxes, icons: iconBoxes,
      fits: card.height >= card.contentTopInset + content.implicitHeight + card.contentBottomInset
        && card.y >= 0 && card.y + card.height <= window.height && card.width <= window.width,
      truncated: truncated, list: box(content.achievementList), readouts: content.readoutCount, lastAct: root.lastAct}
  }

  // The card's controls by key, as they register themselves (Focusable.qml).
  property var controls: ({})
  function register(key, item) { root.controls[key] = item }
  function unregister(key, item) { if (root.controls[key] === item) delete root.controls[key] }

  // ------------------------------------------------------------ lifecycle

  function open(payloadJson) {
    var p = {}
    try { p = JSON.parse(payloadJson || "{}") || {} } catch (e) { p = {} }
    if (p.version !== undefined) {
      if (!Protocol.parse(payloadJson)) { console.warn("omakade.guide: invalid payload"); return }
      root.backend = p.backend || null
      root.update(payloadJson)
    } else if (p.fixture) {
      root.backend = null
      root.fixtureMode = true
      root.fixturePad = p.pad || ""
      fixtureFile.path = ""
      fixtureFile.path = p.fixture
    } else if (!root.opened) {
      root.backend = null
      root.fixtureMode = false
      root.model = ({})
      root.outputName = ""
      root.family = "keyboard"
    }
    if (p.pad) root.family = p.pad
    if (p.version === undefined) root.setCouchScale(p.scale)
    if (root.opened) return
    root.now = new Date()
    root.cursor = Focus.home(root.grid)
    root.focusAnchor = Focus.homeAnchor
    root.view = p.confirm ? "confirm" : "main"
    root.confirmChoice = 0
    root.achIndex = 0
    root.quitting = false
    root.capturing = false
    root.lastAct = ""
    root.backAt = 0
    pointerGate.reset()
    window.targetScreen = root.gameScreen()
    root.opened = true
    root.sizeAttempts = 0
    root.showSurface()
  }

  function close() {
    var was = root.opened
    root.opened = false
    root.presented = false
    root.view = "main"
    root.quitting = false
    root.capturing = false
    root.captureKind = ""
    shutter.stop()
    root.surfaceShown = false
    card.opacity = 0
    fadeIn.stop()
    if (was) root.notify("closed", null)
  }

  function setCouchScale(value) {
    var n = Number(value)
    root.couchScale = isFinite(n) && n > 0 ? Math.max(1, Math.min(3, n)) : 1
    root.fitZoom()
  }

  // Lower the zoom until the card fits: its size is proportional to the zoom,
  // so one measurement says how far.
  function fitZoom() {
    var natural = (card.contentTopInset + content.implicitHeight + card.contentBottomInset) / root.zoom
    var room = Math.min((window.height - Style.gapsOut * 2) / natural,
                        (window.width - Style.gapsOut * 2) / Style.space(root.cardWidth))
    var next = window.height > 1 ? Math.min(root.couchScale, Math.floor(room * 20) / 20) : root.couchScale
    next = Math.max(0.5, next)
    if (Math.abs(next - root.zoom) > 0.001) root.zoom = next
  }

  function gameScreen() {
    for (var i = 0; i < Quickshell.screens.length; i++)
      if (Quickshell.screens[i].name === root.outputName) return Quickshell.screens[i]
    return window.focusedScreen() || window.targetScreen || Quickshell.screens[0] || null
  }

  FileView {
    id: fixtureFile
    path: ""
    onLoaded: {
      if (!root.fixtureMode) return
      // "@/" in a fixture is the preview folder, where its pictures live.
      var base = String(path).replace(/\/[^\/]*\/[^\/]*$/, "/")
      try { root.model = JSON.parse(text().replace(/"@\//g, '"file://' + base)) } catch (e) { console.warn("omakade.guide: bad fixture", e) }
      root.family = root.fixturePad || root.model.pad || "keyboard"
      // A fixture stands for a fresh open: the cursor starts on the first row.
      // A fixture opens on Resume too, unless it names a control to show focused.
      root.cursor = root.rows.indexOf(root.model.cursor) >= 0 ? root.model.cursor : Focus.home(root.grid)
      root.focusAnchor = Focus.homeAnchor
      root.achIndex = root.model.achIndex || 0
      root.view = root.model.confirm ? "confirm" : root.model.view === "achievements" && root.hasAchievements ? "achievements" : "main"
    }
  }

  // ------------------------------------------------------------ surface sizing
  //
  // Hiding parks the overlay at 1x1 asynchronously. A summon that lands before
  // that resize raced the two and could leave an open guide at 1x1, so the
  // surface is only shown again once it has parked, and an open surface that
  // still has no size is asked again.

  property bool surfaceShown: false
  property int sizeAttempts: 0

  function showSurface() {
    // Newer Omarchy maps a fresh window and exposes contentReady. Older shells
    // park at 1x1, so keep their configure handoff without delaying a fresh map.
    if (window.contentReady !== undefined || (window.width <= 1 && window.height <= 1)) { root.surfaceShown = true; return }
    parkWait.restart()
  }

  Timer {
    id: parkWait
    interval: 150
    onTriggered: if (root.opened) root.surfaceShown = true
  }

  Connections {
    target: window
    ignoreUnknownSignals: true
    function onContentReadyChanged() { root.surfaceSized() }
    function onWidthChanged() { root.surfaceSized(); Qt.callLater(root.fitZoom) }
    function onHeightChanged() { root.surfaceSized(); Qt.callLater(root.fitZoom) }
  }

  function surfaceSized() {
    var sized = window.width > 1 && window.height > 1 && (window.contentReady === undefined || window.contentReady)
    // Not from inside the resize itself: a show requested while the 1x1
    // configure is being applied is never answered with a full-size one.
    if (root.opened && !root.surfaceShown && !sized) { parkWait.stop(); Qt.callLater(root.showParked) }
    else if (root.opened && root.surfaceShown && sized && !root.presented) root.present()
  }

  function showParked() {
    if (root.opened && !root.surfaceShown) root.surfaceShown = true
  }

  function present() {
    root.presented = true
    sizeWatch.stop()
    keys.forceActiveFocus()
    card.opacity = 0
    fadeIn.restart()
    root.notify("opened", null)
  }

  onSurfaceShownChanged: {
    if (!surfaceShown) return
    if (window.width > 1 && window.height > 1 && (window.contentReady === undefined || window.contentReady)) root.present()
    else sizeWatch.restart()
  }

  Timer {
    id: sizeWatch
    interval: 250
    onTriggered: {
      if (!root.opened || root.presented) return
      if (++root.sizeAttempts >= 8) {
        root.notify("surface-failed", null)
        root.close()
        return
      }
      root.surfaceShown = false
      Qt.callLater(function() { if (root.opened) root.surfaceShown = true })
    }
  }

  // ------------------------------------------------------------ navigation

  // One D-pad step over the card's grid (GuideFocus.js). False where left or
  // right belong to the control under the cursor (volume, sound output).
  function move(action) {
    var step = Focus.move(root.grid, root.cursor, action, root.focusAnchor)
    if (!step) return false
    root.cursor = step.key
    root.focusAnchor = step.anchor
    return true
  }

  function input(action) {
    if (action.charAt(0) === "{") {
      try { var event = JSON.parse(action); root.input(event.action); root.notify("input-ack", {receivedNs: event.receivedNs}); return "ok" } catch (e) { return "invalid" }
    }
    if (!root.opened) {
      if (action === "guide") root.open("{}")
      return "closed"
    }
    if (root.rows.indexOf(root.cursor) < 0) root.cursor = Focus.home(root.grid)
    pointerGate.reset()
    if (root.view === "confirm") {
      if (["up", "down", "left", "right"].indexOf(action) >= 0) root.confirmChoice = root.confirmChoice === 0 ? 1 : 0
      else if (action === "a") { if (root.confirmChoice === 0) root.cancelQuit(); else root.quitGame() }
      else if (action === "b") root.cancelQuit()
      else if (action === "guide" || action === "start") root.close()
      return "ok"
    }
    if (root.view === "achievements") {
      var last = Math.max(0, root.achievementItems.length - 1)
      if (action === "up") root.achIndex = Math.max(0, root.achIndex - 1)
      else if (action === "down") root.achIndex = Math.min(last, root.achIndex + 1)
      else if (action === "left") root.achIndex = Math.max(0, root.achIndex - 5)
      else if (action === "right") root.achIndex = Math.min(last, root.achIndex + 5)
      else if (action === "b") root.back()
      else if (action === "guide" || action === "start") root.close()
      else if (action === "y") root.activate("screenshot")
      return "ok"
    }
    switch (action) {
    case "up": case "down": root.move(action); break
    case "left": case "right":
      if (root.move(action)) break
      var step = action === "left" ? -1 : 1
      if (root.cursor === "volume") root.act("volume", root.volume + step * 0.05)
      else if (root.cursor === "output") root.act("output", step)
      break
    case "a": root.activate(root.cursor); break
    case "b": if (Date.now() - root.backAt > 300) root.close(); break
    case "guide": case "start": root.close(); break
    case "y": root.activate("screenshot"); break
    }
    return "ok"
  }

  // What each row shows.
  function rowSpec(key) {
    switch (key) {
    // The play triangle draws a third less ink than its neighbours.
    case "resume": return {icon: root.icons.resume, iconScale: 1.3, label: "Resume"}
    case "desktop": return {icon: root.icons.desktop, label: "Return to desktop"}
    case "library": return {icon: root.icons.library, label: "Game library"}
    case "screenshot": return {icon: root.icons.screenshot, label: "Screenshot"}
    // The bar's recording colour while a clip runs.
    case "record": return root.recording ? {icon: root.icons.record, iconColor: root.recordingInk, label: "Stop recording", value: root.recordingTime}
                                         : {icon: root.icons.record, label: "Record clip"}
    case "replay": return {icon: root.icons.replay, label: "Save last " + ((root.replay && root.replay.seconds) || 30) + " s"}
    case "achievements": return {icon: root.icons.achievements, label: "Achievements",
                                 value: (root.achievements.unlocked || 0) + "/" + root.achievements.total}
    case "volume": return {icon: root.muted ? root.icons.volumeOff : root.icons.volume, label: "Volume",
                           value: root.muted ? "Muted" : Math.round(root.volume * 100) + "%"}
    case "output":
      var name = root.currentOutput ? root.currentOutput.name : "Output"
      // Which of how many: says the row cycles, as Achievements says how far.
      return {icon: /head|ear|bud|airpod/i.test(name) ? root.icons.headphones : root.icons.speaker,
              label: name, wrap: true, value: (root.outputs.indexOf(root.currentOutput) + 1) + "/" + root.outputs.length}
    case "quit": return {icon: root.icons.quit, urgent: true,
                         label: root.forceReady ? "Force quit" : root.quitting ? "Closing game\u2026" : "Quit game"}
    }
    return {icon: "", label: key}
  }

  // Out of a view and back to the rows, on the row that opened it.
  function back() {
    root.view = "main"
    root.backAt = Date.now()
  }

  function keyAction(event) {
    switch (event.key) {
    case Qt.Key_Up: return "up"
    case Qt.Key_Down: return "down"
    case Qt.Key_Left: return "left"
    case Qt.Key_Right: return "right"
    case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: return "a"
    case Qt.Key_Escape: return "b"
    case Qt.Key_Y: return "y"
    case Qt.Key_G: case Qt.Key_Home: return "guide"
    }
    return ""
  }

  // ------------------------------------------------------------ actions

  function activate(key) {
    switch (key) {
    case "resume": root.close(); break
    case "desktop": root.act("desktop"); break
    case "library": root.act("library"); break
    case "achievements": root.view = "achievements"; break
    case "output": root.act("output", 1); break
    case "screenshot": root.act("screenshot"); break
    case "record": root.act("record"); break
    case "replay": root.act("save-replay"); break
    case "volume": root.act("mute"); break
    case "quit":
      if (root.quitting && !root.forceReady) break
      root.confirmChoice = 0
      root.view = "confirm"
      break
    }
  }

  // The last action a fixture asked for, for checks (fixture mode only).
  property string lastAct: ""

  function act(name, value) {
    if (root.fixtureMode) {
      console.log("GUIDE_ACT " + name + " " + JSON.stringify(value === undefined ? null : value))
      root.lastAct = name
      if (name === "volume") root.setFixture("audio", Object.assign({}, root.model.audio, {volume: Math.max(0, Math.min(1, value)), muted: root.muted}))
      else if (name === "mute") root.setFixture("audio", Object.assign({}, root.model.audio, {volume: root.volume, muted: !root.muted}))
      else if (name === "output" && root.outputs.length > 1) {
        var list = root.outputs, at = Math.max(0, list.indexOf(root.currentOutput))
        var next = (at + value + list.length) % list.length
        root.setFixture("audio", Object.assign({}, root.model.audio, {outputs: list.map(function(o, i) { return {name: o.name, current: i === next} })}))
      } else if (name === "record" || name === "desktop" || name === "library") root.close()
      return
    }
    switch (name) {
    case "screenshot":
      root.beginCapture("screenshot")
      break
    case "record":
      if (!root.recording) root.beginCapture("record")
      else capture.toggleRecording()
      break
    case "save-replay": capture.saveReplay(); break
    // Omakade parks the game on the desktop, or opens its library, and closes
    // the guide itself.
    case "desktop": case "library":
      if (root.backend) root.notify(name, null)
      else root.close()
      break
    case "output": audio.cycleOutput(Number(value) < 0 ? -1 : 1); break
    case "volume": audio.setVolume(Number(value)); break
    case "mute": audio.toggleMute(); break
    }
  }

  function setFixture(key, value) {
    var copy = JSON.parse(JSON.stringify(root.model))
    copy[key] = value
    root.model = copy
  }

  function beginCapture(kind) {
    if (root.capturing) return
    root.captureKind = kind
    root.hiddenFrame = false
    root.captureDelayPassed = false
    root.capturing = true
    shutter.restart()
  }

  function captureAfterFrame() {
    if (!root.capturing || !root.hiddenFrame || !root.captureDelayPassed || !root.opened) return
    var kind = root.captureKind
    root.captureKind = ""
    if (kind === "record") {
      // The mapped surface has already submitted a frame without card or scrim.
      root.close()
      capture.toggleRecording()
    } else capture.screenshot(function() { if (root.captureKind === "") root.capturing = false })
  }

  Connections {
    target: surface.Window.window
    function onFrameSwapped() {
      if (root.capturing && root.captureKind !== "") {
        root.hiddenFrame = true
        root.captureAfterFrame()
      }
    }
  }

  Timer {
    id: shutter
    interval: 80
    onTriggered: { root.captureDelayPassed = true; root.captureAfterFrame() }
  }

  function cancelQuit() {
    root.back()
    root.confirmChoice = 0
  }

  function quitGame() {
    root.view = "main"
    if (root.fixtureMode || !root.backend) { root.lastAct = "quit-confirmed"; root.close(); return }
    if (root.forceReady) { root.notify("force-quit", null); return }
    root.quitting = true
    root.notify("quit-confirmed", null)
  }

  // ------------------------------------------------------------ text

  function duration(seconds) {
    seconds = Math.max(0, Math.floor(seconds))
    var h = Math.floor(seconds / 3600), m = Math.floor(seconds % 3600 / 60), s = seconds % 60
    var two = function(n) { return (n < 10 ? "0" : "") + n }
    return (h > 0 ? h + ":" + two(m) : two(m)) + ":" + two(s)
  }

  function sessionText() {
    var parts = []
    if (!root.game) {
      if (root.padLevels.length) parts.push((root.padLevels.length > 1 ? "Pads " : "Pad ") + root.padLevels.map(function(p) { return p + "%" }).join(" "))
      return parts.join(" · ")
    }
    var minutes = root.game.sessionMinutes
    if (minutes !== undefined && minutes !== null) {
      parts.push(minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min" : minutes + " min")
    }
    if (root.game.paused) parts.push("Paused")
    var pads = root.padLevels
    if (pads.length) parts.push((pads.length > 1 ? "Pads " : "Pad ") + pads.map(function(p) { return p + "%" }).join(" "))
    return parts.join(" · ")
  }

  readonly property string recordingTime: {
    var r = root.recording
    if (!r) return ""
    if (r.seconds !== undefined) return root.duration(r.seconds)
    return r.started ? root.duration(root.now.getTime() / 1000 - r.started) : ""
  }

  // The bar's clock says whether this desk reads 12 or 24 hours.
  property bool twelveHour: false
  FileView {
    path: Quickshell.env("HOME") + "/.config/omarchy/shell.json"
    printErrors: false
    onLoaded: {
      try {
        var sections = (JSON.parse(text()).bar || {}).layout || {}
        for (var key in sections) (sections[key] || []).forEach(function(entry) {
          if (entry && entry.id === "omarchy.clock" && entry.format)
            root.twelveHour = /h/.test(entry.format) && !/H/.test(entry.format)
        })
      } catch (e) {}
    }
  }
  readonly property string clock: root.model.clock || Qt.formatTime(root.now, root.twelveHour ? "h:mm AP" : "HH:mm")

  Timer {
    interval: 1000
    running: root.opened
    repeat: true
    onTriggered: root.now = new Date()
  }

  // Printed button names for the connected pad, by position: south, east, north.
  readonly property var buttons: ({
    keyboard: ["Enter", "Esc", "Y"],
    playstation: ["✕", "○", "△"],
    nintendo: ["B", "A", "X"]
  })[root.family] || ["A", "B", "Y"]

  // Button names in the text colour, what they do quiet, as in the readings.
  readonly property var hint: {
    var b = root.buttons, list = []
    var leave = root.game ? "resume" : "close"
    if (root.view === "confirm") list.push([b[0], "select"], [b[1], "keep playing"])
    else if (root.view === "achievements") list.push(["\u2191\u2193", "scroll"], ["\u2190\u2192", "page"], [b[1], "back"])
    else if (root.cursor === "volume") list.push([b[0], root.muted ? "unmute" : "mute"], ["\u2190\u2192", "volume"], [b[1], leave])
    else if (root.cursor === "output") list.push([b[0], "next"], ["\u2190\u2192", "output"], [b[1], leave])
    else list.push([b[0], "select"], [b[1], leave], [b[2], "screenshot"])
    return list
  }

  // ------------------------------------------------------------ IPC

  IpcHandler {
    target: "omakade.guide"
    function open(payload: string): void { root.open(payload) }
    function close(): void { root.close() }
    function input(action: string): string { return root.input(action) }
    function act(name: string, value: string): void { root.act(name, JSON.parse(value || "null")) }
    function ready(): string { return root.ready() }
  }

  LiveCapture {
    id: capture
    active: root.opened && !root.fixtureMode
    output: root.gameScreen() ? root.gameScreen().name : ""
  }

  LiveAudio {
    id: audio
    active: root.opened && !root.fixtureMode
  }

  LivePads { id: pads }

  // Measured against the full-screen layer, which never moves: a card that
  // grows under a resting pointer is not the pointer moving.
  PointerMoveGate {
    id: pointerGate
    referenceItem: surface
  }

  // ------------------------------------------------------------ surface

  OverlayWindow {
    id: window
    shown: root.surfaceShown
    // The payload names the game's output; follow it rather than the focus.
    onShownChanged: if (shown) targetScreen = root.gameScreen()
    WlrLayershell.namespace: "omakade-guide"

    Item {
      id: surface
      anchors.fill: parent
      visible: !root.capturing

      Rectangle {
        anchors.fill: parent
        color: Commons.Color.menu.scrim
      }

      MouseArea {
        anchors.fill: parent
        onClicked: root.close()
      }

      Item {
        id: keys
        focus: true
        Keys.onPressed: function(event) {
          var a = root.keyAction(event)
          if (a === "") return
          if (root.family !== "keyboard") { root.family = "keyboard"; root.notify("input-family", "keyboard") }
          root.input(a)
          event.accepted = true
        }
      }

      BorderSurface {
        id: card
        readonly property int pad: root.sized(Style.spacing.panelPadding)
        width: Math.min(root.sized(Style.space(root.cardWidth)), window.width - Style.gapsOut * 2)
        height: Math.min(card.contentTopInset + content.implicitHeight + card.contentBottomInset, window.height - Style.gapsOut * 2)
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(Style.gapsOut, Math.round((window.height - card.height) / 2))
        radius: root.sized(Style.cornerRadius)
        color: Commons.Color.menu.background
        borderSpec: Border.surfaceSpec("menu", "border", Commons.Color.menu.border, Math.max(1, Style.space(2)))
        padding: card.pad
        opacity: 0

        NumberAnimation on opacity {
          id: fadeIn
          running: false
          from: 0
          to: 1
          duration: Style.duration(140)
          easing.type: Easing.OutCubic
        }

        MouseArea { anchors.fill: parent }

        GuideCard {
          id: content
          g: root
          onImplicitHeightChanged: Qt.callLater(root.fitZoom)
          x: card.contentLeftInset
          y: card.contentTopInset
          width: card.width - card.contentLeftInset - card.contentRightInset
        }
      }
    }
  }

  function hover(key, source, mouse) {
    if (root.view !== "main" || !pointerGate.moved(source, mouse)) return
    root.cursor = key
  }

  function hoverConfirm(choice, source, mouse) {
    if (root.view === "confirm" && pointerGate.moved(source, mouse)) root.confirmChoice = choice
  }

  function hoverAchievement(index, source, mouse) {
    if (root.view === "achievements" && pointerGate.moved(source, mouse)) root.achIndex = index
  }
}
