#pragma once

#include <QObject>
#include <QNetworkAccessManager>

// Checks GitHub releases for updates.
class Updater : public QObject {
    Q_OBJECT
public:
    static Updater& instance();

    void check(bool silent);

signals:
    void updateAvailable(const QString& version, const QString& url);
    void noUpdate();

private:
    Updater() = default;
    QNetworkAccessManager m_nam;
};
