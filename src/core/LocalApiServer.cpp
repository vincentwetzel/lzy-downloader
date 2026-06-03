#include "LocalApiServer.h"
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QCoreApplication>
#include <chrono>
#include <QTimer>

LocalApiServer::LocalApiServer(ConfigManager *configManager, QObject *parent)
    : QObject(parent), m_configManager(configManager), m_server(new QTcpServer(this))
{
    generateOrLoadApiKey();
    
    connect(m_server, &QTcpServer::newConnection, this, &LocalApiServer::onNewConnection);
}

LocalApiServer::~LocalApiServer()
{
    stop();
}

void LocalApiServer::start()
{
    if (m_server->isListening()) {
        return;
    }

    // Port can be configured later, defaulting to 8765
    quint16 port = 8765; 
    
    // Bind strictly to localhost (127.0.0.1) to prevent external network access
    if (m_server->listen(QHostAddress::LocalHost, port)) {
        qInfo() << "Local API Server started on port" << port;
    } else {
        qWarning() << "Failed to start Local API Server:" << m_server->errorString();
    }
}

void LocalApiServer::stop()
{
    if (m_server->isListening()) {
        m_server->close();
        qInfo() << "Local API Server stopped.";
    }
}

bool LocalApiServer::isRunning() const
{
    return m_server->isListening();
}

QString LocalApiServer::getApiKey() const
{
    return m_apiKey;
}

void LocalApiServer::generateOrLoadApiKey()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (QCoreApplication::arguments().contains(QStringLiteral("--headless")) || QCoreApplication::arguments().contains(QStringLiteral("--server")) || QCoreApplication::arguments().contains(QStringLiteral("--background"))) {
        dataPath = QDir(dataPath).filePath(QStringLiteral("Server"));
    }
    QDir().mkpath(dataPath);
    QString keyPath = QDir(dataPath).filePath(QStringLiteral("api_token.txt"));

    QFile file(keyPath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_apiKey = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
    }

    if (m_apiKey.isEmpty()) {
        m_apiKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile saveFile(keyPath);
        if (saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            saveFile.write(m_apiKey.toUtf8());
            if (saveFile.commit()) {
                QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            } else {
                qWarning() << "Failed to commit API key to" << keyPath << ":" << saveFile.errorString();
            }
        } else {
            qWarning() << "Failed to open API key file for writing:" << keyPath << ":" << saveFile.errorString();
        }
    }
}

void LocalApiServer::onDownloadAdded(const QVariantMap &itemData)
{
    QString id = itemData.value(QStringLiteral("id")).toString();
    m_activeJobs[id] = itemData;
}

void LocalApiServer::onDownloadProgress(const QString &id, const QVariantMap &progressData)
{
    if (m_activeJobs.contains(id)) {
        for (auto it = progressData.constBegin(); it != progressData.constEnd(); ++it) {
            m_activeJobs[id].insert(it.key(), it.value());
        }
    }
}

void LocalApiServer::onDownloadFinished(const QString &id, bool success, const QString &message)
{
    if (m_activeJobs.contains(id)) {
        m_activeJobs[id].insert(QStringLiteral("status"), success ? QStringLiteral("Complete") : message);
        m_activeJobs[id].insert(QStringLiteral("progress"), 100);
    }
}

void LocalApiServer::onDownloadRemoved(const QString &id)
{
    m_activeJobs.remove(id);
}

