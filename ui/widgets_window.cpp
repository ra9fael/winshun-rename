#include "ui/widgets_window.h"

#include "ui/bridge.h"
#include "ui/vim_qt.h"
#include "ui/widgets_theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QListView>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstdio>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

namespace rename_widgets {

namespace {

QFrame* Divider(Qt::Orientation orientation, QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setLineWidth(1);
    return line;
}

QString StatusTitle(rename_core::Status status, const QString& target, bool zh)
{
    switch (status)
    {
    case rename_core::Status::ErrorAlreadyExists:
        return zh ? QStringLiteral("目标已存在：") + target
                  : QStringLiteral("target exists: ") + target;
    case rename_core::Status::ErrorTooLong:
        return zh ? QStringLiteral("名字过长") : QStringLiteral("too long");
    case rename_core::Status::ErrorInvalidCharacter:
        return zh ? QStringLiteral("含非法字符") : QStringLiteral("invalid char");
    case rename_core::Status::ErrorEmptyName:
        return zh ? QStringLiteral("空名字") : QStringLiteral("empty name");
    case rename_core::Status::ErrorBadRule:
        return zh ? QStringLiteral("规则错误") : QStringLiteral("bad rule");
    default:
        return target;
    }
}

bool IsError(rename_core::Status status)
{
    switch (status)
    {
    case rename_core::Status::ErrorAlreadyExists:
    case rename_core::Status::ErrorTooLong:
    case rename_core::Status::ErrorInvalidCharacter:
    case rename_core::Status::ErrorEmptyName:
    case rename_core::Status::ErrorBadRule:
        return true;
    default:
        return false;
    }
}

void ApplyDarkTitleBar(QWidget* widget, bool dark)
{
#ifdef Q_OS_WIN
    if (!widget)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    BOOL value = dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE; 20 is the documented numeric value and is
    // also available through the SDK enum on current Windows 11 headers.
    ::DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));
#else
    Q_UNUSED(widget);
    Q_UNUSED(dark);
#endif
}

void PaintCheck(QPainter& painter, const QRect& box, bool checked, bool hovered,
                const Theme& theme, bool enabled)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor fill = checked ? theme.accentFill : theme.control;
    const QColor border = enabled ? (hovered ? theme.text : theme.controlBorder) : theme.faint;
    painter.setPen(QPen(checked ? theme.accentFill : border, 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, theme.smallControlRadius, theme.smallControlRadius);

    if (checked)
    {
        QPen mark(theme.accentText);
        mark.setWidthF(1.8);
        mark.setCapStyle(Qt::RoundCap);
        mark.setJoinStyle(Qt::RoundJoin);
        painter.setPen(mark);
        QPainterPath path;
        path.moveTo(rect.left() + 4.0, rect.center().y());
        path.lineTo(rect.left() + 6.8, rect.bottom() - 4.0);
        path.lineTo(rect.right() - 3.5, rect.top() + 4.0);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
}

// The QSS-styled QCheckBox loses its indicator on Fusion; drawing the 16 px
// box here keeps it visible in every theme and at every UI size.
class CheckBox : public QCheckBox
{
public:
    explicit CheckBox(const Theme* theme, QWidget* parent = nullptr)
        : QCheckBox(parent)
        , m_theme(theme)
    {
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override
    {
        QSize size = QCheckBox::sizeHint();
        size.setHeight(qMax(size.height(), 22));
        return size;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_theme)
            return;

        QPainter painter(this);
        const bool hasText = !text().isEmpty();
        const int boxSize = 16;
        const int spacing = 8;
        QRect box(0, (height() - boxSize) / 2, boxSize, boxSize);
        if (!hasText)
            box.moveLeft((width() - boxSize) / 2);

        PaintCheck(painter, box, isChecked(), underMouse(), *m_theme, isEnabled());

        if (hasText)
        {
            painter.setPen(isEnabled() ? m_theme->text : m_theme->faint);
            painter.setFont(font());
            const QRect label(box.right() + spacing, 0, width() - box.right() - spacing, height());
            painter.drawText(label, Qt::AlignVCenter | Qt::AlignLeft, text());
        }
        if (hasFocus())
        {
            QStyleOptionFocusRect focus;
            focus.rect = rect().adjusted(0, 0, -1, -1);
            focus.state = QStyle::State_KeyboardFocusChange;
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
        }
    }

private:
    const Theme* m_theme = nullptr;
};

// QSS can style a combo's box but not draw its arrow on every Windows version
// once Fusion + a stylesheet are combined. A few lines here keep the chevron
// visible and on-theme.
class ComboBox : public QComboBox
{
public:
    explicit ComboBox(const Theme* theme, QWidget* parent = nullptr)
        : QComboBox(parent)
        , m_theme(theme)
    {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(32);
    }

    QSize sizeHint() const override
    {
        QSize size = QComboBox::sizeHint();
        size.setHeight(qMax(size.height(), 32));
        return size;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_theme)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool active = hasFocus();
        const QColor border = active ? m_theme->accent : m_theme->controlBorder;
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(active ? m_theme->inputFocused : m_theme->control);
        painter.drawRoundedRect(box, m_theme->controlRadius, m_theme->controlRadius);

        const QRect textRect = rect().adjusted(8, 0, -28, 0);
        painter.setPen(m_theme->text);
        painter.setFont(font());
        const QString shown = fontMetrics().elidedText(currentText(), Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);

        const int cx = width() - 15;
        const int cy = height() / 2 - 1;
        QPen arrow(m_theme->subtext);
        arrow.setWidthF(1.4);
        arrow.setCapStyle(Qt::RoundCap);
        painter.setPen(arrow);
        painter.drawLine(cx - 4, cy - 2, cx, cy + 2);
        painter.drawLine(cx, cy + 2, cx + 4, cy - 2);
    }

private:
    const Theme* m_theme = nullptr;
};

class CheckDelegate : public QStyledItemDelegate
{
public:
    explicit CheckDelegate(const Theme* theme, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_theme(theme)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        if (!m_theme)
            return;

        painter->save();
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver))
            painter->fillRect(option.rect, m_theme->hover);

        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        QRect box(0, 0, 16, 16);
        box.moveCenter(option.rect.center());
        PaintCheck(*painter, box, checked, option.state & QStyle::State_MouseOver,
                   *m_theme, option.state & QStyle::State_Enabled);
        painter->restore();
    }

private:
    const Theme* m_theme = nullptr;
};

