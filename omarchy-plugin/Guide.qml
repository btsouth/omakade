import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "components"
import "pages"
import "Contrast.js" as Contrast
import "GuideProtocol.js" as Protocol
import "GuideSettings.js" as Settings

// The in-game guide: a panel over the running game with everything you would
// otherwise leave the game for. The panel slides in from the left over a frozen,
// frosted copy of the frame; the game stays visible, dimmed, beside it.
//
// Input arrives as actions (up, down, left, right, a, b, x, y, lb, rb, guide)
// from the keyboard or, for controllers, from Omakade's service through
// `omarchy-shell omakade.guide input <action>`.
Item {
  id: root

  property bool opened: false
  property bool opening: false
  property bool fixtureMode: false
  property string outputName: ""
  property var backend: null
  property var backendQueue: []
  property var themeFrame: null
  property bool themeBusy: false
  property var model: ({})
  property string family: "keyboard"
  property int tab: 0
  property var cursors: [[0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0]]
  property bool notesOpen: false
  property string noteDraft: ""
  property bool forceReady: false
  property var genericWindow: ({})
  property var preferences: Settings.settings({})
  property var monitor: ({})
  property var stats: []
  property bool settingsLoaded: false
  readonly property var liveData: root.fixtureMode ? root.model : Object.assign({}, root.model, {
    capture: liveCapture.captureData, audio: liveMedia.audio, controllers: liveMedia.pads,
    window: root.genericWindow, prompts: root.preferences.prompts,
    system: Object.assign({}, liveSystem.system, {couch: root.preferences.couch}),
    performance: Object.assign({setupHint: "Install MangoHud to see frame timing"}, root.model.performance || {}, {stats: root.stats, profile: liveSystem.profile, refresh: Math.round(root.monitor.refreshRate || 60)})
  })
  property bool confirmingQuit: false
  property int confirmIndex: 0
  property var toastData: null

  GuideTheme {
    id: g
    onThemeRequested: function(theme) {
      if (!root.opened || Style.reduceMotion) {
        themeFade.stop(); previousTheme.opacity = 0
        root.themeBusy = false; root.themeFrame = null
        g.applyTheme(theme); return
      }
      root.themeBusy = true
      panel.grabToImage(function(result) {
        root.themeFrame = result
        previousTheme.source = result.url
        previousTheme.opacity = 1
        g.applyTheme(theme)
        themeFade.restart()
      })
    }
  }
  Socket {
    id: service
    parser: SplitParser {
      onRead: function(line) {
        try {
          var m = JSON.parse(line)
          if (m.type === "input") {
            root.input(m.action)
            root.notify("input-ack", {receivedNs: m.receivedNs, cursor: root.cursorOf()})
          } else if (m.type === "update") root.update(JSON.stringify(m.payload))
          else if (m.type === "toast") root.showToast(m)
        } catch(e) { console.warn("Guide socket message:", e) }
      }
    }
    path: root.backend ? root.backend.socket : ""
    connected: !!root.backend
    onConnectedChanged: {
      if (connected) {
        root.backendQueue.forEach(function(message) { service.write(message) })
        service.flush()
        root.backendQueue = []
      } else if (root.backend && root.opened) {
        root.backend = null
        root.model = ({})
        root.close()
      }
    }
  }
  function notify(name, value) {
    if (!root.backend) return
    var message = JSON.stringify({version: 1, token: root.backend.token, action: name, value: value}) + "\n"
    if (service.connected) { service.write(message); service.flush() }
    else root.backendQueue.push(message)
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
  function state(unused) {
    return JSON.stringify({opened: root.opened, opening: root.opening,
      token: root.backend ? root.backend.token : "", socket: root.backend ? root.backend.socket : "", output: window.screen ? window.screen.name : "",
      tab: root.tabs[root.tab].key, cursor: root.cursorOf(), pad: root.family,
      data: root.model, live: root.liveData, toast: root.toastData, captured: frame.hasContent, themeBusy: root.themeBusy})
  }

  readonly property var tabs: [
    { key: "game", title: "Game", icon: g.icon.game },
    { key: "capture", title: "Capture", icon: g.icon.capture },
    { key: "performance", title: "Performance", icon: g.icon.performance },
    { key: "audio", title: "Sound", icon: g.icon.audio },
    { key: "controllers", title: "Controllers", icon: g.icon.controllers },
    { key: "system", title: "System", icon: g.icon.system }
  ]
  readonly property var pages: [gamePage, capturePage, performancePage, audioPage, controllersPage, systemPage]
  readonly property Item page: pages[tab]

  // ------------------------------------------------------------ lifecycle

  function open(payloadJson) {
    var p = {}
    try { p = JSON.parse(payloadJson || "{}") || {} } catch (e) { p = {} }
    if (p.version !== undefined) {
      if (!Protocol.parse(payloadJson)) { console.warn("omakade.guide: invalid payload"); return }
      root.backend = p.backend || null
      root.update(payloadJson)
    } else if (!p.fixture && !root.opened) {
      root.backend = null
      root.fixtureMode = false
      root.model = ({})
      root.outputName = ""
      root.family = "keyboard"
    }
    if (p.scale) g.scale = Number(p.scale)
    else if (!p.fixture) root.applyScale()
    if (!p.fixture) {
      liveSystem.run(["hyprctl", "-j", "activewindow"], function(ok, text) {
        if (ok) { try { root.genericWindow = JSON.parse(text) } catch(e) {} }
      })
      liveSystem.run(["hyprctl", "-j", "monitors"], function(ok, text) {
        if (!ok) return
        try {
          var monitors = JSON.parse(text)
          root.monitor = monitors.find(m => m.name === root.gameScreen().name) || {}
          root.applyScale()
        } catch(e) {}
      })
    }
    if (p.pad) root.family = p.pad
    if (p.fixture) { root.backend = null; root.fixtureMode = true; fixtureFile.path = p.fixture }
    if (p.tab !== undefined) root.setTab(p.tab)
    root.notesOpen = false
    root.forceReady = false
    root.confirmingQuit = !!p.confirm
    root.confirmIndex = 0
    if (p.toast) root.showToast(p.toast)
    if (p.audit) console.log("GUIDE_AUDIT " + JSON.stringify({ theme: p.audit, colors: g.contrastTheme, audit: Contrast.audit(g.contrastTheme), glyphs: root.glyphAudit(), pairings: Contrast.pairings(g.contrastTheme) }))
    if (root.opened || root.opening) return
    // A fresh open lands on Resume, so A then B never surprises anyone.
    if (p.tab === undefined) { root.tab = 0; root.cursors = root.cursors.map(function() { return [0, 0] }) }
    if (!root.fixtureMode && !root.model.game && p.tab === undefined) root.tab = 5
    window.targetScreen = root.gameScreen()
    root.opening = true
    // Reset the source so hasContent belongs to this capture, not a previous open.
    frame.captureSource = null
    Qt.callLater(function() {
      if (!root.opening) return
      frame.captureSource = window.targetScreen
      frame.captureFrame()
      captureDeadline.restart()
    })
  }

  function gameScreen() {
    for (var i = 0; i < Quickshell.screens.length; i++)
      if (Quickshell.screens[i].name === root.outputName) return Quickshell.screens[i]
    return window.focusedScreen() || window.targetScreen || Quickshell.screens[0] || null
  }
  function captured() {
    if (!root.opening) return
    captureDeadline.stop()
    root.opening = false
    root.opened = true
    root.notify("opened", null)
  }
  function close() {
    if (root.notesOpen && root.family === "keyboard") root.notify("notes-save", root.noteDraft)
    root.notesOpen = false
    root.notify("closed", null)
    root.opening = false
    captureDeadline.stop()
    root.opened = false
    root.confirmingQuit = false
  }

  // The frozen frame has to be taken before the guide covers the screen.
  // On an unsupported capture protocol, open with the approved theme scrim instead.
  Timer { id: captureDeadline; interval: 750; onTriggered: root.captured() }

  // Face-button letters on their discs, for the contrast table.
  function glyphAudit() {
    var bg = Contrast.hex(g.background), fg = Contrast.hex(g.foreground)
    return [["A", g.green], ["B", g.red], ["X", g.blue], ["Y", g.yellow]].map(function(b) {
      var disc = Contrast.hex(b[1]), ink = Contrast.inkOn(disc, bg, fg)
      var c = Contrast.contrast(ink, disc)
      // Below the floor the glyph draws the foreground on a neutral disc instead.
      return { button: b[0], coloured: c >= Contrast.glyphFloor, contrast: c >= Contrast.glyphFloor ? c : Contrast.contrast(fg, bg) }
    })
  }

  function setTab(t) {
    var i = typeof t === "number" ? t : root.tabs.findIndex(function(x) { return x.key === t })
    if (i < 0) return
    root.tab = (i + root.tabs.length) % root.tabs.length
    Qt.callLater(root.updateRing)
  }

  // ------------------------------------------------------------ fixtures

  FileView {
    id: fixtureFile
    path: ""
    onLoaded: {
      if (!root.fixtureMode) return
      // "@/" in a fixture is the folder above the fixture's own, where the preview art lives.
      var base = String(path).replace(/\/[^\/]*\/[^\/]*$/, "/")
      try { root.model = JSON.parse(text().replace(/"@\//g, '"' + base)) } catch (e) { console.warn("omakade.guide: bad fixture", e) }
      if (root.model.pad) root.family = root.model.pad
      Qt.callLater(root.updateRing)
    }
  }

  function setIn(path, value) {
    var copy = JSON.parse(JSON.stringify(root.model))
    var keys = path.split("."), o = copy
    for (var i = 0; i < keys.length - 1; i++) o = o[keys[i]] = o[keys[i]] || {}
    o[keys[keys.length - 1]] = value
    root.model = copy
  }

  function showToast(t) {
    if (!root.opened && !root.opening && !root.fixtureMode) { liveSystem.run(["notify-send", t.title || "Game guide", t.detail || ""]); return }
    root.toastData = t
    toastTimer.restart()
  }
  Timer { id: toastTimer; interval: 3200; onTriggered: root.toastData = null }

  // Fixture behaviour. In Omakade these go to its service; here they change the
  // fixture so every control can be seen working.
  function act(name, arg) {
    var title = (root.model.game || {}).title || "the game"
    if (!root.fixtureMode) {
      if (name === "resume") { root.close(); return }
      if (name === "quit") { root.confirmingQuit = true; root.confirmIndex = 0; return }
      if (name === "notes") { root.noteDraft = (root.model.game || {}).note || ""; root.notesOpen = true; Qt.callLater(function() { if (root.family === "keyboard") noteEdit.forceActiveFocus() }); return }
      if (name === "screenshot") { root.saveScreenshot(); return }
      if (["replay-length", "capture-sound", "couch-scale", "prompts"].indexOf(name) >= 0) {
        var key = {"replay-length": "replaySeconds", "capture-sound": "sound", "couch-scale": "couch", "prompts": "prompts"}[name]
        if (name === "replay-length" && liveCapture.captureData.replayOn) { root.showToast({title: "Stop the replay buffer first"}); return }
        var pref = Object.assign({}, root.preferences); pref[key] = arg
        root.preferences = Settings.settings(pref)
        if (root.settingsLoaded) settingsFile.setText(JSON.stringify(root.preferences) + "\n")
        if (name === "couch-scale") root.applyScale()
        if (name === "prompts" && arg !== "auto") root.family = arg
        return
      }
      if (liveMedia.act(name, arg) || liveCapture.act(name, arg) || liveSystem.act(name, arg)) return
      if (name === "pair") { root.close(); liveSystem.run(["omarchy-shell", "shell", "summon", "omarchy.bluetooth"]); return }
      if (root.backend && ["pause-while-open", "backup", "desktop", "library", "steam-overlay", "hud", "limit", "enable-mangohud"].indexOf(name) >= 0) { root.notify(name, arg); return }
      if (name === "desktop") { root.close(); liveSystem.hypr('hl.dsp.focus({workspace="empty"})', ["workspace", "empty"]); return }
      if (name === "library") { root.close(); liveSystem.run(["omakade"]); return }
      if (name === "enable-mangohud") { root.showToast({title: "MangoHud setup", detail: "Install MangoHud and add MANGOHUD=1 to the game's launch environment"}); return }
      root.showToast({title: "Control unavailable", detail: "The required device or service is not connected"})
      return
    }
    switch (name) {
    case "resume": root.close(); break
    case "quit": root.confirmingQuit = true; root.confirmIndex = 0; break
    case "screenshot": root.showToast({ icon: g.icon.capture, title: "Screenshot saved", detail: "Pictures › Screenshots", image: (root.model.capture || {}).lastShot || "" }); break
    case "save-replay": root.showToast({ icon: g.icon.replay, title: "Replay saved", detail: "Last " + ((root.model.capture || {}).replaySeconds || 30) + " seconds of " + title }); break
    case "backup": root.showToast({ icon: g.icon.save, title: "Saves backed up", detail: title }); break
    case "identify": root.showToast({ icon: g.icon.controllers, title: "Rumbling", detail: arg }); break
    case "replay-buffer": root.setIn("capture.replayOn", arg); root.setIn("status.replay.on", arg); break
    case "replay-length": root.setIn("capture.replaySeconds", arg); root.setIn("status.replay.seconds", arg); break
    case "pause-while-open": root.setIn("game.pauseWhileOpen", arg); break
    case "hud": root.setIn("performance.hud", arg); break
    case "limit": root.setIn("performance.limit", arg); break
    case "profile": root.setIn("performance.profile", arg); break
    case "volume": root.setIn("audio.volume", arg); break
    case "mic": root.setIn("audio.micMuted", !arg); break
    case "brightness": root.setIn("system.brightness", arg); break
    case "wifi": root.setIn("system.wifi", arg); root.setIn("status.wifi", arg); break
    case "bluetooth": root.setIn("system.bluetooth", arg); root.setIn("status.bluetooth", arg); break
    case "dnd": root.setIn("system.dnd", arg); root.setIn("status.dnd", arg); break
    case "capture-sound": root.setIn("capture.sound", arg); break
    case "prompts": root.setIn("prompts", arg); if (arg !== "auto") root.family = arg; break
    case "output":
      root.setIn("audio.outputs", (root.model.audio.outputs || []).map(function(o) { o.current = o.name === arg; return o }))
      break
    case "media": root.setIn("audio.nowPlaying.playing", arg === "play" ? !root.model.audio.nowPlaying.playing : root.model.audio.nowPlaying.playing); break
    }
    console.log("GUIDE_ACT " + name + " " + JSON.stringify(arg === undefined ? null : arg))
  }

  // ------------------------------------------------------------ navigation

  function rowsOf(p) {
    return (p.rows || []).map(function(r) { return r.filter(function(i) { return i && i.visible }) })
      .filter(function(r) { return r.length > 0 })
  }
  readonly property var confirmRows: [[confirmCancel, confirmQuit]]
  function applyScale() { g.scale = Settings.couchScale(root.preferences.couch, Number(root.monitor.physicalWidth || 0)) }
  function saveScreenshot() {
    if (!frame.hasContent) { root.showToast({title: "Screenshot unavailable", detail: "No frozen game frame was captured"}); return }
    liveSystem.run(["python3", liveCapture.helper, "screenshot"], function(ok, path) {
      if (!ok) { root.showToast({title: "Screenshot directory unavailable"}); return }
      frame.grabToImage(function(result) {
        var saved = result.saveToFile(path)
        root.showToast({title: saved ? "Screenshot saved" : "Screenshot could not be saved", image: saved ? "file://" + path : ""})
        liveCapture.refresh()
      }, Qt.size(frame.width, frame.height))
    })
  }
  function quitGame() {
    if (root.fixtureMode) { root.close(); return }
    if (root.backend) { root.notify(root.forceReady || (root.model.game || {}).forceReady ? "force-quit" : "quit-confirmed", null); return }
    var window = root.genericWindow
    if (!window.address) { root.close(); return }
    if (root.forceReady) {
      liveSystem.run(["python3", "-c", "import os,signal,sys; pid=int(sys.argv[1]); start=sys.argv[2]; fd=os.pidfd_open(pid); s=open('/proc/%d/stat'%pid).read(); fields=s[s.rfind(')')+2:].split(); signal.pidfd_send_signal(fd,signal.SIGKILL) if fields[19]==start else None", String(window.pid), String(window.procStart)])
      root.close(); return
    }
    liveSystem.run(["python3", "-c", "import sys; s=open('/proc/%s/stat'%sys.argv[1]).read(); print(s[s.rfind(')')+2:].split()[19])", String(window.pid)], function(ok, start) {
      if (!ok) return
      var w = Object.assign({}, root.genericWindow); w.procStart = start; root.genericWindow = w
      liveSystem.hypr("hl.dsp.window.close({window=" + JSON.stringify("address:" + w.address) + "})", ["closewindow", "address:" + w.address])
      genericQuitTimer.restart()
    })
  }
  Timer { id: genericQuitTimer; interval: 5000; onTriggered: {
    liveSystem.run(["hyprctl", "-j", "clients"], function(ok, text) {
      if (!ok) return
      try {
        root.forceReady = JSON.parse(text).some(w => w.address === root.genericWindow.address && w.pid === root.genericWindow.pid)
        if (root.forceReady) root.showToast({title: "Game is still running", detail: "Force quit is now available"})
        else root.close()
      } catch(e) {}
    })
  } }
  FileView {
    id: settingsFile
    path: liveCapture.files.settingsPath || ""
    onLoaded: { if (!root.settingsLoaded) { try { root.preferences = Settings.settings(JSON.parse(text())) } catch(e) {} root.settingsLoaded = true; root.applyScale() } }
    onLoadFailed: { root.settingsLoaded = true }
  }

  function currentRows() { return root.notesOpen ? [[noteClose]] : root.confirmingQuit ? root.confirmRows : root.rowsOf(root.page) }
  function focusControl(item) {
    var rows = currentRows()
    for (var r = 0; r < rows.length; r++) {
      var c = rows[r].indexOf(item)
      if (c >= 0) { setCursor(r, c); return }
    }
  }
  function cursorOf() { return root.confirmingQuit ? [0, root.confirmIndex] : root.cursors[root.tab] }
  function setCursor(r, c) {
    if (root.confirmingQuit) { root.confirmIndex = c } else {
      var copy = root.cursors.slice(); copy[root.tab] = [r, c]; root.cursors = copy
    }
    root.updateRing()
  }
  readonly property Item focused: {
    var rows = currentRows(), cur = cursorOf()
    if (rows.length === 0) return null
    var r = Math.min(cur[0], rows.length - 1)
    return rows[r][Math.min(cur[1], rows[r].length - 1)]
  }

  function input(action) {
    if (action.charAt(0) === "{") {
      try { var event = JSON.parse(action); root.input(event.action); root.notify("input-ack", {receivedNs: event.receivedNs}); return "ok" } catch(e) { return "invalid" }
    }
    if (!root.opened) {
      if (root.opening && ["guide", "start", "b"].indexOf(action) >= 0) root.close()
      else if (!root.opening && action === "guide") root.open("{}")
      return "closed"
    }
    var rows = currentRows(), cur = cursorOf()
    var r = Math.min(cur[0], Math.max(0, rows.length - 1))
    var c = rows.length ? Math.min(cur[1], rows[r].length - 1) : 0
    switch (action) {
    case "up": if (r > 0) setCursor(r - 1, Math.min(c, rows[r - 1].length - 1)); break
    case "down": if (r < rows.length - 1) setCursor(r + 1, Math.min(c, rows[r + 1].length - 1)); break
    case "left":
    case "right":
      var d = action === "left" ? -1 : 1
      if (root.focused && root.focused.step(d)) break
      var nc = c + d
      if (nc >= 0 && nc < rows[r].length) setCursor(r, nc)
      break
    case "a": if (root.focused) root.focused.activate(); break
    case "b":
      if (root.notesOpen) { root.notesOpen = false; keys.forceActiveFocus() } else if (root.confirmingQuit) { root.confirmingQuit = false; root.updateRing() } else root.close()
      break
    case "guide": case "start": root.close(); break
    case "lb": if (!root.confirmingQuit) setTab(root.tab - 1); break
    case "rb": if (!root.confirmingQuit) setTab(root.tab + 1); break
    case "y": root.act("screenshot"); break
    case "x": root.act("save-replay"); break
    }
    console.log("GUIDE_FOCUS " + root.tabs[root.tab].key + " " + JSON.stringify(cursorOf()))
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
    case Qt.Key_Q: case Qt.Key_PageUp: case Qt.Key_Backtab: return "lb"
    case Qt.Key_E: case Qt.Key_PageDown: case Qt.Key_Tab: return "rb"
    case Qt.Key_Y: return "y"
    case Qt.Key_X: return "x"
    case Qt.Key_G: case Qt.Key_Home: return "guide"
    }
    return ""
  }

  // ------------------------------------------------------------ focus ring

  property bool ringAnimated: false
  function updateRing() {
    if (layoutTimer.running) return
    var item = root.focused
    if (!item || !root.opened) { ring.visible = false; return }
    // Keep the focused control on screen.
    if (!root.confirmingQuit) {
      var inPage = item.mapToItem(scroller.contentItem, 0, 0)
      var margin = g.s(16)
      if (scroller.contentHeight <= scroller.height + g.s(8)) scroller.contentY = 0
      else if (inPage.y - margin < scroller.contentY) scroller.contentY = Math.max(0, inPage.y - margin)
      else if (inPage.y + item.height + margin > scroller.contentY + scroller.height)
        scroller.contentY = Math.min(scroller.contentHeight - scroller.height, inPage.y + item.height + margin - scroller.height)
    }
    var p = item.mapToItem(panel, 0, 0)
    var pad = g.s(Math.max(8, item.ringPad || 0))
    ring.radius = item.radius !== undefined && item.radius > 0 ? item.radius + pad : g.radius
    ring.x = p.x - pad; ring.y = p.y - pad
    ring.width = item.width + pad * 2; ring.height = item.height + pad * 2
    ring.visible = true
  }
  onFocusedChanged: Qt.callLater(updateRing)
  Timer {
    id: layoutTimer
    interval: 32
    onTriggered: {
      scroller.contentY = Math.max(0, Math.min(scroller.contentY, scroller.contentHeight - scroller.height))
      root.updateRing()
      root.ringAnimated = true
    }
  }
  onTabChanged: { ringAnimated = false; scroller.contentY = 0; layoutTimer.restart() }
  onOpenedChanged: if (opened) { ringAnimated = false; scroller.contentY = 0; layoutTimer.restart() }
  onConfirmingQuitChanged: Qt.callLater(updateRing)
  onModelChanged: layoutTimer.restart()

  // ------------------------------------------------------------ IPC

  IpcHandler {
    target: "omakade.guide"
    function open(payload: string): void { root.open(payload) }
    function close(): void { root.close() }
    function input(action: string): string { return root.input(action) }
    function act(name: string, value: string): void { root.act(name, JSON.parse(value || "null")) }
    function tab(name: string): void { root.setTab(name) }
    function ready(): string { return root.opened && panel.opacity === 1 && !layoutTimer.running && !root.opening && !root.themeBusy ? "ready" : "waiting" }
    function toast(json: string): void { root.showToast(JSON.parse(json)) }
  }

  // ------------------------------------------------------------ surface

  OverlayWindow {
    id: window
    shown: root.opened
    // Override OverlayWindow's focus-following handler; the payload owns the target.
    onShownChanged: if (shown) targetScreen = root.gameScreen()
    WlrLayershell.namespace: "omakade-guide"

    // The frame under the guide, captured once as it opens.
    Item {
      anchors.fill: parent
      opacity: 0
    ScreencopyView {
      id: frame
      anchors.fill: parent
      captureSource: null
      onHasContentChanged: if (hasContent && root.opening) Qt.callLater(root.captured)
      live: false
      visible: frame.hasContent
    }
    }
    MultiEffect {
      anchors.fill: parent
      source: frame
      visible: frame.hasContent && !!(root.model.game || {}).pauseWhileOpen
      saturation: -0.7
      scale: 0.985
      opacity: root.opened ? 1 : 0
      Behavior on opacity { NumberAnimation { duration: Style.duration(180) } }
    }

    // The game outside the panel: dimmed most near the panel. The dim is a
    // shadow, not a theme colour, so light themes read as a lit card over a
    // darkened scene instead of washing the game out.
    Rectangle {
      anchors.fill: parent
      opacity: root.opened ? 1 : 0
      Behavior on opacity { NumberAnimation { duration: Style.duration(180) } }
      gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.78) }
        GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.66) }
        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.55) }
      }
    }

    Rectangle {
      anchors.fill: parent
      opacity: root.opened ? 1 : 0
      Behavior on opacity { NumberAnimation { duration: Style.duration(180) } }
      gradient: Gradient {
        GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.12) }
        GradientStop { position: 0.45; color: "transparent" }
        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.20) }
      }
    }

    MouseArea { anchors.fill: parent; onClicked: root.close() }

    Item {
      id: keys
      anchors.fill: parent
      focus: true
      Keys.onPressed: function(event) {
        root.family = "keyboard"
        var a = root.keyAction(event)
        if (a !== "") { root.input(a); event.accepted = true }
      }
    }
    Connections {
      target: window
      function onShownChanged() { if (window.shown) keys.forceActiveFocus() }
    }

    LiveSystem { id: liveSystem; onFailure: title => root.showToast({title: title}); active: root.opened && !root.fixtureMode }
    LiveMedia { id: liveMedia; devices: liveCapture.files.pads || []; onToast: t => root.showToast(t) }
    LiveCapture { id: liveCapture; runner: liveSystem; active: root.opened && !root.fixtureMode; output: root.gameScreen() ? root.gameScreen().name : ""; settings: root.preferences; onToast: t => root.showToast(t) }
    Timer { interval: 2000; running: root.opened && !root.fixtureMode; repeat: true; triggeredOnStart: true; onTriggered: liveSystem.run(["python3", liveCapture.helper, "stats"], function(ok, value) { if (ok) { try { root.stats = JSON.parse(value) } catch(e) {} } }) }

    // ---------------------------------------------------- panel

    Item {
      id: panel
      readonly property int margin: g.s(14)
      x: margin + (root.opened ? 0 : -g.s(40))
      y: margin
      width: rail.width + g.s(452)
      height: parent.height - margin * 2
      opacity: root.opened ? 1 : 0
      Behavior on x { NumberAnimation { duration: Style.duration(180); easing.type: Easing.OutCubic } }
      Behavior on opacity { NumberAnimation { duration: Style.duration(160) } }

      MouseArea { anchors.fill: parent }

      // Depth: a soft shadow under the whole panel.
      Rectangle {
        id: shadowSource
        anchors.fill: parent
        radius: g.radius
        color: "black"
        visible: false
      }
      MultiEffect {
        source: shadowSource
        anchors.fill: parent
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, g.light ? 0.28 : 0.55)
        shadowBlur: 1.0
        shadowVerticalOffset: g.s(8)
        blurMax: 96
        paddingRect: Qt.rect(0, 0, 0, 0)
        autoPaddingEnabled: true
        opacity: 1
      }

      // Glass: the frozen frame, blurred and saturated, under the theme's darkest
      // background at 0.6 and the theme tint the legibility rule allows.
      Item {
        id: glass
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
          maskEnabled: true
          maskSource: glassMask
          maskThresholdMin: 0.5
          maskSpreadAtMin: 1.0
        }

        MultiEffect {
          source: frame
          x: -panel.x; y: -panel.y
          width: window.width; height: window.height
          blurEnabled: true
          blur: 1.0
          blurMax: 64
          saturation: 0.45
        }

        // Game art: colour at the top of the Game page, part of the frame the
        // legibility rule already allows for.
        Image {
          id: ambient
          anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
          height: parent.height * 0.5
          source: (root.model.game || {}).cover || ""
          fillMode: Image.PreserveAspectCrop
          visible: false
        }
        MultiEffect {
          source: ambient
          anchors.fill: ambient
          blurEnabled: true; blur: 1.0; blurMax: 64
          saturation: 0.6
          opacity: root.tab === 0 ? 1 : 0
          Behavior on opacity { NumberAnimation { duration: Style.duration(180) } }
          maskEnabled: true
          maskSource: ambientFade
        }
        Rectangle {
          id: ambientFade
          anchors.fill: ambient
          visible: false
          layer.enabled: true
          gradient: Gradient {
            GradientStop { position: 0.0; color: "white" }
            GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0.6) }
            GradientStop { position: 1.0; color: "transparent" }
          }
        }

        Rectangle { anchors.fill: parent; color: g.base; opacity: 0.6 }
        Rectangle { anchors.fill: parent; color: g.background; opacity: g.tint }
      }
      Rectangle { id: glassMask; anchors.fill: parent; radius: g.radius; visible: false; layer.enabled: true }

      Rectangle {
        anchors.fill: parent; radius: g.radius
        color: "transparent"
        border.width: 1; border.color: g.line
      }
      Rectangle {
        x: g.radius; y: 1
        width: parent.width - g.radius * 2; height: 1
        gradient: Gradient {
          orientation: Gradient.Horizontal
          GradientStop { position: 0; color: "transparent" }
          GradientStop { position: 0.5; color: Util.alpha(g.foreground, 0.22) }
          GradientStop { position: 1; color: "transparent" }
        }
      }

      TabRail {
        id: rail
        g: g
        tabs: root.tabs
        current: root.tab
        family: root.family
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        onPicked: function(i) { root.setTab(i) }
      }

      Item {
        id: content
        anchors.left: rail.right; anchors.right: parent.right
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.leftMargin: g.s(22); anchors.rightMargin: g.s(22)

        StatusStrip {
          id: status
          g: g
          status: root.fixtureMode ? root.model.status || {} : Object.assign({}, liveSystem.status, liveCapture.status, {battery: liveMedia.battery})
          anchors.left: parent.left; anchors.right: parent.right
          anchors.top: parent.top; anchors.topMargin: g.s(12)
        }

        Label {
          id: heading
          g: g
          role: "caps"
          text: root.tabs[root.tab].title
          anchors.left: parent.left
          anchors.top: status.bottom; anchors.topMargin: g.s(10)
        }
        Rectangle {
          anchors.left: heading.right; anchors.leftMargin: g.s(10)
          anchors.right: parent.right
          anchors.verticalCenter: heading.verticalCenter
          height: Math.max(1, g.s(1))
          color: g.line
        }

        Flickable {
          id: scroller
          anchors.left: parent.left; anchors.right: parent.right
          anchors.top: heading.bottom; anchors.topMargin: g.s(16)
          anchors.bottom: footer.top; anchors.bottomMargin: g.s(12)
          contentWidth: width
          contentHeight: root.page ? Math.max(scroller.height, root.page.implicitHeight + g.s(8)) : 0
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          onContentYChanged: root.updateRing()
          onContentHeightChanged: layoutTimer.restart()
          Behavior on contentY { enabled: root.ringAnimated; NumberAnimation { duration: Style.duration(160); easing.type: Easing.OutCubic } }

          Item {
            width: scroller.width
            height: scroller.contentHeight
            GamePage { id: gamePage; g: g; d: root.liveData; family: root.family; width: parent.width; availableHeight: scroller.height - g.s(8); visible: root.tab === 0; onAct: (n, a) => root.act(n, a) }
            CapturePage { id: capturePage; g: g; d: root.liveData; family: root.family; width: parent.width; availableHeight: scroller.height - g.s(8); visible: root.tab === 1; onAct: (n, a) => root.act(n, a) }
            PerformancePage { id: performancePage; g: g; d: root.liveData; family: root.family; width: parent.width; availableHeight: scroller.height - g.s(8); visible: root.tab === 2; onAct: (n, a) => root.act(n, a) }
            AudioPage { id: audioPage; g: g; d: root.liveData; family: root.family; width: parent.width; availableHeight: scroller.height - g.s(8); visible: root.tab === 3; onAct: (n, a) => root.act(n, a) }
            ControllersPage { id: controllersPage; g: g; d: root.liveData; family: root.family; width: parent.width; availableHeight: scroller.height - g.s(8); visible: root.tab === 4; onAct: (n, a) => root.act(n, a) }
            SystemPage { id: systemPage; onRequestedFocus: (item) => root.focusControl(item); g: g; d: root.liveData; family: root.family; width: parent.width; availableHeight: scroller.height - g.s(8); visible: root.tab === 5; onAct: (n, a) => root.act(n, a) }
          }
        }

        // What the buttons do here, in the connected pad's own glyphs.
        Item {
          id: footer
          anchors.left: parent.left; anchors.right: parent.right
          anchors.bottom: parent.bottom; anchors.bottomMargin: g.s(16)
          height: g.s(26)
          Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: g.s(12); width: parent.width; height: Math.max(1, g.s(1)); color: g.line }
          RowLayout {
            anchors.fill: parent
            spacing: g.s(16)
            Hint { g: g; family: root.family; button: "a"; label: "Select" }
            Hint { g: g; family: root.family; button: "b"; label: root.confirmingQuit ? "Cancel" : "Resume" }
            Item { Layout.fillWidth: true }
            Hint { g: g; family: root.family; button: "y"; label: "Screenshot" }
            Hint { g: g; family: root.family; button: "x"; label: "Replay" }
          }
        }
      }

      // Quit asks first, over the page.
      Rectangle {
        anchors.fill: parent
        radius: g.radius
        visible: opacity > 0
        opacity: root.confirmingQuit ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Style.duration(140) } }
        color: Util.alpha(g.background, 0.82)
        MouseArea { anchors.fill: parent }

        Column {
          anchors.centerIn: parent
          width: parent.width - g.s(96)
          spacing: g.s(10)
          Glyph { g: g; name: g.icon.power; size: g.f(30); color: g.urgent; anchors.horizontalCenter: parent.horizontalCenter }
          Label { g: g; role: "heading"; text: "Quit " + ((root.model.game || {}).title || "the game") + "?"; width: parent.width; horizontalAlignment: Text.AlignHCenter }
          Label {
            g: g; role: "small"; width: parent.width; horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.fixtureMode ? "Progress since your last save is lost. If it does not close, you can force it." : "Progress since your last save may be lost. Omakade asks the game to close."
          }
          Item { width: 1; height: g.s(10) }
          Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: g.s(10)
            Action { id: confirmCancel; g: g; variant: "tile"; width: g.s(150); height: g.s(52); title: "Keep playing"; onTriggered: root.input("b") }
            Action { id: confirmQuit; g: g; variant: "tile"; width: g.s(150); height: g.s(52); title: root.forceReady || (root.model.game || {}).forceReady ? "Force quit" : "Quit"; danger: true; onTriggered: root.quitGame() }
          }
        }
      }

      Rectangle {
        visible: root.notesOpen
        anchors.fill: parent; radius: g.radius; color: g.background
        MouseArea { anchors.fill: parent }
        Column {
          x: rail.width + g.s(22); y: g.s(24)
          width: parent.width - x - g.s(22); spacing: g.s(16)
          Label { g: g; role: "heading"; text: "Notes"; width: parent.width }
          Label { g: g; role: "small"; text: root.family === "keyboard" ? "Ctrl+S saves. Escape returns to the game page." : "Edit with a keyboard"; width: parent.width; wrapMode: Text.WordWrap }
          Rectangle {
            width: parent.width; height: panel.height - g.s(230)
            color: g.well; border.color: g.line; border.width: 1; radius: g.innerRadius
            Flickable {
              anchors.fill: parent; anchors.margins: g.s(14); clip: true
              contentWidth: width; contentHeight: Math.max(height, noteEdit.contentHeight)
              TextEdit {
                id: noteEdit
                width: parent.width; height: Math.max(parent.height, contentHeight)
                text: root.noteDraft; onTextChanged: if (activeFocus) root.noteDraft = text
                color: g.foreground; selectionColor: g.accent; selectedTextColor: g.accentInk
                font.family: g.font; font.pixelSize: g.f(16)
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                readOnly: root.family !== "keyboard"; selectByMouse: !readOnly
                Keys.onPressed: function(e) {
                  root.family = "keyboard"
                  if (e.key === Qt.Key_Escape) { root.notify("notes-save", root.noteDraft); root.notesOpen = false; keys.forceActiveFocus(); e.accepted = true }
                  else if (e.key === Qt.Key_S && (e.modifiers & Qt.ControlModifier)) { root.notify("notes-save", root.noteDraft); e.accepted = true }
                }
              }
            }
          }
          Action { id: noteClose; g: g; width: parent.width; title: root.family === "keyboard" ? "Save and return" : "Return"; onTriggered: { if (root.family === "keyboard") root.notify("notes-save", root.noteDraft); root.notesOpen = false; keys.forceActiveFocus() } }
        }
      }

      // One focus ring that glides between controls.
      Rectangle {
        id: ring
        visible: false
        color: "transparent"
        border.width: Math.max(2, g.s(2))
        border.color: g.accent
        Behavior on x { enabled: root.ringAnimated; NumberAnimation { duration: Style.duration(140); easing.type: Easing.OutCubic } }
        Behavior on y { enabled: root.ringAnimated; NumberAnimation { duration: Style.duration(140); easing.type: Easing.OutCubic } }
        Behavior on width { enabled: root.ringAnimated; NumberAnimation { duration: Style.duration(140); easing.type: Easing.OutCubic } }
        Behavior on height { enabled: root.ringAnimated; NumberAnimation { duration: Style.duration(140); easing.type: Easing.OutCubic } }
        Rectangle {
          anchors.fill: parent
          anchors.margins: -g.s(3)
          radius: parent.radius + g.s(3)
          color: "transparent"
          border.width: Math.max(1, g.s(2))
          border.color: Util.alpha(g.accent, 0.25)
        }
      }
    }

    Image {
      id: previousTheme
      x: panel.x; y: panel.y; width: panel.width; height: panel.height
      opacity: 0
      visible: opacity > 0
    }
    NumberAnimation {
      id: themeFade
      target: previousTheme; property: "opacity"; from: 1; to: 0
      duration: Style.reduceMotion ? 0 : 180
      onFinished: { root.themeBusy = false; root.themeFrame = null; previousTheme.source = "" }
    }

    Toast {
      g: g
      anchors.right: parent.right; anchors.bottom: parent.bottom
      anchors.margins: g.s(24)
      opacity: root.toastData ? 1 : 0
      visible: opacity > 0
      Behavior on opacity { NumberAnimation { duration: Style.duration(160) } }
      icon: root.toastData ? root.toastData.icon || "" : ""
      title: root.toastData ? root.toastData.title || "" : ""
      detail: root.toastData ? root.toastData.detail || "" : ""
      image: root.toastData ? root.toastData.image || "" : ""
    }
  }

  Component.onCompleted: ringAnimated = true
}
