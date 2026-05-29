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

int main(int argc, char *argv[]) {
    bool startBackground = false;
    for (int i = 1; i < argc; ++i) {
        QString arg(argv[i]);
        if (arg == "--background" || arg == "-b" || arg == "--headless" || arg == "--server") {
            startBackground = true;
            break;
        }
    }

    const QString APP_NAME = "LzyDownloader";

    QString instanceSuffix = startBackground ? "_Server" : "";

    QSystemSemaphore semaphore(APP_NAME + "Semaphore" + instanceSuffix, 1);
    semaphore.acquire();

    QSharedMemory sharedMemory(APP_NAME + "SingleInstance" + instanceSuffix);
    if (!sharedMemory.create(1)) {
        semaphore.release();
        return 0;
    }

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/app-icon"));

    a.setOrganizationName("");
    a.setApplicationName(APP_NAME);

    QStringList libraryPaths = QApplication::libraryPaths();
    libraryPaths.prepend(a.applicationDirPath());
    libraryPaths.prepend(QDir(a.applicationDirPath()).filePath("plugins"));
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
    semaphore.release();
    return result;
}
