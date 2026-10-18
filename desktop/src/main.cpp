#include <QApplication>
#include <QFont>
#include <QTimer>

#include "ThemeManager.h"
#include "MainWindow.h"
#include "ApiService.h"
#include "ApiClient.h"
#include "Updater.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("HNN CN ET");
    app.setApplicationDisplayName("HNN CN ET");
    app.setOrganizationName("ET");
    app.setApplicationVersion("1.0.2");
    app.setWindowIcon(QIcon(":/app.png"));

    QFont font("Microsoft YaHei UI");
    font.setPointSize(10);
    app.setFont(font);

    // init singletons
    ThemeManager::instance();
    ApiService::instance().restart();
    ApiClient::instance().setConfig({
        ThemeManager::instance().string("modelBackendUrl", "http://127.0.0.1:8080/v1"),
        ThemeManager::instance().string("modelBackendKey"),
        "HNN-CN-ET-Default", false, false,
        ThemeManager::instance().integer("maxTokens", 1024)
    });

    MainWindow w;
    w.show();

    QTimer::singleShot(1500, []() {
        Updater::instance().check(true);
    });

    return app.exec();
}
