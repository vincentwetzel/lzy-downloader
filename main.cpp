#include "src/ui/MainWindow.h"
#include "src/utils/LogManager.h"
#include "src/utils/ExtractorJsonParser.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QStringList>
#include <QSystemSemaphore>
#include <QSharedMemory>
#include <QSslSocket>
#include <QSqlDatabase>
#include <QScopeGuard>

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

    QString instanceSuffix = startBackground ? QStringLiteral("_Server") : QStringLiteral("");

    QSystemSemaphore semaphore(QStringLiteral("%1Semaphore%2").arg(APP_NAME, instanceSuffix), 1);
    semaphore.acquire();
    auto semGuard = qScopeGuard([&semaphore]() {
        semaphore.release();
    });

    QSharedMemory sharedMemory(QStringLiteral("%1SingleInstance%2").arg(APP_NAME, instanceSuffix));
    // Attach and detach to recover the shared memory segment if the app crashed previously
    if (sharedMemory.attach()) {
        sharedMemory.detach();
    }
    if (!sharedMemory.create(1)) {
        return 0;
    }

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/app-icon")));

    a.setOrganizationName(QStringLiteral(""));
    a.setApplicationName(APP_NAME);

    QStringList libraryPaths = QApplication::libraryPaths();
    libraryPaths.prepend(a.applicationDirPath());
    libraryPaths.prepend(QDir(a.applicationDirPath()).filePath(QStringLiteral("plugins")));
    QApplication::setLibraryPaths(libraryPaths);

    LogManager::installHandler();

    qInfo() << "Qt library paths:" << QApplication::libraryPaths();
    qInfo() << "Available SQL drivers:" << QSqlDatabase::drivers();
    qInfo() << "Available TLS backends:" << QSslSocket::availableBackends();
    qInfo() << "Active TLS backend:" << QSslSocket::activeBackend();
    qInfo() << "Supports SSL:" << QSslSocket::supportsSsl();

    // Create the parser here so it can be passed down
    ExtractorJsonParser extractorJsonParser;

    MainWindow w(&extractorJsonParser);
    if (!startBackground) {
        w.show();
    }

    int result = a.exec();
    return result;
}
