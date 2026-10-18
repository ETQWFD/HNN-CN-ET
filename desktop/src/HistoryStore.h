#pragma once

#include <QObject>
#include <QJsonArray>
#include <QStringList>

#include "Common.h"

// Session history persisted in history.json.
class HistoryStore : public QObject {
    Q_OBJECT
public:
    struct Session {
        QString id;
        QString title;
        qint64 created;
        QJsonArray messages; // array of {role, content, ts}
    };

    static HistoryStore& instance();

    QList<Session> sessions() const { return m_sessions; }
    Session* session(const QString& id);
    Session newSession();
    void deleteSession(const QString& id);
    void renameSession(const QString& id, const QString& title);
    void appendMessage(const QString& sessionId, const QString& role, const QString& content);
    void save();

signals:
    void sessionsChanged();

private:
    HistoryStore();
    void load();
    QList<Session> m_sessions;
};