void LocalApiServer::onNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    
    QTimer *timeoutTimer = new QTimer(socket);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
    timeoutTimer->start(std::chrono::seconds(15)); // 15 seconds limit to receive full request payload
    
    connect(socket, &QTcpSocket::readyRead, this, &LocalApiServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void LocalApiServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray buffer = socket->property("requestBuffer").toByteArray();
    buffer.append(socket->readAll());

    // Prevent memory exhaustion if headers are extremely large or missing \r\n\r\n
    if (buffer.size() > 5 * 1024 * 1024) {
        socket->write(QByteArrayLiteral("HTTP/1.1 413 Payload Too Large\r\n\r\n"));
        socket->disconnectFromHost();
        return;
    }

    int headerEnd = buffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headerEnd != -1) {
        int contentLength = 0;
        
        QString headersStr = QString::fromUtf8(buffer.left(headerEnd));

        static const QRegularExpression clRe(QStringLiteral("Content-Length:\\s*(\\d+)"), QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch clMatch = clRe.match(headersStr);
        if (clMatch.hasMatch()) {
            contentLength = clMatch.captured(1).toInt();
        }

        bool expect100 = headersStr.contains(QStringLiteral("Expect: 100-continue"), Qt::CaseInsensitive);

        if (expect100 && !socket->property("100sent").toBool()) {
            socket->write(QByteArrayLiteral("HTTP/1.1 100 Continue\r\n\r\n"));
            socket->setProperty("100sent", true);
        }

        // Check if the entire body has arrived
        if (buffer.size() >= headerEnd + 4 + contentLength) {
            handleRequest(socket, buffer);
            socket->disconnectFromHost();
            return;
        }
    }

    socket->setProperty("requestBuffer", buffer);
}

