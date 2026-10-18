#include "Updater.h"

#include <QCoreApplication>
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
    // HNN CN ET v1.0.1: list releases and pick the newest "desktop-*" tag so the
    // desktop app never mistakes the mobile releases (v1.0.x) for its own updates.
    QNetworkRequest req(QUrl("https://api.github.com/repos/ETQWFD/HNN-CN-ET/releases?per_page=10"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "HNN-CN-ET-Desktop");
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit noUpdate();
            reply->deleteLater();
            return;
        }
        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        reply->deleteLater();
        QString newestTag, newestUrl;
        for (const auto& v : arr) {
            QString tag = v.toObject().value("tag_name").toString();
            if (!tag.startsWith(QLatin1String("desktop-"))) continue;
            newestTag = tag;
            newestUrl = v.toObject().value("html_url").toString();
            break; // releases are sorted newest-first
        }
        if (newestTag.isEmpty()) { emit noUpdate(); return; }
        // Compare against the running desktop version (e.g. 1.0.1).
        QString cur = QCoreApplication::applicationVersion();
        QString ver = newestTag.mid(8); // strip "desktop-v"
        if (ver.isEmpty()) { emit noUpdate(); return; }
        bool newer = false;
        const QStringList a = cur.split('.');
        const QStringList b = ver.split('.');
        for (int i = 0; i < b.size(); ++i) {
            int av = (i < a.size()) ? a.at(i).toInt() : 0;
            int bv = b.at(i).toInt();
            if (bv > av) { newer = true; break; }
            if (bv < av) break;
        }
        if (newer) emit updateAvailable(newestTag, newestUrl);
        else emit noUpdate();
    });
}
