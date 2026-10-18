#include "Updater.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

Updater& Updater::instance() {
    static Updater updater;
    return updater;
}

void Updater::check(bool silent) {
    Q_UNUSED(silent)
    QNetworkRequest req(QUrl("https://api.github.com/repos/ETQWFD/HNN-CN-ET/releases/latest"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "HNN-CN-ET-Desktop");
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit noUpdate();
            reply->deleteLater();
            return;
        }
        QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        QString version = o.value("tag_name").toString();
        QString url = o.value("html_url").toString();
        if (version.isEmpty()) {
            emit noUpdate();
            return;
        }
        // Current desktop version is v1.0.0; mobile releases also appear here.
        emit updateAvailable(version, url);
    });
}
