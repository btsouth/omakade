.pragma library

// The preview and the audit use this same calculation. The game (including grain and
// saturated blur) is bounded by black/white before the 0.6 base and the theme tint.
var glassBase = 0.6;
function rgb(c) {
    if (typeof c !== "string") return [c.r, c.g, c.b];
    return [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16) / 255);
}
function luminance(c) {
    const v = rgb(c).map(x => x <= 0.03928 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4));
    return v[0] * 0.2126 + v[1] * 0.7152 + v[2] * 0.0722;
}
function contrast(a, b) {
    const x = luminance(a), y = luminance(b);
    return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
}
function over(c, a, b) {
    const x = rgb(c), y = rgb(b);
    const v = x.map((n, i) => n * a + y[i] * (1 - a));
    return {r: v[0], g: v[1], b: v[2]};
}
function roles(t) {
    return [["body", t.foreground, 7, 0.9], ["bright", t.brightforeground, 7, 0.9],
            ["muted", t.mutedtext, 4.5, 0.85], ["accent", t.accent, 3, 0.85]];
}
function background(t, alpha, frame) {
    return over(t.background, alpha, over(t.darkerbackground, glassBase, frame));
}
// Account for the 8-bit target and one channel step of layered blend rounding.
// Pick the clearest alpha that meets the original floors even at those bounds.
function quantized(c, step) {
    const v = rgb(c).map(x => Math.max(0, Math.min(255, Math.round(x * 255) + step)) / 255);
    return {r: v[0], g: v[1], b: v[2]};
}
function alpha(t) {
    for (let i = 55; i <= 100; ++i) {
        const a = i / 100;
        if (roles(t).every(r => ["#000000", "#ffffff"].every(frame =>
            [-1, 0, 1].every(step => contrast(r[1], quantized(background(t, a, frame), step)) >=
                Math.min(r[2], r[3] * contrast(r[1], t.background)))))) return a;
    }
    return 1;
}
function audit(t) {
    const a = alpha(t);
    return {theme: t.name, slug: t.slug, alpha: a, roles: roles(t).map(r => {
        const black = contrast(r[1], background(t, a, "#000000"));
        const white = contrast(r[1], background(t, a, "#ffffff"));
        const floor = Math.min(r[2], r[3] * contrast(r[1], t.background));
        return {role: r[0], black: black, white: white, min: Math.min(black, white), floor: floor,
                pass: Math.min(black, white) >= floor};
    })};
}
