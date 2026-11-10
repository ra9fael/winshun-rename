/****************************************************************************
** Meta object code from reading C++ file 'bridge.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../ui/bridge.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'bridge.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10rename_qml12PreviewModelE_t {};
} // unnamed namespace

template <> constexpr inline auto rename_qml::PreviewModel::qt_create_metaobjectdata<qt_meta_tag_ZN10rename_qml12PreviewModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "rename_qml::PreviewModel",
        "selectionChanged",
        "",
        "id",
        "selected"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'selectionChanged'
        QtMocHelpers::SignalData<void(const QString &, bool)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Bool, 4 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PreviewModel, qt_meta_tag_ZN10rename_qml12PreviewModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject rename_qml::PreviewModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10rename_qml12PreviewModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10rename_qml12PreviewModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10rename_qml12PreviewModelE_t>.metaTypes,
    nullptr
} };

void rename_qml::PreviewModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PreviewModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->selectionChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PreviewModel::*)(const QString & , bool )>(_a, &PreviewModel::selectionChanged, 0))
            return;
    }
}

const QMetaObject *rename_qml::PreviewModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *rename_qml::PreviewModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10rename_qml12PreviewModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int rename_qml::PreviewModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void rename_qml::PreviewModel::selectionChanged(const QString & _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN10rename_qml6BridgeE_t {};
} // unnamed namespace

template <> constexpr inline auto rename_qml::Bridge::qt_create_metaobjectdata<qt_meta_tag_ZN10rename_qml6BridgeE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "rename_qml::Bridge",
        "changed",
        "",
        "countsChanged",
        "executed",
        "renamedCount",
        "journalPath",
        "appearanceChanged",
        "setSelection",
        "paths",
        "recursive",
        "switchMode",
        "mode",
        "setFilter",
        "filter",
        "toggleRow",
        "id",
        "selected",
        "selectAll",
        "setSingleRule",
        "scope",
        "match",
        "find",
        "replace",
        "enabled",
        "apply",
        "reset",
        "phase",
        "buffer",
        "blockingReason",
        "readyCount",
        "unchangedCount",
        "errorCount",
        "canExecute",
        "visibleCount",
        "doneLine",
        "previewModel",
        "QAbstractListModel*",
        "look",
        "QVariantMap",
        "locale",
        "dark",
        "uiSize"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'changed'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'countsChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'executed'
        QtMocHelpers::SignalData<void(int, QString)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'appearanceChanged'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setSelection'
        QtMocHelpers::MethodData<void(const QStringList &, bool)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 9 }, { QMetaType::Bool, 10 },
        }}),
        // Method 'switchMode'
        QtMocHelpers::MethodData<void(const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 },
        }}),
        // Method 'setFilter'
        QtMocHelpers::MethodData<void(const QString &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 14 },
        }}),
        // Method 'toggleRow'
        QtMocHelpers::MethodData<void(const QString &, bool)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 16 }, { QMetaType::Bool, 17 },
        }}),
        // Method 'selectAll'
        QtMocHelpers::MethodData<void(bool)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Method 'setSingleRule'
        QtMocHelpers::MethodData<void(int, int, const QString &, const QString &, bool)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 20 }, { QMetaType::Int, 21 }, { QMetaType::QString, 22 }, { QMetaType::QString, 23 },
            { QMetaType::Bool, 24 },
        }}),
        // Method 'apply'
        QtMocHelpers::MethodData<bool()>(25, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'reset'
        QtMocHelpers::MethodData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'mode'
        QtMocHelpers::PropertyData<QString>(12, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'phase'
        QtMocHelpers::PropertyData<QString>(27, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'buffer'
        QtMocHelpers::PropertyData<QString>(28, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'blockingReason'
        QtMocHelpers::PropertyData<QString>(29, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'readyCount'
        QtMocHelpers::PropertyData<int>(30, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'unchangedCount'
        QtMocHelpers::PropertyData<int>(31, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'errorCount'
        QtMocHelpers::PropertyData<int>(32, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'canExecute'
        QtMocHelpers::PropertyData<bool>(33, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'visibleCount'
        QtMocHelpers::PropertyData<int>(34, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'doneLine'
        QtMocHelpers::PropertyData<QString>(35, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'previewModel'
        QtMocHelpers::PropertyData<QAbstractListModel*>(36, 0x80000000 | 37, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'look'
        QtMocHelpers::PropertyData<QVariantMap>(38, 0x80000000 | 39, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 3),
        // property 'locale'
        QtMocHelpers::PropertyData<QString>(40, QMetaType::QString, QMC::DefaultPropertyFlags, 3),
        // property 'dark'
        QtMocHelpers::PropertyData<bool>(41, QMetaType::Bool, QMC::DefaultPropertyFlags, 3),
        // property 'uiSize'
        QtMocHelpers::PropertyData<QString>(42, QMetaType::QString, QMC::DefaultPropertyFlags, 3),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Bridge, qt_meta_tag_ZN10rename_qml6BridgeE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject rename_qml::Bridge::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10rename_qml6BridgeE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10rename_qml6BridgeE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10rename_qml6BridgeE_t>.metaTypes,
    nullptr
} };

void rename_qml::Bridge::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Bridge *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->changed(); break;
        case 1: _t->countsChanged(); break;
        case 2: _t->executed((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->appearanceChanged(); break;
        case 4: _t->setSelection((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->switchMode((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->setFilter((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->toggleRow((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 8: _t->selectAll((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->setSingleRule((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[5]))); break;
        case 10: { bool _r = _t->apply();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 11: _t->reset(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Bridge::*)()>(_a, &Bridge::changed, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Bridge::*)()>(_a, &Bridge::countsChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Bridge::*)(int , QString )>(_a, &Bridge::executed, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Bridge::*)()>(_a, &Bridge::appearanceChanged, 3))
            return;
    }
    if (_c == QMetaObject::RegisterPropertyMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 10:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAbstractListModel* >(); break;
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->mode(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->phase(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->buffer(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->blockingReason(); break;
        case 4: *reinterpret_cast<int*>(_v) = _t->readyCount(); break;
        case 5: *reinterpret_cast<int*>(_v) = _t->unchangedCount(); break;
        case 6: *reinterpret_cast<int*>(_v) = _t->errorCount(); break;
        case 7: *reinterpret_cast<bool*>(_v) = _t->canExecute(); break;
        case 8: *reinterpret_cast<int*>(_v) = _t->visibleCount(); break;
        case 9: *reinterpret_cast<QString*>(_v) = _t->doneLine(); break;
        case 10: *reinterpret_cast<QAbstractListModel**>(_v) = _t->previewModel(); break;
        case 11: *reinterpret_cast<QVariantMap*>(_v) = _t->look(); break;
        case 12: *reinterpret_cast<QString*>(_v) = _t->locale(); break;
        case 13: *reinterpret_cast<bool*>(_v) = _t->dark(); break;
        case 14: *reinterpret_cast<QString*>(_v) = _t->uiSize(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 2: _t->setBuffer(*reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *rename_qml::Bridge::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *rename_qml::Bridge::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10rename_qml6BridgeE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int rename_qml::Bridge::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 12;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void rename_qml::Bridge::changed()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void rename_qml::Bridge::countsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void rename_qml::Bridge::executed(int _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void rename_qml::Bridge::appearanceChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