// Paints the "renamed" column with the changed span highlighted: the text the
// rules kept is drawn faintly, the span between the first and last difference is
// drawn in the accent colour. A common-prefix/suffix walk is all this needs —
// anything smarter cannot survive a window that repaints on every keystroke.
class TargetDelegate : public QStyledItemDelegate
{
public:
    explicit TargetDelegate(const Theme* theme, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_theme(theme)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        if (!m_theme)
            return;

        painter->save();
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver))
            painter->fillRect(option.rect, m_theme->hover);

        const QString original = index.data(rename_qml::PreviewModel::OriginalRole).toString();
        const QString target = index.data(rename_qml::PreviewModel::TargetRole).toString();
        const int status = index.data(rename_qml::PreviewModel::StatusRole).toInt();
        const bool skipped = index.data(rename_qml::PreviewModel::SkippedRole).toBool();
        const bool error = status == static_cast<int>(rename_core::Status::ErrorAlreadyExists)
                        || status == static_cast<int>(rename_core::Status::ErrorTooLong)
                        || status == static_cast<int>(rename_core::Status::ErrorInvalidCharacter)
                        || status == static_cast<int>(rename_core::Status::ErrorEmptyName)
                        || status == static_cast<int>(rename_core::Status::ErrorBadRule);
        const bool unchanged = status == static_cast<int>(rename_core::Status::Unchanged);
        const bool ready = status == static_cast<int>(rename_core::Status::Ready);

        QColor plain = error ? m_theme->danger
                      : skipped ? m_theme->subtext
                      : unchanged ? m_theme->faint
                      : m_theme->text;
        QColor highlight = error ? m_theme->danger
                         : skipped ? m_theme->subtext
                         : m_theme->accent;

        QFont font = option.font;
        if (ready && !error && !skipped)
            font.setBold(true);
        painter->setFont(font);
        painter->setClipRect(option.rect);

        // Only a row that will actually rename gets the two-colour treatment.
        const bool changed = ready && target != original;
        int prefix = 0;
        int suffix = 0;
        if (changed)
        {
            while (prefix < original.length() && prefix < target.length()
                   && original[prefix] == target[prefix])
                ++prefix;
            while (suffix < original.length() - prefix && suffix < target.length() - prefix
                   && original[original.length() - 1 - suffix] == target[target.length() - 1 - suffix])
                ++suffix;
        }

        const int flags = Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine;
        const int midStart = prefix;
        const int midEnd = target.length() - suffix;
        if (!changed || midEnd <= midStart)
        {
            painter->setPen(plain);
            painter->drawText(option.rect.adjusted(2, 0, -4, 0), flags, target);
            painter->restore();
            return;
        }

        const QFontMetrics metrics(font);
        int x = option.rect.left() + 2;
        const int y = option.rect.top() + (option.rect.height() + metrics.ascent() - metrics.descent()) / 2
                    - metrics.ascent();

        auto drawSpan = [&](const QString& text, const QColor& colour) {
            if (text.isEmpty())
                return;
            painter->setPen(colour);
            painter->drawText(QPoint(x, y + metrics.ascent()), text);
            x += metrics.horizontalAdvance(text);
        };

        drawSpan(target.left(midStart), plain);
        drawSpan(target.mid(midStart, midEnd - midStart), highlight);
        drawSpan(target.mid(midEnd), plain);
        painter->restore();
    }

private:
    const Theme* m_theme = nullptr;
};

} // namespace

Window::Window(rename_qml::Bridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setWindowTitle(T("批量重命名", "Batch Rename"));
    setMinimumSize(700, 520);
    resize(822, 616);
    QIcon windowIcon;
    windowIcon.addFile(QStringLiteral(":/resources/icons/app-16.png"));
    windowIcon.addFile(QStringLiteral(":/resources/icons/app-256.png"));
    setWindowIcon(windowIcon);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    m_theme = new Theme(Theme::From(m_bridge->look(), m_bridge->dark(), m_bridge->uiSize(),
                                    m_bridge->locale()));
    // Set the CJK-capable face before any child is created. Setting it only
    // after BuildUi is too late: laying out a Chinese label with the default
    // Latin font has already populated Qt's fallback font cache (measured:
    // 129 MB private versus 9 MB when the font is chosen up front).
    QApplication::setFont(m_theme->font);
    m_theme->apply(this);

    BuildUi();
    ConnectBridge();
    ApplyAppearance();
    RefreshAll();
}

Window::~Window()
{
    delete m_theme;
}

