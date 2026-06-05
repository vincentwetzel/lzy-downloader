#include "Aria2RpcClient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QDebug>

Aria2RpcClient::Aria2RpcClient(QObject* parent)
    : QObject(parent), 
      m_process(new QProcess(this)), 
      m_netManager(new QNetworkAccessManager(this)),
      m_statTimer(new QTimer(this))
{
    // Default local RPC URL for aria2
    m_rpcUrl = QUrl(QStringLiteral("http://127.0.0.1:6800/jsonrpc"));

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        emit daemonError(tr("Aria2 process error: %1").arg(error));
    });

    connect(m_statTimer, &QTimer::timeout, this, [this]() {
        sendRpcRequest(QStringLiteral("aria2.getGlobalStat"), QJsonArray(), [this](const QJsonObject& response) {
            if (response.contains(QStringLiteral("result"))) {
                qint64 speed = response[QStringLiteral("result")].toObject()[QStringLiteral("downloadSpeed")].toString().toLongLong();
                emit globalStatUpdated(speed);
            }
        });
    });
}

Aria2RpcClient::~Aria2RpcClient() {
    stopDaemon();
}

bool Aria2RpcClient::startDaemon(const QString& aria2ExecutablePath, const QString& maxOverallLimit) {
    if (m_process->state() != QProcess::NotRunning) {
        return false; // Already running
    }

    QStringList args;
    args << QStringLiteral("--enable-rpc=true")
         << QStringLiteral("--rpc-listen-all=false")          // Keep it local for security
         << QStringLiteral("--rpc-listen-port=6800")
         << QStringLiteral("--rpc-allow-origin-all=true")
         << QStringLiteral("--max-overall-download-limit=%1").arg(maxOverallLimit)
         << QStringLiteral("--daemon=false");                 // Keep attached to QProcess for lifecycle management

    m_process->start(aria2ExecutablePath, args);
    if (!m_process->waitForStarted()) {
        return false;
    }

    emit daemonStarted();
    m_statTimer->start(1000); // Poll global stats every second
    return true;
}

void Aria2RpcClient::stopDaemon() {
    m_statTimer->stop();
    if (m_process->state() == QProcess::Running) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
    }
}

void Aria2RpcClient::sendRpcRequest(const QString& method, const QJsonArray& params, std::function<void(const QJsonObject&)> callback, std::function<void(const QString&)> errorCallback) {
    QJsonObject requestObj;
    requestObj[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    requestObj[QStringLiteral("id")] = QUuid::createUuid().toString();
    requestObj[QStringLiteral("method")] = method;
    if (!params.isEmpty()) {
        requestObj[QStringLiteral("params")] = params;
    }

    QJsonDocument doc(requestObj);
    QByteArray data = doc.toJson();

    QNetworkRequest request(m_rpcUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* reply = m_netManager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, errorCallback] {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument responseDoc = QJsonDocument::fromJson(reply->readAll());
            if (responseDoc.isObject()) {
                QJsonObject responseObj = responseDoc.object();
                if (responseObj.contains(QStringLiteral("error"))) {
                    QString errMsg = responseObj[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
                    if (errorCallback) errorCallback(errMsg);
                    emit rpcError(tr("RPC Error: %1").arg(errMsg));
                } else if (callback) {
                    callback(responseObj);
                }
            }
        } else {
            if (errorCallback) errorCallback(reply->errorString());
            emit rpcError(tr("Network Error: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
    });
}

void Aria2RpcClient::setGlobalLimit(const QString& maxOverallLimit) {
    QJsonArray params;
    QJsonObject options;
    options[QStringLiteral("max-overall-download-limit")] = maxOverallLimit;
    params.append(options);

    sendRpcRequest(QStringLiteral("aria2.changeGlobalOption"), params, [](const QJsonObject&) {
        qDebug() << "Global limit updated successfully.";
    });
}

void Aria2RpcClient::addDownload(const QString& url, const QString& saveDir, const QString& fileName, const QMap<QString, QString>& headers, std::function<void(const QString&)> callback, std::function<void(const QString&)> errorCallback) {
    QJsonArray params;
    QJsonArray urls;
    urls.append(url);
    params.append(urls);

    QJsonObject options;
    options[QStringLiteral("dir")] = saveDir;
    options[QStringLiteral("out")] = fileName;
    
    if (!headers.isEmpty()) {
        QJsonArray headerArray;
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
            headerArray.append(QStringLiteral("%1: %2").arg(it.key(), it.value()));
        }
        options[QStringLiteral("header")] = headerArray;
    }
    
    params.append(options);

    sendRpcRequest(QStringLiteral("aria2.addUri"), params, [this, callback](const QJsonObject& response) {
        QString gid = response[QStringLiteral("result")].toString();
        emit downloadAdded(gid);
        if (callback) {
            callback(gid);
        }
    }, errorCallback);
}

void Aria2RpcClient::queryStatus(const QString& gid) {
    QJsonArray params;
    params.append(gid);

    sendRpcRequest(QStringLiteral("aria2.tellStatus"), params, [this, gid](const QJsonObject& response) {
        if (response.contains(QStringLiteral("result"))) {
            QJsonObject result = response[QStringLiteral("result")].toObject();
            qint64 completedLength = result[QStringLiteral("completedLength")].toString().toLongLong();
            qint64 totalLength = result[QStringLiteral("totalLength")].toString().toLongLong();
            qint64 downloadSpeed = result[QStringLiteral("downloadSpeed")].toString().toLongLong();
            QString status = result[QStringLiteral("status")].toString(); // "active", "waiting", "paused", "error", "complete", "removed"
            
            emit downloadProgress(gid, completedLength, totalLength, downloadSpeed, status);
        }
    });
}

void Aria2RpcClient::removeDownload(const QString& gid) {
    QJsonArray params;
    params.append(gid);
    sendRpcRequest(QStringLiteral("aria2.remove"), params, nullptr);
}
