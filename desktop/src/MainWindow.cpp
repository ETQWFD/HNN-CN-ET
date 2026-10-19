#include "MainWindow.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QFile>
#include <QKeyEvent>
#include <QDialogButtonBox>

#include "ThemeManager.h"
#include "HistoryStore.h"
#include "ApiClient.h"
#include "AgentEngine.h"
#include "McpClient.h"
#include "SettingsDialog.h"
#include "ApiService.h"
#include "ModelManager.h"
#include "Updater.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("HNN CN ET — 电脑版"));
    resize(1180, 760);
    setMinimumSize(960, 600);
    buildUi();
    applyTheme();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);

    // new session by default
    auto s = HistoryStore::instance().newSession();
    m_currentSessionId = s.id;
    selectSession(0);

    connect(&ApiClient::instance(), &ApiClient::streamChunk, this, [this](const QString& d) {
        m_chat->appendAssistantStream(d);
    });
    connect(&ApiClient::instance(), &ApiClient::streamFinished, this, [this](const QString& full, const QString& err) {
        m_chat->endAssistantStream();
        m_generating = false;
        m_send->setEnabled(true);
        m_send->setText(QStringLiteral("发送"));
        if (!err.isEmpty() && full.isEmpty()) {
            m_chat->addSystemMessage(QStringLiteral("出错了：%1").arg(err));
        }
        HistoryStore::instance().appendMessage(m_currentSessionId, "assistant", full);
    });
    connect(&ApiClient::instance(), &ApiClient::busyChanged, this, [this](bool busy) {
        m_send->setEnabled(!busy);
        m_send->setText(busy ? QStringLiteral("停止") : QStringLiteral("发送"));
    });
    connect(&AgentEngine::instance(), &AgentEngine::agentReplied, this, [this](const QString& reply) {
        m_chat->addAssistantMessage(reply);
        HistoryStore::instance().appendMessage(m_currentSessionId, "assistant", reply);
        // auto-rename session by first user message
        auto* s = HistoryStore::instance().session(m_currentSessionId);
        if (s && s->title == QStringLiteral("新对话") && !reply.isEmpty()) {
            // keep default; renaming handled on user messages
        }
    });
    connect(&AgentEngine::instance(), &AgentEngine::requirePasswordConfirm, this, [this](const QString& input) {
        auto& engine = AgentEngine::instance();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Agent 操作确认"));
        QVBoxLayout* v = new QVBoxLayout(&dlg);
        QLabel* info = new QLabel(QStringLiteral("AI 将执行：%1\n\n该操作会在你的电脑上进行，请确认。").arg(input));
        info->setWordWrap(true);
        v->addWidget(info);
        QLineEdit* pwd = nullptr;
        if (engine.passwordRequired()) {
            v->addWidget(new QLabel(QStringLiteral("请输入电脑密码（未设置密码可直接点确认）：")));
            pwd = new QLineEdit;
            pwd->setEchoMode(QLineEdit::Password);
            v->addWidget(pwd);
        }
        QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认执行"));
        box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        v->addWidget(box);
        if (dlg.exec() == QDialog::Accepted) {
            bool ok = true;
            if (engine.passwordRequired()) {
                QString expect = engine.password();
                ok = expect.isEmpty() || (pwd && pwd->text() == expect);
            }
            engine.runConfirmed(input, ok);
        } else {
            engine.runConfirmed(input, false);
        }
    });
    connect(&HistoryStore::instance(), &HistoryStore::sessionsChanged, this, [this]() {
        m_sessionList->clear();
        const auto sessions = HistoryStore::instance().sessions();
        for (const auto& s : sessions) {
            QListWidgetItem* it = new QListWidgetItem(s.title);
            it->setData(Qt::UserRole, s.id);
            m_sessionList->addItem(it);
        }
    });
}

void MainWindow::buildUi() {
    QWidget* central = new QWidget(this);
    QHBoxLayout* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildSidebar());
    root->addWidget(buildMain(), 1);
    setCentralWidget(central);
}

QWidget* MainWindow::buildSidebar() {
    QWidget* side = new QWidget(this);
    side->setFixedWidth(240);
    QVBoxLayout* v = new QVBoxLayout(side);
    v->setContentsMargins(12, 16, 12, 12);
    v->setSpacing(10);

    QHBoxLayout* brand = new QHBoxLayout;
    QLabel* icon = new QLabel;
    icon->setPixmap(QPixmap(":/app.png").scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand->addWidget(icon);
    QLabel* title = new QLabel(QStringLiteral("HNN CN ET"));
    title->setStyleSheet("font-size: 16px; font-weight: 600;");
    brand->addWidget(title);
    brand->addStretch(1);
    v->addLayout(brand);

    QPushButton* newBtn = new QPushButton(QStringLiteral("＋ 新对话"));
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::newSession);
    v->addWidget(newBtn);

    QLabel* label = new QLabel(QStringLiteral("历史记录"));
    label->setStyleSheet("color: #888; font-size: 12px;");
    v->addWidget(label);

    m_sessionList = new QListWidget;
    m_sessionList->setFrameShape(QFrame::NoFrame);
    connect(m_sessionList, &QListWidget::currentRowChanged, this, &MainWindow::selectSession);
    v->addWidget(m_sessionList, 1);

    QPushButton* settings = new QPushButton(QStringLiteral("⚙ 设置"));
    connect(settings, &QPushButton::clicked, this, &MainWindow::openSettings);
    v->addWidget(settings);

    QLabel* ver = new QLabel(QStringLiteral("v1.0.3 · © ET"));
    ver->setStyleSheet("color: #888; font-size: 11px;");
    ver->setAlignment(Qt::AlignCenter);
    v->addWidget(ver);
    return side;
}