void Window::BuildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- title strip ------------------------------------------------------
    auto* top = new QWidget(this);
    top->setFixedHeight(38);
    top->setObjectName(QStringLiteral("topBar"));
    auto* topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(12, 0, 8, 0);
    topLayout->setSpacing(8);

    // No Unicode pencil/arrow glyphs here: U+270E and U+23F7 make Qt fall back
    // to Segoe UI Emoji/Symbol, which costs ~120 MB of private memory on this
    // machine (measured). The title is text only; the arrows are painted.
    m_title = new QLabel(T("批量重命名", "Batch Rename"), top);
    m_title->setObjectName(QStringLiteral("topTitle"));
    topLayout->addWidget(m_title);
    topLayout->addStretch(1);

    const auto makeModeButton = [this, top](const QString& text, const QString& target) {
        auto* button = new QToolButton(top);
        button->setText(text);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setObjectName(QStringLiteral("modeButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(24);
        connect(button, &QToolButton::clicked, this, [this, target] { RequestMode(target); });
        return button;
    };
    m_modeRules = makeModeButton(T("规则", "Rules"), QStringLiteral("control"));
    m_modeText = makeModeButton(T("文本", "Text"), QStringLiteral("text"));
    topLayout->addWidget(m_modeRules);
    topLayout->addWidget(m_modeText);

    root->addWidget(top);
    root->addWidget(Divider(Qt::Horizontal, this));

    // ---- directory bar ----------------------------------------------------
    auto* pathBar = new QWidget(this);
    pathBar->setFixedHeight(36);
    auto* pathLayout = new QHBoxLayout(pathBar);
    pathLayout->setContentsMargins(12, 0, 8, 0);
    pathLayout->setSpacing(6);

    m_up = new QToolButton(pathBar);
    m_up->setText(T("上一级", "Up"));
    m_up->setObjectName(QStringLiteral("chipButton"));
    m_up->setToolTip(T("上一级", "Parent folder"));
    m_up->setCursor(Qt::PointingHandCursor);
    pathLayout->addWidget(m_up);

    m_path = new QLineEdit(pathBar);
    m_path->setPlaceholderText(T("文件夹", "Folder"));
    m_path->setClearButtonEnabled(false);
    m_path->setToolTip(T("回车进入这个文件夹", "Press Enter to open this folder"));
    pathLayout->addWidget(m_path, 1);

    m_browse = new QToolButton(pathBar);
    m_browse->setText(T("选择…", "Choose…"));
    m_browse->setObjectName(QStringLiteral("chipButton"));
    m_browse->setToolTip(T("选择目录", "Choose a folder"));
    m_browse->setCursor(Qt::PointingHandCursor);
    pathLayout->addWidget(m_browse);

    m_reload = new QToolButton(pathBar);
    m_reload->setText(T("刷新", "Refresh"));
    m_reload->setObjectName(QStringLiteral("chipButton"));
    m_reload->setToolTip(T("重新读取这个文件夹", "Reload this folder"));
    m_reload->setCursor(Qt::PointingHandCursor);
    pathLayout->addWidget(m_reload);

    root->addWidget(pathBar);
    root->addWidget(Divider(Qt::Horizontal, this));

    // ---- body -------------------------------------------------------------
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    root->addLayout(body, 1);

    // Left: rule form / text editor.
    auto* left = new QWidget(this);
    left->setFixedWidth(348);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(16, 14, 16, 13);
    leftLayout->setSpacing(0);

    m_leftStack = new QStackedWidget(left);
    leftLayout->addWidget(m_leftStack, 1);

    auto* controlPage = new QWidget(m_leftStack);
    auto* control = new QVBoxLayout(controlPage);
    control->setContentsMargins(0, 0, 0, 0);
    control->setSpacing(0);

    // ---- presets -----------------------------------------------------------
    auto* presetRow = new QHBoxLayout;
    presetRow->setContentsMargins(0, 0, 0, 0);
    presetRow->setSpacing(6);
    m_preset = new ComboBox(m_theme, controlPage);
    m_preset->setToolTip(T("载入预设", "Load a preset"));
    m_preset->setFixedHeight(30);
    presetRow->addWidget(m_preset, 1);
    m_presetSave = new QToolButton(controlPage);
    m_presetSave->setText(T("存", "Save"));
    m_presetSave->setObjectName(QStringLiteral("chipButton"));
    m_presetSave->setToolTip(T("把当前规则链存为预设", "Save the current chain as a preset"));
    m_presetSave->setFixedHeight(30);
    presetRow->addWidget(m_presetSave);
    m_presetDelete = new QToolButton(controlPage);
    m_presetDelete->setText(T("删", "Del"));
    m_presetDelete->setObjectName(QStringLiteral("chipButton"));
    m_presetDelete->setToolTip(T("删除选中的预设", "Delete the selected preset"));
    m_presetDelete->setFixedHeight(30);
    presetRow->addWidget(m_presetDelete);
    control->addLayout(presetRow);
    control->addSpacing(9);

    // ---- rule chain list ----------------------------------------------------
    m_ruleList = new QListView(controlPage);
    m_ruleList->setModel(m_bridge->ruleModel());
    m_ruleList->setFrameShape(QFrame::NoFrame);
    m_ruleList->setAlternatingRowColors(false);
    m_ruleList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleList->setUniformItemSizes(true);
    m_ruleList->setFixedHeight(88);
    m_ruleList->setObjectName(QStringLiteral("ruleList"));
    control->addWidget(m_ruleList);
    control->addSpacing(6);

    auto* ruleButtons = new QHBoxLayout;
    ruleButtons->setContentsMargins(0, 0, 0, 0);
    ruleButtons->setSpacing(5);
    const auto makeRuleButton = [this, controlPage, &ruleButtons](const QString& text, const QString& tip) {
        auto* button = new QToolButton(controlPage);
        button->setText(text);
        button->setToolTip(tip);
        button->setObjectName(QStringLiteral("chipButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(26);
        ruleButtons->addWidget(button);
        return button;
    };
    m_ruleAdd = makeRuleButton(T("添加", "Add"), T("添加一条规则", "Add a rule"));
    m_ruleDup = makeRuleButton(T("复制", "Copy"), T("复制选中的规则", "Duplicate the selected rule"));
    m_ruleUp = makeRuleButton(T("上移", "Up"), T("把选中的规则上移一位", "Move the selected rule up"));
    m_ruleDown = makeRuleButton(T("下移", "Down"), T("把选中的规则下移一位", "Move the selected rule down"));
    m_ruleRemove = makeRuleButton(T("删除", "Remove"), T("删除选中的规则", "Remove the selected rule"));
    ruleButtons->addStretch(1);
    control->addLayout(ruleButtons);
    control->addSpacing(10);

    control->addWidget(Divider(Qt::Horizontal, controlPage));
    control->addSpacing(10);

    // ---- detail editor for the selected rule ---------------------------------
    m_find = new QLineEdit(controlPage);
    m_find->setPlaceholderText(T("查找", "Find"));
    m_find->setClearButtonEnabled(true);
    control->addWidget(m_find);
    control->addSpacing(7);

    m_replace = new QLineEdit(controlPage);
    m_replace->setPlaceholderText(T("替换为", "Replace with"));
    control->addWidget(m_replace);
    control->addSpacing(7);

    m_regex = new CheckBox(m_theme, controlPage);
    m_regex->setText(T("使用正则表达式", "Use regular expressions"));
    m_matchAll = new CheckBox(m_theme, controlPage);
    m_matchAll->setText(T("替换全部匹配", "Match all occurrences"));
    m_caseSensitive = new CheckBox(m_theme, controlPage);
    m_caseSensitive->setText(T("区分大小写", "Case sensitive"));
    control->addWidget(m_regex);
    control->addSpacing(7);
    control->addWidget(m_matchAll);
    control->addSpacing(7);
    control->addWidget(m_caseSensitive);

    m_scopeLabel = new QLabel(T("应用于", "Apply to"), controlPage);
    m_scopeLabel->setObjectName(QStringLiteral("caption"));
    control->addSpacing(12);
    control->addWidget(m_scopeLabel);
    control->addSpacing(5);

    m_scope = new ComboBox(m_theme, controlPage);
    m_scope->addItem(T("文件名", "Name"));
    m_scope->addItem(T("仅扩展名", "Extension only"));
    m_scope->addItem(T("完整名称", "Full name"));
    control->addWidget(m_scope);

    m_formatLabel = new QLabel(T("文本格式", "Text formatting"), controlPage);
    m_formatLabel->setObjectName(QStringLiteral("caption"));
    control->addSpacing(14);
    control->addWidget(m_formatLabel);
    control->addSpacing(5);

    auto* caseRow = new QHBoxLayout;
    caseRow->setContentsMargins(0, 0, 0, 0);
    caseRow->setSpacing(6);
    const char* caseTexts[4] = {"aa", "AA", "Aa", "Aa Aa"};
    const char* caseTips[4] = {"全部小写", "全部大写", "首字母大写", "每词首字母大写"};
    const char* caseTipsEn[4] = {"lowercase", "UPPERCASE", "Capitalize", "Title Case"};
    for (int i = 0; i < 4; ++i)
    {
        auto* button = new QToolButton(controlPage);
        button->setText(QString::fromLatin1(caseTexts[i]));
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("chipButton"));
        button->setToolTip(T(caseTips[i], caseTipsEn[i]));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        connect(button, &QToolButton::clicked, this, [this, i] { m_bridge->setCaseTransform(i + 1); });
        m_caseButtons[i] = button;
        caseRow->addWidget(button);
    }
    caseRow->addStretch(1);
    control->addLayout(caseRow);
    control->addStretch(1);

    m_leftStack->addWidget(controlPage);

    auto* textPage = new QWidget(m_leftStack);
    auto* textLayout = new QVBoxLayout(textPage);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    // The text page's own header: the vim toggle on the left, the mode badge on
    // the right. Vim mode is off by default, so this row reads as an option and
    // not as a new required step.
    auto* textHead = new QHBoxLayout;
    textHead->setContentsMargins(0, 0, 0, 0);
    textHead->setSpacing(8);
    m_vimToggle = new QToolButton(textPage);
    m_vimToggle->setText(T("Vim", "Vim"));
    m_vimToggle->setCheckable(true);
    m_vimToggle->setObjectName(QStringLiteral("chipButton"));
    m_vimToggle->setCursor(Qt::PointingHandCursor);
    m_vimToggle->setFixedHeight(26);
    m_vimToggle->setToolTip(T("Vim 按键模式（默认关闭）", "Vim key bindings (off by default)"));
    textHead->addWidget(m_vimToggle);
    textHead->addStretch(1);
    m_vimBadge = new QLabel(textPage);
    m_vimBadge->setObjectName(QStringLiteral("caption"));
    m_vimBadge->setToolTip(T("当前 Vim 模式", "Current vim mode"));
    textHead->addWidget(m_vimBadge);
    textLayout->addLayout(textHead);
    textLayout->addSpacing(8);

    auto* textHint = new QLabel(
        T("每行一项：/<编号> 新名字；文件夹以 / 结尾。Ctrl+S 执行。",
          "One item per line: /<id> new name; folders end with /. Ctrl+S runs it."),
        textPage);
    textHint->setObjectName(QStringLiteral("caption"));
    textHint->setWordWrap(true);
    textLayout->addWidget(textHint);
    textLayout->addSpacing(8);

    m_text = new QPlainTextEdit(textPage);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(m_theme->mono);
    m_text->setTabStopDistance(4 * QFontMetricsF(m_theme->mono).horizontalAdvance(QLatin1Char(' ')));
    textLayout->addWidget(m_text, 1);
    m_leftStack->addWidget(textPage);

    // The one verb, shared by both modes.
    auto* bottom = new QWidget(left);
    auto* bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 10, 0, 0);
    bottomLayout->setSpacing(8);
    bottomLayout->addStretch(1);
    m_apply = new QPushButton(T("重命名", "Rename"), bottom);
    m_apply->setObjectName(QStringLiteral("primary"));
    m_apply->setCursor(Qt::PointingHandCursor);
    m_apply->setMinimumWidth(152);
    m_apply->setMinimumHeight(33);
    bottomLayout->addWidget(m_apply);
    leftLayout->addWidget(bottom);

    body->addWidget(left);
    body->addWidget(Divider(Qt::Vertical, this));

    // Right: table.
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto* header = new QWidget(right);
    header->setFixedHeight(33);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(13, 0, 12, 0);
    headerLayout->setSpacing(0);

    m_selectAll = new CheckBox(m_theme, header);
    m_selectAll->setFixedWidth(24);
    m_selectAll->setToolTip(T("全选", "Select all"));
    headerLayout->addWidget(m_selectAll);

    m_originalHeader = new QLabel(header);
    m_originalHeader->setObjectName(QStringLiteral("caption"));
    m_originalHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headerLayout->addWidget(m_originalHeader, 1);

    m_targetHeader = new QLabel(header);
    m_targetHeader->setObjectName(QStringLiteral("caption"));
    m_targetHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headerLayout->addWidget(m_targetHeader, 1);

    m_filter = new ComboBox(m_theme, header);
    m_filter->addItem(T("全部", "All"));
    m_filter->addItem(T("已选", "Selected"));
    m_filter->addItem(T("可改", "Applicable"));
    m_filter->addItem(T("要改", "To rename"));
    m_filter->setFixedWidth(108);
    m_filter->setToolTip(T("筛选显示的条目，不改变要执行的集合", "Filter the rows shown; the batch itself is unchanged"));
    headerLayout->addSpacing(6);
    headerLayout->addWidget(m_filter);
    rightLayout->addWidget(header);
    rightLayout->addWidget(Divider(Qt::Horizontal, right));

    m_tableStack = new QStackedWidget(right);
    m_table = new QTableView(m_tableStack);
    m_table->setModel(m_bridge->previewModel());
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setFocusPolicy(Qt::StrongFocus);
    m_table->horizontalHeader()->hide();
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_theme->rowHeight);
    m_table->setColumnWidth(0, 34);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setItemDelegateForColumn(0, new CheckDelegate(m_theme, m_table));
    m_table->setItemDelegateForColumn(2, new TargetDelegate(m_theme, m_table));
    m_tableStack->addWidget(m_table);

    m_empty = new QLabel(m_tableStack);
    m_empty->setObjectName(QStringLiteral("empty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_tableStack->addWidget(m_empty);
    rightLayout->addWidget(m_tableStack, 1);

    auto* footer = new QWidget(right);
    footer->setFixedHeight(30);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 0, 14, 0);
    footerLayout->setSpacing(8);

    m_dot = new QLabel(footer);
    m_dot->setFixedSize(8, 8);
    footerLayout->addWidget(m_dot);

    m_status = new QLabel(footer);
    m_status->setObjectName(QStringLiteral("caption"));
    footerLayout->addWidget(m_status);
    footerLayout->addStretch(1);

    m_hint = new QLabel(footer);
    m_hint->setObjectName(QStringLiteral("caption"));
    m_hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hint->setMinimumWidth(190);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footerLayout->addWidget(m_hint, 1);
    rightLayout->addWidget(footer);

    body->addWidget(right, 1);

    // ---- input and shortcuts ---------------------------------------------
    connect(m_apply, &QPushButton::clicked, this, &Window::ConfirmAndApply);
    connect(m_selectAll, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_syncing)
            m_bridge->selectAll(checked);
    });
    connect(m_path, &QLineEdit::returnPressed, this, [this] { NavigateTo(m_path->text()); });
    connect(m_up, &QToolButton::clicked, this, &Window::GoUp);
    connect(m_browse, &QToolButton::clicked, this, &Window::BrowseFolder);
    connect(m_reload, &QToolButton::clicked, this, &Window::ReloadFolder);
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid() && m_bridge->rowIsFolder(index.row()))
            NavigateTo(m_bridge->pathForRow(index.row()));
    });

    auto* refresh = new QShortcut(QKeySequence(Qt::Key_F5), this);
    refresh->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refresh, &QShortcut::activated, this, &Window::ReloadFolder);

    auto* upShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Up")), this);
    upShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(upShortcut, &QShortcut::activated, this, &Window::GoUp);

    auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), this);
    enter->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enter, &QShortcut::activated, this, [this] {
        QWidget* focus = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus)
            || qobject_cast<QComboBox*>(focus))
            return;
        ConfirmAndApply();
    });

    auto* ctrlS = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    ctrlS->setContext(Qt::WidgetWithChildrenShortcut);
    connect(ctrlS, &QShortcut::activated, this, &Window::ConfirmAndApply);

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { close(); });
}

