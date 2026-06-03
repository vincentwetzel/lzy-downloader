#pragma once

#include <QDialog>
#include <QList>
#include <QVariantMap>

class QLineEdit;
class QListWidget;

class PlaylistRangeDialog : public QDialog {
    Q_OBJECT
public:
    explicit PlaylistRangeDialog(const QList<QVariantMap> &playlistItems, QWidget *parent = nullptr);
    ~PlaylistRangeDialog() override;

    [[nodiscard]] QList<QVariantMap> getSelectedItems() const;

private slots:
    void onRangeTextChanged(const QString &text);
    void onItemChanged();
    void selectAll();
    void selectNone();

private:
    void setupUi();
    void syncListFromRangeText(const QString &text);
    void syncRangeTextFromList();

    QList<QVariantMap> m_playlistItems;
    QLineEdit *m_rangeEdit;
    QListWidget *m_listWidget;
    
    // Prevents recursive infinite loops between the text box and checkboxes
    bool m_isSyncing = false;
};