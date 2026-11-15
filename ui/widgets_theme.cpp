#include "ui/widgets_theme.h"

#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QWidget>

namespace rename_widgets {

namespace {

QColor LookColor(const QVariantMap& look, const char* key, const QColor& fallback)
{
    const auto it = look.constFind(QString::fromLatin1(key));
    if (it == look.cend())
        return fallback;

    if (it->canConvert<QColor>())
    {
        const QColor direct = it->value<QColor>();
        if (direct.isValid())
            return direct;
    }

    const QString text = it->toString().trimmed();
    if (text.startsWith(QLatin1Char('#')))
    {
        // The host's contract is "#AARRGGBB"; QColor also accepts #RRGGBB.
        if (text.size() == 9)
        {
            bool ok = false;
            const int a = text.mid(1, 2).toInt(&ok, 16);
            const int r = ok ? text.mid(3, 2).toInt(&ok, 16) : 0;
            const int g = ok ? text.mid(5, 2).toInt(&ok, 16) : 0;
            const int b = ok ? text.mid(7, 2).toInt(&ok, 16) : 0;
            if (ok)
                return QColor(r, g, b, a);
        }
        const QColor parsed(text);
        if (parsed.isValid())
            return parsed;
    }
    return fallback;
}

int LookInt(const QVariantMap& look, const char* key, int fallback)
{
    const auto it = look.constFind(QString::fromLatin1(key));
    if (it == look.cend())
        return fallback;
    bool ok = false;
    const int value = it->toInt(&ok);
    return ok ? value : fallback;
}

bool LookBool(const QVariantMap& look, const char* key, bool fallback)
{
    const auto it = look.constFind(QString::fromLatin1(key));
    return it == look.cend() ? fallback : it->toBool();
}

QString Rgba(const QColor& colour)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(colour.red())
        .arg(colour.green())
        .arg(colour.blue())
        .arg(QString::number(colour.alphaF(), 'f', 3));
}

} // namespace

