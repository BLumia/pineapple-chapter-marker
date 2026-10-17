#include "ChapterTableModel.h"

#include <algorithm>

#include <QBrush>
#include <QColor>

#include "ChapterCommands.h"
#include "core/TimeUtils.h"

namespace pcm {

ChapterTableModel::ChapterTableModel(ChapterDocument& doc, QObject* parent)
    : QAbstractTableModel(parent), m_doc(doc)
{
}

int ChapterTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_doc.chapters.size();
}

int ChapterTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant ChapterTableModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_doc.chapters.size())
        return {};
    const Chapter &ch = m_doc.chapters.at(row);
    const ChapterDocument::RowFlags flags = m_doc.rowFlags(row);

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case ColStart:
            if (flags.overlapsPrev)
                return tr("Overlaps the previous chapter's range");
            if (flags.startOutOfRange)
                return tr("Start time is outside the media range");
            break;
        case ColEnd:
            return ch.endUs ? tr("Explicit end time")
                            : tr("Auto: inherits next chapter start / end of media");
        default:
            break;
        }
        return {};
    }

    if (role == Qt::ForegroundRole) {
        if (index.column() == ColStart && (flags.startOutOfRange || flags.overlapsPrev))
            return QBrush(QColor(Qt::red));
        if (index.column() == ColEnd && flags.endInvalid)
            return QBrush(QColor(Qt::red));
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColIndex:  return QVariant::fromValue(Qt::AlignCenter);
        case ColStart:
        case ColEnd:
        case ColLength: return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        default:        return {};
        }
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColIndex: return row + 1;
        case ColStart: return time::formatClockFixed(ch.startUs);
        case ColEnd:   return time::formatClockFixed(m_doc.effectiveEndUs(row));
        case ColLength: {
            const qint64 len = m_doc.lengthUs(row);
            return len > 0 ? QVariant(time::formatClockCompact(len)) : QVariant(QStringLiteral("—"));
        }
        case ColTitle: return ch.title;
        }
    }

    return {};
}

QVariant ChapterTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColIndex:  return QStringLiteral("#");
    case ColStart:  return tr("Start");
    case ColEnd:    return tr("End");
    case ColLength: return tr("Duration");
    case ColTitle:  return tr("Title");
    }
    return {};
}

Qt::ItemFlags ChapterTableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return f;
    switch (index.column()) {
    case ColStart:
    case ColEnd:
    case ColTitle:
        return f | Qt::ItemIsEditable;
    default:
        return f;
    }
}

bool ChapterTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid())
        return false;
    const int row = index.row();
    if (row < 0 || row >= m_doc.chapters.size())
        return false;
    Chapter &ch = m_doc.chapters[row];

    switch (index.column()) {
    case ColStart: {
        const auto t = time::parseTimestamp(value.toString());
        if (!t || *t < 0)
            return false;
        if (m_doc.durationUs > 0 && *t >= m_doc.durationUs)
            return false;
        if (*t == ch.startUs)
            return false;
        const qint64 oldUs = ch.startUs;
        // Crossing a neighbor changes row order, invalidating existing undo records (row-addressed)
        const bool crossing = (row > 0 && *t < m_doc.chapters.at(row - 1).startUs)
            || (row + 1 < m_doc.chapters.size() && *t > m_doc.chapters.at(row + 1).startUs);
        if (crossing && m_undo)
            m_undo->clear();
        applyStart(row, *t, crossing);
        if (!crossing && m_undo)
            m_undo->push(new StartEditCommand(this, row, oldUs, *t));
        return true;
    }
    case ColEnd: {
        const auto t = time::parseTimestamp(value.toString());
        if (!t || *t <= ch.startUs)
            return false;
        // reverting to auto when equal to the auto value, so a pointless explicit end does not block container round-trips
        const std::optional<qint64> newEnd =
            (*t == m_doc.autoEndUs(row)) ? std::nullopt : std::optional<qint64>(*t);
        if (newEnd == ch.endUs)
            return false;
        const auto oldEnd = ch.endUs;
        applyEnd(row, newEnd);
        if (m_undo)
            m_undo->push(new EndEditCommand(this, row, oldEnd, newEnd));
        return true;
    }
    case ColTitle: {
        QString title = value.toString();
        title.remove(QLatin1Char('\n'));
        if (title == ch.title)
            return false;
        const QString oldTitle = ch.title;
        applyTitle(row, title);
        if (m_undo)
            m_undo->push(new TitleEditCommand(this, row, oldTitle, title));
        return true;
    }
    default:
        return false;
    }
}

void ChapterTableModel::refresh()
{
    beginResetModel();
    endResetModel();
}

void ChapterTableModel::applyStart(int row, qint64 us, bool allowReorder)
{
    if (row < 0 || row >= m_doc.chapters.size())
        return;
    m_doc.chapters[row].startUs = us;
    if (allowReorder) {
        emit layoutAboutToBeChanged();
        m_doc.sortByStart();
        emit layoutChanged();
    } else {
        // a start change also affects the previous chapter's duration column
        emit dataChanged(index(row, ColStart), index(row, ColLength));
        if (row > 0)
            emit dataChanged(index(row - 1, ColLength), index(row - 1, ColLength));
    }
    emit documentEdited();
}

void ChapterTableModel::applyEnd(int row, std::optional<qint64> endUs)
{
    if (row < 0 || row >= m_doc.chapters.size())
        return;
    m_doc.chapters[row].endUs = endUs;
    emit dataChanged(index(row, ColEnd), index(row, ColLength));
    emit documentEdited();
}

void ChapterTableModel::applyTitle(int row, const QString& title)
{
    if (row < 0 || row >= m_doc.chapters.size())
        return;
    m_doc.chapters[row].title = title;
    emit dataChanged(index(row, ColTitle), index(row, ColTitle));
    emit documentEdited();
}

void ChapterTableModel::applyChapters(QVector<Chapter> list)
{
    m_doc.chapters = std::move(list);
    m_doc.sortByStart();
    refresh();
    emit documentEdited();
}

void ChapterTableModel::notifyRowsChanged(int first, int last)
{
    if (first > last)
        std::swap(first, last);
    first = qBound(0, first, rowCount() - 1);
    last = qBound(0, last, rowCount() - 1);
    if (first > last)
        return;
    emit dataChanged(index(first, 0), index(last, ColCount - 1));
    emit documentEdited();
}

} // namespace pcm
