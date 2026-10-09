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
  property bool confirming: false
  // Quit was confirmed and Omakade is waiting for the game to exit.
  property bool quitting: false
  // A second B straight after backing out of the confirmation must not also close.
  property double confirmClosedAt: 0
  // The card steps aside while a screenshot is taken.
  property bool capturing: false
  property date now: new Date()

  readonly property var game: root.model.game || null
  readonly property bool forceReady: !!(root.game && root.game.forceReady)
  readonly property var performance: root.model.performance || ({})
  readonly property var stats: root.fixtureMode ? (root.model.stats || {}) : capture.status
  readonly property var recording: root.fixtureMode ? ((root.model.capture || {}).recording || null) : capture.recording
  readonly property var replay: root.fixtureMode ? ((root.model.capture || {}).replay || null) : capture.replay
  readonly property bool volumeAvailable: root.fixtureMode ? (root.model.audio || {}).volume !== undefined : audio.available
  readonly property real volume: root.fixtureMode ? Number((root.model.audio || {}).volume || 0) : audio.volume
  readonly property bool muted: root.fixtureMode ? !!(root.model.audio || {}).muted : audio.muted

  // Every row the card shows, top to bottom. All of them take the cursor.
  readonly property var rows: {
    var list = []
    if (root.game) list.push("resume")
    list.push("screenshot", "record")
    if (root.replay) list.push("replay")
    if (root.volumeAvailable) list.push("volume")
    if (root.game) list.push("quit")
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
    resume: "\u{f040a}", screenshot: "\u{f0100}", record: "\u{f044a}", replay: "\u{f02da}",
    volume: "\u{f057e}", volumeOff: "\u{f0581}", quit: "\u{f0343}"
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
      cursor: root.cursor, rows: root.rows, confirming: root.confirming, confirmChoice: confirm.selectedIndex, pad: root.family, data: root.model,
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
    var rows = [resumeRow, screenshotRow, recordRow, replayRow, volumeRow, quitRow]
    return {card: box(card), rows: {resume: box(resumeRow), screenshot: box(screenshotRow), record: box(recordRow),
      replay: box(replayRow), volume: box(volumeRow), quit: box(quitRow)},
      icons: {resume: box(resumeRow.iconItem), screenshot: box(screenshotRow.iconItem), record: box(recordRow.iconItem),
      replay: box(replayRow.iconItem), volume: box(volumeRow.iconItem), quit: box(quitRow.iconItem)},
      fits: card.height >= card.contentTopInset + content.implicitHeight + card.contentBottomInset,
      truncated: rows.filter(function(r) { return r.visible && r.truncated }).map(function(r) { return r.label }),
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
    if (root.opened) return
    root.now = new Date()
    root.cursor = root.rows[0]
    root.confirming = !!p.confirm
    confirm.selectedIndex = 0
    root.quitting = false
    root.capturing = false
    root.confirmClosedAt = 0
    pointerGate.reset()
    window.targetScreen = root.gameScreen()
    root.opened = true
    root.showSurface()
  }

  function close() {
    var was = root.opened
    root.opened = false
    root.presented = false
    root.confirming = false
    root.quitting = false
    root.capturing = false
    root.surfaceShown = false
    card.opacity = 0
    fadeIn.stop()
    if (was) root.notify("closed", null)
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
      try { root.model = JSON.parse(text()) } catch (e) { console.warn("omakade.guide: bad fixture", e) }
      root.family = root.fixturePad || root.model.pad || "keyboard"
      // A fixture stands for a fresh open: the cursor starts on the first row.
      root.cursor = root.rows.indexOf(root.model.cursor) >= 0 ? root.model.cursor : root.rows[0]
      if (root.model.confirm) root.confirming = true
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
    function onWidthChanged() { root.surfaceSized() }
    function onHeightChanged() { root.surfaceSized() }
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
    if (root.confirming) {
      if (action === "left" || action === "right") confirm.selectedIndex = confirm.selectedIndex === 0 ? 1 : 0
      else if (action === "a") { if (confirm.selectedIndex === 0) root.cancelQuit(); else root.quitGame() }
      else if (action === "b") root.cancelQuit()
      else if (action === "guide" || action === "start") root.close()
      return "ok"
    }
    switch (action) {
    case "up": root.move(-1); break
    case "down": root.move(1); break
    case "left": case "right":
      if (root.cursor === "volume") root.act("volume", root.volume + (action === "left" ? -0.05 : 0.05))
      break
    case "a": root.activate(root.cursor); break
    case "b": if (Date.now() - root.confirmClosedAt > 300) root.close(); break
    case "guide": case "start": root.close(); break
    case "y": root.activate("screenshot"); break
    }
    return "ok"
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
    case "screenshot": root.act("screenshot"); break
    case "record": root.act("record"); break
    case "replay": root.act("save-replay"); break
    case "volume": root.act("mute"); break
    case "quit":
      if (root.quitting && !root.forceReady) break
      confirm.selectedIndex = 0
      root.confirming = true
      break
    }
  }

  function act(name, value) {
    if (root.fixtureMode) {
      console.log("GUIDE_ACT " + name + " " + JSON.stringify(value === undefined ? null : value))
      if (name === "volume") root.setFixture("audio", {volume: Math.max(0, Math.min(1, value)), muted: root.muted})
      else if (name === "mute") root.setFixture("audio", {volume: root.volume, muted: !root.muted})
      else if (name === "record") root.close()
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
    root.confirming = false
    root.confirmClosedAt = Date.now()
    confirm.selectedIndex = 0
  }

  function quitGame() {
    root.confirming = false
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
    if (!root.game) return ""
    var parts = []
    var minutes = root.game.sessionMinutes
    if (minutes !== undefined && minutes !== null) {
      parts.push(minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min" : minutes + " min")
    }
    if (root.game.paused) parts.push("Paused")
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
    if (root.confirming) list.push([b[0], "select"], [b[1], "back"])
    else if (root.cursor === "volume") list.push([b[0], root.muted ? "unmute" : "mute"], ["\u2190\u2192", "volume"], [b[1], root.game ? "resume" : "close"])
    else list.push([b[0], "select"], [b[1], root.game ? "resume" : "close"], [b[2], "screenshot"])
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
        readonly property int pad: Style.spacing.panelPadding
        // Header, readings and hint sit on the row fill's edges, as the menu's
        // title does; rows inset their own icon and value.
        readonly property int inset: 0
        width: Math.min(Style.space(380), window.width - Style.gapsOut * 2)
        height: Math.min(card.contentTopInset + content.implicitHeight + card.contentBottomInset, window.height - Style.gapsOut * 2)
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(Style.gapsOut, Math.round((window.height - card.height) / 2))
        radius: Style.cornerRadius
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
          x: card.contentLeftInset
          y: card.contentTopInset
          width: card.width - card.contentLeftInset - card.contentRightInset

          // Header: the game, how long this session has run and whether it is
          // paused, and the time.
          Item {
            id: header
            x: card.inset
            width: parent.width - card.inset * 2
            height: Math.max(title.height, clockText.height) + (detail.visible ? Style.space(2) + detail.height : 0)

            Text {
              id: title
              textFormat: Text.PlainText
              width: Math.min(implicitWidth, parent.width - clockText.width - Style.space(16))
              text: root.game ? root.game.title : "No game running"
              color: root.text
              font.family: root.fontFamily
              font.pixelSize: Style.font.heading
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
              font.pixelSize: Style.font.body
            }

            Text {
              id: detail
              textFormat: Text.PlainText
              y: title.height + Style.space(2)
              width: parent.width
              visible: text.length > 0
              text: root.sessionText()
              color: root.quiet
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
              wrapMode: Text.Wrap
            }
          }

          Item { width: 1; height: Style.spacing.md; visible: perf.visible }

          PerfLine {
            id: perf
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

          Divider { color: root.text }

          Column {
            width: parent.width
            spacing: Style.spacing.xs

            GuideRow {
              id: resumeRow
              selectedInk: root.selectedInk
              urgentInk: root.urgentInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              width: parent.width
              visible: root.rows.indexOf("resume") >= 0
              icon: root.icons.resume
              // The play triangle draws a third less ink than its neighbours.
              iconScale: 1.3
              label: "Resume"
              current: root.cursor === "resume"
              onHovered: (source, mouse) => root.hover("resume", source, mouse)
              onActivated: root.activate("resume")
            }

            GuideRow {
              id: screenshotRow
              selectedInk: root.selectedInk
              urgentInk: root.urgentInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              width: parent.width
              icon: root.icons.screenshot
              label: "Screenshot"
              current: root.cursor === "screenshot"
              onHovered: (source, mouse) => root.hover("screenshot", source, mouse)
              onActivated: root.activate("screenshot")
            }

            GuideRow {
              id: recordRow
              selectedInk: root.selectedInk
              urgentInk: root.urgentInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              width: parent.width
              icon: root.icons.record
              // The bar's recording colour while a clip runs.
              iconColor: root.recording ? root.recordingInk : recordRow.ink
              label: root.recording ? "Stop recording" : "Record clip"
              value: root.recordingTime
              current: root.cursor === "record"
              onHovered: (source, mouse) => root.hover("record", source, mouse)
              onActivated: root.activate("record")
            }

            GuideRow {
              id: replayRow
              selectedInk: root.selectedInk
              urgentInk: root.urgentInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              width: parent.width
              visible: root.rows.indexOf("replay") >= 0
              icon: root.icons.replay
              label: "Save last " + ((root.replay && root.replay.seconds) || 30) + " s"
              current: root.cursor === "replay"
              onHovered: (source, mouse) => root.hover("replay", source, mouse)
              onActivated: root.activate("replay")
            }

            GuideRow {
              id: volumeRow
              selectedInk: root.selectedInk
              urgentInk: root.urgentInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              width: parent.width
              visible: root.rows.indexOf("volume") >= 0
              icon: root.muted ? root.icons.volumeOff : root.icons.volume
              label: "Volume"
              slider: true
              sliderValue: root.volume
              sliderMuted: root.muted
              value: root.muted ? "Muted" : Math.round(root.volume * 100) + "%"
              current: root.cursor === "volume"
              onHovered: (source, mouse) => root.hover("volume", source, mouse)
              onActivated: root.activate("volume")
              onSliderMoved: v => { root.cursor = "volume"; root.act("volume", v) }
            }

            Divider { color: root.text; visible: root.rows.indexOf("quit") >= 0 }

            GuideRow {
              id: quitRow
              selectedInk: root.selectedInk
              urgentInk: root.urgentInk
              edge: root.needsEdge
              edgeColor: root.focusEdge
              width: parent.width
              visible: root.rows.indexOf("quit") >= 0
              icon: root.icons.quit
              label: root.forceReady ? "Force quit" : root.quitting ? "Closing game…" : "Quit game"
              urgent: true
              current: root.cursor === "quit"
              onHovered: (source, mouse) => root.hover("quit", source, mouse)
              onActivated: root.activate("quit")
            }
          }

          // Room for the hint line, which sits above the quit question's scrim.
          Item { width: 1; height: Style.spacing.xl + hintLine.height }
        }

        // What the buttons do. Above the quit question's scrim, so it stays
        // readable while the question is open.
        Flow {
          id: hintLine
          z: 11
          x: card.contentLeftInset + card.inset
          y: card.height - card.contentBottomInset - height
          width: content.width - card.inset * 2
          spacing: Style.space(16)

          Repeater {
            model: root.hint
            Row {
              required property var modelData
              Text {
                textFormat: Text.PlainText
                text: modelData[0] + " "
                color: root.text
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }
              Text {
                textFormat: Text.PlainText
                text: modelData[1]
                color: root.quiet
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }
            }
          }
        }

        ConfirmDialog {
          id: confirm
          // Inside the card's border, which stays drawn around the question.
          anchors.fill: parent
          anchors.margins: card.borderTop
          opened: root.confirming
          z: 10
          message: root.forceReady
            ? (root.game ? root.game.title : "The game") + " is not closing. Force quit it? Unsaved progress is lost."
            : "Quit " + (root.game ? root.game.title : "the game") + "? Progress since your last save may be lost."
          cancelText: "Keep playing"
          confirmText: root.forceReady ? "Force quit" : "Quit"
          selectedIndex: 0
          background: Color.menu.background
          foreground: Color.menu.text
          // Denser than the menu's 0.5 scrim: inside a card the rows behind
          // the question would otherwise read as part of it.
          scrim: Util.alpha(Color.menu.background, 0.85)
          selectedBackground: Color.menu.selectedBackground
          selectedText: Color.menu.selectedText
          fontFamily: root.fontFamily
          cornerRadius: Style.cornerRadius
          onCanceled: root.cancelQuit()
          onConfirmed: root.quitGame()
        }

        // The question's scrim follows the card's corners.
        Binding {
          target: confirm.children[0]
          property: "radius"
          value: Math.max(0, card.radius - card.borderTop)
        }
      }
    }
  }

  function hover(key, source, mouse) {
    if (root.confirming || !pointerGate.moved(source, mouse)) return
    root.cursor = key
  }
}
