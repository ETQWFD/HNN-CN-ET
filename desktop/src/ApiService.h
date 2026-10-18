#pragma once

#include <QObject>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Built-in OpenAI-compatible HTTP service (proxies to the configured model backend).
class ApiService : public QObject {
    Q_OBJECT
public:
    static ApiService& instance();

    bool enabled() const;
    void setEnabled(bool on);
    int port() const;
    QString apiKey() const;
    QString address() const;

public slots:
    void restart();

signals:
    void stateChanged();

private:
    ApiService();
    void onNewConnection();
    void handleRequest(QTcpSocket* socket, const QByteArray& requestHead);

    QTcpServer m_server;
    QNetworkAccessManager m_nam;
};
