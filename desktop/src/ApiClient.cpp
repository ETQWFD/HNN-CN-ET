#include "ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QTimer>
#include <QtDebug>

ApiClient& ApiClient::instance() {
    static ApiClient client;
    return client;
}

ApiClient::ApiClient() {
    connect(&m_nam, &QNetworkAccessManager::finished, this, [this](QNetworkReply* r) {
        if (r != m_reply) { r->deleteLater(); return; }
        // Reply finished without full stream: flush what we have.
        m_reply = nullptr;
        if (m_busy) {
            bool ok = (r->error() == QNetworkReply::NoError);
            m_busy = false;
            emit busyChanged(false);
            if (ok) {
                if (m_full.isEmpty()) m_full = QString::fromUtf8(r->readAll());
                emit streamFinished(m_full, QString());
            } else {
                emit streamFinished(m_full, r->errorString());
            }
        }
        r->deleteLater();
    });
}

void ApiClient::sendChat(const QList<hnnet::ChatMsg>& history, const QString& userMessage) {
    if (m_busy) return;
    m_busy = true;
    emit busyChanged(true);
    m_full.clear();
    m_buffer.clear();

    QJsonArray messages;
    QList<hnnet::ChatMsg> all = history;
    if (!userMessage.trimmed().isEmpty()) {
        all.append({"user", userMessage});
    }

    // System prompt
    if (m_cfg.direct) {
        messages.append(QJsonObject{{"role", "system"}, {"content", hnnet::defaultDirectSystemPrompt()}});
    } else if (m_cfg.thinking) {
        messages.append(QJsonObject{{"role", "system"}, {"content", hnnet::defaultThinkingSystemPrompt()}});
    }
    for (const auto& m : all) {
        messages.append(QJsonObject{{"role", m.role}, {"content", m.content}});
    }

    QJsonObject body;
    body.insert("model", m_cfg.model);
    body.insert("messages", messages);
    body.insert("stream", true);
    body.insert("max_tokens", m_cfg.maxTokens);
    body.insert("temperature", 0.7);

    QUrl url(m_cfg.baseUrl + "/chat/completions");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_cfg.apiKey.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + m_cfg.apiKey).toUtf8());
    }

    m_reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(m_reply, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        // Error will be finalized in finished() handler.
    });
}

void ApiClient::onReadyRead() {
    if (!m_reply) return;
    m_buffer += m_reply->readAll();
    parseSse(m_buffer);
}

void ApiClient::parseSse(const QByteArray& data) {
    // Consume complete "data:" lines; keep trailing partial in m_buffer.
    int pos = 0;
    while (true) {
        int nl = data.indexOf('\n', pos);
        if (nl < 0) break;
        QByteArray line = data.mid(pos, nl - pos).trimmed();
        pos = nl + 1;
        if (line.startsWith("data:")) {
            QByteArray payload = line.mid(5).trimmed();
            if (payload == "[DONE]") {
                m_busy = false;
                emit busyChanged(false);
                emit streamFinished(m_full, QString());
                if (m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
                m_buffer = data.mid(pos);
                return;
            }
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray choices = doc.object().value("choices").toArray();
                if (!choices.isEmpty()) {
                    QJsonObject delta = choices.first().toObject().value("delta").toObject();
                    QString c = delta.value("content").toString();
                    if (!c.isEmpty()) {
                        m_full += c;
                        emit streamChunk(c);
                    }
                }
            }
        }
    }
    m_buffer = data.mid(pos);
}
