#include "SettingsDialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QTimer>
#include <QScrollArea>
#include <QSpinBox>
#include <QProgressBar>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>

#include "ThemeManager.h"
#include "ModelManager.h"
#include "AgentEngine.h"
#include "McpClient.h"
#include "ApiClient.h"
#include "ApiService.h"
#include "Updater.h"

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("设置 — HNN CN ET"));
    resize(860, 560);

    m_nav = new QListWidget(this);
    m_nav->setFixedWidth(168);
    m_nav->addItem(QStringLiteral("外观"));
    m_nav->addItem(QStringLiteral("模型"));
    m_nav->addItem(QStringLiteral("Agent 工作模式"));
    m_nav->addItem(QStringLiteral("MCP 连接配置"));
    m_nav->addItem(QStringLiteral("API 服务"));
    m_nav->addItem(QStringLiteral("关于与更新"));
    m_nav->setCurrentRow(0);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildAppearancePage());
    m_stack->addWidget(buildModelPage());
    m_stack->addWidget(buildAgentPage());
    m_stack->addWidget(buildMcpPage());
    m_stack->addWidget(buildApiPage());
    m_stack->addWidget(buildAboutPage());

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->addWidget(m_nav);
    root->addWidget(m_stack, 1);

    auto& tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog { background: %1; color: %2; }"
        "QListWidget { background: transparent; border: none; font-size: 14px; }"
        "QListWidget::item { padding: 10px 14px; border-radius: 8px; }"
        "QListWidget::item:selected { background: %3; color: #fff; }"
        "QLabel { color: %2; } QLineEdit, QComboBox, QSpinBox, QTextEdit { background: %4; color: %2; border: 1px solid %5; border-radius: 8px; padding: 6px; }"
        "QPushButton { background: %3; color: #fff; border: none; border-radius: 8px; padding: 8px 16px; }"
        "QPushButton:hover { opacity: 0.9; } QGroupBox { border: 1px solid %5; border-radius: 10px; margin-top: 8px; padding-top: 10px; }"
        "QGroupBox::title { color: %2; subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QSlider::groove:horizontal { height: 4px; background: %5; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; border-radius: 8px; background: %3; }")
        .arg(tm.windowColor().name(), tm.textColor().name(), tm.accentColor(),
             tm.inputColor().name(), tm.borderColor().name()));
}

static QWidget* wrapScroll(QWidget* w) {
    QScrollArea* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(w);
    return area;
}

