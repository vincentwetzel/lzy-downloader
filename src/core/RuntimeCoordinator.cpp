#include "RuntimeCoordinator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <QRegularExpression>

namespace {
constexpr auto kShowUi = "show-ui";
constexpr auto kEnsureApi = "ensure-api";
constexpr qsizetype kMaxCommandBytes = 16384;

bool isAllowedCommand(const QString &command)
{
    if (command == QLatin1String(kShowUi) || command == QLatin1String(kEnsureApi)) {
        return true;
    }
    static const QRegularExpression enqueueCommand(
        QStringLiteral("^enqueue:(?:video|audio|gallery):[A-Za-z0-9_-]{1,16000}$"));
    return enqueueCommand.match(command).hasMatch();
}
}

RuntimeCoordinator::RuntimeCoordinator(const QString &serverName, QObject *parent)
    : QObject(parent), m_serverName(serverName), m_server(new QLocalServer(this))
{
#ifndef Q_OS_WIN
    // Unix sockets need explicit owner-only permissions. Windows named pipes
    // inherit the process DACL; forcing UserAccessOption can fail under some
    // valid user-token configurations with "Access is denied".
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
#endif
    connect(m_server, &QLocalServer::newConnection, this, &RuntimeCoordinator::acceptConnection);
}

RuntimeCoordinator::~RuntimeCoordinator()
{
    if (m_isOwner) {
        m_server->close();
        QLocalServer::removeServer(m_serverName);
    }
}

RuntimeCoordinator::StartResult RuntimeCoordinator::startOrNotify(const QString &command)
{
    if (!isAllowedCommand(command)) {
        return StartResult::Unavailable;
    }
    if (listen()) {
        return StartResult::Owner;
    }
    const NotifyResult notifyResult = notifyOwner(command);
    if (notifyResult == NotifyResult::Notified || notifyResult == NotifyResult::Sent) {
        return StartResult::ClientNotified;
    }
    if (notifyResult != NotifyResult::NoServer) {
        return StartResult::Unavailable;
    }

    // Recover only when Qt reports that no server exists. A busy coordinator
    // must never be replaced merely because it did not answer quickly.
    QLocalServer::removeServer(m_serverName);
    return listen() ? StartResult::Owner : StartResult::Unavailable;
}

void RuntimeCoordinator::dispatchPendingCommands()
{
    const QStringList commands = m_pendingCommands;
    m_pendingCommands.clear();
    for (const QString &command : commands) {
        emit commandReceived(command);
    }
}

bool RuntimeCoordinator::listen()
{
    m_isOwner = m_server->listen(m_serverName);
    if (!m_isOwner) {
        qWarning() << "Runtime coordinator listen failed:" << m_server->errorString();
    }
    return m_isOwner;
}

RuntimeCoordinator::NotifyResult RuntimeCoordinator::notifyOwner(const QString &command) const
{
    const QByteArray payload = command.toUtf8() + '\n';
    bool sawNoServer = false;
    bool sawOtherFailure = false;
    constexpr int kNotificationAttempts = 10;
    for (int attempt = 0; attempt < kNotificationAttempts; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(m_serverName, QIODevice::ReadWrite);
        QElapsedTimer connectTimer;
        connectTimer.start();
        while (socket.state() == QLocalSocket::ConnectingState && connectTimer.elapsed() < 750) {
            // The owner can live in this same thread (for example during
            // startup tests). Process its newConnection/readyRead events so
            // the synchronous client notification cannot deadlock the owner.
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (socket.state() == QLocalSocket::ConnectingState) {
                socket.waitForConnected(1);
            }
        }
        if (socket.state() != QLocalSocket::ConnectedState) {
            if (socket.error() == QLocalSocket::ServerNotFoundError) {
                sawNoServer = true;
            } else {
                sawOtherFailure = true;
            }
            if (attempt + 1 < kNotificationAttempts) {
                QThread::msleep(25);
            }
            continue;
        }
        if (socket.write(payload) != payload.size()) {
            sawOtherFailure = true;
            if (attempt + 1 < kNotificationAttempts) {
                QThread::msleep(25);
            }
            continue;
        }
        socket.flush();
        QElapsedTimer writeTimer;
        writeTimer.start();
        while (socket.bytesToWrite() > 0 && writeTimer.elapsed() < 750) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (socket.bytesToWrite() > 0) {
                if (!socket.waitForBytesWritten(1)) {
                    break;
                }
            }
        }
        QByteArray acknowledgment;
        QElapsedTimer acknowledgmentTimer;
        acknowledgmentTimer.start();
        while (acknowledgmentTimer.elapsed() < 750) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            acknowledgment += socket.readAll();
            if (acknowledgment.indexOf('\n') >= 0) {
                break;
            }
            if (socket.state() != QLocalSocket::ConnectedState
                || !socket.waitForReadyRead(1)) {
                continue;
            }
        }
        // Once the complete command was accepted by the local socket, do not
        // retry it: the owner may have processed it even if the acknowledgment
        // was lost during a Windows named-pipe close.
        socket.disconnectFromServer();
        if (acknowledgment.startsWith(QByteArrayLiteral("ok\n"))) {
            return NotifyResult::Notified;
        }
        return NotifyResult::Sent;
    }
    return sawNoServer && !sawOtherFailure ? NotifyResult::NoServer : NotifyResult::Failed;
}

void RuntimeCoordinator::acceptConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            QByteArray bytes = socket->property("runtimeCoordinatorBuffer").toByteArray();
            bytes += socket->readAll();
            if (bytes.size() > kMaxCommandBytes) {
                socket->disconnectFromServer();
                return;
            }
            const int newline = bytes.indexOf('\n');
            if (newline < 0) {
                socket->setProperty("runtimeCoordinatorBuffer", bytes);
                return;
            }
            handleCommand(QString::fromUtf8(bytes.left(newline)).trimmed());
            const QByteArray acknowledgment = QByteArrayLiteral("ok\n");
            if (socket->write(acknowledgment) == acknowledgment.size()) {
                socket->flush();
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void RuntimeCoordinator::handleCommand(const QString &command)
{
    if (!isAllowedCommand(command)) {
        return;
    }
    m_pendingCommands.append(command);
    QTimer::singleShot(0, this, &RuntimeCoordinator::dispatchPendingCommands);
}
