#include "LocalApiServer.h"
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QCoreApplication>

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
    if (QCoreApplication::arguments().contains("--headless") || QCoreApplication::arguments().contains("--server")) {
        dataPath = QDir(dataPath).filePath("Server");
    }
    QDir().mkpath(dataPath);
    QString keyPath = QDir(dataPath).filePath("api_token.txt");

    QFile file(keyPath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_apiKey = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
    }

    if (m_apiKey.isEmpty()) {
        m_apiKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_apiKey.toUtf8());
            file.close();
        } else {
            qWarning() << "Failed to write API key to" << keyPath;
        }
    }
}

void LocalApiServer::onDownloadAdded(const QVariantMap &itemData)
{
    QString id = itemData.value("id").toString();
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
        m_activeJobs[id].insert("status", success ? "Complete" : message);
        m_activeJobs[id].insert("progress", 100);
    }
}

void LocalApiServer::onDownloadRemoved(const QString &id)
{
    m_activeJobs.remove(id);
}

void LocalApiServer::onNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &LocalApiServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void LocalApiServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray buffer = socket->property("requestBuffer").toByteArray();
    buffer.append(socket->readAll());

    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd != -1) {
        int contentLength = 0;
        
        QString headersStr = QString::fromUtf8(buffer.left(headerEnd));

        QRegularExpression clRe("Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch clMatch = clRe.match(headersStr);
        if (clMatch.hasMatch()) {
            contentLength = clMatch.captured(1).toInt();
        }

        bool expect100 = headersStr.contains("Expect: 100-continue", Qt::CaseInsensitive);

        if (expect100 && !socket->property("100sent").toBool()) {
            socket->write("HTTP/1.1 100 Continue\r\n\r\n");
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
    int bodyIndex = requestData.indexOf("\r\n\r\n");
    QByteArray headersData = (bodyIndex != -1) ? requestData.left(bodyIndex) : requestData;
    QByteArray bodyData = (bodyIndex != -1) ? requestData.mid(bodyIndex + 4) : QByteArray();

    QString requestStr = QString::fromUtf8(headersData);
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) return;

    QStringList requestParts = lines.first().split(" ");
    if (requestParts.size() < 2) return;

    QString method = requestParts[0];
    QString pathQuery = requestParts[1];
    QUrl url(pathQuery);
    QString path = url.path();

    // Enforce API Key
    bool authorized = false;
    for (const QString &line : lines) {
        if (line.startsWith("Authorization:", Qt::CaseInsensitive)) {
            QString token = line.mid(14).trimmed();
            if (token.startsWith("Bearer ", Qt::CaseInsensitive)) token = token.mid(7).trimmed();
            if (token == m_apiKey) {
                authorized = true;
                break;
            }
        }
    }

    if (!authorized) {
        sendHttpResponse(socket, 401, "Unauthorized", "{\"error\": \"Unauthorized. Invalid API Key.\"}");
        return;
    }

    // Route Endpoints
    if (method == "POST" && path == "/enqueue") {
        QString bodyStr;
        if (bodyData.size() >= 2 && (unsigned char)bodyData.at(0) == 0xFF && (unsigned char)bodyData.at(1) == 0xFE) {
            bodyStr = QString::fromUtf16(reinterpret_cast<const char16_t*>(bodyData.constData()), bodyData.size() / 2);
        } else {
            bodyStr = QString::fromUtf8(bodyData);
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(bodyStr.toUtf8(), &parseError);
        if (!doc.isNull() && doc.isObject()) {
            QString targetUrl = doc.object().value("url").toString().trimmed();
            QString downloadType = doc.object().value("type").toString("video"); // Default to "video"
            if (!targetUrl.isEmpty()) {
                // The signal signature in LocalApiServer.h must be updated to:
                // void enqueueRequested(const QString &url, const QString &type, const QString &jobId);
                QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                emit enqueueRequested(targetUrl, downloadType, jobId);
                QJsonObject successObj;
                successObj["status"] = "success";
                successObj["message"] = "Download added to queue.";
                successObj["job_id"] = jobId;
                sendHttpResponse(socket, 200, "OK", QJsonDocument(successObj).toJson(QJsonDocument::Compact));
                return;
            }
        }
        
        QJsonObject errObj;
        errObj["error"] = "Missing or invalid 'url' in JSON body.";
        if (doc.isNull()) {
            errObj["parse_error"] = parseError.errorString();
            errObj["body_length"] = bodyData.size();
            errObj["received_body"] = bodyStr.left(200);
        } else if (!doc.isObject()) {
            errObj["parse_error"] = "JSON is valid but not an object.";
        }
        QByteArray errBytes = QJsonDocument(errObj).toJson(QJsonDocument::Compact);
        sendHttpResponse(socket, 400, "Bad Request", errBytes);
    } else if (method == "GET" && path == "/status") {
        QJsonArray jobsArray;
        for (const QVariantMap &job : m_activeJobs.values()) {
            jobsArray.append(QJsonObject::fromVariantMap(job));
        }
        QJsonObject responseObj;
        responseObj["status"] = "OK";
        responseObj["jobs"] = jobsArray;
        QByteArray responseBytes = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
        sendHttpResponse(socket, 200, "OK", responseBytes);
    } else {
        sendHttpResponse(socket, 404, "Not Found", "{\"error\": \"Endpoint Not Found\"}");
    }
}

void LocalApiServer::sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText, const QByteArray &body)
{
    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append("Content-Type: application/json\r\n");
    response.append(QString("Content-Length: %1\r\n\r\n").arg(body.size()).toUtf8());
    response.append(body);
    socket->write(response);
}