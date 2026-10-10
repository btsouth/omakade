.pragma library

function parse(json) {
  var p
  try { p = JSON.parse(json) } catch (e) { return null }
  if (!p || p.version !== 1 || typeof p.output !== "string" || typeof p.pad !== "string"
      || !p.data || typeof p.data !== "object" || Array.isArray(p.data)) return null
  var game = p.data.game
  if (game !== undefined && game !== null && (typeof game !== "object"
      || typeof game.title !== "string" || typeof game.source !== "string"
      || typeof game.pauseWhileOpen !== "boolean" || typeof game.paused !== "boolean")) return null
  return p
}

// JSON merge patch: absent fields retain static data; null removes a reading.
function merge(before, patch) {
  var result = Object.assign({}, before || {})
  Object.keys(patch || {}).forEach(function(key) {
    var value = patch[key]
    if (value === null) delete result[key]
    else if (typeof value === "object" && !Array.isArray(value)) result[key] = merge(result[key], value)
    else result[key] = value
  })
  return result
}

function update(json, previous) {
  var p
  try { p = JSON.parse(json) } catch (e) { return null }
  if (p && p.delta === true) p.data = merge(previous, p.data)
  return parse(JSON.stringify(p))
}