void Window::ConnectBridge()
{
    connect(m_bridge, &rename_qml::Bridge::changed, this, &Window::RefreshAll);
    connect(m_bridge, &rename_qml::Bridge::countsChanged, this, &Window::RefreshCounts);
    connect(m_bridge, &rename_qml::Bridge::ruleChanged, this, [this] {
        SyncRuleFields();
        RefreshRuleList();
    });
    connect(m_bridge, &rename_qml::Bridge::appearanceChanged, this, &Window::ApplyAppearance);

    m_ruleTimer = new QTimer(this);
    m_ruleTimer->setSingleShot(true);
    m_ruleTimer->setInterval(140);
    connect(m_ruleTimer, &QTimer::timeout, this, &Window::PushRule);

    m_textTimer = new QTimer(this);
    m_textTimer->setSingleShot(true);
    m_textTimer->setInterval(120);
    connect(m_textTimer, &QTimer::timeout, this, [this] {
        if (!m_syncing)
            m_bridge->setBuffer(m_text->toPlainText());
    });

    connect(m_find, &QLineEdit::textChanged, this, [this] {
        if (!m_syncing)
            m_ruleTimer->start();
    });
    connect(m_replace, &QLineEdit::textChanged, this, [this] {
        if (!m_syncing)
            m_ruleTimer->start();
    });
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        m_ruleTimer->stop();
        PushRule();
    });
    connect(m_replace, &QLineEdit::returnPressed, this, [this] {
        m_ruleTimer->stop();
        PushRule();
    });

    connect(m_regex, &QCheckBox::toggled, this, [this] { if (!m_syncing) PushRule(); });
    connect(m_matchAll, &QCheckBox::toggled, this, [this] { if (!m_syncing) PushRule(); });
    connect(m_caseSensitive, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_syncing)
            m_bridge->setRuleCaseSensitive(checked);
    });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_syncing)
            PushRule();
    });

    // ---- rule chain ----------------------------------------------------------
    connect(m_ruleList->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (!m_syncing)
                    m_bridge->selectRule(current.isValid() ? current.row() : -1);
            });
    connect(m_ruleAdd, &QToolButton::clicked, this, [this] {
        m_bridge->addRule();
        SyncRuleSelection();
        SyncRuleFields();
    });
    connect(m_ruleDup, &QToolButton::clicked, this, [this] {
        m_bridge->duplicateRule(m_bridge->selectedRule());
        SyncRuleSelection();
        SyncRuleFields();
    });
    connect(m_ruleRemove, &QToolButton::clicked, this, [this] {
        m_bridge->removeRule(m_bridge->selectedRule());
        SyncRuleSelection();
        SyncRuleFields();
    });
    connect(m_ruleUp, &QToolButton::clicked, this, [this] {
        const int index = m_bridge->selectedRule();
        m_bridge->moveRule(index, index - 1);
        SyncRuleSelection();
    });
    connect(m_ruleDown, &QToolButton::clicked, this, [this] {
        const int index = m_bridge->selectedRule();
        m_bridge->moveRule(index, index + 1);
        SyncRuleSelection();
    });

    // ---- presets -------------------------------------------------------------
    connect(m_presetSave, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, T("保存预设", "Save preset"),
            T("预设名称", "Preset name"), QLineEdit::Normal,
            m_preset->currentText(), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        if (!m_bridge->savePreset(name.trimmed()))
            return;
        RefreshPresets();
        m_preset->setCurrentText(name.trimmed());
    });
    connect(m_presetDelete, &QToolButton::clicked, this, [this] {
        const QString name = m_preset->currentText();
        if (name.isEmpty())
            return;
        m_bridge->deletePreset(name);
        RefreshPresets();
    });
    connect(m_preset, &QComboBox::activated, this, [this](int index) {
        if (index <= 0 || m_syncing)
            return;
        // Index 0 is the placeholder row; a preset row loads and the combo then
        // returns to the placeholder so the same preset can be re-picked.
        m_bridge->loadPreset(m_preset->itemText(index));
        SyncRuleSelection();
        SyncRuleFields();
        m_syncing = true;
        m_preset->setCurrentIndex(0);
        m_syncing = false;
    });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_syncing)
            return;
        switch (index)
        {
        case 1: m_bridge->setFilter(QStringLiteral("selected")); break;
        case 2: m_bridge->setFilter(QStringLiteral("applicable")); break;
        case 3: m_bridge->setFilter(QStringLiteral("to-rename")); break;
        default: m_bridge->setFilter(QStringLiteral("all")); break;
        }
    });

    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_syncing)
            m_textTimer->start();
    });

    // ---- vim mode ------------------------------------------------------------
    // The filter is installed once and gated by its own enabled flag; the toggle
    // only flips that flag and remembers it.
    m_vim = new VimTextFilter(m_text, this);
    connect(m_vim, &VimTextFilter::statusChanged, this, [this](const QString& status) {
        if (m_vimBadge)
            m_vimBadge->setText(m_vim->enabled() ? status : QString());
    });
    connect(m_vimToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (m_syncing)
            return;
        m_bridge->setVimEnabled(checked);
        m_vim->setEnabled(checked);
        RefreshVim();
        m_text->setFocus();
    });
}

