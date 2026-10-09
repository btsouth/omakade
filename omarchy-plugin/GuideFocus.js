.pragma library

// The card's focus model: its controls as rows of keys, top to bottom. The
// tiles share a row, as do Resume and Quit; every other control is a row of
// its own. Only what the card shows is in the grid, so nothing is skipped.
//
// `shown` says what is on the card: {game, replay, achievements, volume, outputs}.
function grid(shown) {
  var s = shown || {}
  return [
    ["screenshot", "record"].concat(s.replay ? ["replay"] : [], s.game ? ["desktop", "library"] : []),
    s.achievements ? ["achievements"] : [],
    s.volume ? ["volume"] : [],
    (s.outputs || 0) > 1 ? ["output"] : [],
    s.game ? ["resume", "quit"] : []
  ].filter(function(row) { return row.length > 0 })
}

// Where the cursor starts each time the card opens.
function home(rows) {
  for (var i = 0; i < rows.length; i++) if (rows[i].indexOf("resume") >= 0) return "resume"
  return rows.length ? rows[0][0] : ""
}

// The horizontal position the cursor starts with: under the first tile.
var homeAnchor = 0.1

// Fraction across the card of the item at `column` in a row of `count`.
function across(column, count) {
  return (column + 0.5) / count
}

// The remembered position while it still lies over the item at `column`,
// otherwise that item's centre: a pointer or a reflow may have moved it.
function settle(anchor, column, count) {
  if (anchor !== undefined && anchor >= column / count && anchor < (column + 1) / count) return anchor
  return across(column, count)
}

// One D-pad step. Up and down change row and wrap at the ends; on a row of
// several items left and right move along it and stop at its ends. `anchor` is
// the horizontal position (0..1) the cursor last had on a row of several items:
// moving through one-item rows keeps it, so Library, down to Achievements and
// Volume, then down again lands on Quit, below it. Returns {key, anchor}, or
// null where the step is not a move: left and right on a one-item row belong to
// that control (the volume level, the sound output).
function move(rows, cursor, action, anchor) {
  var r = -1, c = 0
  for (var i = 0; i < rows.length; i++) {
    var at = rows[i].indexOf(cursor)
    if (at >= 0) { r = i; c = at; break }
  }
  if (r < 0) {
    var start = home(rows)
    return start ? {key: start, anchor: homeAnchor} : null
  }
  var here = rows[r].length > 1 ? settle(anchor, c, rows[r].length) : (anchor === undefined ? homeAnchor : anchor)
  if (action === "up" || action === "down") {
    var nr = (r + (action === "up" ? -1 : 1) + rows.length) % rows.length
    var count = rows[nr].length
    // A position on the line between two items takes the left one.
    var nc = Math.min(count - 1, Math.max(0, Math.ceil(here * count) - 1))
    return {key: rows[nr][nc], anchor: here}
  }
  if (action !== "left" && action !== "right") return null
  if (rows[r].length === 1) return null
  var next = Math.max(0, Math.min(rows[r].length - 1, c + (action === "left" ? -1 : 1)))
  return {key: rows[r][next], anchor: across(next, rows[r].length)}
}