Theme Theme::From(const QVariantMap& look, bool dark, const QString& uiSize,
                  const QString& locale)
{
    Theme t;
    t.dark = dark;
    t.rounded = LookBool(look, "rounded", true);
    t.style = look.value(QStringLiteral("style"), QStringLiteral("fluent")).toString();

    const QColor darkBackground(QStringLiteral("#202020"));
    const QColor darkLayer(QStringLiteral("#272727"));
    const QColor lightBackground(QStringLiteral("#F9F9F9"));
    const QColor lightLayer(QStringLiteral("#FFFFFF"));

    t.background = LookColor(look, "background", dark ? darkBackground : lightBackground);
    t.layer = LookColor(look, "layer", dark ? darkLayer : lightLayer);
    t.page = LookColor(look, "page", dark ? darkBackground : QColor(QStringLiteral("#F3F3F3")));
    t.divider = LookColor(look, "divider", dark ? QColor(QStringLiteral("#2E2E2E")) : QColor(QStringLiteral("#EAEAEA")));
    t.text = LookColor(look, "text", dark ? QColor(QStringLiteral("#F3F3F3")) : QColor(QStringLiteral("#1B1B1B")));
    t.subtext = LookColor(look, "subtext", dark ? QColor(QStringLiteral("#B8B8B8")) : QColor(QStringLiteral("#666666")));
    t.faint = LookColor(look, "faint", QColor(QStringLiteral("#8A8A8A")));
    t.hover = LookColor(look, "hover", dark ? QColor(QStringLiteral("#292929")) : QColor(QStringLiteral("#F1F1F1")));
    t.selection = LookColor(look, "selection", dark ? QColor(QStringLiteral("#2F2F2F")) : QColor(QStringLiteral("#EAEAEA")));
    t.pressed = LookColor(look, "pressed", dark ? QColor(QStringLiteral("#323232")) : QColor(QStringLiteral("#E8E8E8")));
    t.control = LookColor(look, "control", dark ? QColor(QStringLiteral("#373737")) : QColor(QStringLiteral("#FFFFFF")));
    t.controlHover = LookColor(look, "controlHover", dark ? QColor(QStringLiteral("#3C3C3C")) : QColor(QStringLiteral("#F6F6F6")));
    t.controlBorder = LookColor(look, "controlBorder", dark ? QColor(QStringLiteral("#444444")) : QColor(QStringLiteral("#D5D5D5")));
    t.inputFocused = LookColor(look, "inputFocused", dark ? QColor(QStringLiteral("#1F1F1F")) : QColor(QStringLiteral("#FFFFFF")));
    t.menuBackground = LookColor(look, "menuBackground", dark ? QColor(QStringLiteral("#2C2C2C")) : QColor(QStringLiteral("#F9F9F9")));
    t.menuHover = LookColor(look, "menuHover", dark ? QColor(QStringLiteral("#3A3A3A")) : QColor(QStringLiteral("#ECECEC")));
    t.menuEdge = LookColor(look, "menuEdge", dark ? QColor(QStringLiteral("#3A3A3A")) : QColor(QStringLiteral("#E5E5E5")));
    t.accent = LookColor(look, "accent", dark ? QColor(QStringLiteral("#4CC2FF")) : QColor(QStringLiteral("#0067C0")));
    t.accentFill = LookColor(look, "accentFill", QColor(QStringLiteral("#0067C0")));
    t.accentText = LookColor(look, "accentText", QColor(QStringLiteral("#FFFFFF")));
    t.danger = LookColor(look, "danger", dark ? QColor(QStringLiteral("#FF99A4")) : QColor(QStringLiteral("#C42B1C")));
    t.dangerFill = LookColor(look, "dangerFill", QColor(QStringLiteral("#C42B1C")));
    t.ok = LookColor(look, "ok", dark ? QColor(QStringLiteral("#6CCB5F")) : QColor(QStringLiteral("#0F7B0F")));
    t.caution = LookColor(look, "caution", dark ? QColor(QStringLiteral("#FCE100")) : QColor(QStringLiteral("#9D5D00")));
    t.textSelection = LookColor(look, "textSelection", QColor(t.accent.red(), t.accent.green(), t.accent.blue(), 90));

    t.controlRadius = t.rounded ? LookInt(look, "controlRadius", 4) : 0;
    t.menuRadius = t.rounded ? LookInt(look, "menuRadius", 8) : 0;
    t.smallControlRadius = t.rounded ? LookInt(look, "smallControlRadius", 3) : 0;

    // The host's own steps (UiScale.qml): dense, compact, standard, roomy.
    const int level = uiSize == QStringLiteral("dense") ? 0
                    : uiSize == QStringLiteral("compact") ? 1
                    : uiSize == QStringLiteral("roomy") ? 3
                                                       : 2;
    static const int fontBody[] = {14, 14, 16, 18};
    static const int fontCaption[] = {12, 12, 14, 16};
    static const int fontSmall[] = {12, 12, 12, 14};
    static const int controlHeight[] = {28, 32, 36, 40};
    static const int rowHeight[] = {30, 32, 34, 38};
    t.fontBody = fontBody[level];
    t.fontCaption = fontCaption[level];
    t.fontSmall = fontSmall[level];
    t.controlHeight = controlHeight[level];
    t.rowHeight = rowHeight[level];

    // A direct CJK family matters: Qt's fallback from a Latin-only family to a
    // CJK face loads a very large font cache (measured: 122 MB private for four
    // Chinese labels versus 9 MB with Microsoft YaHei UI selected explicitly).
    // Choosing the family here keeps the Widgets window in single-digit MBs.
    const bool zh = locale.startsWith(QLatin1String("zh"));
    t.font = QFont(zh ? QStringLiteral("Microsoft YaHei UI")
                      : QStringLiteral("Segoe UI Variable Text"));
    t.font.setStyleHint(QFont::SansSerif);
    t.font.setPixelSize(t.fontBody);
    t.font.setStyleStrategy(QFont::PreferAntialias);

    // Text mode is a vim/editor surface: pick a monospace family that is really
    // installed (Cascadia Mono ships with Windows Terminal, not with Windows),
    // and keep the CJK UI font as a fallback so Chinese file names still render.
    const QStringList families = QFontDatabase::families();
    QString monoFamily;
    for (const QString& candidate : {QStringLiteral("Cascadia Mono"),
                                     QStringLiteral("Cascadia Code"),
                                     QStringLiteral("Consolas"),
                                     QStringLiteral("Courier New")})
    {
        if (families.contains(candidate, Qt::CaseInsensitive))
        {
            monoFamily = candidate;
            break;
        }
    }
    if (monoFamily.isEmpty())
        monoFamily = QStringLiteral("Consolas");
    t.mono = QFont(monoFamily);
    t.mono.setStyleHint(QFont::Monospace);
    if (zh)
        t.mono.setFamilies({monoFamily, t.font.family()}); // mono Latin, CJK fallback
    t.mono.setPixelSize(t.fontBody);
    return t;
}

void Theme::apply(QWidget* widget) const
{
    if (!widget)
        return;

    widget->setFont(font);

    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, control);
    palette.setColor(QPalette::AlternateBase, layer);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, control);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, danger);
    palette.setColor(QPalette::Highlight, accentFill);
    palette.setColor(QPalette::HighlightedText, accentText);
    palette.setColor(QPalette::ToolTipBase, menuBackground);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, faint);
    palette.setColor(QPalette::Disabled, QPalette::Text, faint);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, faint);
    widget->setPalette(palette);
    widget->setStyleSheet(styleSheet());
}