QString Window::T(const char* zhText, const char* enText) const
{
    return QString::fromUtf8(zh() ? zhText : enText);
}

bool Window::zh() const
{
    return m_bridge && m_bridge->locale().startsWith(QLatin1String("zh"));
}

void Window::SyncRuleFields()
{
    if (!m_bridge)
        return;

    const int index = m_bridge->selectedRule();
    const bool hasRule = index >= 0 && index < m_bridge->ruleCount();
    const QVariantMap rule = hasRule ? m_bridge->ruleAt(index) : QVariantMap();

    // The detail editor edits the selected rule only; with no rules there is
    // nothing to bind, so the fields go dark rather than lying about a rule.
    for (QWidget* widget : std::initializer_list<QWidget*>{
             m_find, m_replace, m_regex, m_matchAll, m_caseSensitive, m_scope, m_scopeLabel})
        widget->setEnabled(hasRule);
    m_ruleDup->setEnabled(hasRule);
    m_ruleUp->setEnabled(hasRule && index > 0);
    m_ruleDown->setEnabled(hasRule && index < m_bridge->ruleCount() - 1);
    m_ruleRemove->setEnabled(hasRule);
    m_presetSave->setEnabled(m_bridge->ruleCount() > 0);

    m_syncing = true;
    if (hasRule)
    {
        const QString find = rule.value(QStringLiteral("find")).toString();
        const QString replace = rule.value(QStringLiteral("replace")).toString();
        // Never overwrite a field the user is typing into: a countsChanged while
        // a debounce is pending would otherwise restore the previous rule text.
        if (!m_find->hasFocus() && m_find->text() != find)
            m_find->setText(find);
        if (!m_replace->hasFocus() && m_replace->text() != replace)
            m_replace->setText(replace);
        m_regex->setChecked(rule.value(QStringLiteral("match")).toInt() == 1);
        m_matchAll->setChecked(rule.value(QStringLiteral("matchAll")).toBool());
        m_caseSensitive->setChecked(m_bridge->ruleCaseSensitive());
        m_scope->setCurrentIndex(qBound(0, rule.value(QStringLiteral("scope")).toInt(), 2));
    }
    for (int i = 0; i < 4; ++i)
        m_caseButtons[i]->setChecked(m_bridge->caseTransform() == i + 1);
    m_syncing = false;

    const bool invalid = hasRule && m_bridge->ruleRegexInvalid();
    if (m_find->property("invalid").toBool() != invalid)
    {
        m_find->setProperty("invalid", invalid);
        m_find->style()->unpolish(m_find);
        m_find->style()->polish(m_find);
    }
}