QWidget* SettingsDialog::buildAppearancePage() {
    auto& tm = ThemeManager::instance();
    QWidget* page = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setSpacing(14);

    QGroupBox* theme = new QGroupBox(QStringLiteral("主题模式"));
    QVBoxLayout* tl = new QVBoxLayout(theme);
    m_themeMode = new QComboBox;
    m_themeMode->addItems({QStringLiteral("明亮"), QStringLiteral("暗黑"), QStringLiteral("跟随设备")});
    m_themeMode->setCurrentIndex(tm.themeMode());
    tl->addWidget(m_themeMode);
    v->addWidget(theme);

    QGroupBox* color = new QGroupBox(QStringLiteral("主题颜色"));
    QVBoxLayout* cl = new QVBoxLayout(color);
    m_accent = new QComboBox;
    m_accent->addItem(QStringLiteral("蓝色"), QStringLiteral("#0078D4"));
    m_accent->addItem(QStringLiteral("青色"), QStringLiteral("#00B38A"));
    m_accent->addItem(QStringLiteral("绿色"), QStringLiteral("#00A67E"));
    m_accent->addItem(QStringLiteral("紫色"), QStringLiteral("#8B5CF6"));
    m_accent->addItem(QStringLiteral("红色"), QStringLiteral("#E53935"));
    m_accent->addItem(QStringLiteral("橙色"), QStringLiteral("#F59E0B"));
    int cur = 0;
    for (int i = 0; i < m_accent->count(); ++i) {
        if (m_accent->itemData(i).toString() == tm.accentColor()) cur = i;
    }
    m_accent->setCurrentIndex(cur);
    cl->addWidget(m_accent);
    v->addWidget(color);

    QGroupBox* bg = new QGroupBox(QStringLiteral("背景图片（50% 透明度可调）"));
    QGridLayout* gl = new QGridLayout(bg);
    m_bgImage = new QLineEdit;
    m_bgImage->setReadOnly(true);
    m_bgImage->setText(tm.backgroundImage());
    QPushButton* pick = new QPushButton(QStringLiteral("选择图片"));
    QPushButton* reset = new QPushButton(QStringLiteral("重置（关闭图片）"));
    connect(pick, &QPushButton::clicked, this, [this]() {
        QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择背景图片"),
                                                 QDir::homePath(),
                                                 "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (!f.isEmpty()) {
            m_bgImage->setText(f);
            applyThemeSettings();
        }
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        m_bgImage->clear();
        applyThemeSettings();
    });
    m_bgOpacity = new QSlider(Qt::Horizontal);
    m_bgOpacity->setRange(10, 90);
    m_bgOpacity->setValue(tm.backgroundOpacity());
    m_bgPreview = new QLabel(QStringLiteral("透明度 %1%").arg(tm.backgroundOpacity()));
    connect(m_bgOpacity, &QSlider::valueChanged, this, [this](int v) {
        m_bgPreview->setText(QStringLiteral("透明度 %1%").arg(v));
    });
    gl->addWidget(m_bgImage, 0, 0, 1, 2);
    gl->addWidget(pick, 0, 2);
    gl->addWidget(reset, 0, 3);
    gl->addWidget(m_bgOpacity, 1, 0, 1, 3);
    gl->addWidget(m_bgPreview, 1, 3);
    v->addWidget(bg);
    v->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsDialog::buildModelPage() {
    auto& tm = ThemeManager::instance();
    QWidget* page = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setSpacing(14);

    QGroupBox* conn = new QGroupBox(QStringLiteral("模型服务（OpenAI 兼容）"));
    QGridLayout* gl = new QGridLayout(conn);
    gl->addWidget(new QLabel(QStringLiteral("服务地址")), 0, 0);
    m_backendUrl = new QLineEdit(tm.string("modelBackendUrl", "http://127.0.0.1:8080/v1"));
    gl->addWidget(m_backendUrl, 0, 1);
    gl->addWidget(new QLabel(QStringLiteral("API Key（可选）")), 1, 0);
    m_backendKey = new QLineEdit(tm.string("modelBackendKey"));
    m_backendKey->setEchoMode(QLineEdit::Password);
    gl->addWidget(m_backendKey, 1, 1);
    gl->addWidget(new QLabel(QStringLiteral("模型名称")), 2, 0);
    m_model = new QComboBox;
    m_model->setEditable(true);
    refreshModelCombo();
    gl->addWidget(m_model, 2, 1);
    gl->addWidget(new QLabel(QStringLiteral("最大输出 tokens")), 3, 0);
    m_maxTokens = new QLineEdit(QString::number(tm.integer("maxTokens", 1024)));
    gl->addWidget(m_maxTokens, 3, 1);
    QPushButton* manage = new QPushButton(QStringLiteral("模型管理（下载到 models 目录）"));
    connect(manage, &QPushButton::clicked, this, &SettingsDialog::openModelManager);
    gl->addWidget(manage, 4, 0, 1, 2);
    v->addWidget(conn);
    v->addWidget(new QLabel(QStringLiteral("提示：可直接连接手机版开启的 API 服务（同一局域网），也可连接任意 OpenAI 兼容服务。")));
    v->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsDialog::buildAgentPage() {
    auto& tm = ThemeManager::instance();
    auto& engine = AgentEngine::instance();
    QWidget* page = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setSpacing(14);

    QGroupBox* g = new QGroupBox(QStringLiteral("Agent 工作模式"));
    QVBoxLayout* gl = new QVBoxLayout(g);
    m_agentOn = new QCheckBox(QStringLiteral("开启 Agent（AI 可操作电脑：打开应用 / 创建文件 / 读取 / 修改文件等）"));
    m_agentOn->setChecked(engine.enabled());
    gl->addWidget(m_agentOn);

    m_agentPwd = new QCheckBox(QStringLiteral("操作前需要密码确认（无密码时直接点确认即可）"));
    m_agentPwd->setChecked(engine.passwordRequired());
    gl->addWidget(m_agentPwd);

    QHBoxLayout* pwdRow = new QHBoxLayout;
    pwdRow->addWidget(new QLabel(QStringLiteral("电脑密码（可选）")));
    m_agentPwdText = new QLineEdit(engine.password());
    m_agentPwdText->setEchoMode(QLineEdit::Password);
    pwdRow->addWidget(m_agentPwdText, 1);
    gl->addLayout(pwdRow);

    QHBoxLayout* folderRow = new QHBoxLayout;
    folderRow->addWidget(new QLabel(QStringLiteral("工作文件夹（Agent 只能在此目录内操作）")));
    m_agentFolder = new QLineEdit(engine.workingFolder());
    folderRow->addWidget(m_agentFolder, 1);
    QPushButton* browse = new QPushButton(QStringLiteral("选择"));
    connect(browse, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Agent 工作文件夹"), m_agentFolder->text());
        if (!dir.isEmpty()) m_agentFolder->setText(dir);
    });
    folderRow->addWidget(browse);
    gl->addLayout(folderRow);
    v->addWidget(g);
    v->addWidget(new QLabel(QStringLiteral("支持的指令：打开记事本 / 创建文件 笔记.txt，内容：你好 / 读取文件 xx / 列出文件 / 删除文件 xx / 修改文件 xx，内容：yy / 回到桌面。操作前会弹出密码确认窗口。")));
    v->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsDialog::buildMcpPage() {
    auto& tm = ThemeManager::instance();
    QWidget* page = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setSpacing(12);
    QCheckBox* enable = new QCheckBox(QStringLiteral("启用 MCP 功能（AI 可调用 MCP 工具）"));
    enable->setChecked(tm.boolean("mcpEnabled", false));
    connect(enable, &QCheckBox::toggled, this, [](bool on){
        ThemeManager::instance().set("mcpEnabled", on);
        ThemeManager::instance().save();
    });
    v->addWidget(enable);
    QLabel* hint = new QLabel(QStringLiteral("MCP 服务器列表："));
    v->addWidget(hint);
    QPushButton* manage = new QPushButton(QStringLiteral("管理 MCP 服务器（添加 / 测试 / 删除）"));
    connect(manage, &QPushButton::clicked, this, &SettingsDialog::openMcpManager);
    v->addWidget(manage);
    QLabel* usage = new QLabel(QStringLiteral(
        "使用方式：\n"
        "· 对话输入「调用工具 工具名 参数=值」即可让 AI 调用 MCP 工具；\n"
        "· 「创建文件 / 读取文件 / 列出文件 / 删除文件 / 修改文件」优先走 MCP 文件工具，未配置时在 Agent 工作文件夹内操作。"));
    usage->setWordWrap(true);
    v->addWidget(usage);
    v->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsDialog::buildApiPage() {
    auto& tm = ThemeManager::instance();
    QWidget* page = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(page);
    v->setSpacing(14);

    QGroupBox* g = new QGroupBox(QStringLiteral("API 服务（OpenAI 兼容）"));
    QVBoxLayout* gl = new QVBoxLayout(g);
    m_apiOn = new QCheckBox(QStringLiteral("开启 API 服务"));
    m_apiOn->setChecked(ApiService::instance().enabled());
    gl->addWidget(m_apiOn);

    QHBoxLayout* portRow = new QHBoxLayout;
    portRow->addWidget(new QLabel(QStringLiteral("端口")));
    m_apiPort = new QLineEdit(QString::number(ApiService::instance().port()));
    portRow->addWidget(m_apiPort, 1);
    gl->addLayout(portRow);

    QHBoxLayout* keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(QStringLiteral("Key")));
    m_apiKey = new QLineEdit(ApiService::instance().apiKey());
    m_apiKey->setReadOnly(true);
    keyRow->addWidget(m_apiKey, 1);
    QPushButton* regen = new QPushButton(QStringLiteral("重新生成"));
    connect(regen, &QPushButton::clicked, this, [this]() {
        m_apiKey->setText(hnnet::generateApiKey());
    });
    keyRow->addWidget(regen);
    gl->addLayout(keyRow);

    m_apiAddress = new QLabel;
    m_apiModels = new QLabel;
    gl->addWidget(m_apiAddress);
    gl->addWidget(m_apiModels);

    m_apiLimit = new QCheckBox(QStringLiteral("启用模型限制（仅允许 models 目录内的模型）"));
    m_apiLimit->setChecked(tm.boolean("apiLimit", false));
    gl->addWidget(m_apiLimit);
    v->addWidget(g);
    refreshApiInfo();
    v->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsDialog::buildAboutPage() {
    QWidget* page = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(page);
    QLabel* title = new QLabel(QStringLiteral("HNN CN ET — 电脑版 v1.0.0"));
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    v->addWidget(title);
    v->addWidget(new QLabel(QStringLiteral("基于 MNN 生态的端侧离线大模型聊天 · Win11 风格界面")));
    v->addWidget(new QLabel(QStringLiteral("© ET 协会 · MIT License")));
    QPushButton* check = new QPushButton(QStringLiteral("检查更新"));
    connect(check, &QPushButton::clicked, this, [this]() {
        Updater::instance().check(false);
    });
    v->addWidget(check);
    QPushButton* repo = new QPushButton(QStringLiteral("打开 GitHub 仓库"));
    connect(repo, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/ETQWFD/HNN-CN-ET"));
    });
    v->addWidget(repo);
    v->addStretch(1);
    return wrapScroll(page);
}

void SettingsDialog::applyThemeSettings() {
    auto& tm = ThemeManager::instance();
    tm.set("themeMode", m_themeMode->currentIndex());
    tm.set("accentColor", m_accent->currentData().toString());
    tm.set("backgroundImage", m_bgImage->text());
    tm.set("backgroundOpacity", m_bgOpacity->value());
    tm.save();
    emit tm.themeChanged();
}

void SettingsDialog::refreshModelCombo() {
    m_model->clear();
    const auto list = ModelManager::instance().localModels();
    for (const auto& n : list) m_model->addItem(n);
    QString active = ModelManager::instance().activeModel();
    if (!active.isEmpty()) {
        int i = m_model->findText(active);
        if (i >= 0) m_model->setCurrentIndex(i);
        else m_model->setEditText(active);
    }
}

void SettingsDialog::refreshMcpList() {
    McpClient::instance().loadServers();
}

void SettingsDialog::refreshApiInfo() {
    auto& svc = ApiService::instance();
    m_apiAddress->setText(QStringLiteral("连接地址：%1").arg(svc.address()));
    const auto list = ModelManager::instance().localModels();
    QString models = list.isEmpty() ? QStringLiteral("（本地暂无模型，可在「模型」页下载）") : list.join("、");
    m_apiModels->setText(QStringLiteral("模型名称：%1").arg(models));
}

void SettingsDialog::openModelManager() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("模型管理 — HNN CN ET"));
    dlg.resize(620, 460);
    QVBoxLayout* v = new QVBoxLayout(&dlg);
    QLabel* local = new QLabel(QStringLiteral("本地 models 目录：%1").arg(hnnet::modelsDir()));
    local->setWordWrap(true);
    v->addWidget(local);
    QListWidget* list = new QListWidget;
    v->addWidget(list, 1);
    auto refreshList = [list]() {
        list->clear();
        const auto files = ModelManager::instance().localModels();
        for (const auto& f : files) {
            QListWidgetItem* it = new QListWidgetItem(QStringLiteral("✔ %1（已下载）").arg(f));
            it->setForeground(Qt::green);
            list->addItem(it);
        }
        for (const auto& m : ModelManager::instance().marketModels()) {
            bool found = false;
            for (const auto& f : files) {
                if (QFileInfo(f).fileName().startsWith(m.name.split(" (").first().replace(' ', '_'), Qt::CaseInsensitive) ||
                    QFileInfo(f).fileName() == (m.name.split(" (").first().replace(' ', '_') + ".mnn") ||
                    QFileInfo(f).fileName() == (m.name.split(" (").first().replace(' ', '_') + ".gguf")) {
                    found = true; break;
                }
            }
            if (!found) {
                list->addItem(QStringLiteral("%1 · %2 · %3 — 未下载").arg(m.name, m.sizeText, m.kind));
            }
        }
    };
    refreshList();
    QHBoxLayout* btns = new QHBoxLayout;
    QPushButton* dl = new QPushButton(QStringLiteral("下载选中模型"));
    QPushButton* imp = new QPushButton(QStringLiteral("从文件夹导入模型"));
    QPushButton* addUrl = new QPushButton(QStringLiteral("添加URL模型"));
    QPushButton* use = new QPushButton(QStringLiteral("使用选中模型"));
    btns->addWidget(dl);
    btns->addWidget(imp);
    btns->addWidget(addUrl);
    btns->addWidget(use);
    v->addLayout(btns);
    QProgressBar* prog = new QProgressBar;
    prog->setVisible(false);
    v->addWidget(prog);
    QLabel* status = new QLabel;
    v->addWidget(status);

    connect(&ModelManager::instance(), &ModelManager::downloadProgress, &dlg,
            [prog, status](const QString& name, qint64 done, qint64 total) {
        prog->setVisible(true);
        if (total > 0) prog->setMaximum(int(total));
        prog->setValue(int(done));
        status->setText(QStringLiteral("下载 %1：%2 / %3 MB").arg(name)
                        .arg(done / 1048576.0, 0, 'f', 1)
                        .arg(total / 1048576.0, 0, 'f', 1));
    });
    connect(&ModelManager::instance(), &ModelManager::downloadFinished, &dlg,
            [refreshList, status, prog](const QString& name, const QString& err, const QString& path) {
        prog->setVisible(false);
        if (err.isEmpty()) {
            status->setText(QStringLiteral("下载完成：%1").arg(path));
        } else {
            status->setText(QStringLiteral("下载失败：%1 — %2").arg(name, err));
        }
        refreshList();
    });
    connect(dl, &QPushButton::clicked, &dlg, [list, status]() {
        int row = list->currentRow();
        if (row < 0) { status->setText(QStringLiteral("请先选中要下载的模型")); return; }
        const auto files = ModelManager::instance().localModels();
        const auto market = ModelManager::instance().marketModels();
        int idx = row - files.size();
        if (idx >= 0 && idx < market.size()) {
            ModelManager::instance().download(market[idx]);
            status->setText(QStringLiteral("开始下载：%1").arg(market[idx].name));
        } else {
            status->setText(QStringLiteral("该模型已下载"));
        }
    });
    connect(imp, &QPushButton::clicked, &dlg, [&dlg, refreshList, status]() {
        QString dir = QFileDialog::getExistingDirectory(&dlg, QStringLiteral("选择包含模型的文件夹"));
        if (dir.isEmpty()) return;
        status->setText(QStringLiteral("正在扫描并导入模型…"));
        QApplication::processEvents();
        int n = ModelManager::instance().importFromDirectory(dir);
        if (n > 0) {
            status->setText(QStringLiteral("导入成功：%1 个模型文件已复制到 models 目录").arg(n));
        } else {
            status->setText(QStringLiteral("未在该文件夹中找到模型文件（.mnn/.gguf/.bin/.safetensors/.onnx）"));
        }
        refreshList();
    });
    connect(addUrl, &QPushButton::clicked, &dlg, [&dlg, refreshList]() {
        bool ok = false;
        QString name = QInputDialog::getText(&dlg, QStringLiteral("添加自定义模型"),
                                             QStringLiteral("模型名称（保存为 models 目录下的文件名）"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty()) return;
        QString url = QInputDialog::getText(&dlg, QStringLiteral("添加自定义模型"),
                                            QStringLiteral("下载地址（http/https）"), QLineEdit::Normal, QString(), &ok);
        if (!ok || url.isEmpty()) return;
        QString size = QInputDialog::getText(&dlg, QStringLiteral("添加自定义模型"),
                                             QStringLiteral("大小说明（可选）"), QLineEdit::Normal, QStringLiteral("?"), &ok);
        ModelManager::instance().addCustomModel({name, size, url, QStringLiteral("自定义")});
        refreshList();
    });
    connect(use, &QPushButton::clicked, &dlg, [this, list, status]() {
        int row = list->currentRow();
        if (row < 0) { status->setText(QStringLiteral("请先选中模型")); return; }
        const auto files = ModelManager::instance().localModels();
        if (row < files.size()) {
            ModelManager::instance().setActiveModel(files[row]);
            status->setText(QStringLiteral("已选择使用模型：%1").arg(files[row]));
            refreshModelCombo();
        } else {
            status->setText(QStringLiteral("该模型尚未下载，请先下载"));
        }
    });
    dlg.exec();
}