QString Theme::styleSheet() const
{
    QString qss = QStringLiteral(R"QSS(
QWidget {
    background: @background;
    color: @text;
}
QLabel#caption, QLabel#footer, QLabel#small {
    color: @subtext;
    background: transparent;
}
QLabel#topTitle {
    color: @text;
    font-weight: 600;
    background: transparent;
}
QLabel#empty {
    background: transparent;
    padding: 24px;
}
QLineEdit, QComboBox, QPlainTextEdit {
    background: @control;
    color: @text;
    border: 1px solid @controlBorder;
    border-radius: @controlRadius;
    padding: 4px 7px;
    selection-background-color: @textSelection;
    selection-color: @text;
}
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus {
    background: @inputFocused;
    border-color: @accent;
}
QLineEdit[invalid="true"] {
    border-color: @danger;
}
QComboBox::drop-down {
    border: none;
    width: 22px;
}
QComboBox QAbstractItemView {
    background: @menuBackground;
    color: @text;
    border: 1px solid @menuEdge;
    selection-background-color: @menuHover;
    selection-color: @text;
    outline: 0;
}
QPushButton {
    background: @control;
    color: @text;
    border: 1px solid @controlBorder;
    border-radius: @controlRadius;
    padding: 6px 12px;
}
QPushButton:hover {
    background: @controlHover;
}
QPushButton:pressed {
    background: @pressed;
}
QPushButton#primary {
    background: @accentFill;
    color: @accentText;
    border: 1px solid @accentFill;
    font-weight: 600;
    padding: 6px 16px;
}
QPushButton#primary:hover {
    background: @accent;
    border-color: @accent;
}
QPushButton#primary:disabled {
    background: @control;
    color: @faint;
    border: 1px solid @controlBorder;
    font-weight: normal;
}
QToolButton#modeButton {
    background: transparent;
    color: @subtext;
    border: none;
    border-radius: @controlRadius;
    padding: 3px 10px;
}
QToolButton#modeButton:hover {
    background: @hover;
    color: @text;
}
QToolButton#modeButton:checked {
    background: @accentFill;
    color: @accentText;
    font-weight: 600;
}
QToolButton#chipButton {
    background: @control;
    color: @text;
    border: 1px solid @controlBorder;
    border-radius: @controlRadius;
    padding: 4px 8px;
}
QToolButton#chipButton:hover {
    background: @controlHover;
}
QToolButton#chipButton:checked {
    background: @accentFill;
    color: @accentText;
    border-color: @accentFill;
}
QCheckBox {
    color: @text;
    spacing: 8px;
    background: transparent;
}
QTableView {
    background: @background;
    color: @text;
    border: none;
    outline: 0;
    selection-background-color: @selection;
    selection-color: @text;
}
QTableView::item {
    padding: 0 4px;
}
QTableView::item:selected {
    background: @selection;
    color: @text;
}
QTableView::item:hover {
    background: @hover;
    color: @text;
}
QHeaderView::section {
    background: @background;
    color: @subtext;
    border: none;
    padding: 0;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: @controlBorder;
    min-height: 24px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover {
    background: @subtext;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QToolTip {
    background: @menuBackground;
    color: @text;
    border: 1px solid @menuEdge;
    padding: 4px 6px;
}
)QSS");

    const auto replace = [&qss](const char* key, const QColor& colour) {
        qss.replace(QString::fromLatin1(key), colour.name(QColor::HexRgb));
    };
    // Radius and multi-word tokens first: "@text" would otherwise eat
    // "@textSelection", and "@control" would eat "@controlRadius".
    qss.replace(QStringLiteral("@textSelection"), Rgba(textSelection));
    qss.replace(QStringLiteral("@controlRadius"), QString::number(controlRadius));
    qss.replace(QStringLiteral("@menuRadius"), QString::number(menuRadius));
    qss.replace(QStringLiteral("@smallControlRadius"), QString::number(smallControlRadius));

    replace("@background", background);
    replace("@selection", selection);
    replace("@controlBorder", controlBorder);
    replace("@controlHover", controlHover);
    replace("@control", control);
    replace("@menuBackground", menuBackground);
    replace("@menuHover", menuHover);
    replace("@menuEdge", menuEdge);
    replace("@accentFill", accentFill);
    replace("@accentText", accentText);
    replace("@accent", accent);
    replace("@inputFocused", inputFocused);
    replace("@divider", divider);
    replace("@subtext", subtext);
    replace("@faint", faint);
    replace("@hover", hover);
    replace("@pressed", pressed);
    replace("@danger", danger);
    replace("@layer", layer);
    replace("@page", page);
    replace("@text", text);
    return qss;
}

} // namespace rename_widgets
