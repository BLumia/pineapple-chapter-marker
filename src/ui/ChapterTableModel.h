#pragma once

#include <QAbstractTableModel>
#include <QUndoStack>
#include <optional>

#include "core/ChapterDocument.h"

namespace pcm {

// Chapter table model: # / start / end / duration / title
// "End" shows the effective end time (explicit or auto); editing the end always writes an explicit value,
// reverting to auto (nullopt) when it exactly equals the auto value.
//
// Undo: editing setData pushes the matching command onto the QUndoStack after the document is updated
// (redo re-applies idempotently); the apply* methods are the command bodies.
// Start edits that reorder chapters invalidate row numbers and clear the undo stack before editing.
class ChapterTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColIndex = 0,
        ColStart,
        ColEnd,
        ColLength,
        ColTitle,
        ColCount
    };

    explicit ChapterTableModel(ChapterDocument& doc, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    // Whole reset (after loading a file; the undo stack is managed by MainWindow)
    void refresh();

    // Command body: the caller pushes the undo command after the document is updated
    void applyStart(int row, qint64 us, bool allowReorder);
    void applyEnd(int row, std::optional<qint64> endUs);
    void applyTitle(int row, const QString& title);
    void applyChapters(QVector<Chapter> list);

    void setUndoStack(QUndoStack* stack) { m_undo = stack; }

    // Notify the view after external changes (e.g. marker drags in the waveform) to rows [first, last]
    void notifyRowsChanged(int first, int last);

signals:
    // emitted on every successful edit
    void documentEdited();

private:
    ChapterDocument& m_doc;
    QUndoStack* m_undo = nullptr;
};

} // namespace pcm
