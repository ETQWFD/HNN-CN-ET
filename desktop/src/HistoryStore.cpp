#include "HistoryStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QDateTime>

HistoryStore& HistoryStore::instance() {
    static HistoryStore store;
    return store;
}

HistoryStore::HistoryStore() {
    load();
}

void HistoryStore::load() {
    QFile f(hnnet::historyPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray()) return;
    QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        Session s;
        s.id = o.value("id").toString();
        s.title = o.value("title").toString();
        s.created = o.value("created").toVariant().toLongLong();
        s.messages = o.value("messages").toArray();
        if (!s.id.isEmpty()) m_sessions.append(s);
    }
}

HistoryStore::Session* HistoryStore::session(const QString& id) {
    for (auto& s : m_sessions) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

HistoryStore::Session HistoryStore::newSession() {
    Session s;
    s.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    s.title = QStringLiteral("新对话");
    s.created = QDateTime::currentMSecsSinceEpoch();
    m_sessions.prepend(s);
    emit sessionsChanged();
    return s;
}

void HistoryStore::deleteSession(const QString& id) {
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id == id) {
            m_sessions.removeAt(i);
            emit sessionsChanged();
            return;
        }
    }
}

void HistoryStore::renameSession(const QString& id, const QString& title) {
    Session* s = session(id);
    if (s) {
        s->title = title;
        emit sessionsChanged();
    }
}

void HistoryStore::appendMessage(const QString& sessionId, const QString& role, const QString& content) {
    Session* s = session(sessionId);
    if (!s) return;
    QJsonObject m;
    m.insert("role", role);
    m.insert("content", content);
    m.insert("ts", QJsonValue::fromVariant(QDateTime::currentMSecsSinceEpoch()));
    s->messages.append(m);
    save();
}

void HistoryStore::save() {
    QJsonArray arr;
    for (const auto& s : m_sessions) {
        QJsonObject o;
        o.insert("id", s.id);
        o.insert("title", s.title);
        o.insert("created", QJsonValue::fromVariant(s.created));
        o.insert("messages", s.messages);
        arr.append(o);
    }
    QFile f(hnnet::historyPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson());
        f.close();
    }
}
