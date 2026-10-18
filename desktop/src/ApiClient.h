#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "Common.h"

// OpenAI-compatible chat client (streaming).
class ApiClient : public QObject {
    Q_OBJECT
public:
    static ApiClient& instance();

    struct Config {
        QString baseUrl;    // e.g. http://127.0.0.1:8080/v1
        QString apiKey;     // optional
        QString model;      // model name
        bool thinking = false;
        bool direct = false;
        int maxTokens = 1024;
    };

    void setConfig(const Config& c) { m_cfg = c; }
    Config config() const { return m_cfg; }

    bool busy() const { return m_busy; }

public slots:
    void sendChat(const QList<hnnet::ChatMsg>& history, const QString& userMessage);

signals:
    void streamChunk(const QString& delta);
    void streamFinished(const QString& fullText, const QString& error);
    void busyChanged(bool busy);

private:
    ApiClient();
    void onReadyRead();
    void parseSse(const QByteArray& data);

    Config m_cfg;
    QNetworkAccessManager m_nam;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_buffer;
    QString m_full;
    bool m_busy = false;
};
