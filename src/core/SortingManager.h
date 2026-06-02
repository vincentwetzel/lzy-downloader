#ifndef SORTINGMANAGER_H
#define SORTINGMANAGER_H

#include <QObject>
#include <QVariantMap>
#include "ConfigManager.h"

class SortingManager : public QObject {
    Q_OBJECT
    friend class TestSortingManager;

public:
    explicit SortingManager(ConfigManager *configManager, QObject *parent = nullptr);

    QString getSortedDirectory(const QVariantMap &videoMetadata, const QVariantMap &downloadOptions);

private:
    QVariant metadataValueForField(const QString &field, const QVariantMap &metadata) const;
    QVariant metadataValueForKey(const QString &key, const QVariantMap &metadata) const;
    QString normalizedMetadataKey(const QString &key) const;
    QString sanitize(const QString &name);
    QString parseAndReplaceTokens(const QString &pattern, const QVariantMap &metadata);

    ConfigManager *m_configManager;
};

#endif // SORTINGMANAGER_H