void Window::SyncRuleSelection()
{
    if (!m_bridge || !m_ruleList)
        return;
    const int index = m_bridge->selectedRule();
    m_syncing = true;
    if (index < 0)
        m_ruleList->clearSelection();
    else
    {
        const QModelIndex modelIndex = m_ruleList->model()->index(index, 0);
        m_ruleList->setCurrentIndex(modelIndex);
    }
    m_syncing = false;
}

void Window::RefreshVim()
{
    if (!m_bridge || !m_vimToggle || !m_vim)
        return;
    const bool enabled = m_bridge->vimEnabled();
    m_syncing = true;
    m_vimToggle->setChecked(enabled);
    m_syncing = false;
    m_vim->setEnabled(enabled);
    m_vimBadge->setVisible(enabled);
    m_vimBadge->setText(enabled ? m_vim->statusText() : QString());
    m_vimToggle->setToolTip(enabled
        ? T("Vim 按键模式已开启（点此关闭）", "Vim key bindings are on (click to turn off)")
        : T("Vim 按键模式（默认关闭）", "Vim key bindings (off by default)"));
}

void Window::RefreshPresets()
{
    if (!m_bridge || !m_preset)
        return;
    m_syncing = true;
    m_preset->clear();
    m_preset->addItem(T("预设…", "Preset…"));
    m_preset->addItems(m_bridge->presetNames());
    m_preset->setCurrentIndex(0);
    m_syncing = false;
}

void Window::RefreshRuleList()
{
    // The rule model resets on every chain edit, which drops the view's current
    // index; put it back where the bridge says it belongs.
    if (!m_bridge || !m_ruleList)
        return;
    const int index = m_bridge->selectedRule();
    const QModelIndex current = m_ruleList->currentIndex();
    const int currentIndex = current.isValid() ? current.row() : -1;
    if (currentIndex != index)
        SyncRuleSelection();
    RefreshPresets();
}

void Window::SyncTextEditor()
{
    if (!m_bridge || !m_text)
        return;
    if (m_bridge->mode() != QStringLiteral("text") || m_text->hasFocus())
        return;
    if (m_text->toPlainText() == m_bridge->buffer())
        return;
    m_syncing = true;
    m_text->setPlainText(m_bridge->buffer());
    m_syncing = false;
    // The vim core holds its own copy of the text; a reload invalidates it.
    if (m_vim)
        m_vim->resync();
}

void Window::SyncPath()
{
    if (!m_path || !m_bridge)
        return;
    const QString folder = m_bridge->currentFolder();
    if (!m_path->hasFocus() && m_path->text() != folder)
    {
        m_path->setText(folder);
        m_path->setCursorPosition(0); // show the drive/root, not the tail
    }
    const bool hasFolder = !folder.isEmpty();
    const QString parent = hasFolder ? QFileInfo(folder).absolutePath() : QString();
    m_up->setEnabled(hasFolder && !parent.isEmpty()
                     && parent.compare(folder, Qt::CaseInsensitive) != 0);
    m_reload->setEnabled(hasFolder);
    m_path->setToolTip(hasFolder ? folder : T("还没有打开文件夹", "No folder is open"));
}

bool Window::ConfirmDiscard()
{
    // Only the text buffer is an edit that navigation would lose. Control-mode
    // rules are intentionally kept across folders.
    if (m_bridge->mode() != QStringLiteral("text"))
        return true;
    if (m_text->toPlainText() == m_bridge->buffer())
        return true;
    return Ask(T("文本里有未执行的修改。切换文件夹会丢弃它们，继续吗？",
                 "The text buffer has unexecuted changes. Switching folders will discard them. Continue?"),
               T("继续", "Continue"));
}

void Window::NavigateTo(const QString& folder)
{
    if (!m_bridge)
        return;
    const QFileInfo info(folder);
    if (folder.isEmpty() || !info.isDir())
    {
        SyncPath();
        QMessageBox::warning(this, T("批量重命名", "Batch Rename"),
                             T("找不到这个文件夹：", "This folder does not exist: ") + folder);
        return;
    }
    if (!ConfirmDiscard())
    {
        SyncPath();
        return;
    }
    m_bridge->openFolder(info.absoluteFilePath());
    RefreshAll();
}

void Window::GoUp()
{
    if (!m_bridge)
        return;
    const QString folder = m_bridge->currentFolder();
    if (folder.isEmpty())
        return;
    const QString parent = QFileInfo(folder).absolutePath();
    if (parent.isEmpty() || parent.compare(folder, Qt::CaseInsensitive) == 0)
        return;
    NavigateTo(parent);
}

void Window::ReloadFolder()
{
    if (!m_bridge)
        return;
    if (m_bridge->currentFolder().isEmpty())
        return;
    if (!ConfirmDiscard())
        return;
    m_bridge->reloadFolder();
    RefreshAll();
}

