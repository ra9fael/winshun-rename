// widgets_theme.h — Win顺's appearance tokens for the Qt Widgets window.
//
// The host sends `appearance.nativeLook` (docs/plugins.md) with colours as
// "#AARRGGBB" strings and radii as logical pixels. This resolves that map once,
// with the same fallbacks Tokens.qml used, and turns it into a QPalette plus a
// stylesheet. Keeping the token names identical means a host change reaches the
// widgets without a second theme definition.
#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QVariantMap>

class QWidget;

namespace rename_widgets {

struct Theme
{
    bool dark = true;
    bool rounded = true;
    QString style = QStringLiteral("fluent");

    QColor background;
    QColor layer;
    QColor page;
    QColor divider;
    QColor text;
    QColor subtext;
    QColor faint;
    QColor hover;
    QColor selection;
    QColor pressed;
    QColor control;
    QColor controlHover;
    QColor controlBorder;
    QColor inputFocused;
    QColor accent;
    QColor accentFill;
    QColor accentText;
    QColor danger;
    QColor dangerFill;
    QColor menuBackground;
    QColor menuHover;
    QColor menuEdge;
    QColor textSelection;
    QColor ok;
    QColor caution;

    int controlRadius = 4;
    int menuRadius = 8;
    int smallControlRadius = 3;
    int controlHeight = 32;
    int rowHeight = 34;
    int fontBody = 16;
    int fontCaption = 14;
    int fontSmall = 12;

    QFont font;
    QFont mono;

    static Theme From(const QVariantMap& look, bool dark, const QString& uiSize,
                      const QString& locale);

    // The widget's palette and stylesheet. Cheap enough to redo when the host
    // sends a new appearance.
    void apply(QWidget* widget) const;
    QString styleSheet() const;
};

} // namespace rename_widgets
