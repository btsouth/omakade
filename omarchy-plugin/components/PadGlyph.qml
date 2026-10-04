import QtQuick
import qs.Commons
import "../Contrast.js" as Contrast

// A controller button the way the connected pad prints it. Xbox and Steam Deck
// colour their face buttons, PlayStation draws shapes, Nintendo swaps the
// letters, and a keyboard gets key caps. Face buttons are named by position
// (a = south, b = east, x = west, y = north) so one hint works for every pad.
Item {
  id: root
  required property var g
  property string family: "xbox"
  property string button: "a"
  property real size: g.f(20)

  readonly property bool face: ["a", "b", "x", "y"].indexOf(button) >= 0
  readonly property bool bumper: button === "lb" || button === "rb"
  readonly property bool keyboard: family === "keyboard"

  readonly property var xboxColor: ({ a: g.green, b: g.red, x: g.blue, y: g.yellow })
  readonly property var psShape: ({ a: "cross", b: "circle", x: "square", y: "triangle" })
  readonly property var psColor: ({ a: g.blue, b: g.red, x: g.pick("magenta", g.accent), y: g.green })
  readonly property var nintendoLetter: ({ a: "B", b: "A", x: "Y", y: "X" })
  readonly property var keyLabel: ({ a: "Enter", b: "Esc", x: "X", y: "Y", lb: "Q", rb: "E", guide: "G", dpad: "←↑→↓" })

  readonly property string bumperText: (family === "playstation" || family === "deck") ? (button === "lb" ? "L1" : "R1")
    : family === "nintendo" ? (button === "lb" ? "L" : "R") : (button === "lb" ? "LB" : "RB")

  readonly property color faceColor: xboxColor[button] || g.track
  readonly property color faceInk: Contrast.inkOn(Contrast.hex(faceColor), Contrast.hex(g.background), Contrast.hex(g.foreground))
  // Coloured discs only where the letter stays legible on them; otherwise a
  // neutral disc with the colour as its ring.
  readonly property bool coloured: (family === "xbox" || family === "deck") && face
    && Contrast.contrast(Contrast.hex(faceInk), Contrast.hex(faceColor)) >= Contrast.glyphFloor
  readonly property color discColor: coloured ? faceColor : g.track
  readonly property color ink: coloured ? faceInk : g.foreground

  implicitWidth: keyboard || bumper ? capText.implicitWidth + g.s(12) : size
  implicitHeight: size

  // Key caps and bumpers.
  Rectangle {
    visible: root.keyboard || root.bumper
    anchors.fill: parent
    radius: root.keyboard ? Math.min(root.g.s(4), root.g.radius) : height / 2
    color: root.g.well
    border.width: Math.max(1, root.g.s(1))
    border.color: Util.alpha(root.g.foreground, 0.35)
    Text {
      id: capText
      anchors.centerIn: parent
      text: root.keyboard ? (root.keyLabel[root.button] || root.button) : root.bumperText
      color: root.g.foreground
      font.family: root.g.font
      font.pixelSize: Math.round(root.size * 0.5)
      font.bold: true
    }
  }

  // Face buttons, Guide and the D-pad.
  Rectangle {
    visible: !root.keyboard && !root.bumper
    anchors.fill: parent
    radius: width / 2
    color: root.face ? root.discColor : root.g.track
    readonly property bool tinted: (root.family === "xbox" || root.family === "deck") && root.face && !root.coloured
    border.width: root.face && !root.coloured ? Math.max(tinted ? 2 : 1, root.g.s(tinted ? 2 : 1)) : 0
    border.color: tinted ? root.faceColor : Util.alpha(root.g.foreground, 0.35)

    Text {
      visible: root.face && root.family !== "playstation"
      anchors.centerIn: parent
      text: root.family === "nintendo" ? root.nintendoLetter[root.button] : root.button.toUpperCase()
      color: root.ink
      font.family: root.g.font
      font.pixelSize: Math.round(root.size * 0.6)
      font.bold: true
    }

    Canvas {
      id: shape
      visible: root.face && root.family === "playstation" || root.button === "dpad"
      anchors.centerIn: parent
      width: parent.width * 0.56
      height: width
      property string kind: root.button === "dpad" ? "dpad" : root.psShape[root.button] || ""
      property color stroke: root.button === "dpad" ? root.g.foreground : root.psColor[root.button] || root.g.foreground
      onKindChanged: requestPaint()
      onStrokeChanged: requestPaint()
      onPaint: {
        var c = getContext("2d"); c.reset()
        var w = width, h = height, lw = Math.max(1.4, w * 0.14)
        c.strokeStyle = stroke; c.fillStyle = stroke; c.lineWidth = lw; c.lineCap = "round"; c.lineJoin = "round"
        if (kind === "cross") { c.beginPath(); c.moveTo(lw, lw); c.lineTo(w - lw, h - lw); c.moveTo(w - lw, lw); c.lineTo(lw, h - lw); c.stroke() }
        else if (kind === "circle") { c.beginPath(); c.arc(w / 2, h / 2, w / 2 - lw / 2, 0, Math.PI * 2); c.stroke() }
        else if (kind === "square") { c.strokeRect(lw / 2 + 1, lw / 2 + 1, w - lw - 2, h - lw - 2) }
        else if (kind === "triangle") { c.beginPath(); c.moveTo(w / 2, lw / 2); c.lineTo(w - lw / 2, h - lw); c.lineTo(lw / 2, h - lw); c.closePath(); c.stroke() }
        else if (kind === "dpad") {
          var t = w * 0.34
          c.fillRect((w - t) / 2, 0, t, h); c.fillRect(0, (h - t) / 2, w, t)
        }
      }
    }

    Glyph {
      visible: root.button === "guide"
      g: root.g
      anchors.centerIn: parent
      size: root.size * 0.62
      name: root.family === "playstation" ? root.g.icon.playstation
        : root.family === "nintendo" ? root.g.icon.nintendo : root.g.icon.controllers
    }
  }
}
