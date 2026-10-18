#include "AgentEngine.h"

#include <QFile>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>
#include "ThemeManager.h"

AgentEngine& AgentEngine::instance() {
    static AgentEngine engine;
    return engine;
}

bool AgentEngine::enabled() const {
    return ThemeManager::instance().boolean("agentEnabled", false);
}

void AgentEngine::setEnabled(bool on) {
    ThemeManager::instance().set("agentEnabled", on);
    ThemeManager::instance().save();
}

bool AgentEngine::passwordRequired() const {
    return ThemeManager::instance().boolean("agentPasswordRequired", true);
}

QString AgentEngine::password() const {
    return ThemeManager::instance().string("agentPassword");
}

void AgentEngine::setPassword(const QString& pwd) {
    ThemeManager::instance().set("agentPassword", pwd);
    ThemeManager::instance().set("agentPasswordRequired", true);
    ThemeManager::instance().save();
}

QString AgentEngine::workingFolder() const {
    QString dir = ThemeManager::instance().string("agentWorkingFolder");
    if (dir.isEmpty()) {
        dir = QDir(QDir::homePath()).filePath("HNN-CN-ET");
        QDir().mkpath(dir);
        const_cast<AgentEngine*>(this)->setWorkingFolder(dir);
    }
    return dir;
}

void AgentEngine::setWorkingFolder(const QString& dir) {
    ThemeManager::instance().set("agentWorkingFolder", dir);
    ThemeManager::instance().save();
}

bool AgentEngine::isAgentCommand(const QString& input) {
    static const QRegularExpression re(
        QStringLiteral("^(创建|新建|生成)\\s*文件|"
                       "^(读取|查看|打开一下文件|读一下)\\s*文件|"
                       "^(列出|查看|浏览)\\s*(文件|目录|文件夹)|"
                       "^(删除|移除)\\s*文件|"
                       "^(修改|编辑|追加|覆盖)\\s*文件|"
                       "^(调用工具|调用mcp|调用MCP|mcp)\\s+|"
                       "^(请|帮我)?(打开|启动|开启)\\s*[\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12}|"
                       "^(返回|回到桌面|回桌面)"));
    return re.match(input.trimmed()).hasMatch();
}

QString AgentEngine::tryHandle(const QString& input) {
    if (!enabled()) return QString();
    if (!isAgentCommand(input)) return QString();
    emit requirePasswordConfirm(input);
    return QStringLiteral("【Agent】等待确认后执行…");
}

void AgentEngine::runConfirmed(const QString& input, bool passwordOk) {
    if (!passwordOk) {
        emit agentReplied(QStringLiteral("【Agent】密码不正确，已取消操作。"));
        return;
    }
    QString trimmed = input.trimmed();
    QString reply;
    if (QRegularExpression("^(创建|新建|生成)\\s*文件.*").match(trimmed).hasMatch()) {
        QString body = trimmed.remove(QRegularExpression("^(创建|新建|生成)\\s*文件\\s*[:：]?"));
        QString path = body, content;
        QRegularExpression marker("(?:，|,|\\s)(?:内容|内容为|内容是|内容：)[:：]?\\s*(.*)");
        auto m = marker.match(body);
        if (m.hasMatch()) { path = body.left(m.capturedStart(1)).trimmed(); content = m.captured(1); }
        if (path.isEmpty()) path = "agent_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".txt";
        if (content.isEmpty()) content = "由 HNN CN ET Agent 创建";
        reply = createFile(path, content);
    } else if (trimmed.startsWith("读取") || trimmed.startsWith("查看") || trimmed.startsWith("打开一下文件") || trimmed.startsWith("读一下")) {
        QString path = trimmed.remove(QRegularExpression("^(读取|查看|打开一下文件|读一下)\\s*文件\\s*[:：]?")).trimmed();
        reply = readFile(path);
    } else if (trimmed.startsWith("列出") || trimmed.startsWith("浏览")) {
        QString path = trimmed.remove(QRegularExpression("^(列出|浏览)\\s*(文件|目录|文件夹)\\s*[:：]?")).trimmed();
        reply = listDir(path);
    } else if (trimmed.startsWith("查看")) {
        QString path = trimmed.remove(QRegularExpression("^查看\\s*(文件|目录|文件夹)\\s*[:：]?")).trimmed();
        reply = path.contains("\\") || path.contains("/") || path.isEmpty() ? listDir(path) : readFile(path);
    } else if (trimmed.startsWith("删除") || trimmed.startsWith("移除")) {
        QString path = trimmed.remove(QRegularExpression("^(删除|移除)\\s*文件\\s*[:：]?")).trimmed();
        reply = deleteFile(path);
    } else if (trimmed.startsWith("修改") || trimmed.startsWith("编辑") || trimmed.startsWith("追加") || trimmed.startsWith("覆盖")) {
        QString body = trimmed.remove(QRegularExpression("^(修改|编辑|追加|覆盖)\\s*文件\\s*[:：]?"));
        QString path = body, content;
        QRegularExpression marker("(?:，|,|\\s)(?:内容|内容为|内容是|内容：)[:：]?\\s*(.*)");
        auto m = marker.match(body);
        if (m.hasMatch()) { path = body.left(m.capturedStart(1)).trimmed(); content = m.captured(1); }
        reply = editFile(path, content);
    } else if (trimmed.startsWith("调用工具") || trimmed.startsWith("调用mcp") || trimmed.startsWith("调用MCP") || trimmed.startsWith("mcp ")) {
        QRegularExpression re("^(调用工具|调用mcp|调用MCP|mcp)\\s+([A-Za-z0-9_.\\-]+)\\s*(.*)");
        auto m = re.match(trimmed);
        if (m.hasMatch()) {
            reply = QStringLiteral("MCP 工具「%1」将在聊天工具中执行（请使用 MCP 管理面板）。").arg(m.captured(2));
        } else {
            reply = QStringLiteral("用法：调用工具 工具名 参数=值");
        }
    } else if (trimmed == "返回" || trimmed == "回到桌面" || trimmed == "回桌面") {
        reply = QStringLiteral("已返回桌面（开始菜单）。");
        QProcess::startDetached("explorer.exe", QStringList());
    } else {
        QRegularExpression re("^(请|帮我)?(打开|启动|开启)\\s*([\\u4e00-\\u9fa5A-Za-z0-9·+_\\-]{1,12})");
        auto m = re.match(trimmed);
        if (m.hasMatch()) {
            reply = openApp(m.captured(3));
        } else {
            reply = QStringLiteral("【Agent】未能识别该指令。");
        }
    }
    emit agentReplied(reply);
}

