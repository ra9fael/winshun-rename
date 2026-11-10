// Main.qml — the window both modes live in (UI-SPEC.md §1-§3).
//
// Every piece of state comes from `bridge`; this file only renders it and
// forwards input, so a QML change can never alter what a rename does.
//
// The look follows Win顺 rather than inventing a theme: every colour and radius
// is read from Theme.qml, which resolves the host's `appearance.nativeLook`.
// QtQuick.Controls.Basic draws nothing of its own, so the controls are styled
// here to read as Win顺's: flat rows, the accent only for the one action that
// changes files, and a status that is always spelled out in words beside its
// colour (UI-SPEC.md §4.3).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 980
    height: 680
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    color: Theme.background
    title: root.zh ? "重命名" : "Rename"
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint

    readonly property bool zh: bridge.locale.indexOf("zh") === 0

    function t(zhText, enText) { return root.zh ? zhText : enText }

    // ---- shared widgets ---------------------------------------------------
    component FlatButton: Button {
        id: control
        property bool primary: false
        implicitHeight: Theme.controlHeight
        implicitWidth: Math.max(72, label.implicitWidth + 24)
        padding: 0
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        contentItem: Text {
            id: label
            text: control.text
            font: control.font
            color: control.primary ? Theme.accentText
                                   : (control.hovered ? Theme.text : Theme.subtext)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: control.primary ? Theme.controlRadius : Theme.controlRadius
            color: {
                if (!control.enabled)
                    return "transparent"
                if (control.primary)
                    return control.pressed ? Qt.darker(Theme.accentFill, 1.15) : Theme.accentFill
                return control.pressed ? Theme.pressed : (control.hovered ? Theme.hover : "transparent")
            }
            border.width: control.primary || !control.enabled ? 0 : 1
            border.color: control.enabled ? Theme.controlBorder : Theme.divider
            opacity: control.enabled ? 1.0 : 0.55
        }
    }

    component FlatCheckBox: CheckBox {
        id: control
        implicitHeight: Theme.rowHeight
        implicitWidth: 16
        indicator: Rectangle {
            id: box
            implicitWidth: 16
            implicitHeight: 16
            anchors.verticalCenter: parent.verticalCenter
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
        contentItem: Item {
            implicitWidth: 0
            implicitHeight: 0
        }
    }

    component FlatField: TextField {
        id: control
        implicitHeight: Theme.controlHeight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.text
        placeholderTextColor: Theme.faint
        leftPadding: 8
        rightPadding: 8
        background: Rectangle {
            radius: Theme.controlRadius
            color: control.activeFocus ? Theme.inputFocused : Theme.control
            border.width: 1
            border.color: control.activeFocus ? Theme.accent : Theme.controlBorder
        }
    }

    component FlatCombo: ComboBox {
        id: control
        implicitHeight: Theme.controlHeight
        implicitWidth: Math.max(96, contentLabel.implicitWidth + 32)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        leftPadding: 8
        rightPadding: 8
        contentItem: Text {
            id: contentLabel
            text: control.displayText
            font: control.font
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - 8
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
            implicitHeight: Theme.rowHeight - 8
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                leftPadding: 8
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

    // ---- status words -----------------------------------------------------
    function isError(status) {
        return status === "exists" || status === "too-long" ||
               status === "invalid-character" || status === "empty-name" ||
               status === "bad-rule"
    }
    function statusWord(status) {
        switch (status) {
        case "ready": return root.t("\u5f85\u6539", "ready")
        case "unchanged": return root.t("\u4e0d\u53d8", "unchanged")
        case "exists": return root.t("\u5df2\u5b58\u5728", "exists")
        case "too-long": return root.t("\u8fc7\u957f", "too long")
        case "invalid-character": return root.t("\u975e\u6cd5\u5b57\u7b26", "invalid character")
        case "empty-name": return root.t("\u7a7a\u540d\u5b57", "empty name")
        case "bad-rule": return root.t("\u89c4\u5219\u9519\u8bef", "bad rule")
        default: return status
        }
    }
    // The presenter's notes are stable identifiers ("trimmed nbsp"); map each to
    // the user's language here, so the C++ side never learns any wording.
    function noteText(note) {
        if (!note)
            return ""
        const parts = []
        const words = note.split(" ")
        for (let i = 0; i < words.length; ++i) {
            switch (words[i]) {
            case "no-extension": parts.push(root.t("\u65e0\u6269\u5c55\u540d\uff0c\u6269\u5c55\u540d\u89c4\u5219\u672a\u5e94\u7528", "no extension; extension rule not applied")); break
            case "trimmed": parts.push(root.t("\u5df2\u53bb\u9664\u5c3e\u90e8\u7a7a\u683c\u6216\u70b9", "trailing whitespace or dot removed")); break
            case "nbsp": parts.push(root.t("\u4e0d\u95f4\u65ad\u7a7a\u683c\u6309\u666e\u901a\u7a7a\u683c\u5904\u7406", "non-breaking space treated as a space")); break
            default: parts.push(words[i])
            }
        }
        return parts.join(root.t("\uff1b", "; "))
    }

    // ---- layout -----------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // header: what this window is, and the mode switch
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.headerHeight
            color: Theme.layer

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Text {
                    text: root.t("\u91cd\u547d\u540d", "Rename")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }

                FlatButton {
                    text: root.t("\u89c4\u5219", "Rules")
                    primary: bridge.mode !== "text"
                    onClicked: bridge.switchMode("control")
                }
                FlatButton {
                    text: root.t("\u6587\u672c", "Text")
                    primary: bridge.mode === "text"
                    onClicked: bridge.switchMode("text")
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }

        // ---- control mode: one rule row ----------------------------------
        // One row covers the common case; the chain editor is the next
        // milestone, so this says only what actually works today.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.headerHeight
            visible: bridge.mode !== "text"
            color: Theme.background

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                FlatCheckBox {
                    id: ruleEnabled
                    checked: true
                    onToggled: pushRule()
                }
                FlatCombo {
                    id: ruleScope
                    model: [root.t("\u6587\u4ef6\u540d", "Name"),
                            root.t("\u6269\u5c55\u540d", "Extension"),
                            root.t("\u5168\u540d", "Full name")]
                    currentIndex: 0
                    onCurrentIndexChanged: pushRule()
                }
                FlatCombo {
                    id: ruleMatch
                    model: [root.t("\u6587\u672c", "Literal"), root.t("\u6b63\u5219", "Regex")]
                    currentIndex: 0
                    onCurrentIndexChanged: pushRule()
                }
                FlatField {
                    id: ruleFind
                    Layout.fillWidth: true
                    placeholderText: root.t("\u67e5\u627e", "Find")
                    onEditingFinished: pushRule()
                }
                Text {
                    text: "\u2192"
                    color: Theme.faint
                    font.pixelSize: Theme.fontBody
                }
                FlatField {
                    id: ruleReplace
                    Layout.fillWidth: true
                    placeholderText: root.t("\u66ff\u6362\u4e3a", "Replace with")
                    onEditingFinished: pushRule()
                }
            }
        }

        // ---- text mode: the oil buffer -----------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: bridge.mode === "text"
            color: Theme.background

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 6

                Text {
                    text: root.t("一行一个名字；行首的 /<id> 指向文件本身，不要改",
                                 "One name per line; the /<id> prefix anchors the line to a file. Do not edit it.")
                    color: Theme.subtext
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.controlRadius
                    color: Theme.control
                    border.width: 1
                    border.color: oilEditor.activeFocus ? Theme.accent : Theme.controlBorder

                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.VerticalFlick
                        contentWidth: oilEditor.implicitWidth
                        contentHeight: oilEditor.implicitHeight
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        TextArea {
                            id: oilEditor
                            width: parent.width
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontBody
                            color: Theme.text
                            wrapMode: TextEdit.NoWrap
                            selectByMouse: true
                            selectionColor: Theme.textSelection
                            background: null
                            text: bridge.buffer
                            // Only a user edit is pushed back: assigning `text`
                            // fires this too, and echoing it would move the caret.
                            onTextChanged: {
                                if (text !== bridge.buffer)
                                    bridge.buffer = text
                            }
                        }
                    }
                }
            }
        }

        // ---- preview table -----------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    spacing: 8

                    FlatCombo {
                        id: filterBox
                        model: [root.t("\u5168\u90e8", "All"),
                                root.t("\u5df2\u9009", "Selected"),
                                root.t("\u53ef\u6539", "Applicable"),
                                root.t("\u4f1a\u6539", "To rename")]
                        currentIndex: 0
                        onCurrentIndexChanged: {
                            const names = ["all", "selected", "applicable", "to-rename"]
                            bridge.setFilter(names[Math.max(0, Math.min(3, currentIndex))])
                        }
                    }

                    Item { Layout.fillWidth: true }

                    FlatButton {
                        text: root.t("\u5168\u9009", "Select all")
                        enabled: bridge.visibleCount > 0
                        onClicked: bridge.selectAll(true)
                    }
                    FlatButton {
                        text: root.t("\u5168\u4e0d\u9009", "Select none")
                        enabled: bridge.visibleCount > 0
                        onClicked: bridge.selectAll(false)
                    }
                }

                ListView {
                    id: preview
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

                        width: preview.width
                        implicitHeight: Theme.rowHeight
                        color: rowHover.hovered ? Theme.hover : "transparent"

                        HoverHandler { id: rowHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16 + rowItem.depth * 16
                            anchors.rightMargin: 16
                            spacing: 8

                            FlatCheckBox {
                                checked: rowItem.selected
                                onToggled: bridge.toggleRow(rowItem.rowId, checked)
                            }
                            Text {
                                text: rowItem.isFolder ? "\uE8B7" : "\uE8A5"
                                font.family: "Segoe Fluent Icons"
                                font.pixelSize: Theme.fontBody
                                color: Theme.faint
                            }
                            Text {
                                Layout.preferredWidth: Math.max(160, (rowItem.width - 260) / 2)
                                text: rowItem.originalName
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: "\u2192"
                                color: Theme.faint
                                font.pixelSize: Theme.fontBody
                            }
                            Text {
                                Layout.fillWidth: true
                                text: rowItem.targetName
                                color: rowItem.status === "ready" ? Theme.text : Theme.subtext
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideMiddle
                            }
                            Text {
                                // The word, then the colour: never colour alone.
                                text: root.statusWord(rowItem.status)
                                      + (rowItem.note ? "  \u00b7  " + root.noteText(rowItem.note) : "")
                                color: root.isError(rowItem.status) ? Theme.danger
                                     : (rowItem.status === "ready" ? Theme.ok : Theme.faint)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                                Layout.maximumWidth: 260
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: Theme.divider
                        }
                    }

                    // The table says why it is empty: a finished batch is not
                    // the same as never having files.
                    Text {
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, preview.width - 48)
                        visible: preview.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: bridge.doneLine.length > 0 ? bridge.doneLine
                              : root.t("没有文件。选中文件后，从结果菜单进入“重命名…”。",
                                       "No files. Select files, then pick “Rename…” from the result menu.")
                        color: bridge.doneLine.length > 0 ? Theme.ok : Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }

        // ---- action bar ---------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.footerHeight + 12
            color: Theme.layer

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Text {
                    text: root.t(bridge.readyCount + " 项会改 \u00b7 " + bridge.unchangedCount
                                 + " 项不变 \u00b7 " + bridge.errorCount + " 项有问题",
                                 bridge.readyCount + " to rename \u00b7 " + bridge.unchangedCount
                                 + " unchanged \u00b7 " + bridge.errorCount + " with problems")
                    color: Theme.subtext
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    Layout.fillWidth: true
                    text: bridge.blockingReason
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                    visible: text.length > 0
                }

                FlatButton {
                    text: root.t("\u53d6\u6d88", "Cancel")
                    onClicked: root.close()
                }
                FlatButton {
                    primary: true
                    enabled: bridge.canExecute
                    text: root.t("\u91cd\u547d\u540d " + bridge.readyCount + " \u9879",
                                 "Rename " + bridge.readyCount)
                    onClicked: bridge.apply()
                }
            }
        }
    }

    // The rule row forwards its four controls as one rule.
    function pushRule() {
        bridge.setSingleRule(ruleScope.currentIndex,
                             ruleMatch.currentIndex,
                             ruleFind.text,
                             ruleReplace.text,
                             ruleEnabled.checked)
    }
}
