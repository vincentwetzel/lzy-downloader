#include "src/ui/MainWindow.h"
#include "src/utils/LogManager.h"
#include "src/utils/ExtractorJsonParser.h"
#include "src/integration/BrowserNativeHostRegistration.h"
#include "src/core/RuntimeCoordinator.h"
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QUrl>
#include <QStringList>
#include <QSslSocket>
#include <QSqlDatabase>
#include <QSharedMemory>
#include <QEvent>
#include <QWidget>

#include <memory>
#include <vector>

namespace {

/**
 * Keep dialog-style popups from exposing a minimize button.  This is applied
 * at the application boundary so it also covers QMessageBox/QFileDialog
 * instances created through Qt's static convenience functions.
 */
class PopupWindowPolicy final : public QObject {
public:
    explicit PopupWindowPolicy(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((event->type() == QEvent::Polish || event->type() == QEvent::Show)
            && watched->isWidgetType()) {
            auto *widget = static_cast<QWidget *>(watched);
            if (widget->isWindow() && widget->windowType() == Qt::Dialog) {
                widget->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

/**
 * v1.2.47 and older releases used these shared-memory keys for single-instance
 * ownership. Keep them alive while the new local-socket coordinator owns the
 * process so an older still-running process cannot launch a second queue owner
 * during an upgrade.
 */
class LegacyInstanceGuard final {
public:
    bool acquire()
    {
        // GUI and server launches used separate keys before RuntimeCoordinator
        // unified their ownership. Claim both to cover either older process.
        static const QStringList keys = {
            QStringLiteral("LzyDownloaderSingleInstance"),
            QStringLiteral("LzyDownloaderSingleInstance_Server")
        };

        for (const QString &key : keys) {
            auto segment = std::make_unique<QSharedMemory>(key);
            // A crashed process can leave the native segment behind. Attaching
            // and detaching lets the operating system reclaim an unowned one;
            // an active process still causes create() to fail below.
            if (segment->attach()) {
                segment->detach();
            }
            if (!segment->create(1)) {
                return false;
            }
            m_segments.push_back(std::move(segment));
        }
        return true;
    }

private:
    std::vector<std::unique_ptr<QSharedMemory>> m_segments;
};

} // namespace

int main(int argc, char *argv[]) {
    bool startBackground = false;
    for (int i = 1; i < argc; ++i) {
        // Parse argument safely to avoid implicit char* conversion
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--background") || arg == QStringLiteral("-b") || 
            arg == QStringLiteral("--headless") || arg == QStringLiteral("--server")) {
            startBackground = true;
            break;
        }
    }

    const QString APP_NAME = QStringLiteral("LzyDownloader");

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/app-icon")));

    PopupWindowPolicy popupWindowPolicy(&a);
    a.installEventFilter(&popupWindowPolicy);

    a.setOrganizationName(QStringLiteral(""));
    a.setApplicationName(APP_NAME);

    QStringList libraryPaths = QApplication::libraryPaths();
    libraryPaths.prepend(a.applicationDirPath());
    libraryPaths.prepend(QDir(a.applicationDirPath()).filePath(QStringLiteral("plugins")));
    QApplication::setLibraryPaths(libraryPaths);

    QString directUrl;
    QString directType = QStringLiteral("video");
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QStringLiteral("--audio")) {
            directType = QStringLiteral("audio");
        } else if (argument == QStringLiteral("--gallery")) {
            directType = QStringLiteral("gallery");
        } else if (!argument.startsWith(QLatin1String("--"))
                   && QUrl(argument).isValid()
                   && (argument.startsWith(QLatin1String("http://"))
                       || argument.startsWith(QLatin1String("https://")))) {
            directUrl = argument;
        }
    }

    QString coordinatorCommand = startBackground ? QStringLiteral("ensure-api")
                                                  : QStringLiteral("show-ui");
    if (!directUrl.isEmpty()) {
        const QByteArray encodedUrl = directUrl.toUtf8().toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        coordinatorCommand = QStringLiteral("enqueue:%1:%2")
            .arg(directType, QString::fromLatin1(encodedUrl));
    }

    RuntimeCoordinator coordinator(APP_NAME + QStringLiteral("CoordinatorV1"));
    const RuntimeCoordinator::StartResult coordinatorResult = coordinator.startOrNotify(coordinatorCommand);
    if (coordinatorResult == RuntimeCoordinator::StartResult::ClientNotified) {
        return 0;
    }
    if (coordinatorResult != RuntimeCoordinator::StartResult::Owner) {
        qCritical() << "Could not create or contact the LzyDownloader coordinator.";
        return 1;
    }

    LegacyInstanceGuard legacyInstanceGuard;
    if (!legacyInstanceGuard.acquire()) {
        qInfo() << "An older LzyDownloader instance is already running; forwarding is unavailable. Exiting.";
        return 0;
    }

    BrowserNativeHostRegistration::registerHostIfConfigured();

    LogManager::installHandler();

    qInfo() << "Qt library paths:" << QApplication::libraryPaths();
    qInfo() << "Available SQL drivers:" << QSqlDatabase::drivers();
    qInfo() << "Available TLS backends:" << QSslSocket::availableBackends();
    qInfo() << "Active TLS backend:" << QSslSocket::activeBackend();
    qInfo() << "Supports SSL:" << QSslSocket::supportsSsl();

    // Create the parser here so it can be passed down
    ExtractorJsonParser extractorJsonParser;

    MainWindow w(&extractorJsonParser);
    QObject::connect(&coordinator, &RuntimeCoordinator::commandReceived, &w,
                     [&w](const QString &command) {
        if (command == QStringLiteral("show-ui")) {
            w.activateCoordinatorUi();
        } else if (command == QStringLiteral("ensure-api")) {
            w.ensureCoordinatorApi();
        } else if (command.startsWith(QStringLiteral("enqueue:"))) {
            const QStringList parts = command.split(QLatin1Char(':'), Qt::KeepEmptyParts);
            if (parts.size() != 3) {
                return;
            }
            const QByteArray decodedUrl = QByteArray::fromBase64(
                parts.at(2).toLatin1(), QByteArray::Base64UrlEncoding);
            const QUrl url(QString::fromUtf8(decodedUrl));
            if (!url.isValid() || (url.scheme() != QStringLiteral("http")
                                   && url.scheme() != QStringLiteral("https"))) {
                return;
            }
            w.ensureCoordinatorApi();
            w.enqueueCoordinatorDownload(url.toString(), parts.at(1));
        }
    });
    coordinator.dispatchPendingCommands();
    if (!startBackground) {
        w.show();
    }

    int result = a.exec();
    return result;
}