void Window::BrowseFolder()
{
    if (!m_bridge)
        return;
    const QString start = m_bridge->currentFolder();
    const QString folder = QFileDialog::getExistingDirectory(
        this, T("选择文件夹", "Choose a folder"), start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!folder.isEmpty())
        NavigateTo(folder);
}

void Window::PushRule()
{
    if (!m_bridge || m_syncing)
        return;
    const int index = m_bridge->selectedRule();
    if (index < 0)
        return;
    // Keep the row's enabled flag: the list checkbox owns it, not the editor.
    const QVariantMap rule = m_bridge->ruleAt(index);
    m_bridge->setRuleAt(index, m_scope->currentIndex(), m_regex->isChecked() ? 1 : 0,
                        m_find->text(), m_replace->text(),
                        rule.value(QStringLiteral("enabled")).toBool(),
                        m_matchAll->isChecked());
}

void Window::RequestMode(const QString& target)
{
    if (!m_bridge || m_bridge->mode() == target)
        return;

    if (!m_bridge->switchMode(target))
    {
        const QString text = T("当前有未执行的修改。切换到另一种模式会丢弃它，继续吗？",
                               "There are unexecuted changes. Switching modes will discard them. Continue?");
        if (Ask(text, T("继续", "Continue")))
        {
            m_bridge->forceSwitchMode(target);
        }
        else
        {
            RefreshMode();
            return;
        }
    }
    RefreshAll();
    if (m_bridge->mode() == QStringLiteral("text"))
        m_text->setFocus();
}

void Window::RefreshAll()
{
    if (!m_bridge)
        return;
    SyncRuleFields();
    RefreshRuleList();
    RefreshMode();
    RefreshCounts();
    RefreshEmptyState();
    RefreshFooter();
    SyncPath();
    SyncTextEditor();
    RefreshVim();
}

void Window::Retranslate()
{
    setWindowTitle(T("批量重命名", "Batch Rename"));
    if (m_title)
        m_title->setText(T("批量重命名", "Batch Rename"));
    m_modeRules->setText(T("规则", "Rules"));
    m_modeText->setText(T("文本", "Text"));

    m_path->setPlaceholderText(T("文件夹", "Folder"));
    m_up->setText(T("上一级", "Up"));
    m_up->setToolTip(T("上一级", "Parent folder"));
    m_browse->setText(T("选择…", "Choose…"));
    m_browse->setToolTip(T("选择目录", "Choose a folder"));
    m_reload->setText(T("刷新", "Refresh"));
    m_reload->setToolTip(T("重新读取这个文件夹", "Reload this folder"));

    m_preset->setItemText(0, T("预设…", "Preset…"));
    m_preset->setToolTip(T("载入预设", "Load a preset"));
    m_presetSave->setText(T("存", "Save"));
    m_presetSave->setToolTip(T("把当前规则链存为预设", "Save the current chain as a preset"));
    m_presetDelete->setText(T("删", "Del"));
    m_presetDelete->setToolTip(T("删除选中的预设", "Delete the selected preset"));
    m_ruleAdd->setText(T("添加", "Add"));
    m_ruleAdd->setToolTip(T("添加一条规则", "Add a rule"));
    m_ruleDup->setText(T("复制", "Copy"));
    m_ruleDup->setToolTip(T("复制选中的规则", "Duplicate the selected rule"));
    m_ruleUp->setText(T("上移", "Up"));
    m_ruleUp->setToolTip(T("把选中的规则上移一位", "Move the selected rule up"));
    m_ruleDown->setText(T("下移", "Down"));
    m_ruleDown->setToolTip(T("把选中的规则下移一位", "Move the selected rule down"));
    m_ruleRemove->setText(T("删除", "Remove"));
    m_ruleRemove->setToolTip(T("删除选中的规则", "Remove the selected rule"));

    m_find->setPlaceholderText(T("查找", "Find"));
    m_replace->setPlaceholderText(T("替换为", "Replace with"));
    m_vimToggle->setText(T("Vim", "Vim"));
    m_regex->setText(T("使用正则表达式", "Use regular expressions"));
    m_matchAll->setText(T("替换全部匹配", "Match all occurrences"));
    m_caseSensitive->setText(T("区分大小写", "Case sensitive"));
    m_scopeLabel->setText(T("应用于", "Apply to"));
    m_formatLabel->setText(T("文本格式", "Text formatting"));
    m_scope->setItemText(0, T("文件名", "Name"));
    m_scope->setItemText(1, T("仅扩展名", "Extension only"));
    m_scope->setItemText(2, T("完整名称", "Full name"));
    m_filter->setItemText(0, T("全部", "All"));
    m_filter->setItemText(1, T("已选", "Selected"));
    m_filter->setItemText(2, T("可改", "Applicable"));
    m_filter->setItemText(3, T("要改", "To rename"));
    m_selectAll->setToolTip(T("全选", "Select all"));

    static const char* caseTipsZh[4] = {"全部小写", "全部大写", "首字母大写", "每词首字母大写"};
    static const char* caseTipsEn[4] = {"lowercase", "UPPERCASE", "Capitalize", "Title Case"};
    for (int i = 0; i < 4; ++i)
        m_caseButtons[i]->setToolTip(T(caseTipsZh[i], caseTipsEn[i]));
}

void Window::RefreshMode()
{
    const bool text = m_bridge->mode() == QStringLiteral("text");
    m_modeRules->setChecked(!text);
    m_modeText->setChecked(text);
    m_leftStack->setCurrentIndex(text ? 1 : 0);
}

void Window::RefreshCounts()
{
    if (!m_bridge)
        return;

    const int total = m_bridge->totalCount();
    const int ready = m_bridge->readyCount();
    m_originalHeader->setText(T("原始 (%1)", "Original (%1)").arg(total));
    m_targetHeader->setText(T("重命名后 (%1)", "Renamed (%1)").arg(ready));

    m_syncing = true;
    m_selectAll->setChecked(m_bridge->allSelected());
    const QString filter = m_bridge->filter();
    m_filter->setCurrentIndex(filter == QStringLiteral("selected") ? 1
                            : filter == QStringLiteral("applicable") ? 2
                            : filter == QStringLiteral("to-rename") ? 3 : 0);
    m_syncing = false;

    m_apply->setText(T("重命名 %1 项", "Rename %1").arg(ready));
    m_apply->setEnabled(m_bridge->canExecute());
    m_apply->setVisible(m_bridge->doneLine().isEmpty());
}

