#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QComboBox>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QListWidgetItem>

// Win11-style settings dialog (left navigation + pages).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    QWidget* buildAppearancePage();
    QWidget* buildModelPage();
    QWidget* buildAgentPage();
    QWidget* buildMcpPage();
    QWidget* buildApiPage();
    QWidget* buildAboutPage();

    void applyThemeSettings();
    void refreshModelCombo();
    void refreshMcpList();
    void refreshApiInfo();
    void accept() override;
    void openModelManager();
    void openMcpManager();

    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;

    // appearance
    QComboBox* m_themeMode = nullptr;
    QComboBox* m_accent = nullptr;
    QLineEdit* m_bgImage = nullptr;
    QSlider* m_bgOpacity = nullptr;
    QLabel* m_bgPreview = nullptr;

    // model
    QLineEdit* m_backendUrl = nullptr;
    QLineEdit* m_backendKey = nullptr;
    QComboBox* m_model = nullptr;
    QLineEdit* m_maxTokens = nullptr;

    // agent
    QCheckBox* m_agentOn = nullptr;
    QCheckBox* m_agentPwd = nullptr;
    QLineEdit* m_agentPwdText = nullptr;
    QLineEdit* m_agentFolder = nullptr;

    // api
    QCheckBox* m_apiOn = nullptr;
    QLineEdit* m_apiPort = nullptr;
    QLineEdit* m_apiKey = nullptr;
    QLabel* m_apiAddress = nullptr;
    QLabel* m_apiModels = nullptr;
    QCheckBox* m_apiLimit = nullptr;
};
