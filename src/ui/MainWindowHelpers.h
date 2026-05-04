#ifndef MAINWINDOWHELPERS_H
#define MAINWINDOWHELPERS_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace MainWindowHelpers {

inline bool isHttpUrlArgument(const QString &arg)
{
    return !arg.startsWith("--") && (arg.startsWith("http://") || arg.startsWith("https://"));
}

inline QString directCliUrl()
{
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (isHttpUrlArgument(args[i])) {
            return args[i];
        }
    }
    return {};
}

inline bool hasNonInteractiveLaunchArgument()
{
    const QStringList args = QCoreApplication::arguments();
    return args.contains("--headless") || args.contains("--server") || !directCliUrl().isEmpty();
}

inline bool hasServerLaunchArgument()
{
    const QStringList args = QCoreApplication::arguments();
    return args.contains("--headless") || args.contains("--server");
}

inline bool isNonInteractiveRequest(const QVariantMap &options)
{
    return options.value("non_interactive", false).toBool();
}

inline void applyNonInteractiveDownloadDefaults(QVariantMap &options)
{
    options["non_interactive"] = true;
    options["override_archive"] = true;
    options["playlist_logic"] = "Download All (no prompt)";
    options["runtime_format_selected"] = true;
    options["download_sections_set"] = true;
}

}

#endif // MAINWINDOWHELPERS_H
