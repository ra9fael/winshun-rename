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
#include <QDockWidget>
#include <QDialog>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QDir>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QListView>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QStatusBar>
#include <QToolBar>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QShortcut>
#include <QStyle>
#include <QStackedWidget>
#include <QTableView>
#include <QTabBar>
#include <QTextBrowser>
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

class PathLabel : public QWidget
{
public:
    using QWidget::QWidget;
    QString path;
    QSize sizeHint() const override { return QSize(80, 28); }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(path, Qt::ElideLeft, width()));
    }
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
        if (option.state & QStyle::State_Selected)
            painter->fillRect(option.rect, m_theme->selection);
        else if (option.state & QStyle::State_MouseOver)
            painter->fillRect(option.rect, m_theme->hover);

        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        QRect box(0, 0, 16, 16);
        box.moveCenter(option.rect.center());
        PaintCheck(*painter, box, checked, option.state & QStyle::State_MouseOver,
                   *m_theme, option.state & QStyle::State_Enabled);
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        if (!index.isValid() || !model || index.column() != 0)
            return false;
        if (event->type() == QEvent::MouseButtonRelease)
        {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (option.rect.contains(mouse->position().toPoint()))
            {
                const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
                return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            }
        }
        if (event->type() == QEvent::KeyPress)
        {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Space)
            {
                const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
                return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
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
        if (option.state & QStyle::State_Selected)
            painter->fillRect(option.rect, m_theme->selection);
        else if (option.state & QStyle::State_MouseOver)
            painter->fillRect(option.rect, m_theme->hover);

        const QString original = index.data(rename_qml::PreviewModel::OriginalRole).toString();
        const QString target = index.data(rename_qml::PreviewModel::TargetRole).toString();
        const int status = index.data(rename_qml::PreviewModel::StatusRole).toInt();
        const bool pendingDelete = index.data(rename_qml::PreviewModel::PendingDeleteRole).toBool();
        const bool error = status == static_cast<int>(rename_core::Status::ErrorAlreadyExists)
                        || status == static_cast<int>(rename_core::Status::ErrorTooLong)
                        || status == static_cast<int>(rename_core::Status::ErrorInvalidCharacter)
                        || status == static_cast<int>(rename_core::Status::ErrorEmptyName)
                        || status == static_cast<int>(rename_core::Status::ErrorBadRule);
        const bool unchanged = status == static_cast<int>(rename_core::Status::Unchanged);
        const bool ready = status == static_cast<int>(rename_core::Status::Ready);

        QColor plain = error || pendingDelete ? m_theme->danger
                      : unchanged ? m_theme->faint
                      : m_theme->text;
        QColor highlight = error || pendingDelete ? m_theme->danger
                         : m_theme->accent;

        QFont font = option.font;
        if (ready && !error && !pendingDelete)
            font.setBold(true);
        if (pendingDelete)
            font.setStrikeOut(true);
        painter->setFont(font);
        painter->setClipRect(option.rect);

        // Only a row that will actually rename gets the two-colour treatment.
        const bool changed = ready && !pendingDelete && target != original;
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

// Text elided from the left, keeping the end: a path's last segments are what
// tell two rows apart, and Qt's ElideMiddle spends the room on the drive.
QString ElideLeft(const QFontMetrics& metrics, const QString& text, int width)
{
    if (metrics.horizontalAdvance(text) <= width)
        return text;
    const QString ellipsis = QStringLiteral("…");
    int drop = 0;
    while (drop < text.size() && metrics.horizontalAdvance(ellipsis + text.mid(drop)) > width)
        ++drop;
    return ellipsis + text.mid(drop);
}

// Paints the original column: the name, and — only when the batch spans folders
// — its folder after it in the faint colour, elided in the middle. Two files
// called "same.txt" from different folders must not look like one row twice,
// and the name must keep the room it needs, which the stock delegate's
// middle-elided display string cannot do.
class OriginalDelegate : public QStyledItemDelegate
{
public:
    explicit OriginalDelegate(const Theme* theme, QObject* parent = nullptr)
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
        if (option.state & QStyle::State_Selected)
            painter->fillRect(option.rect, m_theme->selection);
        else if (option.state & QStyle::State_MouseOver)
            painter->fillRect(option.rect, m_theme->hover);

        const QString name = index.data(rename_qml::PreviewModel::OriginalRole).toString();
        const QString folder = index.data(rename_qml::PreviewModel::ParentRole).toString();
        const QFontMetrics metrics(option.font);
        const int flags = Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine;
        const int room = option.rect.width() - 6;
        const int nameWidth = metrics.horizontalAdvance(name);
        const QString separator = QStringLiteral("  ·  ");
        const int separatorWidth = metrics.horizontalAdvance(separator);

        painter->setFont(option.font);
        if (folder.isEmpty() || nameWidth + separatorWidth + 40 > room)
        {
            painter->setPen(m_theme->text);
            painter->drawText(option.rect.adjusted(2, 0, -4, 0), flags,
                              metrics.elidedText(name, Qt::ElideRight, room));
            painter->restore();
            return;
        }

        const QRect nameCell(option.rect.left() + 2, option.rect.top(), nameWidth, option.rect.height());
        painter->setPen(m_theme->text);
        painter->drawText(nameCell, flags, name);

        const int tail = room - nameWidth - separatorWidth;
        painter->setPen(m_theme->faint);
        painter->drawText(QRect(nameCell.right() + 1, option.rect.top(), separatorWidth, option.rect.height()),
                          flags, separator);
        painter->drawText(QRect(nameCell.right() + 1 + separatorWidth, option.rect.top(), tail,
                                option.rect.height()), flags, ElideLeft(metrics, folder, tail));
        painter->restore();
    }

private:
    const Theme* m_theme = nullptr;
};

} // namespace

