// Main.qml — the rename window, laid out like PowerRename.
//
// Left column is the rule: find, three switches, replace, "apply to", text
// formatting, and the two time/metadata sources. Right side is the table: the
// original name and the name it will become, one row per item. That split is the
// whole reason this layout is worth copying — the rule is written once on the
// left, and what it does is visible on the right as you type.
//
// Every value comes from `bridge`; this file renders it and forwards input, so a
// QML change can never alter what a rename does.
//
// Look: Win顺's fluent tokens from Tokens.qml (the host's `appearance.nativeLook`
// resolved). QtQuick.Controls.Basic draws nothing of its own, so each control is
// styled here: flat surfaces, one accent for the primary action, and a status
// always spelled out in words beside its colour.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 822
    height: 616
    minimumWidth: 700
    minimumHeight: 520
    visible: true
    color: Theme.background
    title: root.t("重命名", "Rename")
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint

    readonly property bool zh: bridge.locale.indexOf("zh") === 0
    function t(zhText, enText) { return root.zh ? zhText : enText }

    // ---- shared controls --------------------------------------------------
    component Field: TextField {
        id: control
        property bool invalid: false
        implicitHeight: 32
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.text
        placeholderTextColor: Theme.faint
        leftPadding: 9
        rightPadding: 9
        selectByMouse: true
        selectionColor: Theme.textSelection
        background: Rectangle {
            radius: Theme.controlRadius
            color: control.activeFocus ? Theme.inputFocused : Theme.control
            border.width: 1
            border.color: control.invalid ? Theme.danger
                        : (control.activeFocus ? Theme.accent : Theme.controlBorder)
        }
    }

    // The small round "i" beside a field: a tooltip anchor, not a button.
    component Hint: Item {
        id: hint
        property string text: ""
        implicitWidth: 21
        implicitHeight: 21
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: hintHover.hovered ? Theme.subtext : Theme.controlBorder
        }
        Text {
            anchors.centerIn: parent
            text: "i"
            color: hintHover.hovered ? Theme.text : Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
        HoverHandler { id: hintHover }
        ToolTip.visible: hintHover.hovered && hint.text.length > 0
        ToolTip.text: hint.text
        ToolTip.delay: 400
    }

    component Switch2: CheckBox {
        id: control
        implicitHeight: 20
        spacing: 9
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        contentItem: Text {
            text: control.text
            font: control.font
            color: Theme.text
            leftPadding: control.indicator.width + control.spacing
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Rectangle {
            id: box
            x: 0
            y: (control.height - height) / 2
            implicitWidth: 16
            implicitHeight: 16
            radius: Theme.smallControlRadius
            color: control.checked ? Theme.accentFill : "transparent"
            border.width: 1
            border.color: control.checked ? Theme.accentFill
                                          : (control.hovered ? Theme.text : Theme.faint)
            Text {
                anchors.centerIn: parent
                visible: control.checked
                text: "\u2713"
                color: Theme.accentText
                font.pixelSize: 11
            }
        }
    }

    component Dropdown: ComboBox {
        id: control
        implicitHeight: 32
        implicitWidth: 150
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        leftPadding: 9
        rightPadding: 26
        contentItem: Text {
            text: control.displayText
            font: control.font
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - 9
            y: (control.height - height) / 2
            text: "\u2304"
            color: Theme.subtext
            font.pixelSize: 12
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: control.pressed || control.popup.visible ? Theme.pressed
                                                          : (control.hovered ? Theme.controlHover : Theme.control)
            border.width: 1
            border.color: control.activeFocus || control.popup.visible ? Theme.accent : Theme.controlBorder
        }
        delegate: ItemDelegate {
            id: item
            width: control.width
            implicitHeight: 30
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                leftPadding: 9
                text: item.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.text
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.smallControlRadius
                color: item.highlighted ? Theme.menuHover : "transparent"
            }
        }
        popup: Popup {
            y: control.height + 2
            width: control.width
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4
            background: Rectangle {
                radius: Theme.menuRadius
                color: Theme.menuBackground
                border.width: 1
                border.color: Theme.menuEdge
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
            }
        }
    }

    // PowerRename's "Apply to" trio: square buttons beside the dropdown, the
    // chosen one filled with the accent.
    component ScopeButton: Rectangle {
        id: sg
        property string glyph: ""
        property string tip: ""
        property bool chosen: false
        property int scopeIndex: 0
        implicitWidth: 32
        implicitHeight: 32
        radius: Theme.controlRadius
        color: sg.chosen ? Theme.accentFill
                         : (sgHover.hovered ? Theme.controlHover : Theme.control)
        border.width: sg.chosen ? 0 : 1
        border.color: Theme.controlBorder
        Text {
            anchors.centerIn: parent
            text: sg.glyph
            font.pixelSize: 13
            color: sg.chosen ? Theme.accentText : Theme.subtext
        }
        HoverHandler { id: sgHover }
        ToolTip.visible: sgHover.hovered && sg.tip.length > 0
        ToolTip.text: sg.tip
        ToolTip.delay: 400
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: bridge.setScope(sg.scopeIndex)
        }
    }

    // A text-formatting chip ("aa", "AA", "Aa", "Aa Aa"): a case transform over
    // the whole batch. `caseFlag` is what the bridge maps to a core flag.
    component FormatButton: Rectangle {
        id: fb
        property string glyph: ""
        property string tip: ""
        property bool chosen: false
        property int caseFlag: 0
        implicitWidth: 32
        implicitHeight: 32
        radius: Theme.controlRadius
        color: fb.chosen ? Theme.accentFill : (fbHover.hovered ? Theme.controlHover : Theme.control)
        border.width: fb.chosen ? 0 : 1
        border.color: Theme.controlBorder
        Text {
            anchors.centerIn: parent
            text: fb.glyph
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: fb.chosen ? Theme.accentText : Theme.text
        }
        HoverHandler { id: fbHover }
        ToolTip.visible: fbHover.hovered && fb.tip.length > 0
        ToolTip.text: fb.tip
        ToolTip.delay: 400
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: bridge.setCaseTransform(fb.caseFlag)
        }
    }

    component RoundIcon: Rectangle {
        id: ri
        property string glyph: ""
        property string tip: ""
        implicitWidth: 28
        implicitHeight: 28
        radius: width / 2
        color: riHover.hovered ? Theme.hover : "transparent"
        Text {
            anchors.centerIn: parent
            text: ri.glyph
            font.pixelSize: 14
            color: Theme.subtext
        }
        HoverHandler { id: riHover }
        ToolTip.visible: riHover.hovered && ri.tip.length > 0
        ToolTip.text: ri.tip
        ToolTip.delay: 400
    }

    // ---- status words -----------------------------------------------------
    function isError(status) {
        return status === "exists" || status === "too-long" ||
               status === "invalid-character" || status === "empty-name" ||
               status === "bad-rule"
    }

    function statusWord(status) {
        switch (status) {
        case "ready": return root.t("待改", "ready")
        case "unchanged": return root.t("不变", "unchanged")
        case "exists": return root.t("已存在", "exists")
        case "too-long": return root.t("过长", "too long")
        case "invalid-character": return root.t("非法字符", "invalid char")
        case "empty-name": return root.t("空名字", "empty name")
        case "bad-rule": return root.t("规则错误", "bad rule")
        default: return status
        }
    }

    // ---- body -------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ============== title strip ========================================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: Theme.layer

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                spacing: 8

                Text { text: "\u270E"; color: Theme.subtext; font.pixelSize: 13 }
                Text {
                    text: root.t("重命名", "Rename")
                    color: Theme.subtext
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }

                // The two ways to describe a change.
                Rectangle {
                    implicitWidth: modeRuleText.implicitWidth + 20
                    implicitHeight: 24
                    radius: Theme.controlRadius
                    color: bridge.mode !== "text" ? Theme.accentFill : "transparent"
                    Text {
                        id: modeRuleText
                        anchors.centerIn: parent
                        text: root.t("规则", "Rules")
                        color: bridge.mode !== "text" ? Theme.accentText : Theme.subtext
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: bridge.mode !== "text" ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bridge.switchMode("control")
                    }
                }
                Rectangle {
                    Layout.rightMargin: 4
                    implicitWidth: modeTextText.implicitWidth + 20
                    implicitHeight: 24
                    radius: Theme.controlRadius
                    color: bridge.mode === "text" ? Theme.accentFill : "transparent"
                    Text {
                        id: modeTextText
                        anchors.centerIn: parent
                        text: root.t("文本", "Text")
                        color: bridge.mode === "text" ? Theme.accentText : Theme.subtext
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: bridge.mode === "text" ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bridge.switchMode("text")
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }

        // ============== panes ==============================================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // -------- left: the rule ---------------------------------------
            Rectangle {
                Layout.preferredWidth: 348
                Layout.fillHeight: true
                color: Theme.background

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 14
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Field {
                            id: findField
                            Layout.fillWidth: true
                            placeholderText: root.t("查找", "Find")
                            text: bridge.ruleFind
                            invalid: bridge.ruleRegexInvalid
                            onEditingFinished: root.pushRule()
                        }
                        Hint {
                            text: root.t("要在名字里找的文字；打开“使用正则表达式”后按正则解释",
                                         "Text to look for; with regular expressions it is a pattern")
                        }
                    }

                    ColumnLayout {
                        Layout.topMargin: 9
                        spacing: 8
                        Switch2 {
                            id: regexSwitch
                            text: root.t("使用正则表达式", "Use regular expressions")
                            checked: bridge.ruleMatch === 1
                            onToggled: root.pushRule()
                        }
                        Switch2 {
                            id: matchAllSwitch
                            text: root.t("替换全部匹配", "Match all occurrences")
                            checked: bridge.ruleMatchAll
                            onToggled: root.pushRule()
                        }
                        Switch2 {
                            id: sensitiveSwitch
                            text: root.t("区分大小写", "Case sensitive")
                            checked: bridge.ruleCaseSensitive
                            onToggled: root.pushRule()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        implicitHeight: 1
                        color: Theme.divider
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        spacing: 6
                        Field {
                            id: replaceField
                            Layout.fillWidth: true
                            placeholderText: root.t("替换为", "Replace with")
                            text: bridge.ruleReplace
                            onEditingFinished: root.pushRule()
                        }
                        Hint {
                            text: root.t("新的文字；$1 这样的分组引用需要“使用正则表达式”",
                                         "The new text; $1 style groups need regular expressions")
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        spacing: 6
                        Text {
                            text: root.t("应用于", "Apply to")
                            color: Theme.subtext
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Dropdown {
                                id: scopeBox
                                Layout.fillWidth: true
                                model: [root.t("文件名", "Name"),
                                        root.t("仅扩展名", "Extension only"),
                                        root.t("完整名称", "Full name")]
                                currentIndex: bridge.ruleScope
                                onActivated: bridge.setScope(currentIndex)
                            }
                            ScopeButton {
                                glyph: "\uE8A5"
                                tip: root.t("文件名", "Name")
                                scopeIndex: 0
                                chosen: bridge.ruleScope === 0
                            }
                            ScopeButton {
                                glyph: "\uE8B7"
                                tip: root.t("仅扩展名", "Extension only")
                                scopeIndex: 1
                                chosen: bridge.ruleScope === 1
                            }
                            ScopeButton {
                                glyph: "\uE8B9"
                                tip: root.t("完整名称", "Full name")
                                scopeIndex: 2
                                chosen: bridge.ruleScope === 2
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        spacing: 6
                        Text {
                            text: root.t("文本格式", "Text formatting")
                            color: Theme.subtext
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                        RowLayout {
                            spacing: 6
                            FormatButton {
                                glyph: "aa"
                                tip: root.t("全部小写", "lowercase")
                                caseFlag: 1
                                chosen: bridge.caseTransform === 1
                            }
                            FormatButton {
                                glyph: "AA"
                                tip: root.t("全部大写", "UPPERCASE")
                                caseFlag: 2
                                chosen: bridge.caseTransform === 2
                            }
                            FormatButton {
                                glyph: "Aa"
                                tip: root.t("首字母大写", "Capitalize")
                                caseFlag: 3
                                chosen: bridge.caseTransform === 3
                            }
                            FormatButton {
                                glyph: "Aa Aa"
                                tip: root.t("每词首字母大写", "Title Case")
                                caseFlag: 4
                                chosen: bridge.caseTransform === 4
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: root.t("时间模板基准", "Time used for replacement")
                                color: Theme.subtext
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Dropdown {
                                Layout.fillWidth: true
                                model: [root.t("创建时间", "Creation time"),
                                        root.t("修改时间", "Modified time"),
                                        root.t("访问时间", "Accessed time")]
                                currentIndex: 0
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: root.t("元数据来源", "Metadata source")
                                color: Theme.subtext
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                            }
                            Dropdown {
                                Layout.fillWidth: true
                                model: ["EXIF", "XMP"]
                                currentIndex: 0
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 13
                        spacing: 8
                        RoundIcon { glyph: "\uE713"; tip: root.t("设置", "Settings") }
                        RoundIcon { glyph: "?"; tip: root.t("帮助", "Help") }
                        Item { Layout.fillWidth: true }

                        // The one verb. Hidden after a finished batch: there is
                        // nothing left to run, and a disabled "Rename 0" beside
                        // "Renamed 3" reads as a failure.
                        Rectangle {
                            visible: bridge.doneLine.length === 0
                            implicitWidth: applyLabel.implicitWidth + 30
                            implicitHeight: 33
                            radius: Theme.controlRadius
                            color: !bridge.canExecute ? Theme.control
                                 : (applyHover.hovered ? Qt.lighter(Theme.accentFill, 1.08) : Theme.accentFill)
                            border.width: bridge.canExecute ? 0 : 1
                            border.color: Theme.controlBorder
                            Text {
                                id: applyLabel
                                anchors.centerIn: parent
                                text: "\u270E  " + root.t("重命名 " + bridge.readyCount + " 项",
                                                           "Rename " + bridge.readyCount)
                                color: bridge.canExecute ? Theme.accentText : Theme.faint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.weight: bridge.canExecute ? Font.DemiBold : Font.Normal
                            }
                            HoverHandler { id: applyHover }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: bridge.canExecute ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: if (bridge.canExecute) bridge.apply()
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.divider }

            // -------- right: the table -------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 33
                    color: Theme.background

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 13
                        anchors.rightMargin: 12
                        spacing: 0

                        Switch2 {
                            implicitWidth: 24
                            checked: bridge.allSelected
                            onToggled: bridge.selectAll(checked)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.t("原始 (" + bridge.totalCount + ")",
                                         "Original (" + bridge.totalCount + ")")
                            color: Theme.subtext
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            leftPadding: 4
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.t("重命名后 (" + bridge.readyCount + ")",
                                         "Renamed (" + bridge.readyCount + ")")
                            color: Theme.subtext
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Text { text: "\u23F7"; color: Theme.faint; font.pixelSize: 12 }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.divider
                    }
                }

                ListView {
                    id: table
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: bridge.previewModel
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: rowItem
                        required property int index
                        required property string rowId
                        required property string originalName
                        required property string targetName
                        required property string status
                        required property bool selected
                        required property bool isFolder
                        required property int depth
                        required property string note

                        readonly property bool same: status === "unchanged"
                        readonly property bool blocked: root.isError(status)

                        width: table.width
                        implicitHeight: 34
                        color: rowHover.hovered ? Theme.hover : "transparent"

                        HoverHandler { id: rowHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 13
                            anchors.rightMargin: 12
                            spacing: 0

                            Switch2 {
                                implicitWidth: 24
                                checked: rowItem.selected
                                onToggled: bridge.toggleRow(rowItem.rowId, checked)
                            }
                            Text {
                                Layout.preferredWidth: 18
                                text: rowItem.isFolder ? "\uE8B7" : "\uE8A5"
                                font.family: "Segoe Fluent Icons"
                                font.pixelSize: 12
                                color: Theme.subtext
                            }
                            Text {
                                Layout.fillWidth: true
                                text: rowItem.originalName
                                color: rowItem.blocked ? Theme.faint
                                     : (rowItem.same ? Theme.subtext : Theme.text)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideMiddle
                                rightPadding: 8
                            }
                            Text {
                                Layout.fillWidth: true
                                // A blocked row says why in place of a name: the
                                // red words are the whole point of that row.
                                text: rowItem.blocked
                                      ? (rowItem.status === "exists"
                                         ? root.t("目标已存在：" + rowItem.targetName,
                                                  "target exists: " + rowItem.targetName)
                                         : root.statusWord(rowItem.status))
                                      : rowItem.targetName
                                color: rowItem.blocked ? Theme.danger
                                     : (rowItem.same ? Theme.faint : Theme.text)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.weight: (!rowItem.blocked && !rowItem.same) ? Font.DemiBold : Font.Normal
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    // Empty state: a finished batch is not the same as no files.
                    Text {
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, table.width - 88)
                        visible: table.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: bridge.doneLine.length > 0
                              ? bridge.doneLine + "\n"
                                + root.t("可在资源管理器里按 Ctrl+Z 撤消", "Undo with Ctrl+Z in Explorer")
                              : root.t("这里会列出要改名的文件。\n从文件夹标签打开，或选中文件后右键“重命名…”。",
                                       "The files to rename appear here.\nOpen it from a folder tab, or right-click selected files.")
                        color: bridge.doneLine.length > 0 ? Theme.ok : Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: bridge.doneLine.length > 0 ? 15 : 13
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: Theme.layer

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.divider
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 12

                        Rectangle {
                            implicitWidth: 7
                            implicitHeight: 7
                            radius: 4
                            color: bridge.doneLine.length > 0 ? Theme.ok
                                 : (bridge.errorCount > 0 ? Theme.caution
                                    : (bridge.readyCount > 0 ? Theme.ok : Theme.faint))
                        }
                        Text {
                            text: {
                                if (bridge.doneLine.length > 0)
                                    return bridge.doneLine
                                let line = root.t(bridge.readyCount + " 项会改 · "
                                                  + bridge.unchangedCount + " 项不变",
                                                  bridge.readyCount + " to rename · "
                                                  + bridge.unchangedCount + " unchanged")
                                if (bridge.errorCount > 0)
                                    line += root.t(" · " + bridge.errorCount + " 项冲突或有问题",
                                                   " · " + bridge.errorCount + " blocked")
                                else if (bridge.readyCount > 0)
                                    line += root.t(" · 无冲突", " · no conflicts")
                                return line
                            }
                            color: Theme.subtext
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: bridge.blockingReason.length > 0
                                  ? bridge.blockingReason
                                  : (bridge.readyCount > 0
                                     ? root.t("Enter 执行 · 可在资源管理器撤消",
                                              "Enter runs it · undoable in Explorer")
                                     : root.t("在上方输入查找与替换",
                                              "Type a find and a replace above"))
                            color: bridge.blockingReason.length > 0 ? Theme.danger : Theme.faint
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // ---- input forwarded to the bridge ------------------------------------
    function pushRule() {
        bridge.setRule(bridge.ruleScope,
                       regexSwitch.checked ? 1 : 0,
                       findField.text,
                       replaceField.text,
                       bridge.ruleEnabled,
                       matchAllSwitch.checked,
                       sensitiveSwitch.checked)
    }

    // Enter runs the batch, as PowerRename's Enter does; Escape closes. The two
    // text fields keep Enter for themselves (they commit the rule first), which is
    // why this is on the window and not on the button only.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: !findField.activeFocus && !replaceField.activeFocus && bridge.canExecute
        onActivated: bridge.apply()
    }
    Shortcut {
        sequence: "Esc"
        onActivated: root.close()
    }
}
