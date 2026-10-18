#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>

#include "ChatView.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void applyTheme();
    void sendMessage();
    void stopGenerating();
    void newSession();
    void selectSession(int row);
    void loadSessionIntoView(const QString& id);
    void openSettings();
    bool eventFilter(QObject* obj, QEvent* ev) override;

    QWidget* buildSidebar();
    QWidget* buildMain();

    // sidebar
    QListWidget* m_sessionList = nullptr;

    // main
    QComboBox* m_modelCombo = nullptr;
    QCheckBox* m_thinking = nullptr;
    QCheckBox* m_direct = nullptr;
    QLabel* m_apiStatus = nullptr;
    ChatView* m_chat = nullptr;
    QTextEdit* m_input = nullptr;
    QPushButton* m_send = nullptr;

    QString m_currentSessionId;
    bool m_generating = false;
};
