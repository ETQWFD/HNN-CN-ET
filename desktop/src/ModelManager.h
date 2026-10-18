#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QStringList>

#include "Common.h"

// Downloads models into ./models with progress; lists local model files.
class ModelManager : public QObject {
    Q_OBJECT
public:
    struct ModelEntry {
        QString name;
        QString sizeText;
        QString url;
        QString kind; // 魔搭 / 社区 / 自定义
    };

    static ModelManager& instance();

    QStringList localModels() const;              // file names in models dir
    QList<ModelEntry> marketModels() const { return m_market; }
    QString activeModel() const;
    void setActiveModel(const QString& name);
    void addCustomModel(const ModelEntry& e);

    void download(const ModelEntry& e);

signals:
    void downloadProgress(const QString& name, qint64 done, qint64 total);
    void downloadFinished(const QString& name, const QString& error, const QString& path);
    void modelsChanged();

private:
    ModelManager();
    void loadMarket();

    QNetworkAccessManager m_nam;
    QList<ModelEntry> m_market;
    QString m_downloading;
    QNetworkReply* m_reply = nullptr;
    QFile* m_out = nullptr;
};
