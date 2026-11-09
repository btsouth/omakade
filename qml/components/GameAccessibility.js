.pragma library

// Screen readers hear a game card's title as its name and this as its description, so they
// get the state the card shows on screen: where the game comes from, its playtime and rating,
// whether it is a favorite, its completion status and whether it can be launched.
function cardDescription(game) {
    const parts = []
    if (game.source) parts.push(game.source)
    if (game.playtime) parts.push(game.playtime + " played")
    if (game.rating !== undefined && game.rating >= 0) parts.push("rated " + game.rating + " out of 100")
    if (game.favorite) parts.push("favorite")
    if (game.completionStatus) parts.push("status " + game.completionStatus)
    if (game.installed === false) parts.push("not installed")
    if (game.available === false) parts.push("unavailable")
    return parts.join(", ")
}

// A hidden achievement keeps its name and description secret until it is unlocked, for
// screen readers as well as on screen.
function achievementName(achievement) {
    return achievement.hidden && !achievement.unlocked ? "Hidden achievement" : achievement.title
}

// unlockedOn is the already formatted unlock date, or empty when it is not known.
function achievementDescription(achievement, unlockedOn) {
    const parts = []
    if (achievement.hidden && !achievement.unlocked) parts.push("Unlock to reveal details")
    else if (achievement.description) parts.push(achievement.description)
    parts.push(achievement.unlocked ? (unlockedOn ? "unlocked " + unlockedOn : "unlocked") : "locked")
    if (achievement.rarity > 0) parts.push(achievement.rarity.toFixed(1) + "% of players")
    return parts.join(", ")
}
