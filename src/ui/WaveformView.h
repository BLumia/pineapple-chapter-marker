#pragma once

#include <QWidget>

#include "core/ChapterDocument.h"
#include "core/WaveformPeaks.h"

namespace pcm {

class TimelineController;
class PlaybackController;

// Main waveform view (zoomed): ruler + waveform + chapter markers + playhead.
// Interaction:
//   - Ctrl+wheel: zoom centered on the mouse; wheel: horizontal pan
//   - left-drag on empty space / middle-drag: pan; left-drag a chapter marker: move the chapter start
//   - click a chapter marker: emits chapterClicked (table follows)
//   - click/drag the ruler: move the playhead
class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(TimelineController* tl, QWidget* parent = nullptr);

    void setPeaks(WaveformPeaks* peaks);
    void setChapters(ChapterDocument* doc);
    void setAudioAvailable(bool available);
    void setSelectedRow(int row);
    void setPlayback(PlaybackController* playback);

signals:
    void chapterClicked(int row);
    void markerDragged(int row, qint64 newStartUs);
    // Drag gesture finished: commit one undo command (the document already holds newStartUs)
    void markerEditCommitted(int row, qint64 oldStartUs, qint64 newStartUs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int timeToX(qint64 t, int width) const;
    qint64 xToTime(int x, int width) const;
    int hitMarker(int x) const;
    void drawRuler(QPainter& p, const QRect& rect);

    TimelineController* m_tl = nullptr;
    WaveformPeaks* m_peaks = nullptr;
    ChapterDocument* m_doc = nullptr;
    PlaybackController* m_playback = nullptr;
    bool m_audioAvailable = false;
    int m_selectedRow = -1;

    enum class Drag { None, Pan, Marker, Seek };
    Drag m_drag = Drag::None;
    int m_grabX = 0;
    int m_dragRow = -1;
    qint64 m_dragOrigUs = 0;

    static constexpr int kRulerHeight = 20;
    static constexpr int kMarkerHitRadius = 5;
};

} // namespace pcm
