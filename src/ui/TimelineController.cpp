#include "TimelineController.h"

namespace pcm {

TimelineController::TimelineController(QObject* parent)
    : QObject(parent)
{
}

void TimelineController::setDuration(qint64 us)
{
    m_durationUs = qMax<qint64>(0, us);
    if (m_durationUs <= 0) {
        m_startUs = 0;
        m_spanUs = 0;
    } else if (m_spanUs <= 0 || m_startUs + m_spanUs > m_durationUs) {
        // Current window invalid (new file / duration corrected) -> reset to the full range
        m_startUs = 0;
        m_spanUs = m_durationUs;
    } else {
        m_startUs = qBound<qint64>(0, m_startUs, m_durationUs - m_spanUs);
    }
    emit durationChanged();
    emit viewChanged();
}

void TimelineController::setView(qint64 startUs, qint64 spanUs)
{
    if (m_durationUs <= 0)
        return;
    spanUs = qBound(minSpanUs(), spanUs, m_durationUs);
    startUs = qBound<qint64>(0, startUs, m_durationUs - spanUs);
    if (startUs == m_startUs && spanUs == m_spanUs)
        return;
    m_startUs = startUs;
    m_spanUs = spanUs;
    emit viewChanged();
}

void TimelineController::zoomAt(double centerRatio, double factor)
{
    if (m_durationUs <= 0 || factor <= 0.0)
        return;
    const qint64 center = m_startUs + qint64(centerRatio * double(m_spanUs));
    qint64 newSpan = qint64(double(m_spanUs) / factor);
    newSpan = qBound(minSpanUs(), newSpan, m_durationUs);
    qint64 newStart = center - qint64(centerRatio * double(newSpan));
    newStart = qBound<qint64>(0, newStart, m_durationUs - newSpan);
    setView(newStart, newSpan);
}

void TimelineController::panByFraction(double fraction)
{
    if (m_durationUs <= 0)
        return;
    setView(m_startUs + qint64(fraction * double(m_spanUs)), m_spanUs);
}

} // namespace pcm
