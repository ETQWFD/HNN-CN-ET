#pragma once

#include <QObject>
#include <QString>
#include <QMap>

#include "Common.h"

// Agent engine: parses device-operation commands, requires password/confirm gate,
// restricts file access to the working folder (sandbox), records history.
class AgentEngine : public QObject {
    Q_OBJECT
public:
    static AgentEngine& instance();

    bool enabled() const;
    void setEnabled(bool on);
    bool passwordRequired() const;
    QString password() const;
    void setPassword(const QString& pwd);
    QString workingFolder() const;
    void setWorkingFolder(const QString& dir);

    // Returns a reply if the input is an agent command (execution is gated by password confirm).
    QString tryHandle(const QString& input);

    static bool isAgentCommand(const QString& input);

signals:
    void requirePasswordConfirm(const QString& input); // UI shows password dialog, then calls runConfirmed
    void agentReplied(const QString& reply);

public slots:
    void runConfirmed(const QString& input, bool passwordOk);

private:
    AgentEngine() = default;
    QString createFile(const QString& path, const QString& content);
    QString readFile(const QString& path);
    QString listDir(const QString& path);
    QString deleteFile(const QString& path);
    QString editFile(const QString& path, const QString& content);
    QString openApp(const QString& label);
    QString openPath(const QString& path);
    QString sandboxPath(const QString& path) const;
    // HNN CN ET v1.0.1: real MCP tool calls + installed-app discovery.
    QString callMcpTool(const QString& toolName, const QString& argsText);
    QStringList findInstalledApps(const QString& label) const;
    // Try to run a file operation through an MCP tool first (falls back to local).
    QString runMcpFileTool(const QStringList& candidates, const QMap<QString, QString>& args);
};
