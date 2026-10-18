#pragma once

#include <QObject>
#include <QString>
#include <QList>

// MCP (Model Context Protocol) HTTP JSON-RPC client.
class McpClient : public QObject {
    Q_OBJECT
public:
    struct Server {
        QString name;
        QString url;
    };

    static McpClient& instance();

    QList<Server> servers() const { return m_servers; }
    void loadServers();
    void saveServers(const QList<Server>& servers);
    void addServer(const Server& s);

    // Blocking-free async wrappers that emit signals.
    void testServer(const Server& s);
    void listTools(const Server& s);
    void callTool(const Server& s, const QString& tool, const QMap<QString, QString>& args);

    // HNN CN ET v1.0.1: synchronous helpers for the Agent engine (real MCP calls).
    static QStringList listToolsSync(const QString& url);
    static QString callToolSync(const QString& url, const QString& tool, const QMap<QString, QString>& args);

signals:
    void testResult(const QString& name, bool ok, int toolCount, const QString& error);
    void toolsListed(const QString& name, const QStringList& tools);
    void toolResult(const QString& tool, const QString& text, const QString& error);

private:
    McpClient() = default;
    QList<Server> m_servers;
};
