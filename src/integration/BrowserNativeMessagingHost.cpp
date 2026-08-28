#include "BrowserCookieFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringView>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <cstdio>
#include <limits>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {

constexpr int kProtocolVersion = 1;
constexpr qsizetype kMaxMessageBytes = 1024 * 1024;
constexpr qsizetype kMaxUrlBytes = 8192;
constexpr qsizetype kMaxJobIdBytes = 128;
constexpr qsizetype kMaxRequestIdBytes = 128;
constexpr qsizetype kMaxStatusMessageBytes = 1000;
constexpr int kHttpTimeoutMs = 5000;
constexpr int kServerStartupTimeoutMs = 20000;

struct HttpResult {
    bool completed = false;
    int statusCode = 0;
    QByteArray body;
    QString error;
};

void writeDiagnostic(const QString &message)
{
    const QByteArray bytes = (message + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fflush(stderr);
}

bool readExact(char *destination, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        const size_t readCount = std::fread(destination + offset, 1, length - offset, stdin);
        if (readCount == 0) {
            return false;
        }
        offset += readCount;
    }
    return true;
}

bool readMessage(QByteArray &message)
{
    unsigned char lengthBytes[4] = {};
    if (!readExact(reinterpret_cast<char *>(lengthBytes), sizeof(lengthBytes))) {
        return false;
    }

    const quint32 length = static_cast<quint32>(lengthBytes[0])
        | (static_cast<quint32>(lengthBytes[1]) << 8)
        | (static_cast<quint32>(lengthBytes[2]) << 16)
        | (static_cast<quint32>(lengthBytes[3]) << 24);
    if (length == 0 || length > static_cast<quint32>(kMaxMessageBytes)) {
        return false;
    }

    message.resize(static_cast<qsizetype>(length));
    return readExact(message.data(), static_cast<size_t>(length));
}

bool writeMessage(const QJsonObject &response)
{
    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > kMaxMessageBytes
        || body.size() > static_cast<qsizetype>(std::numeric_limits<quint32>::max())) {
        return false;
    }

    const quint32 length = static_cast<quint32>(body.size());
    const unsigned char lengthBytes[4] = {
        static_cast<unsigned char>(length & 0xff),
        static_cast<unsigned char>((length >> 8) & 0xff),
        static_cast<unsigned char>((length >> 16) & 0xff),
        static_cast<unsigned char>((length >> 24) & 0xff)
    };
    if (std::fwrite(lengthBytes, 1, sizeof(lengthBytes), stdout) != sizeof(lengthBytes)
        || std::fwrite(body.constData(), 1, static_cast<size_t>(body.size()), stdout)
            != static_cast<size_t>(body.size())) {
        return false;
    }
    std::fflush(stdout);
    return true;
}

QJsonObject errorResponse(const QString &requestId, const QString &code, const QString &message)
{
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;

    QJsonObject response;
    response[QStringLiteral("protocol")] = kProtocolVersion;
    response[QStringLiteral("request_id")] = requestId;
    response[QStringLiteral("ok")] = false;
    response[QStringLiteral("error")] = error;
    return response;
}

QJsonObject successResponse(const QString &requestId, const QJsonObject &payload)
{
    QJsonObject response;
    response[QStringLiteral("protocol")] = kProtocolVersion;
    response[QStringLiteral("request_id")] = requestId;
    response[QStringLiteral("ok")] = true;
    response[QStringLiteral("payload")] = payload;
    return response;
}

QString apiTokenPath()
{
    const QString localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (localData.isEmpty()) {
        return {};
    }
    return QDir(localData).filePath(QStringLiteral("Server/api_token.txt"));
}

QString readApiToken()
{
    const QString tokenPath = apiTokenPath();
    if (tokenPath.isEmpty()) {
        return {};
    }
    QFile tokenFile(tokenPath);
    if (!tokenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(tokenFile.readAll()).trimmed();
}

HttpResult requestApi(const QString &method, const QUrl &url, const QString &token,
                      const QByteArray &body = {}, int timeoutMs = kHttpTimeoutMs)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8());
    request.setRawHeader(QByteArrayLiteral("Host"), QByteArrayLiteral("127.0.0.1:8765"));
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }

    QNetworkReply *reply = nullptr;
    if (method == QStringLiteral("GET")) {
        reply = manager.get(request);
    } else if (method == QStringLiteral("POST")) {
        reply = manager.post(request, body);
    } else {
        return {false, 0, {}, QStringLiteral("Unsupported local API method.")};
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return {false, 0, {}, QStringLiteral("The local LzyDownloader server timed out.")};
    }

    const HttpResult result{
        reply->error() == QNetworkReply::NoError,
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
        reply->readAll(),
        reply->error() == QNetworkReply::NoError ? QString() : reply->errorString()
    };
    reply->deleteLater();
    return result;
}

bool serverIsHealthy(const QString &token)
{
    if (token.isEmpty()) {
        return false;
    }
    const HttpResult result = requestApi(QStringLiteral("GET"),
                                         QUrl(QStringLiteral("http://127.0.0.1:8765/status")), token,
                                         {}, 1500);
    return result.completed && result.statusCode == 200;
}

