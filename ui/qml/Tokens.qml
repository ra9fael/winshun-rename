// Theme.qml — the window's design tokens, taken from Win顺's own look.
//
// The host sends `appearance.nativeLook` (docs/plugins.md, 外观) with colours as
// "#AARRGGBB" strings and radii in logical pixels already resolved by its
// 圆角/直角 setting. `Bridge.ApplyAppearance` keeps that map verbatim and this
// object resolves it, so the window has exactly one source of truth for a token.
//
// Nothing here is a second theme system: a key the host did not send falls back
// to the value below, which is Win顺's own fluent look at 100 %, and `uiStyle`,
// `uiSize` and `dark` only decide which fallback applies. A host that sends no
// look at all (older Win顺, or the moment before its interface is built) still
// gets a window that matches the light or dark theme it is in.
//
// This object is created from C++ and exposed to the scene as the context
// property `Theme` (ui/qt_window_host.cpp), so every component reads the same
// tokens and `look` can be replaced in one place when the host's look changes.
import QtQuick

QtObject {
    id: root

    // `appearance.nativeLook`, verbatim. Empty means "no look from the host".
    property var look: ({})

    function pick(key, fallback) {
        const value = look ? look[key] : undefined;
        return (value === undefined || value === null || value === "") ? fallback : value
    }

    // ---- the host's choices this window has to follow ----------------------
    readonly property bool dark: look && look.dark !== undefined ? look.dark : false
    readonly property bool rounded: look && look.rounded !== undefined ? look.rounded : true
    readonly property string style: look && look.style ? look.style : "fluent"
    // `uiSize` arrives beside nativeLook, not inside it (Bridge.uiSize).
    property string uiSize: "standard"

    // A square setting zeroes every radius, and so does every capsule a
    // plugin computes itself (docs/plugins.md): a pill is a radius too.
    function r(value) { return rounded ? value : 0 }
    function pill(height) { return rounded ? height / 2 : 0 }

    // ---- type and row heights, per 界面大小 --------------------------------
    // The host's own steps (src/app/qml/UiScale.qml), so this window's text and
    // rows grow and shrink with the launcher's instead of beside them.
    readonly property int level: {
        const at = ["dense", "compact", "standard", "roomy"].indexOf(uiSize)
        return at < 0 ? 2 : at
    }
    readonly property int fontBody: [14, 14, 16, 18][level]
    readonly property int fontCaption: [12, 12, 14, 16][level]
    readonly property int fontSmall: [12, 12, 12, 14][level]
    readonly property int fontTitle: [14, 16, 18, 20][level]
    readonly property int rowHeight: [32, 52, 60, 68][level]
    readonly property int headerHeight: [44, 56, 64, 72][level]
    readonly property int footerHeight: [32, 36, 42, 48][level]
    readonly property int controlHeight: [28, 32, 36, 40][level]
    readonly property string fontFamily: "Segoe UI Variable Text"
    readonly property string monoFamily: "Cascadia Mono"

    // ---- radii ------------------------------------------------------------
    readonly property int cardRadius: r(pick("cardRadius", 6))
    readonly property int controlRadius: r(pick("controlRadius", 4))
    readonly property int largeControlRadius: r(pick("largeControlRadius", 6))
    readonly property int smallControlRadius: r(pick("smallControlRadius", 3))
    readonly property int menuRadius: r(pick("menuRadius", 8))

    // ---- colours ----------------------------------------------------------
    readonly property color background: pick("background", dark ? "#202020" : "#F9F9F9")
    readonly property color layer: pick("layer", dark ? "#272727" : "#FFFFFF")
    readonly property color page: pick("page", dark ? "#202020" : "#F3F3F3")
    readonly property color windowEdge: pick("windowEdge", dark ? "#6A6A6A" : "#B4B4B4")
    readonly property color divider: pick("divider", dark ? "#2E2E2E" : "#EAEAEA")
    readonly property color text: pick("text", dark ? "#F3F3F3" : "#1B1B1B")
    readonly property color subtext: pick("subtext", dark ? "#B8B8B8" : "#666666")
    readonly property color faint: pick("faint", "#8A8A8A")
    readonly property color hover: pick("hover", dark ? "#292929" : "#F1F1F1")
    readonly property color pressed: pick("pressed", dark ? "#323232" : "#E8E8E8")
    readonly property color selection: pick("selection", dark ? "#2F2F2F" : "#EAEAEA")
    readonly property color textSelection: pick("textSelection", Qt.rgba(accent.r, accent.g, accent.b, 0.35))
    readonly property color tagFill: pick("tagFill", dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.05))
    readonly property color control: pick("control", dark ? "#373737" : "#FFFFFF")
    readonly property color controlHover: pick("controlHover", dark ? "#3C3C3C" : "#F6F6F6")
    readonly property color controlBorder: pick("controlBorder", dark ? "#444444" : "#D5D5D5")
    readonly property color inputFocused: pick("inputFocused", dark ? "#1F1F1F" : "#FFFFFF")
    readonly property color menuBackground: pick("menuBackground", dark ? "#2C2C2C" : "#F9F9F9")
    readonly property color menuHover: pick("menuHover", dark ? "#3A3A3A" : "#ECECEC")
    readonly property color menuEdge: pick("menuEdge", dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.10))
    readonly property color accent: pick("accent", dark ? "#4CC2FF" : "#0067C0")
    readonly property color accentFill: pick("accentFill", "#0067C0")
    readonly property color accentLine: pick("accentLine", accent)
    readonly property color accentText: pick("accentText", "#FFFFFF")
    readonly property color danger: pick("danger", dark ? "#FF99A4" : "#C42B1C")
    readonly property color dangerFill: pick("dangerFill", "#C42B1C")
    // Status is never colour alone (UI-SPEC.md §4.3); this only tints the word.
    readonly property color ok: dark ? "#6CCB5F" : "#0F7B0F"
    readonly property color caution: dark ? "#FCE100" : "#9D5D00"
}