Window::Window(rename_qml::Bridge* bridge, QWidget* parent)
    : QMainWindow(parent)
    , m_bridge(bridge)
{
    setWindowTitle(T("批量重命名", "Batch Rename"));
    setMinimumSize(700, 680);
    resize(1000, 720);
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
    setContextMenuPolicy(Qt::PreventContextMenu);

    const auto makeGlyphButton = [this](QChar glyph, const QString& tip) {
        auto* button = new QToolButton(this);
        button->setProperty("phGlyph", glyph.unicode());
        button->setToolTip(tip);
        button->setObjectName(QStringLiteral("toolbarGlyph"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(30, 26);
        button->setAccessibleName(tip);
        return button;
    };

    m_vimToggle = new QToolButton(this);
    m_vimToggle->setText(QStringLiteral("VIM"));
    m_vimToggle->setCheckable(true);
    m_vimToggle->setObjectName(QStringLiteral("vimToggle"));
    m_vimToggle->setCursor(Qt::PointingHandCursor);
    m_vimToggle->setFixedHeight(26);
    // One width for "Vim" and every mode letter keeps the status bar stable.
    m_vimToggle->setFixedWidth(40);
    m_vimToggle->setToolTip(T("Vim 按键模式（默认关闭）", "Vim key bindings (off by default)"));

    m_apply = new QPushButton(T("重命名", "Rename"), this);
    m_apply->setObjectName(QStringLiteral("statusApply"));
    m_apply->setProperty("phGlyph", 0xEB0A);
    m_apply->setToolTip(T("执行重命名（Ctrl+S）", "Run rename (Ctrl+S)"));
    // Compact chip that matches the status-bar badges and the Vim toggle; the
    // flexible hint text is the part that yields space, never this button.
    m_apply->setFixedHeight(26);
    m_apply->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* helpButton = makeGlyphButton(QChar(0xE3E8), T("帮助", "Help"));
    helpButton->setObjectName(QStringLiteral("statusHelp"));

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
    m_up->setProperty("phGlyph", 0xE08E);
    m_up->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_up->setFixedSize(30, 28);
    pathLayout->addWidget(m_up);

    m_breadcrumb = new QWidget(pathBar);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumb);
    m_breadcrumbLayout->setContentsMargins(7, 0, 7, 0);
    m_breadcrumbLayout->setSpacing(2);
    m_breadcrumb->setObjectName(QStringLiteral("breadcrumb"));
    m_breadcrumb->setMinimumWidth(0);
    m_breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_breadcrumb->setToolTip(T("双击路径栏编辑完整路径",
                               "Double-click to edit the full path"));
    m_breadcrumb->installEventFilter(this);
    pathLayout->addWidget(m_breadcrumb, 1);

    m_path = new QLineEdit(pathBar);
    m_path->setPlaceholderText(T("文件夹", "Folder"));
    m_path->setClearButtonEnabled(false);
    m_path->setToolTip(T("回车进入这个文件夹", "Press Enter to open this folder"));
    m_path->setVisible(false);
    pathLayout->addWidget(m_path, 1);

    m_browse = new QToolButton(pathBar);
    m_browse->setText(T("选择…", "Choose…"));
    m_browse->setObjectName(QStringLiteral("chipButton"));
    m_browse->setToolTip(T("选择目录", "Choose a folder"));
    m_browse->setCursor(Qt::PointingHandCursor);
    m_browse->setProperty("phGlyph", 0xE256);
    m_browse->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_browse->setFixedSize(30, 28);
    pathLayout->addWidget(m_browse);

    m_reload = new QToolButton(pathBar);
    m_reload->setText(T("刷新", "Refresh"));
    m_reload->setObjectName(QStringLiteral("chipButton"));
    m_reload->setToolTip(T("重新读取这个文件夹", "Reload this folder"));
    m_reload->setCursor(Qt::PointingHandCursor);
    m_reload->setProperty("phGlyph", 0xE036);
    m_reload->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_reload->setFixedSize(30, 28);
    pathLayout->addWidget(m_reload);

    // Left: rules or text editor, resized with the native dock separator.
    auto* left = new QWidget(this);
    left->setMinimumWidth(280);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(16, 14, 16, 13);
    leftLayout->setSpacing(0);

    m_editorTabs = new QTabBar(left);
    m_editorTabs->addTab(T("规则", "Rules"));
    m_editorTabs->addTab(T("文本", "Text"));
    m_editorTabs->setExpanding(false);
    m_editorTabs->setObjectName(QStringLiteral("editorTabs"));
    leftLayout->addWidget(m_editorTabs);

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

    m_text = new QPlainTextEdit(textPage);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(m_theme->mono);
    m_text->setObjectName(QStringLiteral("renameTextEditor"));
    m_text->setTabStopDistance(4 * QFontMetricsF(m_theme->mono).horizontalAdvance(QLatin1Char(' ')));
    textLayout->addWidget(m_text, 1);

    // The vim command/status line: ":norm a b" while typing a command,
    // "-- NORMAL --" otherwise. Always visible while vim is on (so the editor
    // does not jump when the line appears), hidden entirely when vim is off.
    m_vimCommandLine = new QLabel(textPage);
    m_vimCommandLine->setObjectName(QStringLiteral("vimCommandLine"));
    m_vimCommandLine->setFont(m_theme->mono);
    m_vimCommandLine->setFixedHeight(22);
    m_vimCommandLine->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_vimCommandLine->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_vimCommandLine->setVisible(false);
    textLayout->addWidget(m_vimCommandLine);

    m_leftStack->addWidget(textPage);

    // Right: table.
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(pathBar);

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

    auto* statusHeader = new QLabel(header);
    statusHeader->setObjectName(QStringLiteral("caption"));
    statusHeader->setFixedWidth(78);
    statusHeader->setText(T("状态", "Status"));
    headerLayout->addWidget(statusHeader);

    m_filter = new ComboBox(m_theme, header);
    m_filter->addItem(T("全部", "All"));
    m_filter->addItem(T("已选", "Selected"));
    m_filter->addItem(T("可改", "Applicable"));
    m_filter->addItem(T("要改", "To rename"));
    m_filter->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_filter->setToolTip(T("筛选显示的条目，不改变要执行的集合", "Filter the rows shown; the batch itself is unchanged"));
    m_filter->setFixedHeight(28);
    pathLayout->addWidget(m_filter);
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
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(3, 78);
    m_table->setItemDelegateForColumn(0, new CheckDelegate(m_theme, m_table));
    m_table->setItemDelegateForColumn(1, new OriginalDelegate(m_theme, m_table));
    m_table->setItemDelegateForColumn(2, new TargetDelegate(m_theme, m_table));
    m_tableStack->addWidget(m_table);

    m_empty = new QLabel(m_tableStack);
    m_empty->setObjectName(QStringLiteral("empty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_tableStack->addWidget(m_empty);
    rightLayout->addWidget(m_tableStack, 1);

    auto* footer = statusBar();
    footer->setSizeGripEnabled(false);
    footer->setMinimumHeight(38);
    auto* statusContent = new QWidget(footer);
    auto* statusLayout = new QHBoxLayout(statusContent);
    statusLayout->setContentsMargins(14, 4, 14, 4);
    statusLayout->setSpacing(6);

    auto* statusLeft = new QWidget(statusContent);
    auto* statusLeftLayout = new QHBoxLayout(statusLeft);
    statusLeftLayout->setContentsMargins(0, 0, 0, 0);
    statusLeftLayout->setSpacing(6);
    // Help sits at the far left and rename at the far right; the mode, caret
    // position and counts stay grouped on the left, the hint takes the gap.
    helpButton->setParent(statusContent);
    helpButton->setFixedSize(30, 26);
    statusLayout->addWidget(helpButton, 0, Qt::AlignLeft | Qt::AlignVCenter);
    auto* helpDivider = Divider(Qt::Vertical, statusContent);
    helpDivider->setFixedHeight(20);
    statusLayout->addWidget(helpDivider);
    statusLayout->addWidget(statusLeft, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_vimToggle->setParent(statusLeft);
    statusLeftLayout->addWidget(m_vimToggle);
    m_cursorPosition = new QLabel(statusContent);
    m_cursorPosition->setObjectName(QStringLiteral("cursorPosition"));
    m_cursorPosition->setMinimumWidth(48);
    m_cursorPosition->setAlignment(Qt::AlignCenter);
    m_cursorPosition->setFont(m_theme->mono);
    m_cursorPosition->setParent(statusLeft);
    statusLeftLayout->addWidget(m_cursorPosition);
    auto* editorDivider = Divider(Qt::Vertical, statusLeft);
    editorDivider->setFixedHeight(20);
    statusLeftLayout->addWidget(editorDivider);

    const auto makeBadge = [statusLeft](QLabel*& target) {
        target = new QLabel(statusLeft);
        target->setAlignment(Qt::AlignCenter);
        target->setMinimumHeight(22);
        return target;
    };
    makeBadge(m_readyBadge);
    makeBadge(m_conflictBadge);
    makeBadge(m_deleteBadge);
    statusLeftLayout->addWidget(m_readyBadge);
    statusLeftLayout->addWidget(m_conflictBadge);
    statusLeftLayout->addWidget(m_deleteBadge);
    auto* countDivider = Divider(Qt::Vertical, statusLeft);
    countDivider->setFixedHeight(20);
    statusLeftLayout->addWidget(countDivider);
    m_hint = new QLabel(statusContent);
    m_hint->setObjectName(QStringLiteral("caption"));
    m_hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hint->setMinimumWidth(0);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLayout->addWidget(m_hint, 1);
    auto* actionDivider = Divider(Qt::Vertical, statusContent);
    actionDivider->setFixedHeight(20);
    statusLayout->addWidget(actionDivider);
    m_apply->setParent(statusContent);
    statusLayout->addWidget(m_apply, 0, Qt::AlignRight | Qt::AlignVCenter);
    footer->addPermanentWidget(statusContent, 1);

    // One editor dock contains the two editor tabs. This keeps the default
    // layout deterministic and prevents the rules/text surfaces from splitting
    // into separate floating windows.
    auto* editorDock = new QDockWidget(T("编辑区", "Editor"), this);
    editorDock->setObjectName(QStringLiteral("renameEditorDock"));
    editorDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    editorDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    editorDock->setWidget(left);

    auto* filesDock = new QDockWidget(T("文件列表", "File list"), this);
    filesDock->setObjectName(QStringLiteral("renameFilesDock"));
    filesDock->setAllowedAreas(Qt::RightDockWidgetArea);
    filesDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    filesDock->setWidget(right);

    addDockWidget(Qt::LeftDockWidgetArea, editorDock);
    addDockWidget(Qt::RightDockWidgetArea, filesDock);
    resizeDocks({editorDock, filesDock}, {500, 500}, Qt::Horizontal);

    connect(m_editorTabs, &QTabBar::currentChanged, this, [this](int index) {
        m_leftStack->setCurrentIndex(index);
        RequestMode(index == 1 ? QStringLiteral("text") : QStringLiteral("control"));
    });

    auto* helpBrowser = new QTextBrowser(this);
    helpBrowser->setOpenExternalLinks(false);
    helpBrowser->setFrameShape(QFrame::NoFrame);
    helpBrowser->setText(T(
        "筛选只改变列表显示，不改变实际执行集合。\n\n"
        "全部：显示所有条目，包括不变和有问题的条目。\n"
        "已选：只显示当前勾选的条目。\n"
        "可改：显示没有错误、且目标名称不同的条目。\n"
        "要改：显示已选且可以执行重命名的条目。\n\n"
        "文本模式中的 Vim：开启后，j/k 移动行，h/l 移动字符，dd 标记待删除，u 恢复。\n"
        "待删除行会保留在列表中，并以淡红背景、删除线和状态列提示。\n\n"
        "Ctrl+Q：列选择；j/k 扩展行，I 在块首插入，A 在块尾插入，r 替换块内字符，Esc 将输入扩散到其他选中行。\n"
        "df, / dt, / dF, / dT,：d 支持 f F t T 位移；d; 和 d, 重复上一次查找。v/V/Ctrl+Q 里按 r 替换选中字符。\n"
        "Ctrl+S：结束当前插入并确认重命名。\n"
        ":q 关闭窗口（还有未执行的修改会拒绝），:q! 放弃这些修改并关闭。\n"
        ":%norm A.txt：在每行末尾追加 .txt。\n"
        ":g/模式/norm 按键：对匹配行执行普通模式命令。\n"
        "文本语法：每行 “状态字母 编号 文件名”。y 改名、n 跳过、d 删除（移入回收站）。\n"
        "编号是文件身份，改名字时请保留；删掉一整行会报错，删除请把状态改成 d。\n"
        "文件名可含空格；< > : \" / \\ | ? * 与 CON、NUL 等保留名 Windows 不允许。",
        "Filters change only what is visible; they never change the execution set.\n\n"
        "All: every row, including unchanged and blocked rows.\n"
        "Selected: rows currently checked.\n"
        "Applicable: rows without errors whose target name changes.\n"
        "To rename: selected rows that can be executed.\n\n"
        "Vim in Text mode: when enabled, j/k move between rows, h/l move by character, dd marks a row for deletion, and u restores it.\n"
        "Pending deletes stay in the list with a pale red background, strike-through text, and an explicit status.\n\n"
        "Ctrl+Q: select a block; j/k extend rows, I inserts before it, A appends after it, r replaces the characters inside it, Esc repeats the insertion across selected rows.\n"
        "df, / dt, / dF, / dT,: d takes f F t T as a motion; d; and d, repeat the last find. r in v/V/Ctrl+Q replaces the selected characters.\n"
        "Ctrl+S: finish insertion and confirm renaming.\n"
        ":q closes the window (refused while changes are unapplied), :q! discards them and closes.\n"
        ":%norm A.txt: append .txt to every line.\n"
        ":g/pattern/norm keys: run Normal commands on matching lines.\n"
        "Text syntax: each line is \"state id name\". y renames, n skips, d deletes (to the Recycle Bin).\n"
        "The id is the file's identity: keep it when you edit names. A removed line is an error, so delete with d.\n"
        "Names may contain spaces; < > : \" / \\ | ? * and reserved names such as CON or NUL are not allowed."));
    auto* helpDialog = new QDialog(this);
    helpDialog->setObjectName(QStringLiteral("renameHelpDialog"));
    helpDialog->setWindowTitle(T("帮助", "Help"));
    helpDialog->resize(560, 420);
    auto* helpLayout = new QVBoxLayout(helpDialog);
    helpLayout->setContentsMargins(16, 16, 16, 16);
    helpLayout->addWidget(helpBrowser);
    connect(helpButton, &QToolButton::clicked, this, [this, helpDialog] {
        m_theme->apply(helpDialog);
        helpDialog->show();
        helpDialog->raise();
        helpDialog->activateWindow();
    });

    setDockOptions(QMainWindow::DockOptions{});

    // ---- input and shortcuts ---------------------------------------------
    connect(m_apply, &QPushButton::clicked, this, &Window::ConfirmAndApply);
    connect(m_selectAll, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_syncing)
            m_bridge->selectAll(checked);
    });
    connect(m_path, &QLineEdit::returnPressed, this, [this] { NavigateTo(m_path->text()); });
    connect(m_path, &QLineEdit::editingFinished, this, [this] { ShowPathEditor(false); });
    connect(m_up, &QToolButton::clicked, this, &Window::GoUp);
    connect(m_browse, &QToolButton::clicked, this, &Window::BrowseFolder);
    connect(m_reload, &QToolButton::clicked, this, &Window::ReloadFolder);
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        if (index.data(rename_qml::PreviewModel::PendingDeleteRole).toBool())
        {
            m_bridge->restorePendingDelete(index.data(rename_qml::PreviewModel::IdRole).toString());
            RefreshAll();
        }
        else if (m_bridge->rowIsFolder(index.row()))
            NavigateTo(m_bridge->pathForRow(index.row()));
    });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const QModelIndex index = m_table->indexAt(point);
        QMenu menu(m_table);

        // Undo lives here rather than only on vim's "u": every model-level change
        // rewrites the buffer, which invalidates the editor's undo stack. These
        // items go through the presenter and then re-publish the text.
        const QString id = index.isValid()
            ? index.data(rename_qml::PreviewModel::IdRole).toString() : QString();
        QAction* revertRow = nullptr;
        QAction* restoreDelete = nullptr;
        if (!id.isEmpty() && index.data(rename_qml::PreviewModel::PendingDeleteRole).toBool())
            restoreDelete = menu.addAction(T("取消待删除", "Cancel pending delete"));
        if (!id.isEmpty())
            revertRow = menu.addAction(T("恢复此行为原名", "Restore this row's original name"));
        QAction* revertAll = menu.addAction(T("还原全部未执行的修改", "Undo all unapplied edits"));
        menu.addSeparator();
        QAction* browse = index.isValid() && m_bridge->rowIsFolder(index.row())
            ? menu.addAction(T("打开这个文件夹", "Open this folder")) : nullptr;

        QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(point));
        if (!chosen)
            return;
        if (chosen == restoreDelete)
            m_bridge->restorePendingDelete(id);
        else if (chosen == revertRow)
            m_bridge->revertRow(id);
        else if (chosen == revertAll)
            m_bridge->revertAllEdits();
        else if (browse && chosen == browse)
            NavigateTo(m_bridge->pathForRow(index.row()));
        else
            return;
        RefreshAll();
    });
    // Small list-only Vim layer. vimcore remains the text editor's state machine;
    // this only maps the familiar table navigation/deletion gestures to model
    // operations when the table owns focus.
    m_table->installEventFilter(this);
    // A nested dock host does not bubble an editor's unhandled Esc to Window's
    // keyPressEvent. Watch the editor directly so non-Vim Esc still closes the
    // plugin while Vim's later-installed filter retains priority when enabled.
    m_text->installEventFilter(this);

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

    qApp->installEventFilter(this);

    // Esc is NOT a QShortcut: shortcut matching runs before key delivery, so it
    // would steal Esc from the vim filter and close the window mid-edit. The
    // keyPressEvent override below handles it and knows when the editor owns it.
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
    connect(m_text, &QPlainTextEdit::cursorPositionChanged,
            this, &Window::RefreshCursorPosition);

    // ---- vim mode ------------------------------------------------------------
    // The filter is installed once and gated by its own enabled flag; the toggle
    // only flips that flag and remembers it.
    m_vim = new VimTextFilter(m_text, this);
    connect(m_vim, &VimTextFilter::cursorLineChanged, this, &Window::HighlightPreviewRow);
    connect(m_vim, &VimTextFilter::statusChanged, this, [this](const QString& status) {
        Q_UNUSED(status);
        m_vimMessage.clear(); // any key press replaces a transient message
        RefreshVimStatus();
        RefreshFooter();
    });
    connect(m_vim, &VimTextFilter::quitRequested, this, &Window::OnVimQuit);
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

