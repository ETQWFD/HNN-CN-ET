#include "ModelManager.h"

#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "ThemeManager.h"

ModelManager& ModelManager::instance() {
    static ModelManager mgr;
    return mgr;
}

ModelManager::ModelManager() {
    loadMarket();
}

void ModelManager::loadMarket() {
    // Bundled catalog (name, size, source URL). All URLs verified live (HTTP 200/206).
    // Users may add custom URLs too.
    m_market.clear();
    m_market.append({"Qwen3-8B-MNN (mnn)", "~8.0 GB",
        "https://modelscope.cn/models/MNN/Qwen3-8B-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen2.5-7B-Instruct-MNN (mnn)", "~7.0 GB",
        "https://modelscope.cn/models/MNN/Qwen2.5-7B-Instruct-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"DeepSeek-R1-7B-Qwen-MNN (mnn)", "~7.0 GB",
        "https://modelscope.cn/models/MNN/DeepSeek-R1-7B-Qwen-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen3-4B-MNN (mnn)", "~4.0 GB",
        "https://modelscope.cn/models/MNN/Qwen3-4B-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen2.5-3B-Instruct-MNN (mnn)", "~3.0 GB",
        "https://modelscope.cn/models/MNN/Qwen2.5-3B-Instruct-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"gemma-4-E2B-it-MNN (mnn)", "~2.0 GB",
        "https://modelscope.cn/models/MNN/gemma-4-E2B-it-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen3-1.7B-MNN (mnn)", "~1.7 GB",
        "https://modelscope.cn/models/MNN/Qwen3-1.7B-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"DeepSeek-R1-1.5B-Qwen-MNN (mnn)", "~1.5 GB",
        "https://modelscope.cn/models/MNN/DeepSeek-R1-1.5B-Qwen-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen2.5-1.5B-Instruct-MNN (mnn)", "~1.5 GB",
        "https://modelscope.cn/models/MNN/Qwen2.5-1.5B-Instruct-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen2-1.5B-Instruct-MNN (mnn)", "~1.5 GB",
        "https://modelscope.cn/models/MNN/Qwen2-1.5B-Instruct-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"LFM2.5-1.2B-Instruct-MNN (mnn)", "~1.2 GB",
        "https://modelscope.cn/models/MNN/LFM2.5-1.2B-Instruct-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"LFM2-1.2B-Tool-MNN (mnn)", "~1.2 GB",
        "https://modelscope.cn/models/MNN/LFM2-1.2B-Tool-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen3.5-0.8B-MNN (mnn)", "~0.8 GB",
        "https://modelscope.cn/models/MNN/Qwen3.5-0.8B-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen3-0.6B-MNN (mnn)", "~0.6 GB",
        "https://modelscope.cn/models/MNN/Qwen3-0.6B-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen2.5-0.5B-Instruct-MNN (mnn)", "~0.5 GB",
        "https://modelscope.cn/models/MNN/Qwen2.5-0.5B-Instruct-MNN/resolve/master/llm.mnn", "魔搭"});
    m_market.append({"Qwen2.5-1.5B-Instruct-GGUF (gguf)", "~1.1 GB",
        "https://modelscope.cn/models/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf", "魔搭"});
    // Merge custom entries saved in settings.
    QJsonArray arr = QJsonDocument::fromJson(
        ThemeManager::instance().string("customModels").toUtf8()).array();
    for (const auto& v : arr) {
        QJsonObject o = v.toObject();
        m_market.append({o.value("name").toString(), o.value("size").toString(),
                         o.value("url").toString(), "自定义"});
    }
}

QStringList ModelManager::localModels() const {
    // Recursive scan: real models may live in subfolders (e.g. models/DeepSeek-R1/llm.mnn),
    // single files (models/xxx.mnn) or old extension-less files. Show relative paths.
    QStringList list;
    QDirIterator it(hnnet::modelsDir(), QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString abs = it.next();
        QString rel = QDir(hnnet::modelsDir()).relativeFilePath(abs);
        bool keep = rel.endsWith(".mnn") || rel.endsWith(".gguf") ||
                    rel.endsWith(".bin") || rel.endsWith(".safetensors") ||
                    rel.endsWith(".onnx") || !rel.contains('.');
        if (keep) list.append(rel.replace('\\', "/"));
    }
    list.sort();
    return list;
}

