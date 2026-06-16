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
    constexpr quint16 DEFAULT_PORT = 8765;

    // Bind strictly to localhost (127.0.0.1) to prevent external network access
    if (m_server->listen(QHostAddress::LocalHost, DEFAULT_PORT)) {
        qInfo() << "Local API Server started on port" << DEFAULT_PORT;
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
    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QStringLiteral("--headless")) || args.contains(QStringLiteral("--server")) || args.contains(QStringLiteral("--background"))) {
        dataPath = QDir(dataPath).filePath(QStringLiteral("Server"));
    }
    QDir().mkpath(dataPath);
    QString keyPath = QDir(dataPath).filePath(QStringLiteral("api_token.txt"));

    QFile file(keyPath);
    // Atomic open prevents Time-of-Check to Time-of-Use (TOCTOU) race conditions
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
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
    auto jobIt = m_activeJobs.find(id);
    if (jobIt != m_activeJobs.end()) {
        for (auto it = progressData.constBegin(); it != progressData.constEnd(); ++it) {
            jobIt.value().insert(it.key(), it.value());
        }
    }
}

void LocalApiServer::onDownloadFinished(const QString &id, bool success, const QString &message)
{
    auto jobIt = m_activeJobs.find(id);
    if (jobIt != m_activeJobs.end()) {
        jobIt.value().insert(QStringLiteral("status"), success ? QStringLiteral("Complete") : message);
        jobIt.value().insert(QStringLiteral("progress"), success ? 100 : -1);
    }
}

void LocalApiServer::onDownloadRemoved(const QString &id)
{
    m_activeJobs.remove(id);
}

