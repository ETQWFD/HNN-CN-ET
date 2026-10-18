#include "ModelManager.h"

#include <QFile>
#include <QDir>
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
    // Bundled catalog (name, size, source URL). Users may add custom URLs too.
    m_market.clear();
    m_market.append({"LFM2-350M-MNN (mnn)", "213 MB",
        "https://modelscope.cn/models/LFM/LFM2-350M-MNN/resolve/master/model.mnn", "魔搭"});
    m_market.append({"DeepSeek-R1-7B-Qwen-MNN (mnn)", "4.33 GB",
        "https://modelscope.cn/models/DeepSeek-R1-7B-Qwen-MNN/resolve/master/model.mnn", "魔搭"});
    m_market.append({"Qwen2.5-1.5B-Instruct (mnn)", "~1.1 GB",
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
    QDir d(hnnet::modelsDir());
    QStringList list;
    for (const auto& fi : d.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        list.append(fi.fileName());
    }
    return list;
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
    QString filename = e.name + ".download";
    QFile* f = new QFile(QDir(hnnet::modelsDir()).filePath(filename), this);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFinished(e.name, QStringLiteral("无法写入模型目录"), QString());
        delete f;
        m_downloading.clear();
        return;
    }
    m_out = f;
    QNetworkRequest req(QUrl(e.url));
    req.setTransferTimeout(0);
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_out) m_out->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, e](qint64 done, qint64 total) {
        emit downloadProgress(e.name, done, total);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, e]() {
        bool ok = m_reply->error() == QNetworkReply::NoError;
        QString err = ok ? QString() : m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_out) {
            m_out->close();
            if (ok) {
                QString finalPath = QDir(hnnet::modelsDir()).filePath(e.name);
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
