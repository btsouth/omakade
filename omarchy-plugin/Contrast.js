.pragma library

// Legibility maths for the guide surface. The panel is a frosted copy of the game
// under a base of the theme's darkest background at 0.6 and a tint of the theme's
// background. The game can be anything, so every check runs over a pure black and a
// pure white frame, at one 8-bit rounding step either side. Focus is a ring around a
// control, never a fill under its text, so it leaves these numbers alone. The tint
// alpha is the clearest value that keeps every role at its floor: the fixed minimum,
// or a share of the role's contrast on the theme's own background when the theme
// itself sits below that minimum.

var glassBase = 0.6;

function rgb(c) {
    if (typeof c === "string") return [1, 3, 5].map(function(i) { return parseInt(c.slice(i, i + 2), 16) / 255; });
    return [c.r, c.g, c.b];
}

function luminance(c) {
    var v = rgb(c).map(function(x) { return x <= 0.03928 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4); });
    return v[0] * 0.2126 + v[1] * 0.7152 + v[2] * 0.0722;
}

function contrast(a, b) {
    var x = luminance(a), y = luminance(b);
    return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
}

function over(c, a, b) {
    var x = rgb(c), y = rgb(b);
    var v = x.map(function(n, i) { return n * a + y[i] * (1 - a); });
    return { r: v[0], g: v[1], b: v[2] };
}

function hex(c) {
    return "#" + rgb(c).map(function(x) {
        var s = Math.round(Math.max(0, Math.min(1, x)) * 255).toString(16);
        return s.length < 2 ? "0" + s : s;
    }).join("");
}

function quantized(c, step) {
    var v = rgb(c).map(function(x) { return Math.max(0, Math.min(255, Math.round(x * 255) + step)) / 255; });
    return { r: v[0], g: v[1], b: v[2] };
}

// Roles: [name, colour, minimum, share of own-background contrast].
function roles(t) {
    return [["body", t.foreground, 7, 0.9],
            ["dim", t.dim, 4.5, 0.85],
            ["accent", t.accent, 3, 0.85],
            ["urgent", t.urgent, 3, 0.85]];
}

function surface(t, alpha, frame) {
    return over(t.background, alpha, over(t.base, glassBase, frame));
}

function floorFor(t, r) {
    return Math.min(r[2], r[3] * contrast(r[1], t.background));
}

function passes(t, a) {
    return roles(t).every(function(r) {
        var floor = floorFor(t, r);
        return ["#000000", "#ffffff"].every(function(frame) {
            return [-1, 0, 1].every(function(step) {
                return contrast(r[1], quantized(surface(t, a, frame), step)) >= floor;
            });
        });
    });
}

function alpha(t) {
    for (var i = 55; i <= 100; ++i) {
        if (passes(t, i / 100)) return i / 100;
    }
    return 1;
}

function audit(t) {
    var a = alpha(t);
    return { alpha: a, roles: roles(t).map(function(r) {
        var worst = Infinity;
        ["#000000", "#ffffff"].forEach(function(frame) {
            worst = Math.min(worst, contrast(r[1], surface(t, a, frame)));
        });
        var floor = floorFor(t, r);
        return { role: r[0], min: worst, floor: floor, pass: worst >= floor };
    }) };
}

// Face-button letters on a coloured disc need this much contrast, or the disc
// goes neutral with a coloured ring.
var glyphFloor = 3;

// Text drawn on a coloured disc (controller face buttons): whichever theme
// colour reads best on it.
function inkOn(fill, light, dark) {
    return contrast(light, fill) >= contrast(dark, fill) ? light : dark;
}