void LocalApiServer::onNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket) return;

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
    constexpr int MAX_PAYLOAD_SIZE = 5 * 1024 * 1024; // 5 MB
    if (buffer.size() > MAX_PAYLOAD_SIZE) {
        socket->write(QByteArrayLiteral("HTTP/1.1 413 Payload Too Large\r\n\r\n"));
        socket->disconnectFromHost();
        return;
    }

    qsizetype headerEnd = buffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headerEnd != -1) {
        int contentLength = 0;

        const QString headersStr = QString::fromUtf8(QByteArrayView(buffer).first(headerEnd));

        static const QRegularExpression clRe(QStringLiteral("^Content-Length:\\s*(\\d+)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
        const QRegularExpressionMatch clMatch = clRe.match(headersStr);
        if (clMatch.hasMatch()) {
            contentLength = clMatch.capturedView(1).toInt();
        }

        static const QRegularExpression expectRe(QStringLiteral("^Expect:\\s*100-continue"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
        const bool expect100 = expectRe.match(headersStr).hasMatch();

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
    const qsizetype bodyIndex = requestData.indexOf(QByteArrayLiteral("\r\n\r\n"));
    const QByteArrayView headersView = (bodyIndex != -1) ? QByteArrayView(requestData).first(bodyIndex) : QByteArrayView(requestData);
    const QByteArray bodyData = (bodyIndex != -1) ? requestData.mid(bodyIndex + 4) : QByteArray();

    const QString requestStr = QString::fromUtf8(headersView);
    const auto lines = QStringView(requestStr).split(u"\r\n", Qt::SkipEmptyParts);
    if (lines.isEmpty() || lines.first().trimmed().isEmpty()) {
        sendHttpResponse(socket, 400, QStringLiteral("Bad Request"), QByteArrayLiteral("{\"error\": \"Empty request.\"}"));
        return;
    }

    const auto requestParts = lines.first().split(u' ', Qt::SkipEmptyParts);
    if (requestParts.size() < 2) {
        sendHttpResponse(socket, 400, QStringLiteral("Bad Request"), QByteArrayLiteral("{\"error\": \"Malformed request line.\"}"));
        return;
    }

    const QStringView method = requestParts[0];
    const QStringView pathQuery = requestParts[1];
    const qsizetype queryIndex = pathQuery.indexOf(u'?');
    const QStringView path = (queryIndex != -1) ? pathQuery.first(queryIndex) : pathQuery;

    // Enforce API Key, Host, and Origin
    bool authorized = false;
    bool validHost = false;
    QString originHeader;

    for (const QStringView &line : lines) {
        if (line.startsWith(u"Authorization:", Qt::CaseInsensitive)) {
            QStringView token = line.mid(14).trimmed();
            if (token.startsWith(u"Bearer ", Qt::CaseInsensitive)) token = token.mid(7).trimmed();
            if (token == m_apiKey) {
                authorized = true;
            }
        } else if (line.startsWith(u"Host:", Qt::CaseInsensitive)) {
            const QStringView host = line.mid(5).trimmed();
            static const QRegularExpression hostRe(QStringLiteral("^(?:127\\.0\\.0\\.1|localhost)(?::\\d+)?$"), QRegularExpression::CaseInsensitiveOption);
            if (hostRe.matchView(host).hasMatch()) {
                validHost = true;
            }
        } else if (line.startsWith(u"Origin:", Qt::CaseInsensitive)) {
            originHeader = line.mid(7).trimmed().toString();
        }
    }

    if (!validHost) {
        sendHttpResponse(socket, 403, QStringLiteral("Forbidden"), QByteArrayLiteral("{\"error\": \"Invalid Host header.\"}"));
        return;
    }

    if (!originHeader.isEmpty()) {
        QUrl originUrl(originHeader);
        QString originHost = originUrl.host();
        if (originHost != u"127.0.0.1" && originHost != u"localhost" &&
            originUrl.scheme() != u"chrome-extension" &&
            originUrl.scheme() != u"moz-extension") {
            sendHttpResponse(socket, 403, QStringLiteral("Forbidden"), QByteArrayLiteral("{\"error\": \"Unauthorized cross-origin request.\"}"));
            return;
        }
        socket->setProperty("RequestOrigin", originHeader);
    }

    if (method == u"OPTIONS") {
        // Accept CORS preflight for permitted origins
        sendHttpResponse(socket, 204, QStringLiteral("No Content"), QByteArray());
        return;
    }

    if (!authorized) {
        sendHttpResponse(socket, 401, QStringLiteral("Unauthorized"), QByteArrayLiteral("{\"error\": \"Unauthorized. Invalid API Key.\"}"));
        return;
    }

    // Route Endpoints
    if (method == u"POST" && path == u"/enqueue") {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bodyData, &parseError);

        if (!doc.isNull() && doc.isObject()) {
            const QJsonObject jsonObj = doc.object();
            const QString targetUrl = jsonObj.value(QStringLiteral("url")).toString().trimmed();
            const QString downloadType = jsonObj.value(QStringLiteral("type")).toString(QStringLiteral("video")); // Default to "video"
            if (!targetUrl.isEmpty()) {
                // The signal signature in LocalApiServer.h must be updated to:
                // void enqueueRequested(const QString &url, const QString &type, const QString &jobId);
                QString jobId = jsonObj.value(QStringLiteral("id")).toString().trimmed();
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
            errObj[QStringLiteral("received_body")] = QString::fromUtf8(QByteArrayView(bodyData).first(qMin(bodyData.size(), qsizetype(200))));
        } else if (!doc.isObject()) {
            errObj[QStringLiteral("parse_error")] = QStringLiteral("JSON is valid but not an object.");
        }
        QByteArray errBytes = QJsonDocument(errObj).toJson(QJsonDocument::Compact);
        sendHttpResponse(socket, 400, QStringLiteral("Bad Request"), errBytes);
    } else if (method == u"GET" && path == u"/status") {
        QJsonArray jobsArray;
        for (auto it = m_activeJobs.cbegin(); it != m_activeJobs.cend(); ++it) {
            jobsArray.append(QJsonObject::fromVariantMap(it.value()));
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
    response.reserve(body.size() + 256);
    response.append(QByteArrayLiteral("HTTP/1.1 "));
    response.append(QByteArray::number(statusCode));
    response.append(' ');
    response.append(statusText.toUtf8());
    const QString origin = socket->property("RequestOrigin").toString();
    response.append(QByteArrayLiteral("\r\nContent-Type: application/json"
                                      "\r\nConnection: close"
                                      "\r\nAccess-Control-Allow-Origin: "));
    response.append(origin.isEmpty() ? QByteArrayLiteral("*") : origin.toUtf8());
    response.append(QByteArrayLiteral("\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS"
                                      "\r\nAccess-Control-Allow-Headers: Content-Type, Authorization"
                                      "\r\nContent-Length: "));
    response.append(QByteArray::number(body.size()));
    response.append(QByteArrayLiteral("\r\n\r\n"));
    response.append(body);
    socket->write(response);
}
