import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "components"
import "pages"
import "Contrast.js" as Contrast

// The in-game guide: a panel over the running game with everything you would
// otherwise leave the game for. The panel slides in from the left over a frozen,
// frosted copy of the frame; the game stays visible, dimmed, beside it.
//
// Input arrives as actions (up, down, left, right, a, b, x, y, lb, rb, guide)
// from the keyboard or, for controllers, from Omakade's service through
// `omarchy-shell shell call omakade.guide input <action>`.
Item {
  id: root

  property bool opened: false
  property var model: ({})
  property string family: "keyboard"
  property int tab: 0
  property var cursors: [[0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0]]
  property bool confirmingQuit: false
  property int confirmIndex: 0
  property var toastData: null

  GuideTheme { id: g }

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
    if (p.scale) g.scale = Number(p.scale)
    if (p.pad) root.family = p.pad
    if (p.fixture) fixtureFile.path = p.fixture
    if (p.tab !== undefined) root.setTab(p.tab)
    root.confirmingQuit = !!p.confirm
    root.confirmIndex = 0
    if (p.toast) root.showToast(p.toast)
    if (p.audit) console.log("GUIDE_AUDIT " + JSON.stringify({ theme: p.audit, colors: g.contrastTheme, audit: Contrast.audit(g.contrastTheme), glyphs: root.glyphAudit() }))
    if (root.opened) return
    // A fresh open lands on Resume, so A then B never surprises anyone.
    if (p.tab === undefined) { root.tab = 0; root.cursors = root.cursors.map(function() { return [0, 0] }) }
    frame.captureFrame()
    openDelay.restart()
  }

  function close() {
    root.opened = false
    root.confirmingQuit = false
  }

  // The frozen frame has to be taken before the guide covers the screen.
  Timer { id: openDelay; interval: 50; onTriggered: root.opened = true }

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
    root.toastData = t
    toastTimer.restart()
  }
  Timer { id: toastTimer; interval: 3200; onTriggered: root.toastData = null }

  // Fixture behaviour. In Omakade these go to its service; here they change the
  // fixture so every control can be seen working.
  function act(name, arg) {
    var title = (root.model.game || {}).title || "the game"
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
  function currentRows() { return root.confirmingQuit ? root.confirmRows : root.rowsOf(root.page) }
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
    if (!root.opened) { if (action === "guide") root.open("{}"); return "closed" }
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
      if (root.confirmingQuit) { root.confirmingQuit = false; root.updateRing() } else root.close()
      break
    case "guide": root.close(); break
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
    var item = root.focused
    if (!item || !root.opened) { ring.visible = false; return }
    // Keep the focused control on screen.
    if (!root.confirmingQuit) {
      var inPage = item.mapToItem(scroller.contentItem, 0, 0)
      var margin = g.s(16)
      if (inPage.y - margin < scroller.contentY) scroller.contentY = Math.max(0, inPage.y - margin)
      else if (inPage.y + item.height + margin > scroller.contentY + scroller.height)
        scroller.contentY = Math.min(scroller.contentHeight - scroller.height, inPage.y + item.height + margin - scroller.height)
    }
    var p = item.mapToItem(panel, 0, 0)
    var pad = g.s(item.ringPad !== undefined ? item.ringPad : 3)
    ring.radius = item.radius !== undefined && item.radius > 0 ? item.radius + pad : g.radius
    ring.x = p.x - pad; ring.y = p.y - pad
    ring.width = item.width + pad * 2; ring.height = item.height + pad * 2
    ring.visible = true
  }
  onFocusedChanged: Qt.callLater(updateRing)
  onTabChanged: { ringAnimated = false; Qt.callLater(function() { updateRing(); ringAnimated = true }) }
  onConfirmingQuitChanged: Qt.callLater(updateRing)
  onModelChanged: Qt.callLater(updateRing)

  // ------------------------------------------------------------ IPC

  IpcHandler {
    target: "omakade.guide"
    function open(payload: string): void { root.open(payload) }
    function close(): void { root.close() }
    function input(action: string): string { return root.input(action) }
    function tab(name: string): void { root.setTab(name) }
    function toast(json: string): void { root.showToast(JSON.parse(json)) }
  }

  // ------------------------------------------------------------ surface

  OverlayWindow {
    id: window
    shown: root.opened
    WlrLayershell.namespace: "omakade-guide"

    // The frame under the guide, captured once as it opens.
    ScreencopyView {
      id: frame
      anchors.fill: parent
      captureSource: window.screen
      live: false
      visible: false
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
        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.62) }
        GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.36) }
        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.16) }
      }
    }

    MouseArea { anchors.fill: parent; onClicked: root.close() }

    Item {
      id: keys
      anchors.fill: parent
      focus: true
      Keys.onPressed: function(event) {
        var a = root.keyAction(event)
        if (a !== "") { root.input(a); event.accepted = true }
      }
    }
    Connections {
      target: window
      function onShownChanged() { if (window.shown) keys.forceActiveFocus() }
    }

    // ---------------------------------------------------- panel

    Item {
      id: panel
      readonly property int margin: g.s(14)
      x: margin + (root.opened ? 0 : -g.s(40))
      y: margin
      width: rail.width + g.s(452)
      height: parent.height - margin * 2
      opacity: root.opened ? 1 : 0
      Behavior on x { NumberAnimation { duration: Style.duration(220); easing.type: Easing.OutCubic } }
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
        blurMax: 64
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
          Behavior on opacity { NumberAnimation { duration: Style.duration(240) } }
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

      // The shell's popup border, so themes that style panels style this too.
      BorderSurface {
        anchors.fill: parent
        radius: g.radius
        color: "transparent"
        borderSpec: Border.surfaceSpec("popups", "border", Color.popups.border, Math.max(1, Style.space(2)))
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
          status: root.model.status || {}
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
          contentHeight: root.page ? root.page.implicitHeight + g.s(8) : 0
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          onContentYChanged: root.updateRing()
          Behavior on contentY { NumberAnimation { duration: Style.duration(160); easing.type: Easing.OutCubic } }

          Item {
            width: scroller.width
            height: scroller.contentHeight
            GamePage { id: gamePage; g: g; d: root.model; family: root.family; width: parent.width; visible: root.tab === 0; onAct: (n, a) => root.act(n, a) }
            CapturePage { id: capturePage; g: g; d: root.model; family: root.family; width: parent.width; visible: root.tab === 1; onAct: (n, a) => root.act(n, a) }
            PerformancePage { id: performancePage; g: g; d: root.model; family: root.family; width: parent.width; visible: root.tab === 2; onAct: (n, a) => root.act(n, a) }
            AudioPage { id: audioPage; g: g; d: root.model; family: root.family; width: parent.width; visible: root.tab === 3; onAct: (n, a) => root.act(n, a) }
            ControllersPage { id: controllersPage; g: g; d: root.model; family: root.family; width: parent.width; visible: root.tab === 4; onAct: (n, a) => root.act(n, a) }
            SystemPage { id: systemPage; g: g; d: root.model; family: root.family; width: parent.width; visible: root.tab === 5; onAct: (n, a) => root.act(n, a) }
          }
        }

        // What the buttons do here, in the connected pad's own glyphs.
        Item {
          id: footer
          anchors.left: parent.left; anchors.right: parent.right
          anchors.bottom: parent.bottom; anchors.bottomMargin: g.s(16)
          height: g.s(26)
          Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: g.s(12); width: parent.width; height: Math.max(1, g.s(1)); color: g.line }
          Row {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            spacing: g.s(16)
            Hint { g: g; family: root.family; button: "a"; label: "Select" }
            Hint { g: g; family: root.family; button: "b"; label: root.confirmingQuit ? "Cancel" : "Resume" }
          }
          Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            spacing: g.s(16)
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
            text: "Progress since your last save is lost. If it does not close, you can force it."
          }
          Item { width: 1; height: g.s(10) }
          Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: g.s(10)
            Action { id: confirmCancel; g: g; variant: "tile"; width: g.s(150); height: g.s(52); title: "Keep playing"; onTriggered: root.input("b") }
            Action { id: confirmQuit; g: g; variant: "tile"; width: g.s(150); height: g.s(52); title: "Quit"; danger: true; onTriggered: { root.confirmingQuit = false; root.close(); console.log("GUIDE_ACT quit-confirmed") } }
          }
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
