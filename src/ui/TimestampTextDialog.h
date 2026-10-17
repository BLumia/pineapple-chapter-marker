#pragma once

#include <QDialog>
#include <QVector>

#include "core/Chapter.h"
#include "core/TimeUtils.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace pcm {

// Import/export dialog for YouTube-style timestamp text.
// Import: editable text + live parse status; take importedChapters() after accepting.
// Export: read-only text + copy/save-as, with YouTube rule-check notes.
class TimestampTextDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { Import, Export };

    TimestampTextDialog(Mode mode, QWidget* parent = nullptr);

    void setExportText(const QString& text, const QStringList& ruleWarnings = {});
    QVector<Chapter> importedChapters() const { return m_parsed.chapters; }

private:
    void updateImportStatus();

    Mode m_mode;
    QPlainTextEdit* m_edit = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_okButton = nullptr;
    time::TimestampParseResult m_parsed;
};

} // namespace pcm
