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