QString desktopExecutablePath()
{
#ifdef Q_OS_WIN
    constexpr auto executableName = "LzyDownloader.exe";
#else
    constexpr auto executableName = "LzyDownloader";
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(executableName));
}

bool ensureServer(QString &token, QString &error)
{
    token = readApiToken();
    if (serverIsHealthy(token)) {
        return true;
    }

    const QString executable = desktopExecutablePath();
    if (!QFileInfo(executable).isFile()) {
        error = QStringLiteral("LzyDownloader is not installed in the companion's application directory.");
        return false;
    }

    qint64 processId = 0;
    if (!QProcess::startDetached(executable,
                                 {QStringLiteral("--server"), QStringLiteral("--exit-after")},
                                 QCoreApplication::applicationDirPath(), &processId)) {
        error = QStringLiteral("LzyDownloader could not be started in server mode.");
        return false;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < kServerStartupTimeoutMs) {
        token = readApiToken();
        if (serverIsHealthy(token)) {
            return true;
        }

        QEventLoop waitLoop;
        QTimer::singleShot(250, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
    }

    error = QStringLiteral("LzyDownloader did not become ready before the startup timeout.");
    return false;
}

bool isSafeJobId(const QString &jobId)
{
    return !jobId.isEmpty() && jobId.toUtf8().size() <= kMaxJobIdBytes
        && !jobId.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
}

bool isSafeClientId(const QString &clientId)
{
    static const QRegularExpression clientIdRe(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    return clientIdRe.match(clientId).hasMatch() && clientId.toUtf8().size() <= 128;
}

QJsonObject sanitizedJob(const QJsonObject &job, const QString &jobId)
{
    QJsonObject result;
    result[QStringLiteral("job_id")] = jobId;
    result[QStringLiteral("status")] = job.value(QStringLiteral("status")).toString().left(100);
    if (job.value(QStringLiteral("progress")).isDouble()) {
        result[QStringLiteral("progress")] = qBound(0.0, job.value(QStringLiteral("progress")).toDouble(), 100.0);
    } else {
        result[QStringLiteral("progress")] = -1;
    }
    const QString message = job.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        result[QStringLiteral("message")] = message.left(kMaxStatusMessageBytes);
    }
    return result;
}

QJsonObject handleRequest(const QJsonObject &request)
{
    QString requestId = request.value(QStringLiteral("request_id")).toString().trimmed();
    if (requestId.toUtf8().size() > kMaxRequestIdBytes || requestId.contains(QChar::Null)
        || requestId.contains(QRegularExpression(QStringLiteral("[\\r\\n]")))) {
        requestId.clear();
    }

    if (request.value(QStringLiteral("protocol")).toInt() != kProtocolVersion) {
        return errorResponse(requestId, QStringLiteral("UNSUPPORTED_PROTOCOL"),
                             QStringLiteral("This browser companion uses an unsupported protocol version."));
    }

    const QString operation = request.value(QStringLiteral("operation")).toString();
    const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
    if (operation == QStringLiteral("ping")) {
        QJsonObject result;
        result[QStringLiteral("host_version")] = QStringLiteral("1");
        result[QStringLiteral("protocol")] = kProtocolVersion;
        result[QStringLiteral("paired")] = true;
        result[QStringLiteral("desktop_available")] = QFileInfo(desktopExecutablePath()).isFile();
        return successResponse(requestId, result);
    }

    if (operation != QStringLiteral("enqueue") && operation != QStringLiteral("status")
        && operation != QStringLiteral("cancel")) {
        return errorResponse(requestId, QStringLiteral("INVALID_OPERATION"),
                             QStringLiteral("The requested browser companion operation is not supported."));
    }

    const QString clientId = payload.value(QStringLiteral("client_id")).toString().trimmed();
    if (!isSafeClientId(clientId)) {
        return errorResponse(requestId, QStringLiteral("INVALID_CLIENT"),
                             QStringLiteral("The browser companion client identity is invalid."));
    }

    QString token;
    QString startupError;
    if (!ensureServer(token, startupError)) {
        return errorResponse(requestId, QStringLiteral("DESKTOP_UNAVAILABLE"), startupError);
    }

    if (operation == QStringLiteral("enqueue")) {
        const QString urlText = payload.value(QStringLiteral("url")).toString().trimmed();
        const QUrl url(urlText, QUrl::StrictMode);
        if (urlText.toUtf8().size() > kMaxUrlBytes || !url.isValid() || url.host().isEmpty()
            || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
            return errorResponse(requestId, QStringLiteral("INVALID_URL"),
                                 QStringLiteral("Only valid HTTP and HTTPS URLs can be queued."));
        }

        const QString type = payload.value(QStringLiteral("type")).toString(QStringLiteral("video"));
        if (type != QStringLiteral("video") && type != QStringLiteral("audio")) {
            return errorResponse(requestId, QStringLiteral("UNSUPPORTED_TYPE"),
                                 QStringLiteral("The browser companion supports video and audio downloads."));
        }

        QString cookieFile;
        if (payload.contains(QStringLiteral("cookies"))) {
            if (!payload.value(QStringLiteral("cookies")).isArray()) {
                return errorResponse(requestId, QStringLiteral("INVALID_COOKIES"),
                                     QStringLiteral("The browser returned an invalid cookie bundle."));
            }
            const BrowserCookieFile::CreateResult cookieResult =
                BrowserCookieFile::createForUrl(payload.value(QStringLiteral("cookies")).toArray(), url);
            if (!cookieResult.success) {
                return errorResponse(requestId, QStringLiteral("INVALID_COOKIES"), cookieResult.error);
            }
            cookieFile = cookieResult.path;
        }

        const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject body;
        body[QStringLiteral("url")] = urlText;
        body[QStringLiteral("type")] = type;
        body[QStringLiteral("id")] = jobId;
        body[QStringLiteral("client_id")] = clientId;
        body[QStringLiteral("override_archive")] = true;
        if (!cookieFile.isEmpty()) {
            body[QStringLiteral("cookie_file")] = cookieFile;
        }
        const HttpResult result = requestApi(QStringLiteral("POST"),
                                             QUrl(QStringLiteral("http://127.0.0.1:8765/enqueue")), token,
                                             QJsonDocument(body).toJson(QJsonDocument::Compact));
        if (!result.completed || result.statusCode < 200 || result.statusCode >= 300) {
            BrowserCookieFile::remove(cookieFile);
            return errorResponse(requestId, QStringLiteral("DESKTOP_REJECTED"),
                                 QStringLiteral("LzyDownloader rejected the download request."));
        }

        QJsonObject responsePayload;
        responsePayload[QStringLiteral("job_id")] = jobId;
        responsePayload[QStringLiteral("status")] = QStringLiteral("Queued");
        return successResponse(requestId, responsePayload);
    }

    const QString jobId = payload.value(QStringLiteral("job_id")).toString().trimmed();
    if (!isSafeJobId(jobId)) {
        return errorResponse(requestId, QStringLiteral("UNKNOWN_JOB"),
                             QStringLiteral("The requested download job is not available."));
    }

    if (operation == QStringLiteral("cancel")) {
        QJsonObject body;
        body[QStringLiteral("job_id")] = jobId;
        body[QStringLiteral("client_id")] = clientId;
        const HttpResult result = requestApi(QStringLiteral("POST"),
                                             QUrl(QStringLiteral("http://127.0.0.1:8765/cancel")), token,
                                             QJsonDocument(body).toJson(QJsonDocument::Compact));
        if (result.statusCode == 404) {
            return errorResponse(requestId, QStringLiteral("UNKNOWN_JOB"),
                                 QStringLiteral("The requested download job is not available."));
        }
        if (!result.completed || result.statusCode < 200 || result.statusCode >= 300) {
            return errorResponse(requestId, QStringLiteral("DESKTOP_REJECTED"),
                                 QStringLiteral("LzyDownloader rejected the cancellation request."));
        }
        QJsonObject responsePayload;
        responsePayload[QStringLiteral("job_id")] = jobId;
        responsePayload[QStringLiteral("status")] = QStringLiteral("Cancellation requested");
        return successResponse(requestId, responsePayload);
    }

    if (operation == QStringLiteral("status")) {
        QUrl statusUrl(QStringLiteral("http://127.0.0.1:8765/status"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("client_id"), clientId);
        statusUrl.setQuery(query);
        const HttpResult result = requestApi(QStringLiteral("GET"), statusUrl, token);
        if (!result.completed || result.statusCode != 200) {
            return errorResponse(requestId, QStringLiteral("DESKTOP_REJECTED"),
                                 QStringLiteral("LzyDownloader status could not be read."));
        }
        const QJsonDocument document = QJsonDocument::fromJson(result.body);
        const QJsonArray jobs = document.object().value(QStringLiteral("jobs")).toArray();
        for (const QJsonValue &value : jobs) {
            const QJsonObject job = value.toObject();
            if (job.value(QStringLiteral("id")).toString() == jobId
                || job.value(QStringLiteral("job_id")).toString() == jobId) {
                return successResponse(requestId, sanitizedJob(job, jobId));
            }
        }
        return errorResponse(requestId, QStringLiteral("UNKNOWN_JOB"),
                             QStringLiteral("The requested download job is not available."));
    }

    return errorResponse(requestId, QStringLiteral("INVALID_OPERATION"),
                         QStringLiteral("The requested browser companion operation is not supported."));
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    QCoreApplication application(argc, argv);
    application.setOrganizationName(QString());
    application.setApplicationName(QStringLiteral("LzyDownloader"));

    QByteArray message;
    while (readMessage(message)) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
        QJsonObject response;
        if (!document.isObject()) {
            response = errorResponse(QString(), QStringLiteral("INVALID_MESSAGE"),
                                     QStringLiteral("The browser companion received invalid JSON."));
        } else {
            response = handleRequest(document.object());
        }

        if (!writeMessage(response)) {
            writeDiagnostic(QStringLiteral("Unable to write the native messaging response."));
            return 1;
        }
    }

    return 0;
}
