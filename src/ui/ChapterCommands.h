#pragma once

#include <QUndoCommand>
#include <optional>
#include <QVector>

#include "ChapterTableModel.h"
#include "core/Chapter.h"

namespace pcm {

// Undo command set: commands address chapters by row number.
// Note: edits that reorder chapters invalidate row numbers; those paths (cross-row start edits in the table)
// clear the undo stack before editing; marker drags are clamped by neighbors and never reorder.
// The document is already in the new state when a command is pushed (redo re-applies idempotently).

class StartEditCommand : public QUndoCommand {
public:
    StartEditCommand(ChapterTableModel* model, int row, qint64 oldUs, qint64 newUs)
        : m_model(model), m_row(row), m_old(oldUs), m_new(newUs)
    {
        setText(QObject::tr("Move chapter start"));
    }
    void undo() override { m_model->applyStart(m_row, m_old, false); }
    void redo() override { m_model->applyStart(m_row, m_new, false); }

private:
    ChapterTableModel* m_model;
    int m_row;
    qint64 m_old;
    qint64 m_new;
};

class EndEditCommand : public QUndoCommand {
public:
    EndEditCommand(ChapterTableModel* model, int row,
                   std::optional<qint64> oldEnd, std::optional<qint64> newEnd)
        : m_model(model), m_row(row), m_old(oldEnd), m_new(newEnd)
    {
        setText(QObject::tr("Change chapter end"));
    }
    void undo() override { m_model->applyEnd(m_row, m_old); }
    void redo() override { m_model->applyEnd(m_row, m_new); }

private:
    ChapterTableModel* m_model;
    int m_row;
    std::optional<qint64> m_old;
    std::optional<qint64> m_new;
};

class TitleEditCommand : public QUndoCommand {
public:
    TitleEditCommand(ChapterTableModel* model, int row, QString oldTitle, QString newTitle)
        : m_model(model), m_row(row), m_old(std::move(oldTitle)), m_new(std::move(newTitle))
    {
        setText(QObject::tr("Rename chapter"));
    }
    void undo() override { m_model->applyTitle(m_row, m_old); }
    void redo() override { m_model->applyTitle(m_row, m_new); }

private:
    ChapterTableModel* m_model;
    int m_row;
    QString m_old;
    QString m_new;
};

// Whole-list replace (text import / undo of import)
class ReplaceAllCommand : public QUndoCommand {
public:
    ReplaceAllCommand(ChapterTableModel* model, QVector<Chapter> oldList, QVector<Chapter> newList)
        : m_model(model), m_old(std::move(oldList)), m_new(std::move(newList))
    {
        setText(QObject::tr("Import chapters"));
    }
    void undo() override { m_model->applyChapters(m_old); }
    void redo() override { m_model->applyChapters(m_new); }

private:
    ChapterTableModel* m_model;
    QVector<Chapter> m_old;
    QVector<Chapter> m_new;
};

} // namespace pcm
