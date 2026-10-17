#pragma once

#include <QObject>
#include <QtGlobal>

namespace pcm {

// Timeline state shared by the waveform views (main view / overview bar): media duration + visible window.
// Both views read/write through it, keeping the viewports in sync.
class TimelineController : public QObject {
    Q_OBJECT
public:
    explicit TimelineController(QObject* parent = nullptr);

    qint64 durationUs() const { return m_durationUs; }
    qint64 viewStartUs() const { return m_startUs; }
    qint64 viewSpanUs() const { return m_spanUs; }
    qint64 minSpanUs() const { return qMin<qint64>(m_durationUs, 5000); } // 5ms

    // Load new media (resets/clamps the view when the duration changes)
    void setDuration(qint64 us);
    // Set the window directly (clamped to the valid range)
    void setView(qint64 startUs, qint64 spanUs);
    // Zoom around centerRatio (0..1, relative to the current window); factor>1 zooms in
    void zoomAt(double centerRatio, double factor);
    // Pan by a fraction of the current window width (positive = rightward)
    void panByFraction(double fraction);

signals:
    void viewChanged();
    void durationChanged();

private:
    qint64 m_durationUs = 0;
    qint64 m_startUs = 0;
    qint64 m_spanUs = 0;
};

} // namespace pcm
