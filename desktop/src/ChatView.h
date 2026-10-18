#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>

// Chat message list with user / assistant / system bubbles.
class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget* parent = nullptr);

    void addUserMessage(const QString& text);
    void addAssistantMessage(const QString& text);
    void addSystemMessage(const QString& text);
    void clear();
    void beginAssistantStream();           // creates an empty streaming bubble
    void appendAssistantStream(const QString& delta);
    void endAssistantStream();

    QVBoxLayout* layout() { return m_layout; }

public:
    void applyTheme();

private:
    QWidget* makeBubble(const QString& text, bool user);
    void scrollToBottom();

    QScrollArea* m_scroll = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QLabel* m_streamLabel = nullptr;
    QString m_streamText;
};