int ModelManager::importFromDirectory(const QString& dir) {
    // Copy every model file found under <dir> (recursive) into the models folder,
    // preserving relative subfolders. Returns number of files copied.
    QDir src(dir);
    QDir dst(hnnet::modelsDir());
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    int copied = 0;
    QStringList exts = {"mnn", "gguf", "bin", "safetensors", "onnx", "ggml"};
    while (it.hasNext()) {
        QString abs = it.next();
        QString lower = abs.toLower();
        bool ok = false;
        for (const auto& e : exts) if (lower.endsWith("." + e)) { ok = true; break; }
        if (!ok) continue;
        QString rel = src.relativeFilePath(abs);
        QString target = dst.filePath(rel);
        if (QFileInfo::exists(target)) {
            // skip identical files, overwrite different ones
            if (QFileInfo(target).size() == QFileInfo(abs).size()) continue;
        }
        dst.mkpath(QFileInfo(target).absolutePath());
        if (QFile::copy(abs, target)) copied++;
    }
    emit modelsChanged();
    return copied;
}

QString ModelManager::activeModel() const {
    QString m = ThemeManager::instance().string("activeModel");
    if (m.isEmpty()) {
        const auto list = localModels();
        if (!list.isEmpty()) m = list.first();
    }
    return m;
}

void ModelManager::setActiveModel(const QString& name) {
    ThemeManager::instance().set("activeModel", name);
    ThemeManager::instance().save();
}

void ModelManager::addCustomModel(const ModelEntry& e) {
    QJsonArray arr = QJsonDocument::fromJson(
        ThemeManager::instance().string("customModels").toUtf8()).array();
    arr.append(QJsonObject{{"name", e.name}, {"size", e.sizeText}, {"url", e.url}});
    ThemeManager::instance().set("customModels", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact).trimmed()));
    ThemeManager::instance().save();
    loadMarket();
    emit modelsChanged();
}

void ModelManager::download(const ModelEntry& e) {
    if (m_downloading == e.name) return;
    m_downloading = e.name;
    // Keep the real extension so the file is recognized as a model (was saved
    // without extension before -> invisible to inference / recursive scan).
    QString url = e.url;
    QString ext = "mnn";
    if (url.endsWith(".gguf", Qt::CaseInsensitive)) ext = "gguf";
    else if (url.endsWith(".bin", Qt::CaseInsensitive)) ext = "bin";
    else if (url.endsWith(".safetensors", Qt::CaseInsensitive)) ext = "safetensors";
    QString base = e.name;
    base.remove(QRegularExpression("\\s*\\([^)]*\\)\\s*$")); // strip "(mnn)" suffix
    base.replace(QRegularExpression("[\\\\/:*?\"<>|\\s]+"), "_");
    QString finalName = base + "." + ext;
    QString filename = finalName + ".download";
    QFile* f = new QFile(QDir(hnnet::modelsDir()).filePath(filename), this);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFinished(e.name, QStringLiteral("无法写入模型目录"), QString());
        delete f;
        m_downloading.clear();
        return;
    }
    m_out = f;
    QUrl requestUrl(e.url);
    QNetworkRequest req(requestUrl);
    req.setTransferTimeout(0);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_out) m_out->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, e](qint64 done, qint64 total) {
        emit downloadProgress(e.name, done, total);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, e, finalName]() {
        bool ok = m_reply->error() == QNetworkReply::NoError;
        QString err = ok ? QString() : m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_out) {
            m_out->close();
            if (ok) {
                QString finalPath = QDir(hnnet::modelsDir()).filePath(finalName);
                m_out->rename(finalPath);
                delete m_out; m_out = nullptr;
                emit downloadFinished(e.name, QString(), finalPath);
                emit modelsChanged();
            } else {
                m_out->remove();
                delete m_out; m_out = nullptr;
                emit downloadFinished(e.name, err, QString());
            }
        }
        m_downloading.clear();
    });
}
