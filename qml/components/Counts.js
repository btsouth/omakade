.pragma library

// A count with its noun, so a label reads "1 game" and "2 games". Pass the words in the
// label's own case ("GAME", "GAMES"); the plural defaults to the singular plus "s".
function text(value, singular, plural) {
    const count = Number(value) || 0
    return count + " " + (count === 1 ? singular : (plural || singular + "s"))
}
