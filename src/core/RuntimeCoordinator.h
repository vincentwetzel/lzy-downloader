#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

/** One process owns the queue, workers, persistence, and Local API. */
class RuntimeCoordinator final : public QObject {
    Q_OBJECT

public:
    enum class StartResult { Owner, ClientNotified, Unavailable };

    explicit RuntimeCoordinator(const QString &serverName, QObject *parent = nullptr);
    ~RuntimeCoordinator() override;

    StartResult startOrNotify(const QString &command);
    void dispatchPendingCommands();

signals:
    void commandReceived(const QString &command);

private slots:
    void acceptConnection();

private:
    enum class NotifyResult { Notified, Sent, NoServer, Failed };

    bool listen();
    NotifyResult notifyOwner(const QString &command) const;
    void handleCommand(const QString &command);

    QString m_serverName;
    QLocalServer *m_server = nullptr;
    QStringList m_pendingCommands;
    bool m_isOwner = false;
};