QWidget* MainWindow::buildMain() {
    QWidget* main = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(main);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // top bar
    QWidget* bar = new QWidget;
    bar->setFixedHeight(54);
    QHBoxLayout* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(20, 0, 20, 0);
    m_modelCombo = new QComboBox;
    const auto models = ModelManager::instance().localModels();
    for (const auto& m : models) m_modelCombo->addItem(m);
    m_modelCombo->setEditable(true);
    m_modelCombo->setCurrentText(ModelManager::instance().activeModel());
    bl->addWidget(m_modelCombo);
    m_thinking = new QCheckBox(QStringLiteral("深度思考"));
    m_direct = new QCheckBox(QStringLiteral("高速直答"));
    m_apiStatus = new QLabel(QStringLiteral("API 服务：关闭"));
    bl->addWidget(m_thinking);
    bl->addWidget(m_direct);
    bl->addStretch(1);
    bl->addWidget(m_apiStatus);
    v->addWidget(bar);

    // chat
    m_chat = new ChatView;
    v->addWidget(m_chat, 1);

    // input
    QWidget* inputWrap = new QWidget;
    QVBoxLayout* il = new QVBoxLayout(inputWrap);
    il->setContentsMargins(20, 10, 20, 14);
    m_input = new QTextEdit;
    m_input->setFixedHeight(96);
    m_input->setPlaceholderText(QStringLiteral("输入消息，回车发送（Shift+Enter 换行）。可输入：打开记事本 / 创建文件 笔记.txt，内容：你好 / 调用工具 工具名 参数=值"));
    QHBoxLayout* inputRow = new QHBoxLayout;
    m_send = new QPushButton(QStringLiteral("发送"));
    m_send->setFixedWidth(88);
    m_send->setFixedHeight(40);
    connect(m_send, &QPushButton::clicked, this, &MainWindow::sendMessage);
    inputRow->addStretch(1);
    inputRow->addWidget(m_send);
    il->addWidget(m_input);
    il->addLayout(inputRow);
    v->addWidget(inputWrap);

    // Enter to send
    m_input->installEventFilter(this);
    return main;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_input && ev->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(ev);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            !(ke->modifiers() & Qt::ShiftModifier)) {
            sendMessage();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::applyTheme() {
    auto& tm = ThemeManager::instance();
    QString bg = tm.windowColor().name();
    if (!tm.backgroundImage().isEmpty()) {
        int op = qBound(10, tm.backgroundOpacity(), 90);
        bg = QStringLiteral("rgba(%1,%2,%3,%4)").arg(tm.windowColor().red())
             .arg(tm.windowColor().green()).arg(tm.windowColor().blue()).arg(op / 100.0);
    }
    QString css = QStringLiteral(
        "QMainWindow, QWidget#central { background: %1; color: %2; }"
        "QWidget { color: %2; font-size: 14px; }"
        "QPushButton { background: %3; color: #fff; border: none; border-radius: 8px; padding: 8px 14px; }"
        "QPushButton:hover { opacity: 0.9; }"
        "QPushButton:disabled { background: #999; }"
        "QLineEdit, QTextEdit, QComboBox { background: %4; color: %2; border: 1px solid %5; border-radius: 8px; padding: 6px 10px; }"
        "QListWidget { background: transparent; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 8px; }"
        "QListWidget::item:selected { background: %3; color: #fff; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
    ).arg(bg, tm.textColor().name(), tm.accentColor(), tm.inputColor().name(), tm.borderColor().name());

    if (!tm.backgroundImage().isEmpty()) {
        QString img = tm.backgroundImage();
        img.replace('\\', '/');
        css += QStringLiteral(
            "QMainWindow { background-image: url(\"%1\"); background-repeat: no-repeat; background-position: center; background-size: cover; }"
        ).arg(img);
    }
    setStyleSheet(css);
    m_apiStatus->setText(ApiService::instance().enabled()
                         ? QStringLiteral("API 服务：开启 · %1").arg(ApiService::instance().address())
                         : QStringLiteral("API 服务：关闭"));
    m_chat->applyTheme();
}

void MainWindow::sendMessage() {
    if (m_generating) { stopGenerating(); return; }
    QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    m_chat->addUserMessage(text);
    HistoryStore::instance().appendMessage(m_currentSessionId, "user", text);

    // auto-rename session
    auto* s = HistoryStore::instance().session(m_currentSessionId);
    if (s && s->title == QStringLiteral("新对话")) {
        QString t = text.left(18) + (text.size() > 18 ? "…" : "");
        HistoryStore::instance().renameSession(m_currentSessionId, t);
    }

    // Agent commands
    if (AgentEngine::instance().enabled() && AgentEngine::isAgentCommand(text)) {
        m_chat->addSystemMessage(QStringLiteral("Agent 指令已提交，等待确认…"));
        QString pending = AgentEngine::instance().tryHandle(text);
        if (pending.contains("等待确认")) {
            m_input->clear();
            return;
        }
    }

    // MCP direct tool call via chat
    QRegularExpression re("^(调用工具|调用mcp|调用MCP|mcp)\\s+([A-Za-z0-9_.\\-]+)\\s*(.*)");
    auto m = re.match(text);
    if (m.hasMatch() && !McpClient::instance().servers().isEmpty()) {
        QString tool = m.captured(2);
        QString argsText = m.captured(3);
        QMap<QString, QString> args;
        QRegularExpression kv("([A-Za-z0-9_.\\-]+)\\s*=\\s*([^\\s]+|\"[^\"]*\")");
        auto it = kv.globalMatch(argsText);
        bool has = false;
        while (it.hasNext()) {
            auto mm = it.next();
            args[mm.captured(1)] = mm.captured(2).remove('"');
            has = true;
        }
        if (!has) args["text"] = argsText;
        auto server = McpClient::instance().servers().first();
        m_chat->addSystemMessage(QStringLiteral("调用 MCP 工具：%1 …").arg(tool));
        m_input->clear();
        connect(&McpClient::instance(), &McpClient::toolResult, this, [this](const QString& t, const QString& txt, const QString& err) {
            if (err.isEmpty()) {
                m_chat->addAssistantMessage(QStringLiteral("【MCP %1】\n%2").arg(t, txt));
                HistoryStore::instance().appendMessage(m_currentSessionId, "assistant",
                                                       QStringLiteral("【MCP %1】\n%2").arg(t, txt));
            } else {
                m_chat->addSystemMessage(QStringLiteral("MCP 调用失败：%1").arg(err));
            }
        });
        McpClient::instance().callTool(server, tool, args);
        return;
    }

    // Normal chat
    QList<hnnet::ChatMsg> history;
    auto* sess = HistoryStore::instance().session(m_currentSessionId);
    if (sess) {
        QJsonArray msgs = sess->messages;
        // include last 20 messages as context
        int start = qMax(0, msgs.size() - 20);
        for (int i = start; i < msgs.size(); ++i) {
            QJsonObject o = msgs.at(i).toObject();
            history.append({o.value("role").toString(), o.value("content").toString()});
        }
    }
    m_chat->beginAssistantStream();
    m_generating = true;
    ApiClient::instance().setConfig({
        ThemeManager::instance().string("modelBackendUrl", "http://127.0.0.1:8080/v1"),
        ThemeManager::instance().string("modelBackendKey"),
        m_modelCombo->currentText().trimmed(),
        m_thinking->isChecked(),
        m_direct->isChecked(),
        ThemeManager::instance().integer("maxTokens", 1024)
    });
    m_input->clear();
    ApiClient::instance().sendChat(history, text);
}

void MainWindow::stopGenerating() {
    m_generating = false;
    m_chat->endAssistantStream();
    m_send->setEnabled(true);
    m_send->setText(QStringLiteral("发送"));
}

void MainWindow::newSession() {
    auto s = HistoryStore::instance().newSession();
    m_currentSessionId = s.id;
    m_chat->clear();
    m_chat->addSystemMessage(QStringLiteral("新对话已创建。选择左侧历史记录可回到旧对话。"));
    selectSession(0);
}

void MainWindow::selectSession(int row) {
    QListWidgetItem* item = m_sessionList->item(row);
    if (!item) return;
    m_currentSessionId = item->data(Qt::UserRole).toString();
    loadSessionIntoView(m_currentSessionId);
}

void MainWindow::loadSessionIntoView(const QString& id) {
    m_chat->clear();
    auto* s = HistoryStore::instance().session(id);
    if (!s) return;
    for (const auto& v : s->messages) {
        QJsonObject o = v.toObject();
        QString role = o.value("role").toString();
        QString content = o.value("content").toString();
        if (role == "user") m_chat->addUserMessage(content);
        else if (role == "assistant") m_chat->addAssistantMessage(content);
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
    // reload model combo + api status
    m_modelCombo->clear();
    const auto models = ModelManager::instance().localModels();
    for (const auto& m : models) m_modelCombo->addItem(m);
    m_modelCombo->setCurrentText(ModelManager::instance().activeModel());
    m_apiStatus->setText(ApiService::instance().enabled()
                         ? QStringLiteral("API 服务：开启 · %1").arg(ApiService::instance().address())
                         : QStringLiteral("API 服务：关闭"));
    applyTheme();
}
