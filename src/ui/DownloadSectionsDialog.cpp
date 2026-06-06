#include "DownloadSectionsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QComboBox>
#include <QStackedWidget>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QRegularExpression>
#include <utility>

DownloadSectionsDialog::DownloadSectionsDialog(const QVariantMap &infoDict, QWidget *parent)
    : QDialog(parent)
{
    if (infoDict.contains(QStringLiteral("chapters")) && infoDict[QStringLiteral("chapters")].typeId() == QMetaType::QVariantList) {
        m_chapters = infoDict[QStringLiteral("chapters")].toList();
    }
    setupUi();
    addSectionWidget(); // Start with one section by default
}

DownloadSectionsDialog::~DownloadSectionsDialog() = default;

void DownloadSectionsDialog::setupUi()
{
    setWindowTitle(tr("Download Sections"));
    setMinimumSize(600, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *descriptionLabel = new QLabel(tr(
        "Define one or more sections to download. Each section can be a time range or a chapter.\n"
        "yt-dlp will download only these parts of the video. For time ranges, you can leave a field blank "
        "to download from the beginning or to the very end."
        "\n\nYou can disable Download Sections in Advanced Settings -> Download Flow"), this);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setToolTip(tr("Use the 'Add Section' button to define multiple parts to download."));
    mainLayout->addWidget(descriptionLabel);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::StyledPanel);

    QWidget *scrollWidget = new QWidget();
    m_sectionsLayout = new QVBoxLayout(scrollWidget);
    m_sectionsLayout->setSpacing(10);
    m_sectionsLayout->addStretch();

    scrollArea->setWidget(scrollWidget);
    mainLayout->addWidget(scrollArea);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *addSectionButton = new QPushButton(tr("Add Section"), this);
    addSectionButton->setToolTip(tr("Add another time range or chapter to download."));

    QPushButton *okButton = new QPushButton(tr("OK"), this);
    okButton->setDefault(true);
    QPushButton *cancelButton = new QPushButton(tr("Cancel"), this);

    buttonLayout->addWidget(addSectionButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(buttonLayout);

    connect(addSectionButton, &QPushButton::clicked, this, &DownloadSectionsDialog::addSectionWidget);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void DownloadSectionsDialog::addSectionWidget()
{
    QWidget *sectionWidget = createSectionWidget();
    m_sectionsLayout->insertWidget(m_sectionsLayout->count() - 1, sectionWidget);
}

void DownloadSectionsDialog::removeSectionWidget(QWidget *sectionWidget)
{
    m_sectionsLayout->removeWidget(sectionWidget);
    sectionWidget->deleteLater();
}

QWidget* DownloadSectionsDialog::createSectionWidget()
{
    QFrame *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);

    QHBoxLayout *layout = new QHBoxLayout(frame);

    QComboBox *typeCombo = new QComboBox(frame);
    typeCombo->setObjectName(QStringLiteral("typeCombo"));
    typeCombo->setToolTip(tr("Choose whether to define the section by time or by chapter name."));
    typeCombo->addItem(tr("Time Range"), QStringLiteral("Time Range"));
    typeCombo->addItem(tr("Chapter"), QStringLiteral("Chapter"));
    typeCombo->setFixedWidth(120);
    if (m_chapters.isEmpty()) {
        if (auto *model = qobject_cast<QStandardItemModel*>(typeCombo->model())) {
            model->item(1)->setEnabled(false);
        }
        typeCombo->setToolTip(tr("Choose whether to define the section by time. Chapters not available for this video."));
    }

    QStackedWidget *stack = new QStackedWidget(frame);
    stack->setObjectName(QStringLiteral("stack"));

    // Time Range Widget
    QWidget *timeWidget = new QWidget(frame);
    QHBoxLayout *timeLayout = new QHBoxLayout(timeWidget);
    timeLayout->setContentsMargins(0, 0, 0, 0);
    QLineEdit *startEdit = new QLineEdit(timeWidget);
    startEdit->setObjectName(QStringLiteral("startEdit"));
    startEdit->setPlaceholderText(QStringLiteral("HH:MM:SS"));
    startEdit->setToolTip(tr("Enter the start time (HH:MM:SS). Leave blank to start from the beginning."));
    QLabel *toLabel = new QLabel(tr(" to "), timeWidget);
    QLineEdit *endEdit = new QLineEdit(timeWidget);
    endEdit->setObjectName(QStringLiteral("endEdit"));
    endEdit->setPlaceholderText(QStringLiteral("HH:MM:SS"));
    endEdit->setToolTip(tr("Enter the end time (HH:MM:SS). Leave blank to download to the end."));
    timeLayout->addWidget(new QLabel(tr("From:"), timeWidget));
    timeLayout->addWidget(startEdit);
    timeLayout->addWidget(toLabel);
    timeLayout->addWidget(endEdit);
    timeLayout->addStretch();
    stack->addWidget(timeWidget);

    // Chapter Widget
    QWidget *chapterWidget = new QWidget(frame);
    QHBoxLayout *chapterLayout = new QHBoxLayout(chapterWidget);
    chapterLayout->setContentsMargins(0, 0, 0, 0);
    QComboBox *chapterCombo = new QComboBox(chapterWidget);
    chapterCombo->setObjectName(QStringLiteral("chapterCombo"));
    chapterCombo->setToolTip(tr("Select a chapter to download."));
    for (const QVariant &chapter : std::as_const(m_chapters)) {
        QVariantMap chapterMap = chapter.toMap();
        if (chapterMap.contains(QStringLiteral("title"))) {
            chapterCombo->addItem(chapterMap[QStringLiteral("title")].toString());
        }
    }
    chapterLayout->addWidget(new QLabel(tr("Chapter:"), chapterWidget));
    chapterLayout->addWidget(chapterCombo);
    chapterLayout->addStretch();
    stack->addWidget(chapterWidget);

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), stack, &QStackedWidget::setCurrentIndex);

    QPushButton *removeButton = new QPushButton(tr("Remove"), frame);
    removeButton->setToolTip(tr("Remove this section from the download."));
    connect(removeButton, &QPushButton::clicked, this, [this, frame] {
        removeSectionWidget(frame);
    });

    layout->addWidget(typeCombo);
    layout->addWidget(stack);
    layout->addWidget(removeButton);

    return frame;
}

