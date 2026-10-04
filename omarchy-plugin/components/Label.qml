import QtQuick

// Guide text in one of a few roles. "caps" matches the shell's section headers.
Text {
  required property var g
  property string role: "body"

  readonly property var sizes: ({
    caps: 10, caption: 11, small: 12, body: 13, title: 15, heading: 18, display: 26, hero: 40
  })

  color: role === "caps" || role === "caption" || role === "small" ? g.dim : g.foreground
  font.family: g.font
  font.pixelSize: g.f(sizes[role] || 13)
  font.bold: role === "caps" || role === "title" || role === "heading" || role === "display" || role === "hero"
  font.letterSpacing: role === "caps" ? g.s(1.2) : (role === "hero" || role === "display" ? -g.s(0.5) : 0)
  font.capitalization: role === "caps" ? Font.AllUppercase : Font.MixedCase
  elide: Text.ElideRight
  textFormat: Text.PlainText
  verticalAlignment: Text.AlignVCenter
}