void Window::keyPressEvent(QKeyEvent* event)
{
    // Esc closes the window — unless the vim-enabled text editor owns it (leave
    // insert, cancel a pending operator). The vim filter consumes those keys
    // before they bubble here; this guard covers the remaining focus cases.
    if (event->key() == Qt::Key_Escape)
    {
        const bool vimOwnsIt = m_vim && m_vim->enabled() && m_text && m_text->hasFocus();
        if (!vimOwnsIt)
        {
            close();
            return;
        }
        event->ignore();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool Window::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto* target = qobject_cast<QWidget*>(watched);
        auto* key = static_cast<QKeyEvent*>(event);
        if (target && target->window() == this && !QApplication::activeModalWidget()
            && key->key() == Qt::Key_S && key->modifiers() == Qt::ControlModifier)
        {
            key->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat())
                ConfirmAndApply();
            return true;
        }
    }
    if (watched == m_breadcrumb && event->type() == QEvent::MouseButtonDblClick)
    {
        ShowPathEditor(true);
        return true;
    }
    if (watched == m_text && event->type() == QEvent::KeyPress)
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && (!m_vim || !m_vim->enabled()))
        {
            close();
            return true;
        }
    }
    if (watched != m_table || event->type() != QEvent::KeyPress || !m_vim
        || !m_vim->enabled() || !m_table)
        return QWidget::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    if (key->modifiers() != Qt::NoModifier || key->text().isEmpty())
        return QWidget::eventFilter(watched, event);
    const QChar ch = key->text().at(0);
    const int rows = m_table->model() ? m_table->model()->rowCount() : 0;
    if (rows <= 0)
        return QWidget::eventFilter(watched, event);

    const int current = m_table->currentIndex().isValid() ? m_table->currentIndex().row() : 0;
    const auto selectRow = [this, rows](int row) {
        const QModelIndex index = m_table->model()->index(qBound(0, row, rows - 1), 0);
        m_table->selectionModel()->setCurrentIndex(index,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_table->scrollTo(index, QAbstractItemView::EnsureVisible);
    };

    if (m_listPendingG)
    {
        m_listPendingG = false;
        if (ch == QLatin1Char('g'))
        {
            selectRow(0);
            return true;
        }
    }
    if (m_listPendingD)
    {
        m_listPendingD = false;
        if (ch == QLatin1Char('d'))
        {
            const QModelIndex index = m_table->currentIndex();
            if (index.isValid())
            {
                const QString id = index.data(rename_qml::PreviewModel::IdRole).toString();
                m_bridge->markRowForDelete(id);
                RefreshAll();
                selectRow(qMin(current, rows - 2));
            }
            return true;
        }
    }

    switch (ch.unicode())
    {
    case 'j': selectRow(current + 1); return true;
    case 'k': selectRow(current - 1); return true;
    case 'G': selectRow(rows - 1); return true;
    case 'g': m_listPendingG = true; return true;
    case 'd': m_listPendingD = true; return true;
    case 'u':
    {
        const QModelIndex index = m_table->currentIndex();
        if (index.isValid()
            && index.data(rename_qml::PreviewModel::PendingDeleteRole).toBool())
        {
            m_bridge->restorePendingDelete(index.data(rename_qml::PreviewModel::IdRole).toString());
            RefreshAll();
            selectRow(qMin(current, m_table->model()->rowCount() - 1));
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    default: return QWidget::eventFilter(watched, event);
    }
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
    RefreshVimStatus();
}

void Window::OnVimQuit(bool force)
{
    if (!m_bridge)
        return;
    // "Unapplied edits" means exactly what the status bar counts as ready to run:
    // renames to perform and rows marked for deletion.
    const bool hasPendingEdits = m_bridge->readyCount() > 0 || m_bridge->deleteCount() > 0;
    if (hasPendingEdits && !force)
    {
        // vim's E37: the buffer is "modified", :q will not throw the work away.
        ShowVimMessage(T("E37: 还有未执行的修改（用 :q! 放弃）",
                         "E37: pending changes (use :q! to discard them)"));
        return;
    }
    if (hasPendingEdits)
        m_bridge->revertAllEdits(); // :q! — drop them, then close
    // Closing from inside a key event would destroy the widget that is still
    // delivering it, so the close is queued.
    QMetaObject::invokeMethod(this, [this] { close(); }, Qt::QueuedConnection);
}

void Window::ShowVimMessage(const QString& message)
{
    if (!m_vimCommandLine)
        return;
    m_vimMessage = message;
    RefreshVimStatus();
    // vim clears its message on the next key press; a timer covers the case where
    // the next key does not change the mode at all.
    QTimer::singleShot(4000, this, [this, message] {
        if (m_vimMessage == message)
        {
            m_vimMessage.clear();
            RefreshVimStatus();
        }
    });
}

void Window::RefreshVimStatus()
{
    if (!m_vimToggle || !m_vim || !m_vimCommandLine)
        return;
    const bool enabled = m_vim->enabled();
    // One character per mode, vim-style. The button keeps a fixed width, so
    // the toolbar never twitches as the letters change.
    QString modeLetter;
    QString modeName;
    if (enabled)
    {
        switch (m_vim->mode())
        {
        case vimcore::Mode::Insert:      modeLetter = QStringLiteral("I"); modeName = T("插入", "Insert"); break;
        case vimcore::Mode::Replace:     modeLetter = QStringLiteral("R"); modeName = T("替换", "Replace"); break;
        case vimcore::Mode::Visual:      modeLetter = QStringLiteral("v"); modeName = T("可视", "Visual"); break;
        case vimcore::Mode::VisualLine:  modeLetter = QStringLiteral("V"); modeName = T("行可视", "Visual Line"); break;
        case vimcore::Mode::VisualBlock: modeLetter = QStringLiteral("B"); modeName = T("块可视", "Visual Block"); break;
        case vimcore::Mode::Normal:      break;
        }
        if (modeLetter.isEmpty())
        {
            modeLetter = QStringLiteral("N");
            modeName = T("普通", "Normal");
        }
    }
    m_syncing = true;
    m_vimToggle->setText(enabled ? modeLetter : QStringLiteral("VIM"));
    m_syncing = false;
    m_vimToggle->setToolTip(enabled
        ? T("Vim 按键模式已开启（%1）· 点此关闭", "Vim key bindings are on (%1) · click to turn off").arg(modeName)
        : T("Vim 按键模式（默认关闭）", "Vim key bindings (off by default)"));

    // The command line under the editor: the raw status the core asked for,
    // i.e. the ":" command while one is being typed, "-- NORMAL --" otherwise.
    // A transient message such as "E37: ..." wins until the next key press.
    m_vimCommandLine->setVisible(enabled);
    QString status = m_vim->statusText();
    if (!m_vimMessage.isEmpty() && !status.startsWith(QLatin1Char(':')))
        status = m_vimMessage;
    m_vimCommandLine->setText(status);
    m_vimCommandLine->setToolTip(status);
    const bool enteringCommand = status.startsWith(QLatin1Char(':'));
    m_vimCommandLine->setStyleSheet(enteringCommand
        ? QStringLiteral("color:%1; font-weight:600; padding-left:7px;")
              .arg(m_theme ? m_theme->accent.name() : QStringLiteral("#0078d4"))
        : QStringLiteral("color:%1; padding-left:7px;")
              .arg(m_theme ? m_theme->faint.name() : QStringLiteral("#808080")));
}

void Window::RefreshCursorPosition()
{
    if (!m_cursorPosition || !m_text || !m_editorTabs)
        return;
    const bool textPage = m_editorTabs->currentIndex() == 1;
    m_cursorPosition->setVisible(textPage);
    if (!textPage)
        return;
    const QTextCursor cursor = m_text->textCursor();
    m_cursorPosition->setText(QStringLiteral("%1:%2")
                                  .arg(cursor.blockNumber() + 1)
                                  .arg(cursor.positionInBlock() + 1));
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
    if (m_bridge->mode() != QStringLiteral("text"))
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
    if (m_breadcrumb && !m_path->isVisible())
    {
        while (QLayoutItem* item = m_breadcrumbLayout->takeAt(0))
        {
            if (QWidget* widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        auto* label = new PathLabel(m_breadcrumb);
        label->path = QDir::toNativeSeparators(folder);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_breadcrumbLayout->addWidget(label, 1);
        m_breadcrumb->setToolTip(label->path + T("\n双击编辑完整路径", "\nDouble-click to edit the full path"));
    }
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

void Window::ShowPathEditor(bool show)
{
    if (!m_path || !m_breadcrumb)
        return;
    m_breadcrumb->setVisible(!show);
    m_path->setVisible(show);
    if (show)
    {
        m_path->setText(m_bridge ? m_bridge->currentFolder() : QString());
        m_path->setFocus(Qt::OtherFocusReason);
        m_path->selectAll();
    }
    else
        SyncPath();
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
    if (m_table && m_table->selectionModel())
        m_table->selectionModel()->clearSelection();
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
    // Rows are rebuilt on every buffer change; keep the highlighted row in step.
    HighlightPreviewRow(m_vimLine);
}

void Window::HighlightPreviewRow(int line)
{
    // The text buffer is "<state> <id> <name>" per line; the preview table is
    // keyed by the same id, so one lookup connects the line the vim cursor is on
    // to its row.
    m_vimLine = line;
    if (!m_bridge || !m_table || !m_vim || !m_vim->enabled())
        return;
    if (m_bridge->mode() != QStringLiteral("text"))
        return;

    const QStringList lines = m_bridge->buffer().split(QLatin1Char('\n'));
    if (line < 0 || line >= lines.size())
        return;
    // Skip the state letter and the separator, then read the id digits.
    const QString text = lines.at(line);
    int i = 1;
    while (i < text.size() && text.at(i).isSpace())
        ++i;
    const int digitsStart = i;
    while (i < text.size() && text.at(i).isDigit())
        ++i;
    if (i == digitsStart)
        return;
    const QString id = text.mid(digitsStart, i - digitsStart);

    QAbstractItemModel* model = m_bridge->previewModel();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QModelIndex index = model->index(row, 0);
        if (model->data(index, rename_qml::PreviewModel::IdRole).toString() == id)
        {
            m_table->selectionModel()->setCurrentIndex(
                index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_table->scrollTo(index, QAbstractItemView::EnsureVisible);
            return;
        }
    }
}

void Window::Retranslate()
{
    setWindowTitle(T("批量重命名", "Batch Rename"));
    m_editorTabs->setTabText(0, T("规则", "Rules"));
    m_editorTabs->setTabText(1, T("文本", "Text"));

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
    RefreshVimStatus();
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
    if (m_editorTabs)
    {
        QSignalBlocker blocker(m_editorTabs);
        m_editorTabs->setCurrentIndex(text ? 1 : 0);
    }
    m_leftStack->setCurrentIndex(text ? 1 : 0);
    RefreshCursorPosition();
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

    m_apply->setText(T("重命名", "Rename"));
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
    const int toDelete = m_bridge->deleteCount();

    const QString blocking = m_bridge->blockingReason();

    const auto badgeStyle = [this](QLabel* badge, const QColor& colour) {
        if (!badge)
            return;
        badge->setStyleSheet(QStringLiteral(
            "padding:2px 8px; border:1px solid %1; border-radius:5px; "
            "background:%2; color:%3; font-weight:600;")
            .arg(QStringLiteral("transparent"), m_theme->layer.name(), colour.name()));
    };
    if (m_readyBadge)
    {
        m_readyBadge->setText(T("%1 项可改", "%1 applicable").arg(ready));
            badgeStyle(m_readyBadge, ready > 0 ? m_theme->ok : m_theme->subtext);
    }
    if (m_conflictBadge)
    {
        m_conflictBadge->setText(T("%1 个冲突", "%1 conflicts").arg(errors));
        badgeStyle(m_conflictBadge, errors > 0 ? m_theme->danger : m_theme->subtext);
    }
    if (m_deleteBadge)
    {
        m_deleteBadge->setText(T("%1 项待删除", "%1 pending delete").arg(toDelete));
        badgeStyle(m_deleteBadge, toDelete > 0 ? m_theme->danger : m_theme->subtext);
    }

    QString hint;
    if (!done.isEmpty())
        hint = done.section(QLatin1Char('\n'), 0, 0);
    else if (!blocking.isEmpty())
        hint = blocking;
    else if (m_vim && m_vim->enabled())
    {
        // The mode itself shows in the status-bar chip; keep the footer for
        // key hints and leave the status-bar middle available for counts.
        hint = T("i 插入 · v 可视 · : 命令 · Ctrl+S 执行",
                 "i insert · v visual · : command · Ctrl+S run");
    }
    else if (ready > 0)
        hint = T("Enter 执行 · 可撤销", "Enter runs · undoable");
    else
        hint = T("? 查看帮助", "? for help");
    m_hint->setText(hint);
    m_hint->setToolTip(done.isEmpty() ? hint : done);
    m_hint->setStyleSheet(QStringLiteral("color:%1;")
                              .arg((blocking.isEmpty() ? m_theme->faint : m_theme->danger).name()));

}

void Window::ApplyAppearance()
{
    static const int iconFont = QFontDatabase::addApplicationFont(QStringLiteral(":/resources/icons/Phosphor.ttf"));
    const QStringList families = QFontDatabase::applicationFontFamilies(iconFont);
    if (!families.isEmpty())
    {
        QFont font(families.front());
        font.setPixelSize(18);
        font.setStyleStrategy(QFont::NoFontMerging);
        for (auto* button : findChildren<QAbstractButton*>())
        {
            if (!button->property("phGlyph").isValid()) continue;
            QPixmap image(36, 36);
            image.setDevicePixelRatio(2);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setFont(font);
            painter.setPen(m_theme->text);
            painter.drawText(QRect(0, 0, 18, 18), Qt::AlignCenter,
                             QString(QChar(button->property("phGlyph").toUInt())));
            painter.end();
            button->setIcon(QIcon(image));
            button->setIconSize(QSize(18, 18));
        }
    }
    if (!m_bridge || !m_theme)
        return;

    *m_theme = Theme::From(m_bridge->look(), m_bridge->dark(), m_bridge->uiSize(),
                           m_bridge->locale());
    QApplication::setFont(m_theme->font);
    m_theme->apply(this);

    setWindowTitle(T("批量重命名", "Batch Rename"));
    if (m_vimToggle)
    {
        // "VIM" and a single mode letter must both fit: size to the measured text
        // rather than a hard-coded width, which elided the uppercase label to "...".
        const int textWidth = m_vimToggle->fontMetrics().horizontalAdvance(QStringLiteral("VIM"));
        m_vimToggle->setFixedWidth(qMax(40, textWidth + 22));
    }
    m_table->verticalHeader()->setDefaultSectionSize(m_theme->rowHeight);
    m_table->setColumnWidth(0, 34);
    m_table->setColumnWidth(3, 78);
    QPalette tablePalette = m_table->palette();
    tablePalette.setColor(QPalette::Highlight, m_theme->selection);
    tablePalette.setColor(QPalette::HighlightedText, m_theme->text);
    m_table->setPalette(tablePalette);
    m_bridge->previewModel()->setBaseFont(m_theme->font);
    if (m_text)
    {
        QPalette editorPalette = m_text->palette();
        editorPalette.setColor(QPalette::Highlight, m_theme->textSelection);
        editorPalette.setColor(QPalette::HighlightedText, m_theme->text);
        m_text->setPalette(editorPalette);
        m_text->setStyleSheet(QStringLiteral(
            "QPlainTextEdit#renameTextEditor { background:%1; color:%2; "
            "border:0; selection-background-color:%3; selection-color:%4; }")
            .arg(m_theme->layer.name(), m_theme->text.name(),
                 m_theme->textSelection.name(QColor::HexArgb), m_theme->text.name()));
    }
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
    if (m_vim)
        m_vim->finishEdit();
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
    const int toDelete = m_bridge->deleteCount();

    QString text = T("将重命名 %1 项。", "Rename %1 item(s).").arg(ready);
    if (unchanged > 0)
        text += T("\n%1 项不变。", "\n%1 unchanged.").arg(unchanged);
    if (toDelete > 0)
        text += T("\n将删除 %1 项（移至回收站）。", "\nDelete %1 item(s) (move to Recycle Bin).").arg(toDelete);
    if (errors > 0)
        text += T("\n%1 项冲突或有问题，不会执行。", "\n%1 item(s) blocked and will not run.").arg(errors);
    text += T("\n\n确定执行吗？", "\n\nRun it now?");

    if (!Ask(text, toDelete > 0 ? T("执行", "Run") : T("重命名", "Rename")))
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
    box.setDefaultButton(qobject_cast<QPushButton*>(accept));
    box.setEscapeButton(cancel);
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
