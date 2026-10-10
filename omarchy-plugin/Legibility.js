.pragma library

// WCAG contrast for the card. The card keeps the theme's menu colours; where a
// theme's own pairing falls under the release bar (4.5:1 for text, 3:1 for the
// focus edge and status marks), the colour moves only as far as it must toward
// the theme's text colour. Cards may be translucent, so every check is made
// over the card composited on a black and on a white game frame.

function channel(v) {
  return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
}

function luminance(c) {
  return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b)
}

function contrast(a, b) {
  var la = luminance(a), lb = luminance(b)
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
}

function mix(fg, bg, t) {
  return { r: fg.r * t + bg.r * (1 - t), g: fg.g * t + bg.g * (1 - t), b: fg.b * t + bg.b * (1 - t), a: 1 }
}

// `top` drawn with its own alpha over an opaque `base`.
function over(top, base) {
  var a = top.a === undefined ? 1 : top.a
  return mix(top, base, a)
}

// What a surface looks like over the darkest and the lightest game frame.
function grounds(surface, beneath) {
  return (beneath || [{ r: 0, g: 0, b: 0 }, { r: 1, g: 1, b: 1 }]).map(function(b) { return over(surface, b) })
}

function legible(c, list, ratio) {
  for (var i = 0; i < list.length; i++) if (contrast(c, list[i]) < ratio) return false
  return true
}

// `fg` blended into `base` at the lowest strength from `preferred` up that
// keeps `ratio` against every ground.
function fade(fg, base, list, preferred, ratio) {
  for (var t = preferred; t < 1; t += 0.01) {
    var c = mix(fg, base, t)
    if (legible(c, list, ratio)) return c
  }
  return mix(fg, base, 1)
}

// `ink` as it is when legible, otherwise moved toward `target` until it is.
function toward(ink, target, list, ratio) {
  for (var t = 0; t <= 1; t += 0.02) {
    var c = mix(target, ink, t)
    if (legible(c, list, ratio)) return c
  }
  return mix(target, ink, 1)
}

// `ink` made legible on every ground: toward the theme's text colour first,
// toward black or white only if even that colour falls short.
function legibleInk(ink, text, list, ratio) {
  var c = toward(ink, text, list, ratio)
  if (legible(c, list, ratio)) return c
  var light = luminance(list[0]) > 0.18
  return toward(ink, light ? { r: 0, g: 0, b: 0 } : { r: 1, g: 1, b: 1 }, list, ratio)
}

function weakest(a, b) {
  var low = 99
  for (var i = 0; i < a.length; i++) low = Math.min(low, contrast(a[i], b[i]))
  return low
}