void LocalApiServer::handleRequest(QTcpSocket *socket, const QByteArray &requestData)
{
    int bodyIndex = requestData.indexOf(QByteArrayLiteral("\r\n\r\n"));
    QByteArray headersData = (bodyIndex != -1) ? requestData.left(bodyIndex) : requestData;
    QByteArray bodyData = (bodyIndex != -1) ? requestData.mid(bodyIndex + 4) : QByteArray();

    QString requestStr = QString::fromUtf8(headersData);
    QStringList lines = requestStr.split(QStringLiteral("\r\n"));
    if (lines.isEmpty()) return;

    QStringList requestParts = lines.first().split(QLatin1Char(' '));
    if (requestParts.size() < 2) return;

    QString method = requestParts[0];
    QString pathQuery = requestParts[1];
    QUrl url(pathQuery);
    QString path = url.path();

    // Enforce API Key, Host, and Origin
    bool authorized = false;
    bool validHost = false;
    QString originHeader;

    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("Authorization:"), Qt::CaseInsensitive)) {
            QString token = line.mid(14).trimmed();
            if (token.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)) token = token.mid(7).trimmed();
            if (token == m_apiKey) {
                authorized = true;
            }
        } else if (line.startsWith(QStringLiteral("Host:"), Qt::CaseInsensitive)) {
            QString host = line.mid(5).trimmed();
            if (host.startsWith(QStringLiteral("127.0.0.1")) || host.startsWith(QStringLiteral("localhost"))) {
                validHost = true;
            }
        } else if (line.startsWith(QStringLiteral("Origin:"), Qt::CaseInsensitive)) {
            originHeader = line.mid(7).trimmed();
        }
    }

    if (!validHost) {
        sendHttpResponse(socket, 403, QStringLiteral("Forbidden"), QByteArrayLiteral("{\"error\": \"Invalid Host header.\"}"));
        return;
    }

    if (!originHeader.isEmpty()) {
        QUrl originUrl(originHeader);
        QString originHost = originUrl.host();
        if (originHost != QStringLiteral("127.0.0.1") && originHost != QStringLiteral("localhost") && 
            !originUrl.scheme().startsWith(QStringLiteral("chrome-extension")) && 
            !originUrl.scheme().startsWith(QStringLiteral("moz-extension"))) {
            sendHttpResponse(socket, 403, QStringLiteral("Forbidden"), QByteArrayLiteral("{\"error\": \"Unauthorized cross-origin request.\"}"));
            return;
        }
    }

    if (method == QStringLiteral("OPTIONS")) {
        // Explicitly reject CORS preflight
        sendHttpResponse(socket, 403, QStringLiteral("Forbidden"), QByteArrayLiteral("{\"error\": \"CORS preflight rejected.\"}"));
        return;
    }

    if (!authorized) {
        sendHttpResponse(socket, 401, QStringLiteral("Unauthorized"), QByteArrayLiteral("{\"error\": \"Unauthorized. Invalid API Key.\"}"));
        return;
    }

    // Route Endpoints
    if (method == QStringLiteral("POST") && path == QStringLiteral("/enqueue")) {
        QJsonParseError parseError;
        QJsonDocument doc;

        if (bodyData.size() >= 2 && static_cast<unsigned char>(bodyData.at(0)) == 0xFF && static_cast<unsigned char>(bodyData.at(1)) == 0xFE) {
            QString bodyStr = QString::fromUtf16(reinterpret_cast<const char16_t*>(bodyData.constData()), bodyData.size() / 2);
            doc = QJsonDocument::fromJson(bodyStr.toUtf8(), &parseError);
        } else {
            doc = QJsonDocument::fromJson(bodyData, &parseError);
        }

        if (!doc.isNull() && doc.isObject()) {
            QString targetUrl = doc.object().value(QStringLiteral("url")).toString().trimmed();
            QString downloadType = doc.object().value(QStringLiteral("type")).toString(QStringLiteral("video")); // Default to "video"
            if (!targetUrl.isEmpty()) {
                // The signal signature in LocalApiServer.h must be updated to:
                // void enqueueRequested(const QString &url, const QString &type, const QString &jobId);
                QString jobId = doc.object().value(QStringLiteral("id")).toString().trimmed();
                if (jobId.isEmpty()) {
                    jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                }
                emit enqueueRequested(targetUrl, downloadType, jobId);
                QJsonObject successObj;
                successObj[QStringLiteral("status")] = QStringLiteral("success");
                successObj[QStringLiteral("message")] = QStringLiteral("Download added to queue.");
                successObj[QStringLiteral("job_id")] = jobId;
                sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(successObj).toJson(QJsonDocument::Compact));
                return;
            }
        }
        
        QJsonObject errObj;
        errObj[QStringLiteral("error")] = QStringLiteral("Missing or invalid 'url' in JSON body.");
        if (doc.isNull()) {
            errObj[QStringLiteral("parse_error")] = parseError.errorString();
            errObj[QStringLiteral("body_length")] = bodyData.size();
            errObj[QStringLiteral("received_body")] = QString::fromUtf8(bodyData.left(200));
        } else if (!doc.isObject()) {
            errObj[QStringLiteral("parse_error")] = QStringLiteral("JSON is valid but not an object.");
        }
        QByteArray errBytes = QJsonDocument(errObj).toJson(QJsonDocument::Compact);
        sendHttpResponse(socket, 400, QStringLiteral("Bad Request"), errBytes);
    } else if (method == QStringLiteral("GET") && path == QStringLiteral("/status")) {
        QJsonArray jobsArray;
        for (const QVariantMap &job : m_activeJobs.values()) {
            jobsArray.append(QJsonObject::fromVariantMap(job));
        }
        QJsonObject responseObj;
        responseObj[QStringLiteral("status")] = QStringLiteral("OK");
        responseObj[QStringLiteral("jobs")] = jobsArray;
        QByteArray responseBytes = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
        sendHttpResponse(socket, 200, QStringLiteral("OK"), responseBytes);
    } else {
        sendHttpResponse(socket, 404, QStringLiteral("Not Found"), QByteArrayLiteral("{\"error\": \"Endpoint Not Found\"}"));
    }
}

void LocalApiServer::sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText, const QByteArray &body)
{
    QByteArray response;
    response.append(QStringLiteral("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append(QByteArrayLiteral("Content-Type: application/json\r\n"));
    response.append(QByteArrayLiteral("Connection: close\r\n"));
    response.append(QStringLiteral("Content-Length: %1\r\n\r\n").arg(body.size()).toUtf8());
    response.append(body);
    socket->write(response);
}