void SettingsDialog::openMcpManager() {
    McpClient::instance().loadServers();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("MCP 服务器管理"));
    dlg.resize(600, 420);
    QVBoxLayout* v = new QVBoxLayout(&dlg);
    QListWidget* list = new QListWidget;
    v->addWidget(list, 1);
    auto refresh = [list]() {
        list->clear();
        for (const auto& s : McpClient::instance().servers()) {
            list->addItem(QStringLiteral("%1 — %2").arg(s.name, s.url));
        }
    };
    refresh();
    QHBoxLayout* btns = new QHBoxLayout;
    QPushButton* add = new QPushButton(QStringLiteral("添加"));
    QPushButton* test = new QPushButton(QStringLiteral("测试"));
    QPushButton* del = new QPushButton(QStringLiteral("删除"));
    btns->addWidget(add);
    btns->addWidget(test);
    btns->addWidget(del);
    v->addLayout(btns);
    QLabel* status = new QLabel;
    v->addWidget(status);

    connect(add, &QPushButton::clicked, &dlg, [&dlg, refresh]() {
        bool ok = false;
        QString name = QInputDialog::getText(&dlg, QStringLiteral("添加 MCP 服务器"),
                                             QStringLiteral("服务器名称"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty()) return;
        QString url = QInputDialog::getText(&dlg, QStringLiteral("添加 MCP 服务器"),
                                            QStringLiteral("地址，如 http://192.168.0.108:5080/mcp"),
                                            QLineEdit::Normal, QStringLiteral("http://"), &ok);
        if (!ok || url.isEmpty()) return;
        // HNN CN ET v1.0.2: optional access token — empty = public server without auth.
        QString token = QInputDialog::getText(&dlg, QStringLiteral("添加 MCP 服务器"),
                                              QStringLiteral("访问令牌（可选；无令牌的公共服务器留空即可，MT 等需要令牌则填写）"),
                                              QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        auto servers = McpClient::instance().servers();
        servers.append({name, url, token});
        McpClient::instance().saveServers(servers);
        refresh();
    });
    connect(test, &QPushButton::clicked, &dlg, [&dlg, list, status]() {
        int row = list->currentRow();
        auto servers = McpClient::instance().servers();
        if (row < 0 || row >= servers.size()) { status->setText(QStringLiteral("请先选中服务器")); return; }
        status->setText(QStringLiteral("测试中…"));
        connect(&McpClient::instance(), &McpClient::testResult, &dlg, [status](const QString&, bool ok, int n, const QString& err) {
            if (ok) status->setText(QStringLiteral("连接成功，可用工具：%1 个").arg(n));
            else status->setText(QStringLiteral("连接失败：%1").arg(err == "timeout" ? QStringLiteral("连接超时") :
                                                       err == "connect" ? QStringLiteral("无法连接服务器") : err));
        });
        McpClient::instance().testServer(servers[row]);
    });
    connect(del, &QPushButton::clicked, &dlg, [&dlg, list, refresh]() {
        int row = list->currentRow();
        auto servers = McpClient::instance().servers();
        if (row < 0 || row >= servers.size()) return;
        servers.removeAt(row);
        McpClient::instance().saveServers(servers);
        refresh();
    });
    dlg.exec();
}

// Accept/save
void SettingsDialog::accept() {
    auto& tm = ThemeManager::instance();
    applyThemeSettings();
    tm.set("modelBackendUrl", m_backendUrl->text().trimmed());
    tm.set("modelBackendKey", m_backendKey->text().trimmed());
    tm.set("activeModel", m_model->currentText().trimmed());
    bool tok = false;
    int maxT = m_maxTokens->text().trimmed().toInt(&tok);
    tm.set("maxTokens", tok ? maxT : 1024);
    tm.save();

    auto& engine = AgentEngine::instance();
    engine.setEnabled(m_agentOn->isChecked());
    engine.setPassword(m_agentPwdText->text());
    engine.setWorkingFolder(m_agentFolder->text().trimmed());

    auto& svc = ApiService::instance();
    bool wasEnabled = svc.enabled();
    svc.setEnabled(m_apiOn->isChecked());
    int port = m_apiPort->text().trimmed().toInt(&tok);
    if (tok && port > 0 && port < 65536) {
        tm.set("apiServicePort", port);
        tm.save();
    }
    if (!m_apiKey->text().isEmpty()) {
        tm.set("apiServiceKey", m_apiKey->text());
        tm.save();
    }
    tm.set("apiLimit", m_apiLimit->isChecked());
    tm.save();
    if (wasEnabled != m_apiOn->isChecked() || m_apiOn->isChecked()) svc.restart();

    ApiClient::instance().setConfig({
        tm.string("modelBackendUrl", "http://127.0.0.1:8080/v1"),
        tm.string("modelBackendKey"),
        m_model->currentText().trimmed(),
        false, false, maxT
    });
    QDialog::accept();
}
