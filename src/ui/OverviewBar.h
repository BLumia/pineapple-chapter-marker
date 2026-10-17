#pragma once

#include <QWidget>

#include "core/ChapterDocument.h"
#include "core/WaveformPeaks.h"

namespace pcm {

class TimelineController;
class PlaybackController;

// DAW-style overview scrollbar: full-duration mini waveform + chapter markers (red) + viewport box + playhead.
// Interaction: drag the viewport to pan; drag its edges to zoom; click empty space to center-jump; wheel zooms/pans.
class OverviewBar : public QWidget {
    Q_OBJECT
public:
    explicit OverviewBar(TimelineController* tl, QWidget* parent = nullptr);

    void setPeaks(WaveformPeaks* peaks);
    void setChapters(ChapterDocument* doc);
    void setAudioAvailable(bool available);
    void setPlayback(PlaybackController* playback);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int timeToX(qint64 t) const;
    qint64 xToTime(int x) const;
    QRect viewportRect() const;

    TimelineController* m_tl = nullptr;
    WaveformPeaks* m_peaks = nullptr;
    ChapterDocument* m_doc = nullptr;
    PlaybackController* m_playback = nullptr;
    bool m_audioAvailable = false;

    enum class Drag { None, Move, Left, Right };
    Drag m_drag = Drag::None;
    int m_grabOffset = 0;

    static constexpr int kEdgeGrabRadius = 5;
    static constexpr int kInnerMargin = 8;
};

} // namespace pcm
