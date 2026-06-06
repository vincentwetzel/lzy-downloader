#include "ExtractorJsonParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QtConcurrent>
#include <QFuture>

ExtractorJsonParser::ExtractorJsonParser(QObject *parent) : QObject(parent)
{
    m_loader = new QFutureWatcher<QPair<QJsonObject, QJsonObject>>(this);
    connect(m_loader, &QFutureWatcher<QPair<QJsonObject, QJsonObject>>::finished, this, [this]() {
        QPair<QJsonObject, QJsonObject> result = m_loader->result();
        m_ytDlpExtractors = result.first;
        m_galleryDlExtractors = result.second;
        qInfo() << "ExtractorJsonParser: Loaded" << m_ytDlpExtractors.size() << "yt-dlp and" << m_galleryDlExtractors.size() << "gallery-dl extractors.";
        emit extractorsReady();
    });
}

QJsonObject ExtractorJsonParser::getYtDlpExtractors() const
{
    return m_ytDlpExtractors;
}

QJsonObject ExtractorJsonParser::getGalleryDlExtractors() const
{
    return m_galleryDlExtractors;
}

QJsonObject ExtractorJsonParser::getAllExtractors() const
{
    QJsonObject merged = m_ytDlpExtractors;
    for (auto it = m_galleryDlExtractors.constBegin(); it != m_galleryDlExtractors.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

void ExtractorJsonParser::startGeneration()
{
    if (m_loader->isRunning()) {
        return;
    }
    QFuture<QPair<QJsonObject, QJsonObject>> future = QtConcurrent::run([this]() {
        return loadExtractors();
    });
    m_loader->setFuture(future);
}

QPair<QJsonObject, QJsonObject> ExtractorJsonParser::loadExtractors()
{
    static const QString YTDLP_EXTRACTORS_FILE = QStringLiteral("extractors_yt-dlp.json");
    static const QString GALLERYDL_EXTRACTORS_FILE = QStringLiteral("extractors_gallery-dl.json");

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString ytDlpPath = QDir(appDir).filePath(YTDLP_EXTRACTORS_FILE);
    const QString galleryDlPath = QDir(appDir).filePath(GALLERYDL_EXTRACTORS_FILE);

    QJsonObject ytDlpExtractors = loadExtractorsFromFile(ytDlpPath);
    QJsonObject galleryDlExtractors = loadExtractorsFromFile(galleryDlPath);

    if (ytDlpExtractors.isEmpty()) {
        qWarning() << "ExtractorJsonParser:" << YTDLP_EXTRACTORS_FILE << "not found or is empty in" << appDir;
    }
    if (galleryDlExtractors.isEmpty()) {
        qWarning() << "ExtractorJsonParser:" << GALLERYDL_EXTRACTORS_FILE << "not found or is empty in" << appDir;
    }

    return qMakePair(ytDlpExtractors, galleryDlExtractors);
}

QJsonObject ExtractorJsonParser::loadExtractorsFromFile(const QString &path) const
{
    QFile file(path);
    if (!file.exists()) {
        return QJsonObject();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ExtractorJsonParser: Failed to open extractors file:" << path << "Error:" << file.errorString();
        return QJsonObject();
    }

    QByteArray fileContent = file.readAll();
    file.close();
    if (fileContent.isEmpty()) {
        return QJsonObject();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(fileContent, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "ExtractorJsonParser: Failed to parse JSON from" << path << "Error:" << parseError.errorString();
        return QJsonObject();
    }
    return doc.object();
}
