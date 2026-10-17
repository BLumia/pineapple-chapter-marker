#include "TimestampTextDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "core/TimeUtils.h"

namespace pcm {

TimestampTextDialog::TimestampTextDialog(Mode mode, QWidget* parent)
    : QDialog(parent), m_mode(mode)
{
    setModal(true);

    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(8);

    m_edit = new QPlainTextEdit(this);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_edit->setFont(mono);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::RichText);
    m_status->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(this);

    if (mode == Import) {
        setWindowTitle(tr("Import Timestamp Text (YouTube Style)"));
        auto* hint = new QLabel(
            tr("One entry per line: <timestamp> <title>\n"
               "Formats like \"0:00 Intro\" or \"01:23:45.5 Chapter name\" are accepted;\n"
               "both m:ss and h:mm:ss work. Full-width colons and bullet markers are ignored."),
            this);
        lay->addWidget(hint);
        lay->addWidget(m_edit, 1);
        lay->addWidget(m_status);
        buttons->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        m_okButton = buttons->button(QDialogButtonBox::Ok);
        m_okButton->setText(tr("Import"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_edit, &QPlainTextEdit::textChanged, this, &TimestampTextDialog::updateImportStatus);
        resize(560, 440);
    } else {
        setWindowTitle(tr("Export Timestamp Text (YouTube Style)"));
        m_edit->setReadOnly(true);
        lay->addWidget(m_edit, 1);
        lay->addWidget(m_status);
        buttons->setStandardButtons(QDialogButtonBox::Close);
        auto* copyBtn = buttons->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
        auto* saveBtn = buttons->addButton(tr("Save as Text File…"), QDialogButtonBox::ActionRole);
        connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn] {
            QApplication::clipboard()->setText(m_edit->toPlainText());
            const QString original = copyBtn->text();
            copyBtn->setText(tr("Copied ✓"));
            QTimer::singleShot(1500, this, [copyBtn, original] { copyBtn->setText(original); });
        });
        connect(saveBtn, &QPushButton::clicked, this, [this] {
            const QString file = QFileDialog::getSaveFileName(
                this, tr("Save Timestamp Text"), QStringLiteral("chapters.txt"),
                tr("Text Files (*.txt);;All Files (*)"));
            if (file.isEmpty())
                return;
            QFile f(file);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write(m_edit->toPlainText().toUtf8());
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        resize(560, 460);
    }

    lay->addWidget(buttons);
}

void TimestampTextDialog::setExportText(const QString& text, const QStringList& ruleWarnings)
{
    m_edit->setPlainText(text);
    m_status->setText(ruleWarnings.join(QStringLiteral("<br>")));
}

void TimestampTextDialog::updateImportStatus()
{
    m_parsed = time::parseTimestampText(m_edit->toPlainText());
    QStringList parts;
    parts << tr("Recognized <b>%1</b> chapters").arg(m_parsed.chapters.size());
    if (m_parsed.skippedLines > 0)
        parts << tr("<span style=\"color:orange\">%1 line(s) could not be parsed</span>").arg(m_parsed.skippedLines);
    m_status->setText(parts.join(QStringLiteral("，")));

    if (m_okButton)
        m_okButton->setEnabled(!m_parsed.chapters.isEmpty());
}

} // namespace pcm