void Window::RefreshEmptyState()
{
    if (!m_bridge)
        return;

    const bool empty = m_bridge->previewModel()->rowCount() == 0;
    m_tableStack->setCurrentWidget(empty ? static_cast<QWidget*>(m_empty) : m_table);
    if (!empty)
        return;

    const QString done = m_bridge->doneLine();
    const QString blocked = m_bridge->blockedLine();
    if (!done.isEmpty())
    {
        m_empty->setText(done + QLatin1Char('\n')
                         + T("可在资源管理器里按 Ctrl+Z 撤消", "Undo with Ctrl+Z in Explorer"));
        m_empty->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme->ok.name()));
    }
    else if (!blocked.isEmpty())
    {
        m_empty->setText(blocked);
        m_empty->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme->danger.name()));
    }
    else
    {
        m_empty->setText(T("这里会列出要改名的文件。\n从文件夹标签打开，或选中文件后右键“重命名…”。",
                           "The files to rename appear here.\nOpen it from a folder tab, or right-click selected files."));
        m_empty->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme->faint.name()));
    }
}

void Window::RefreshFooter()
{
    if (!m_bridge)
        return;

    const QString done = m_bridge->doneLine();
    const int errors = m_bridge->errorCount();
    const int ready = m_bridge->readyCount();
    const int unchanged = m_bridge->unchangedCount();
    const int skipped = m_bridge->skippedCount();

    const QString blocking = m_bridge->blockingReason();
    const bool blocked = !m_bridge->blockedLine().isEmpty();
    QColor dot = m_theme->faint;
    if (!done.isEmpty())
        dot = m_theme->ok;
    else if (blocked || errors > 0 || skipped > 0)
        dot = m_theme->caution;
    else if (ready > 0)
        dot = m_theme->ok;
    m_dot->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;").arg(dot.name()));

    if (!done.isEmpty())
    {
        m_status->setText(done);
    }
    else
    {
        QString line = T("%1 项会改 · %2 项不变", "%1 to rename · %2 unchanged")
                           .arg(ready)
                           .arg(unchanged);
        if (errors > 0)
            line += T(" · %1 项冲突或有问题", " · %1 blocked").arg(errors);
        else if (ready > 0)
            line += T(" · 无冲突", " · no conflicts");
        if (skipped > 0)
            line += T(" · %1 项未出现在文本中", " · %1 not in text").arg(skipped);
        m_status->setText(line);
    }

    QString hint;
    if (!blocking.isEmpty())
        hint = blocking;
    else if (ready > 0)
        hint = T("Enter 执行 · 可撤销", "Enter runs · undoable");
    else
        hint = T("添加规则或载入预设", "Add a rule or load a preset");
    m_hint->setText(hint);
    m_hint->setToolTip(hint);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme->subtext.name()));
    m_hint->setStyleSheet(QStringLiteral("color:%1;")
                              .arg((blocking.isEmpty() ? m_theme->faint : m_theme->danger).name()));
}

void Window::ApplyAppearance()
{
    if (!m_bridge || !m_theme)
        return;

    *m_theme = Theme::From(m_bridge->look(), m_bridge->dark(), m_bridge->uiSize(),
                           m_bridge->locale());
    QApplication::setFont(m_theme->font);
    m_theme->apply(this);

    setWindowTitle(T("批量重命名", "Batch Rename"));
    m_table->verticalHeader()->setDefaultSectionSize(m_theme->rowHeight);
    m_table->setColumnWidth(0, 34);
    QPalette tablePalette = m_table->palette();
    tablePalette.setColor(QPalette::Highlight, m_theme->selection);
    tablePalette.setColor(QPalette::HighlightedText, m_theme->text);
    m_table->setPalette(tablePalette);
    m_bridge->previewModel()->setBaseFont(m_theme->font);
    if (m_scope)
        m_scope->view()->setFont(m_theme->font);
    if (m_filter)
        m_filter->view()->setFont(m_theme->font);
    if (m_text)
    {
        m_text->setFont(m_theme->mono);
        m_text->setTabStopDistance(4 * QFontMetricsF(m_theme->mono).horizontalAdvance(QLatin1Char(' ')));
    }
    Retranslate();
    RefreshCounts();
    ApplyTitleBar();
    RefreshFooter();
    RefreshEmptyState();
}

void Window::ApplyTitleBar()
{
    ApplyDarkTitleBar(this, m_theme && m_theme->dark);
}

void Window::ConfirmAndApply()
{
    if (!m_bridge)
        return;

    // A click can arrive before the debounce has pushed the last keystroke.
    if (m_ruleTimer)
        m_ruleTimer->stop();
    if (m_textTimer)
        m_textTimer->stop();
    if (m_bridge->mode() == QStringLiteral("control"))
        PushRule();
    else
        m_bridge->setBuffer(m_text->toPlainText());

    if (!m_bridge->canExecute())
        return;

    const int ready = m_bridge->readyCount();
    const int unchanged = m_bridge->unchangedCount();
    const int errors = m_bridge->errorCount();
    const int skipped = m_bridge->skippedCount();

    QString text = T("将重命名 %1 项。", "Rename %1 item(s).").arg(ready);
    if (unchanged > 0)
        text += T("\n%1 项不变。", "\n%1 unchanged.").arg(unchanged);
    if (skipped > 0)
        text += T("\n%1 项未出现在文本中，保持不变。", "\n%1 item(s) are not in the text and stay unchanged.").arg(skipped);
    if (errors > 0)
        text += T("\n%1 项冲突或有问题，不会执行。", "\n%1 item(s) blocked and will not run.").arg(errors);
    text += T("\n\n确定执行吗？", "\n\nRun it now?");

    if (!Ask(text, T("重命名", "Rename")))
        return;

    m_bridge->apply();
    RefreshAll();
}

bool Window::Ask(const QString& text, const QString& acceptText)
{
    if (m_asking)
        return false;
    m_asking = true;
    QMessageBox box(QMessageBox::Question, T("重命名", "Rename"), text,
                    QMessageBox::NoButton, this);
    box.setIcon(QMessageBox::Question);
    if (m_theme)
        m_theme->apply(&box);
    QAbstractButton* accept = box.addButton(acceptText, QMessageBox::AcceptRole);
    QAbstractButton* cancel = box.addButton(T("取消", "Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(cancel));
    box.exec();
    m_asking = false;
    return box.clickedButton() == accept;
}

void Window::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    static bool placed = false;
    if (!placed)
    {
        placed = true;
        if (QScreen* screen = this->screen())
        {
            const QRect area = screen->availableGeometry();
            move(area.center() - rect().center());
        }
    }
    ApplyTitleBar();
}

void Window::GrabToFile(const QString& path)
{
    QTimer::singleShot(800, this, [this, path] {
        const QPixmap pixmap = grab();
        const bool saved = pixmap.save(path, "PNG");
        std::fprintf(stderr, "[rename] grab %s: %s (%dx%d)\n",
                     saved ? "written" : "FAILED", path.toUtf8().constData(),
                     pixmap.width(), pixmap.height());
        std::fflush(stderr);
        QCoreApplication::quit();
    });
}

} // namespace rename_widgets
