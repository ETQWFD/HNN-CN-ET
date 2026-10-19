#include "ApiService.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include "ThemeManager.h"
#include "ApiClient.h"
#include "ModelManager.h"

ApiService& ApiService::instance() {
    static ApiService svc;
    return svc;
}

ApiService::ApiService() {
    connect(&m_server, &QTcpServer::newConnection, this, &ApiService::onNewConnection);
    connect(&m_nam, &QNetworkAccessManager::finished, this, [this](QNetworkReply* r) {
        // Streaming proxy: pipe upstream chunks to the client socket.
        QVariant v = r->property("socket");
        if (!v.isValid()) { r->deleteLater(); return; }
        QTcpSocket* sock = qvariant_cast<QTcpSocket*>(v);
        r->deleteLater();
        if (sock) {
            sock->write("data: [DONE]\n\n");
            sock->flush();
            sock->disconnectFromHost();
        }
    });
}

bool ApiService::enabled() const {
    return ThemeManager::instance().boolean("apiServiceEnabled", false);
}

void ApiService::setEnabled(bool on) {
    ThemeManager::instance().set("apiServiceEnabled", on);
    if (on) {
        QString key = ThemeManager::instance().string("apiServiceKey");
        if (key.isEmpty()) {
            key = hnnet::generateApiKey();
            ThemeManager::instance().set("apiServiceKey", key);
        }
    }
    ThemeManager::instance().save();
    restart();
    emit stateChanged();
}

int ApiService::port() const {
    return ThemeManager::instance().integer("apiServicePort", 8080);
}

QString ApiService::apiKey() const {
    return ThemeManager::instance().string("apiServiceKey");
}

QString ApiService::address() const {
    QString lan = QStringLiteral("127.0.0.1");
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        const auto entries = iface.addressEntries();
        for (const auto& e : entries) {
            const auto ip = e.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isLoopback()) {
                lan = ip.toString();
                break;
            }
        }
        if (lan != QStringLiteral("127.0.0.1")) break;
    }
    return QStringLiteral("http://%1:%2/v1").arg(lan).arg(port());
}

void ApiService::restart() {
    if (m_server.isListening()) {
        m_server.close();
        // keep the socket connections alive; they will finish on their own
    }
    if (!enabled()) return;
    // HNN CN ET v1.0.3: bind to all interfaces so phones / browsers on the LAN
    // can call the API, not just localhost.
    if (!m_server.listen(QHostAddress::Any, port())) {
        // try next port
        for (int p = port() + 1; p < port() + 20; ++p) {
            if (m_server.listen(QHostAddress::Any, p)) {
                ThemeManager::instance().set("apiServicePort", p);
                ThemeManager::instance().save();
                break;
            }
        }
    }
}

void ApiService::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            // HNN CN ET v1.0.3: buffer until the full HTTP body arrives — a large
            // JSON body arrives in multiple TCP segments and parsing a partial
            // body broke browser calls.
            m_buffers[socket].append(socket->readAll());
            QByteArray& buf = m_buffers[socket];
            int headerEnd = buf.indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            int contentLength = 0;
            QList<QByteArray> lines = buf.left(headerEnd).split('\r\n');
            for (const auto& l : lines) {
                if (l.left(15).toUpper() == "CONTENT-LENGTH:") {
                    contentLength = l.mid(15).trimmed().toInt();
                    break;
                }
            }
            if (contentLength <= 0) {
                handleRequest(socket, buf);
                m_buffers.remove(socket);
                return;
            }
            if (buf.size() < headerEnd + 4 + contentLength) return; // wait for more
            handleRequest(socket, buf.left(headerEnd + 4 + contentLength));
            m_buffers.remove(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void ApiService::handleRequest(QTcpSocket* socket, const QByteArray& requestHead) {
    if (requestHead.isEmpty()) return;
    // Parse request line + headers minimally
    QList<QByteArray> lines = requestHead.split('\n');
    if (lines.isEmpty()) return;
    QString requestLine = QString::fromUtf8(lines.first()).trimmed();
    QStringList parts = requestLine.split(' ');
    if (parts.size() < 3) return;
    QString method = parts[0];
    QString path = parts[1];
    QString auth;
    for (int i = 1; i < lines.size(); ++i) {
        QString line = QString::fromUtf8(lines[i]).trimmed();
        if (line.startsWith("Authorization:", Qt::CaseInsensitive)) {
            auth = line.mid(15).trimmed();
            break;
        }
    }

    // HNN CN ET v1.0.3: CORS for browser clients + OPTIONS preflight.
    if (method == "OPTIONS") {
        socket->write("HTTP/1.1 204 No Content\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                      "Access-Control-Max-Age: 86400\r\n\r\n");
        socket->flush();
        socket->disconnectFromHost();
        return;
    }
    QString cors = "Access-Control-Allow-Origin: *\r\n";
    QStringList segs = path.split('?').first().split('/');
    // /v1/models  |  /v1/chat/completions
    if (method == "GET" && path.contains("/v1/models")) {
        QJsonArray models;
        const auto list = ModelManager::instance().localModels();
        for (const auto& n : list) models.append(QJsonObject{{"id", n}});
        models.append(QJsonObject{{"id", "HNN-CN-ET-Default"}});
        QJsonObject resp{{"object", "list"}, {"data", models}};
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" + cors.toUtf8() +
                      "Content-Length: " + QByteArray::number(QJsonDocument(resp).toJson().size()) + "\r\n\r\n" +
                      QJsonDocument(resp).toJson());
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    if (method == "POST" && path.contains("/v1/chat/completions")) {
        // Key check: "Bearer etc-..."
        if (!auth.isEmpty()) {
            QString token = auth.mid(7).trimmed();
            if (!apiKey().isEmpty() && token != apiKey()) {
                QJsonObject err{{"error", QJsonObject{{"message", "Invalid API key"}, {"type", "invalid_request_error"}}}};
                QByteArray body = QJsonDocument(err).toJson();
                socket->write("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->flush();
                socket->disconnectFromHost();
                return;
            }
        }
        // Extract body after headers
        int idx = requestHead.indexOf("\r\n\r\n");
        QByteArray body = (idx >= 0) ? requestHead.mid(idx + 4) : QByteArray();
        QJsonObject req = QJsonDocument::fromJson(body).object();
        if (req.isEmpty()) {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{}");
            socket->flush();
            socket->disconnectFromHost();
            return;
        }

        // Proxy to configured backend
        QString backend = ThemeManager::instance().string("modelBackendUrl", "http://127.0.0.1:8080/v1");
        QNetworkRequest upstream(QUrl(backend + "/chat/completions"));
        upstream.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QString backendKey = ThemeManager::instance().string("modelBackendKey");
        if (!backendKey.isEmpty()) upstream.setRawHeader("Authorization", ("Bearer " + backendKey).toUtf8());
        QNetworkReply* reply = m_nam.post(upstream, QJsonDocument(req).toJson());
        reply->setProperty("socket", QVariant::fromValue(socket));
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, socket]() {
            QByteArray chunk = reply->readAll();
            if (socket && socket->isWritable()) socket->write(chunk);
        });
        // Stream the response back as SSE
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n" + cors.toUtf8() +
                      "Connection: keep-alive\r\nCache-Control: no-cache\r\n\r\n");
        socket->flush();
        return;
    }

    socket->write("HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\":\"not found\"}");
    socket->flush();
    socket->disconnectFromHost();
}
