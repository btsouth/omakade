import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "components"
import "GuideProtocol.js" as Protocol
import "Legibility.js" as Legibility

// The in-game guide: one card over the paused game, built like Omarchy's menu.
// Header with the game and the time, one line of performance readings, then
// Resume, capture, volume and Quit. Input arrives as actions (up, down, left,
// right, a, b, y, guide) from the keyboard or, for controllers, from Omakade's
// service over the guide socket.
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
  // Omakade resends the whole payload every second; the list and the rows are
  // only rebuilt when what they show changed, so scrolling and hover hold.
  property var achievementItems: []
  property string achievementsJson: "[]"
  onAchievementsChanged: {
    var items = (root.achievements && root.achievements.items) || []
    var json = JSON.stringify(items)
    if (json !== root.achievementsJson) { root.achievementsJson = json; root.achievementItems = items }
  }
  property var rowLayout: []
  onLayoutChanged: if (JSON.stringify(root.layout) !== JSON.stringify(root.rowLayout)) root.rowLayout = root.layout
  readonly property var padLevels: root.fixtureMode ? (root.model.pads || []) : pads.levels

  // The card's rows in groups, top to bottom; a divider runs between groups.
  // Every row shown takes the cursor.
  readonly property var groups: [
    root.game ? ["resume", "desktop", "library"] : [],
    ["screenshot", "record"].concat(root.replay ? ["replay"] : []),
    root.hasAchievements ? ["achievements"] : [],
    (root.volumeAvailable ? ["volume"] : []).concat(root.outputs.length > 1 ? ["output"] : []),
    root.game ? ["quit"] : []
  ].filter(function(g) { return g.length > 0 })
  readonly property var rows: [].concat.apply([], root.groups)
  // The rows with the divider each one opens, for the card's repeater.
  readonly property var layout: {
    var list = []
    root.groups.forEach(function(g, gi) { g.forEach(function(key, ki) { list.push({key: key, divider: gi > 0 && ki === 0}) }) })
    return list
  }

  // ------------------------------------------------------------ colours and type

  readonly property string fontFamily: Style.font.menuFamily
  readonly property color text: Color.menu.text
  readonly property color background: Color.menu.background
  // The menu's colours, held to the release bar: 4.5:1 for text and 3:1 for
  // the focus edge and the recording mark, over a dark and a light frame.
  // Secondary text is Omarchy's 0.52, raised only where a theme needs it.
  readonly property var cardGrounds: Legibility.grounds(Color.menu.background)
  readonly property var fillGrounds: Legibility.grounds(Color.menu.selectedBackground, root.cardGrounds)
  readonly property color quiet: root.solid(Legibility.fade(root.text, root.background, root.cardGrounds, 0.52, 4.6))
  readonly property color selectedInk: root.solid(Legibility.legibleInk(Color.menu.selectedText, root.text, root.fillGrounds, 4.6))
  readonly property color urgentInk: root.solid(Legibility.legibleInk(Color.urgent, root.text, root.fillGrounds, 4.6))
  readonly property color recordingInk: root.solid(Legibility.legibleInk(Color.bar.active, root.text, root.cardGrounds.concat(root.fillGrounds), 3))
  // Where the selected fill alone is too close to the card to see at 3:1, the
  // row also takes a hairline in the theme's focus colour.
  readonly property bool needsEdge: Legibility.weakest(root.fillGrounds, root.cardGrounds) < 3
  readonly property color focusEdge: root.solid(Legibility.fade(Style.focusStateColor(root.text, Color.accent, Color.urgent), root.background, root.cardGrounds, 0.25, 3))

  function solid(c) { return Qt.rgba(c.r, c.g, c.b, 1) }

  // Material Design glyphs from the Nerd Font: one set, one weight.
  readonly property var icons: ({
    resume: "\u{f040a}", desktop: "\u{f0379}", library: "\u{f0570}", screenshot: "\u{f0100}",
    record: "\u{f044a}", replay: "\u{f02da}", achievements: "\u{f0538}", volume: "\u{f057e}",
    volumeOff: "\u{f0581}", speaker: "\u{f04c3}", headphones: "\u{f02cb}", quit: "\u{f0343}"
  })

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
    var p = Protocol.parse(json)
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
    return JSON.stringify({opened: root.opened, opening: root.opened && !root.presented, presented: root.presented, ready: root.ready(),
      token: root.backend ? root.backend.token : "", socket: root.backend ? root.backend.socket : "",
      output: window.screen ? window.screen.name : "", surface: [window.width, window.height],
      cursor: root.cursor, rows: root.rows, view: root.view, confirming: root.confirming, confirmChoice: root.confirmChoice, achIndex: root.achIndex, pad: root.family, data: root.model,
      geometry: root.geometry(), palette: {text: String(Color.menu.text), quiet: String(root.quiet),
        background: String(Color.menu.background), selectedBackground: String(Color.menu.selectedBackground),
        selectedText: String(Color.menu.selectedText), urgent: String(Color.urgent), border: String(Color.menu.border),
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
    for (var i = 0; i < rowRepeater.count; i++) {
      var slot = rowRepeater.itemAt(i)
      if (!slot || !slot.row.visible) continue
      rowBoxes[slot.key] = box(slot.row)
      iconBoxes[slot.key] = box(slot.row.iconItem)
      if (slot.row.truncated) truncated.push(slot.row.label)
    }
    var confirmRows = [keepRow, quitConfirmRow]
    confirmRows.forEach(function(r, n) {
      if (!r.visible) return
      var key = n === 0 ? "keep" : "confirm-quit"
      rowBoxes[key] = box(r); iconBoxes[key] = box(r.iconItem)
      if (r.truncated) truncated.push(r.label)
    })
    return {card: box(card), rows: rowBoxes, icons: iconBoxes,
      fits: card.height >= card.contentTopInset + content.implicitHeight + card.contentBottomInset,
      truncated: truncated, list: box(achievementList),
      perfLines: perf.visible ? perf.lines : 0}
  }

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
    root.cursor = root.rows[0]
    root.view = p.confirm ? "confirm" : "main"
    root.confirmChoice = 0
    root.achIndex = 0
    root.quitting = false
    root.capturing = false
    root.backAt = 0
    pointerGate.reset()
    window.targetScreen = root.gameScreen()
    root.opened = true
    root.showSurface()
  }

  function close() {
    var was = root.opened
    root.opened = false
    root.presented = false
    root.view = "main"
    root.quitting = false
    root.capturing = false
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
                        (window.width - Style.gapsOut * 2) / Style.space(380))
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
      root.cursor = root.rows.indexOf(root.model.cursor) >= 0 ? root.model.cursor : root.rows[0]
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

  function showSurface() {
    if (window.width <= 1 && window.height <= 1) { root.surfaceShown = true; return }
    parkWait.restart()
  }

  Timer {
    id: parkWait
    interval: 150
    onTriggered: if (root.opened) root.surfaceShown = true
  }

  Connections {
    target: window
    function onWidthChanged() { root.surfaceSized(); Qt.callLater(root.fitZoom) }
    function onHeightChanged() { root.surfaceSized(); Qt.callLater(root.fitZoom) }
  }

  function surfaceSized() {
    var sized = window.width > 1 && window.height > 1
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
    if (window.width > 1 && window.height > 1) root.present()
    else sizeWatch.restart()
  }

  Timer {
    id: sizeWatch
    interval: 250
    onTriggered: {
      if (!root.opened || root.presented) return
      root.surfaceShown = false
      Qt.callLater(function() { if (root.opened) root.surfaceShown = true })
    }
  }

  // ------------------------------------------------------------ navigation

  function move(delta) {
    var list = root.rows
    var i = list.indexOf(root.cursor)
    if (i < 0) { root.cursor = list[0]; return }
    root.cursor = list[(i + delta + list.length) % list.length]
  }

  function input(action) {
    if (action.charAt(0) === "{") {
      try { var event = JSON.parse(action); root.input(event.action); root.notify("input-ack", {receivedNs: event.receivedNs}); return "ok" } catch (e) { return "invalid" }
    }
    if (!root.opened) {
      if (action === "guide") root.open("{}")
      return "closed"
    }
    if (root.rows.indexOf(root.cursor) < 0) root.cursor = root.rows[0]
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
    case "up": root.move(-1); break
    case "down": root.move(1); break
    case "left": case "right":
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

  function act(name, value) {
    if (root.fixtureMode) {
      console.log("GUIDE_ACT " + name + " " + JSON.stringify(value === undefined ? null : value))
      if (name === "volume") root.setFixture("audio", {volume: Math.max(0, Math.min(1, value)), muted: root.muted})
      else if (name === "mute") root.setFixture("audio", {volume: root.volume, muted: !root.muted})
      else if (name === "output" && root.outputs.length > 1) {
        var list = root.outputs, at = Math.max(0, list.indexOf(root.currentOutput))
        var next = (at + value + list.length) % list.length
        root.setFixture("audio", Object.assign({}, root.model.audio, {outputs: list.map(function(o, i) { return {name: o.name, current: i === next} })}))
      } else if (name === "record" || name === "desktop" || name === "library") root.close()
      return
    }
    switch (name) {
    case "screenshot":
      if (root.capturing) return
      root.capturing = true
      // Let the compositor show a frame without the card before the capture.
      shutter.restart()
      break
    case "record":
      var starting = !root.recording
      capture.toggleRecording()
      // A new clip is of the game, not of the guide.
      if (starting) root.close()
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

  Timer {
    id: shutter
    interval: 80
    onTriggered: capture.screenshot(function() { root.capturing = false })
  }

  function cancelQuit() {
    root.back()
    root.confirmChoice = 0
  }

  function quitGame() {
    root.view = "main"
    if (root.fixtureMode || !root.backend) { root.close(); return }
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
        color: Color.menu.scrim
      }

      MouseArea {
        anchors.fill: parent
        onClicked: root.close()
      }

      BorderSurface {
        id: card
        readonly property int pad: root.sized(Style.spacing.panelPadding)
        // Header, readings and hint sit on the row fill's edges, as the menu's
        // title does; rows inset their own icon and value.
        readonly property int inset: 0
        width: Math.min(root.sized(Style.space(380)), window.width - Style.gapsOut * 2)
        height: Math.min(card.contentTopInset + content.implicitHeight + card.contentBottomInset, window.height - Style.gapsOut * 2)
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(Style.gapsOut, Math.round((window.height - card.height) / 2))
        radius: root.sized(Style.cornerRadius)
        color: Color.menu.background
        borderSpec: Border.surfaceSpec("menu", "border", Color.menu.border, Math.max(1, Style.space(2)))
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

        Column {
          id: content
          onImplicitHeightChanged: Qt.callLater(root.fitZoom)
          x: card.contentLeftInset
          y: card.contentTopInset
          width: card.width - card.contentLeftInset - card.contentRightInset

          // Header: the game, how long this session has run and whether it is
          // paused, and the time.
          Item {
            id: header
            x: card.inset
            width: parent.width - card.inset * 2
            height: Math.max(title.height, clockText.height) + (detail.visible ? root.sized(Style.space(2)) + detail.height : 0)

            Text {
              id: title
              textFormat: Text.PlainText
              width: Math.min(implicitWidth, parent.width - clockText.width - root.sized(Style.space(16)))
              text: root.game ? root.game.title : "No game running"
              color: root.text
              font.family: root.fontFamily
              font.pixelSize: root.sized(Style.font.heading)
              font.weight: Font.Medium
              wrapMode: Text.Wrap
            }

            Text {
              id: clockText
              textFormat: Text.PlainText
              anchors.right: parent.right
              anchors.baseline: title.baseline
              text: root.clock
              color: root.quiet
              font.family: root.fontFamily
              font.pixelSize: root.sized(Style.font.body)
            }

            Text {
              id: detail
              textFormat: Text.PlainText
              y: title.height + root.sized(Style.space(2))
              width: parent.width
              visible: text.length > 0
              text: root.sessionText()
              color: root.quiet
              font.family: root.fontFamily
              font.pixelSize: root.sized(Style.font.body)
              wrapMode: Text.Wrap
            }
          }

          Item { width: 1; height: root.sized(Style.spacing.md); visible: perf.visible }

          PerfLine {
            id: perf
            zoom: root.zoom
            x: card.inset
            width: parent.width - card.inset * 2
            text: root.text
            quiet: root.quiet
            fontFamily: root.fontFamily
            fps: root.performance.fps
            frametime: root.performance.frametime
            cpu: root.stats.cpu
            cpuTemp: root.stats.cpuTemp
            gpu: root.stats.gpu
            gpuTemp: root.stats.gpuTemp
          }

          Divider { zoom: root.zoom; color: root.text }

          // The card body: the rows, the achievements list or the quit question.
          // The list takes the rows' height, so opening it does not move the
          // card; the question takes its own.
          Item {
            id: body
            width: parent.width
            height: root.view === "confirm" ? question.implicitHeight : rowsColumn.implicitHeight

            Column {
              id: rowsColumn
              width: parent.width
              visible: root.view === "main"
              spacing: root.sized(Style.spacing.xs)

              Repeater {
                id: rowRepeater
                model: root.rowLayout

                Column {
                  id: slot
                  required property var modelData
                  readonly property string key: modelData.key
                  readonly property alias row: row
                  readonly property var spec: root.rowSpec(slot.key)
                  width: rowsColumn.width
                  spacing: root.sized(Style.spacing.xs)

                  Divider { zoom: root.zoom; color: root.text; visible: slot.modelData.divider }

                  GuideRow {
                    id: row
                    zoom: root.zoom
                    selectedInk: root.selectedInk
                    urgentInk: root.urgentInk
                    edge: root.needsEdge
                    edgeColor: root.focusEdge
                    width: parent.width
                    icon: slot.spec.icon
                    iconScale: slot.spec.iconScale || 1
                    iconColor: slot.spec.iconColor !== undefined ? slot.spec.iconColor : row.ink
                    label: slot.spec.label
                    value: slot.spec.value || ""
                    urgent: !!slot.spec.urgent
                    wrapLabel: !!slot.spec.wrap
                    slider: slot.key === "volume"
                    sliderValue: root.volume
                    sliderMuted: root.muted
                    current: root.cursor === slot.key
                    onHovered: (source, mouse) => root.hover(slot.key, source, mouse)
                    onActivated: { root.cursor = slot.key; root.activate(slot.key) }
                    onSliderMoved: v => { root.cursor = "volume"; root.act("volume", v) }
                  }
                }
              }
            }

            AchievementList {
              id: achievementList
              anchors.fill: parent
              visible: root.view === "achievements"
              items: root.achievementItems
              unlockedCount: root.achievements ? (root.achievements.unlocked || 0) : 0
              total: root.achievements ? (root.achievements.total || 0) : 0
              current: root.achIndex
              zoom: root.zoom
              fontFamily: root.fontFamily
              text: root.text
              quiet: root.quiet
              selectedInk: root.selectedInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              onHovered: (index, source, mouse) => { if (pointerGate.moved(source, mouse)) root.achIndex = index }
            }

            // The quit question, in the card's own rows.
            Column {
              id: question
              width: parent.width
              visible: root.view === "confirm"
              spacing: root.sized(Style.spacing.xs)

              Text {
                width: parent.width
                textFormat: Text.PlainText
                text: root.forceReady ? (root.game ? root.game.title : "The game") + " is not closing"
                  : "Quit " + (root.game ? root.game.title : "the game") + "?"
                color: root.text
                font.family: root.fontFamily
                font.pixelSize: root.sized(Style.font.heading)
                font.weight: Font.Medium
                wrapMode: Text.Wrap
              }
              Text {
                width: parent.width
                textFormat: Text.PlainText
                text: root.forceReady ? "Force quit ends it now. Unsaved progress is lost."
                  : "Progress since your last save may be lost."
                color: root.quiet
                font.family: root.fontFamily
                font.pixelSize: root.sized(Style.font.body)
                wrapMode: Text.Wrap
              }
              Item { width: 1; height: root.sized(Style.spacing.md) }

              GuideRow {
                id: keepRow
                zoom: root.zoom
                selectedInk: root.selectedInk
                urgentInk: root.urgentInk
                edge: root.needsEdge
                edgeColor: root.focusEdge
                width: parent.width
                icon: root.icons.resume
                iconScale: 1.3
                label: "Keep playing"
                current: root.confirmChoice === 0
                onHovered: (source, mouse) => { if (pointerGate.moved(source, mouse)) root.confirmChoice = 0 }
                onActivated: root.cancelQuit()
              }
              GuideRow {
                id: quitConfirmRow
                zoom: root.zoom
                selectedInk: root.selectedInk
                urgentInk: root.urgentInk
                edge: root.needsEdge
                edgeColor: root.focusEdge
                width: parent.width
                icon: root.icons.quit
                label: root.forceReady ? "Force quit" : "Quit game"
                urgent: true
                current: root.confirmChoice === 1
                onHovered: (source, mouse) => { if (pointerGate.moved(source, mouse)) root.confirmChoice = 1 }
                onActivated: root.quitGame()
              }
            }
          }

          // Room for the hint line.
          Item { width: 1; height: root.sized(Style.spacing.xl) + hintLine.height }
        }

        // What the buttons do.
        Flow {
          id: hintLine
          x: card.contentLeftInset + card.inset
          y: card.height - card.contentBottomInset - height
          width: content.width - card.inset * 2
          spacing: root.sized(Style.space(16))

          Repeater {
            model: root.hint
            Row {
              required property var modelData
              Text {
                textFormat: Text.PlainText
                text: modelData[0] + " "
                color: root.text
                font.family: root.fontFamily
                font.pixelSize: root.sized(Style.font.body)
              }
              Text {
                textFormat: Text.PlainText
                text: modelData[1]
                color: root.quiet
                font.family: root.fontFamily
                font.pixelSize: root.sized(Style.font.body)
              }
            }
          }
        }
      }
    }
  }

  function hover(key, source, mouse) {
    if (root.view !== "main" || !pointerGate.moved(source, mouse)) return
    root.cursor = key
  }
}