QString AgentEngine::sandboxPath(const QString& path) const {
    QString base = workingFolder();
    if (path.isEmpty()) return base;
    QString p = QDir::fromNativeSeparators(path);
    QFileInfo fi(p);
    if (fi.isAbsolute()) {
        // Only allow paths inside the working folder; otherwise map relative.
        if (p.startsWith(QDir::cleanPath(base), Qt::CaseInsensitive) && p.size() > QDir::cleanPath(base).size()) {
            return p;
        }
        return QDir(base).filePath(fi.fileName());
    }
    return QDir(base).filePath(p);
}

QString AgentEngine::createFile(const QString& path, const QString& content) {
    QString target = sandboxPath(path);
    QFileInfo fi(target);
    QDir().mkpath(fi.absolutePath());
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("创建文件失败：%1").arg(target);
    }
    f.write(content.toUtf8());
    f.close();
    return QStringLiteral("已创建文件：%1").arg(target);
}

QString AgentEngine::readFile(const QString& path) {
    QString target = sandboxPath(path);
    QFile f(target);
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringLiteral("读取文件失败：%1").arg(target);
    }
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    if (content.size() > 4000) content = content.left(4000) + "\n…（已截断）";
    return QStringLiteral("【%1】\n%2").arg(target, content);
}

QString AgentEngine::listDir(const QString& path) {
    QString target = sandboxPath(path);
    QDir d(target);
    if (!d.exists()) {
        return QStringLiteral("目录不存在：%1").arg(target);
    }
    QStringList names = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    QString out;
    for (const auto& n : names) {
        QFileInfo fi(d.filePath(n));
        out += (fi.isDir() ? "[目录] " : "[文件] ") + n + "\n";
    }
    if (out.isEmpty()) out = "（空目录）";
    return QStringLiteral("【%1】\n%2").arg(target, out);
}

QString AgentEngine::deleteFile(const QString& path) {
    QString target = sandboxPath(path);
    QFileInfo fi(target);
    if (fi.isDir()) {
        return QDir(target).removeRecursively()
            ? QStringLiteral("已删除目录：%1").arg(target)
            : QStringLiteral("删除目录失败：%1").arg(target);
    }
    QFile f(target);
    if (!f.exists()) return QStringLiteral("文件不存在：%1").arg(target);
    return f.remove()
        ? QStringLiteral("已删除文件：%1").arg(target)
        : QStringLiteral("删除文件失败：%1").arg(target);
}

QString AgentEngine::editFile(const QString& path, const QString& content) {
    QString target = sandboxPath(path);
    QFileInfo fi(target);
    QDir().mkpath(fi.absolutePath());
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("修改文件失败：%1").arg(target);
    }
    f.write(content.toUtf8());
    f.close();
    return QStringLiteral("已写入文件：%1").arg(target);
}

QString AgentEngine::openApp(const QString& label) {
    // Common Windows apps; best-effort start.
    struct Known { const char* label; const char* cmd; };
    static const Known known[] = {
        {"记事本", "notepad.exe"}, {"计算器", "calc.exe"},
        {"画图", "mspaint.exe"}, {"浏览器", "explorer.exe"},
        {"资源管理器", "explorer.exe"}, {"我的电脑", "explorer.exe"},
        {"控制面板", "control.exe"}, {"任务管理器", "taskmgr.exe"},
        {"命令行", "cmd.exe"}, {"终端", "cmd.exe"},
    };
    for (const auto& k : known) {
        if (QString(k.label).contains(label, Qt::CaseInsensitive) || label.contains(k.label, Qt::CaseInsensitive)) {
            QProcess::startDetached(QString::fromLatin1(k.cmd), QStringList());
            return QStringLiteral("已打开「%1」。").arg(label);
        }
    }
    return QStringLiteral("未找到应用「%1」，请尝试：打开记事本 / 打开计算器 / 打开浏览器。").arg(label);
}

QString AgentEngine::openPath(const QString& path) {
    QString target = sandboxPath(path);
    if (!QFileInfo::exists(target)) return QStringLiteral("路径不存在：%1").arg(target);
    QProcess::startDetached("explorer.exe", QStringList() << target);
    return QStringLiteral("已打开：%1").arg(target);
}