QString DownloadSectionsDialog::getFilenameLabel() const
{
    auto sanitizeLabelPart = [](QString value) {
        value = value.trimmed();
        if (value.isEmpty()) {
            return QString();
        }

        value.replace(QLatin1Char(':'), QLatin1Char('-'));
        value.replace(QLatin1Char('/'), QLatin1Char('-'));
        value.replace(QLatin1Char('\\'), QLatin1Char('-'));
        value.replace(QLatin1Char(' '), QLatin1Char('_'));
        
        static const QRegularExpression illegalCharsRe(QStringLiteral(R"([<>:"/\\|?*])"));
        static const QRegularExpression multipleUnderscoresRe(QStringLiteral(R"(_{2,})"));
        static const QRegularExpression multipleDashesRe(QStringLiteral(R"(-{2,})"));
        value.remove(illegalCharsRe);
        value.replace(multipleUnderscoresRe, QStringLiteral("_"));
        value.replace(multipleDashesRe, QStringLiteral("-"));
        return value.left(40);
    };

    QStringList labels;
    for (int i = 0; i < m_sectionsLayout->count(); ++i) {
        QWidget *widget = m_sectionsLayout->itemAt(i)->widget();
        if (!widget) continue;

        QComboBox *typeCombo = widget->findChild<QComboBox*>(QStringLiteral("typeCombo"));
        QStackedWidget *stack = widget->findChild<QStackedWidget*>(QStringLiteral("stack"));
        if (!typeCombo || !stack) continue;

        if (typeCombo->currentData().toString() == QStringLiteral("Time Range")) {
            QLineEdit *startEdit = stack->widget(0)->findChild<QLineEdit*>(QStringLiteral("startEdit"));
            QLineEdit *endEdit = stack->widget(0)->findChild<QLineEdit*>(QStringLiteral("endEdit"));
            if (!startEdit || !endEdit) continue;

            QString startTime = sanitizeLabelPart(startEdit->text());
            QString endTime = sanitizeLabelPart(endEdit->text());
            if (startTime.isEmpty() && endTime.isEmpty()) {
                continue;
            }

            if (startTime.isEmpty()) startTime = QStringLiteral("start");
            if (endTime.isEmpty()) endTime = QStringLiteral("end");
            labels.append(QStringLiteral("%1_to_%2").arg(startTime, endTime));
        } else if (typeCombo->currentData().toString() == QStringLiteral("Chapter")) {
            QComboBox *chapterCombo = stack->widget(1)->findChild<QComboBox*>(QStringLiteral("chapterCombo"));
            if (!chapterCombo || chapterCombo->count() == 0) continue;

            QString chapterName = sanitizeLabelPart(chapterCombo->currentText());
            if (!chapterName.isEmpty()) {
                labels.append(QStringLiteral("chapter_%1").arg(chapterName));
            }
        }
    }

    if (labels.isEmpty()) {
        return QString();
    }

    if (labels.size() == 1) {
        return labels.first();
    }

    QString label = labels.mid(0, 3).join(QStringLiteral("__"));
    if (labels.size() > 3) {
        label = QStringLiteral("%1__plus_%2_more").arg(label).arg(labels.size() - 3);
    }
    return label.left(90);
}
QString DownloadSectionsDialog::getSectionsString() const
{
    QStringList sectionStrings;
    for (int i = 0; i < m_sectionsLayout->count(); ++i) {
        QWidget *widget = m_sectionsLayout->itemAt(i)->widget();
        if (!widget) continue;

        QComboBox *typeCombo = widget->findChild<QComboBox*>(QStringLiteral("typeCombo"));
        QStackedWidget *stack = widget->findChild<QStackedWidget*>(QStringLiteral("stack"));
        if (!typeCombo || !stack) continue;

        if (typeCombo->currentData().toString() == QStringLiteral("Time Range")) {
            QLineEdit *startEdit = stack->widget(0)->findChild<QLineEdit*>(QStringLiteral("startEdit"));
            QLineEdit *endEdit = stack->widget(0)->findChild<QLineEdit*>(QStringLiteral("endEdit"));
            if (startEdit && endEdit) {
                QString startTime = startEdit->text().trimmed();
                QString endTime = endEdit->text().trimmed();

                // Don't add empty sections
                if (startTime.isEmpty() && endTime.isEmpty()) {
                    continue;
                }
                sectionStrings.append(QStringLiteral("*%1-%2").arg(startTime, endTime)); // yt-dlp handles empty start/end correctly
            }
        } else if (typeCombo->currentData().toString() == QStringLiteral("Chapter")) {
            QComboBox *chapterCombo = stack->widget(1)->findChild<QComboBox*>(QStringLiteral("chapterCombo"));
            if (chapterCombo && chapterCombo->count() > 0) {
                QString chapterName = chapterCombo->currentText();
                // yt-dlp is sensitive to some characters, but we'll let it handle it.
                // We just need to make sure it's not empty.
                if (!chapterName.isEmpty()) {
                    // Chapter names can contain regex, so we might need to escape them.
                    // For now, we'll pass it directly. The user can use regex if they know how.
                    // A simple approach is to just use the name.
                    // For more complex names, yt-dlp recommends `*re:^Chapter Title$`
                    // For now, we'll just use the chapter name.
                    sectionStrings.append(QStringLiteral("*%1").arg(chapterName));
                }
            }
        }
    }

    return sectionStrings.join(QLatin1Char('+'));
}
