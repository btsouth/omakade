.pragma library

// Cursor moves over a grid of rows (each a list of keys). Up and down change
// row, wrapping, and keep the position across in proportion; left and right
// move along a row. Returns the next key, or "" when a one-item row leaves
// left and right to the item itself (the volume slider).
function move(grid, cursor, action) {
  var r = -1, c = 0
  for (var i = 0; i < grid.length; i++) {
    var at = grid[i].indexOf(cursor)
    if (at >= 0) { r = i; c = at; break }
  }
  if (r < 0) return grid.length ? grid[0][0] : ""
  if (action === "up" || action === "down") {
    var nr = (r + (action === "up" ? -1 : 1) + grid.length) % grid.length
    var nc = Math.min(grid[nr].length - 1, Math.floor(c * grid[nr].length / grid[r].length))
    return grid[nr][nc]
  }
  if (grid[r].length === 1) return ""
  return grid[r][Math.max(0, Math.min(grid[r].length - 1, c + (action === "left" ? -1 : 1)))]
}
