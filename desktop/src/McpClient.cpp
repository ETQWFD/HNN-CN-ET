#include "McpClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include "ThemeManager.h"

McpClient& McpClient::instance() {
    static McpClient client;
    return client;
}

void McpClient::loadServers() {
    m_servers.clear();
    QJsonArray arr = QJsonDocument::fromJson(
        ThemeManager::instance().string("mcpServersJson").toUtf8()).array();
    for (const auto& v : arr) {
        QJsonObject o = v.toObject();
        m_servers.append({o.value("name").toString(), o.value("url").toString()});
    }
}

void McpClient::saveServers(const QList<Server>& servers) {
    QJsonArray arr;
    for (const auto& s : servers) {
        arr.append(QJsonObject{{"name", s.name}, {"url", s.url}});
    }
    ThemeManager::instance().set("mcpServersJson", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact).trimmed()));
    ThemeManager::instance().save();
    m_servers = servers;
}

void McpClient::addServer(const Server& s) {
    m_servers.append(s);
    saveServers(m_servers);
}

static QJsonObject rpc(const QString& urlStr, const QString& method, const QJsonObject* params, int timeoutMs, QString* errOut) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(urlStr)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(timeoutMs);
    QJsonObject body;
    body.insert("jsonrpc", "2.0");
    body.insert("id", qint64(QDateTime::currentMSecsSinceEpoch()));
    body.insert("method", method);
    if (params) body.insert("params", *params);
    QNetworkReply* reply = nam.post(req, QJsonDocument(body).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError || !reply->isFinished()) {
        if (errOut) *errOut = reply->error() == QNetworkReply::TimeoutError ? "timeout" : "connect";
        reply->deleteLater();
        return QJsonObject();
    }
    QByteArray data = reply->readAll();
    reply->deleteLater();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errOut) *errOut = "empty";
        return QJsonObject();
    }
    QJsonObject obj = doc.object();
    if (obj.contains("result")) return obj.value("result").toObject();
    return obj;
}

// Build JSON-RPC params for tools/call.
static QJsonObject paramsFor(const QString& tool, const QMap<QString, QString>& args) {
    QJsonObject params;
    params.insert("name", tool);
    QJsonObject a;
    for (auto it = args.begin(); it != args.end(); ++it) a.insert(it.key(), it.value());
    params.insert("arguments", a);
    return params;
}

// Extract the human-readable text from a tools/call result.
static QString extractText(const QJsonObject& res) {
    QString text;
    QJsonArray content = res.value("content").toArray();
    for (const auto& c : content) {
        QString t = c.toObject().value("text").toString();
        if (!t.isEmpty()) { if (!text.isEmpty()) text += "\n"; text += t; }
    }
    return text;
}

void McpClient::testServer(const Server& s) {
    QTimer::singleShot(0, this, [this, s]() {
        QString err;
        QJsonObject res = rpc(s.url, "tools/list", nullptr, 8000, &err);
        if (err.isEmpty()) {
            QJsonArray tools = res.value("tools").toArray();
            emit testResult(s.name, true, tools.size(), QString());
        } else {
            emit testResult(s.name, false, 0, err);
        }
    });
}

void McpClient::listTools(const Server& s) {
    QTimer::singleShot(0, this, [this, s]() {
        QString err;
        QJsonObject res = rpc(s.url, "tools/list", nullptr, 8000, &err);
        QStringList names;
        if (err.isEmpty()) {
            QJsonArray tools = res.value("tools").toArray();
            for (const auto& t : tools) {
                QString n = t.toObject().value("name").toString();
                if (!n.isEmpty()) names.append(n);
            }
        }
        emit toolsListed(s.name, names);
    });
}

void McpClient::callTool(const Server& s, const QString& tool, const QMap<QString, QString>& args) {
    QTimer::singleShot(0, this, [this, s, tool, args]() {
        QString err;
        QJsonObject p = paramsFor(tool, args);
        QJsonObject res = rpc(s.url, "tools/call", &p, 8000, &err);
        if (!err.isEmpty()) {
            emit toolResult(tool, QString(), err);
            return;
        }
        QString text = extractText(res);
        if (text.isEmpty()) text = res.value("result").toString();
        emit toolResult(tool, text, QString());
    });
}

// ---------- HNN CN ET v1.0.1: synchronous helpers (Agent engine) ----------

QStringList McpClient::listToolsSync(const QString& url) {
    QString err;
    QJsonObject res = rpc(url, "tools/list", nullptr, 8000, &err);
    QStringList names;
    if (err.isEmpty()) {
        QJsonArray tools = res.value("tools").toArray();
        for (const auto& t : tools) {
            QString n = t.toObject().value("name").toString();
            if (!n.isEmpty()) names.append(n);
        }
    }
    return names;
}

QString McpClient::callToolSync(const QString& url, const QString& tool, const QMap<QString, QString>& args) {
    QString err;
    QJsonObject p = paramsFor(tool, args);
    QJsonObject res = rpc(url, "tools/call", &p, 8000, &err);
    if (!err.isEmpty()) return err == "timeout" ? "timeout" : "connect";
    QString text = extractText(res);
    if (text.isEmpty()) text = res.value("result").toString();
    return text;
}
