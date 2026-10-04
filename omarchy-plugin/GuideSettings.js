.pragma library
function couchScale(setting, widthMm) {
  if (["1", "1.25", "1.5", "2"].indexOf(String(setting)) >= 0) return Number(setting)
  return widthMm >= 800 ? 1.5 : widthMm > 0 && widthMm <= 200 ? 1.25 : 1
}
function settings(value) {
  value = value || {}
  return {couch: ["auto", "1", "1.25", "1.5", "2"].indexOf(value.couch) >= 0 ? value.couch : "auto",
    replaySeconds: [30, 60, 120].indexOf(value.replaySeconds) >= 0 ? value.replaySeconds : 30,
    sound: ["game", "game-mic", "none"].indexOf(value.sound) >= 0 ? value.sound : "game",
    prompts: ["auto", "xbox", "playstation", "nintendo", "deck", "generic"].indexOf(value.prompts) >= 0 ? value.prompts : "auto"}
}
