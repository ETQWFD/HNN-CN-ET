#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QMap>

// Persists app settings in settings.json and applies them to the app.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum ThemeMode { ThemeLight = 0, ThemeDark = 1, ThemeSystem = 2 };

    static ThemeManager& instance();

    QJsonObject all() const { return m_obj; }
    QString string(const QString& key, const QString& def = QString()) const;
    bool boolean(const QString& key, bool def = false) const;
    int integer(const QString& key, int def = 0) const;
    void set(const QString& key, const QJsonValue& val);
    void save();

    int themeMode() const { return integer("themeMode", ThemeSystem); }
    QString accentColor() const { return string("accentColor", "#0078D4"); }
    QString backgroundImage() const { return string("backgroundImage"); }
    int backgroundOpacity() const { return integer("backgroundOpacity", 50); }
    void clearBackground() { set("backgroundImage", QString()); save(); }

    // Computed palette entry points
    QColor windowColor() const;
    QColor cardColor() const;
    QColor textColor() const;
    QColor subTextColor() const;
    QColor inputColor() const;
    QColor userBubbleColor() const;
    QColor borderColor() const;
    bool isDark() const;

signals:
    void themeChanged();

private:
    ThemeManager();
    void load();
    QJsonObject m_obj;
};
