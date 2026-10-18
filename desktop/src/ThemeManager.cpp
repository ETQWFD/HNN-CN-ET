#include "ThemeManager.h"
#include "Common.h"

#include <QFile>
#include <QPalette>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ThemeManager& ThemeManager::instance() {
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager() {
    load();
}

void ThemeManager::load() {
    QFile f(hnnet::settingsPath());
    if (f.open(QIODevice::ReadOnly)) {
        m_obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
}

QString ThemeManager::string(const QString& key, const QString& def) const {
    return m_obj.value(key).toString(def);
}

bool ThemeManager::boolean(const QString& key, bool def) const {
    return m_obj.value(key).toBool(def);
}

int ThemeManager::integer(const QString& key, int def) const {
    return m_obj.value(key).toInt(def);
}

void ThemeManager::set(const QString& key, const QJsonValue& val) {
    m_obj.insert(key, val);
}

void ThemeManager::save() {
    QFile f(hnnet::settingsPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_obj).toJson());
        f.close();
    }
}

bool ThemeManager::isDark() const {
    int mode = themeMode();
    if (mode == ThemeSystem) {
        return qApp->palette().color(QPalette::Window).lightness() < 128;
    }
    return mode == ThemeDark;
}

QColor ThemeManager::windowColor() const {
    return isDark() ? QColor(0x1B, 0x1B, 0x1F) : QColor(0xF6, 0xF6, 0xF8);
}

QColor ThemeManager::cardColor() const {
    return isDark() ? QColor(0x27, 0x27, 0x2C) : QColor(0xFF, 0xFF, 0xFF);
}

QColor ThemeManager::textColor() const {
    return isDark() ? QColor(0xE8, 0xE8, 0xEC) : QColor(0x20, 0x20, 0x24);
}

QColor ThemeManager::subTextColor() const {
    return isDark() ? QColor(0x99, 0x99, 0xA5) : QColor(0x6B, 0x6B, 0x74);
}

QColor ThemeManager::inputColor() const {
    return isDark() ? QColor(0x2D, 0x2D, 0x33) : QColor(0xFF, 0xFF, 0xFF);
}

QColor ThemeManager::userBubbleColor() const {
    return QColor(accentColor());
}

QColor ThemeManager::borderColor() const {
    return isDark() ? QColor(0x3A, 0x3A, 0x42) : QColor(0xE2, 0xE2, 0xE8);
}
