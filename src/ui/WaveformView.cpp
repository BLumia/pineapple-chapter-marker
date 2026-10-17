#include "WaveformView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

#include "TimelineController.h"
#include "PlaybackController.h"
#include "core/TimeUtils.h"

namespace {

const QColor kBackground(0x26, 0x26, 0x26);
const QColor kRulerBackground(0x2f, 0x2f, 0x2f);
const QColor kRulerBorder(0x3c, 0x3c, 0x3c);
const QColor kWaveformPeak(0x3f, 0x82, 0x35); // peak envelope (dark)
const QColor kWaveformRms(0x76, 0xc6, 0x62);  // RMS core (bright)
const QColor kCenterLine(0x3a, 0x3a, 0x3a);
const QColor kMarker(0xe8, 0x4c, 0x3c);
const QColor kMarkerSelected(0xff, 0xa5, 0x3c);
const QColor kText(0xcc, 0xcc, 0xcc);
const QColor kTextDim(0x8a, 0x8a, 0x8a);

// Ruler tick interval candidates (microseconds)
const qint64 kTickCandidates[] = {
    1000,      2000,      5000,      10000,     20000,     50000,
    100000,    200000,    500000,    1000000,   2000000,   5000000,
    10000000,  15000000,  30000000,  60000000,  120000000, 300000000,
    600000000, 1800000000, 3600000000
};

} // namespace

namespace pcm {

WaveformView::WaveformView(TimelineController* tl, QWidget* parent)
    : QWidget(parent), m_tl(tl)
{
    setMouseTracking(true);
    setMinimumHeight(60);
    connect(m_tl, &TimelineController::viewChanged, this, qOverload<>(&QWidget::update));
    connect(m_tl, &TimelineController::durationChanged, this, qOverload<>(&QWidget::update));
}

void WaveformView::setPeaks(WaveformPeaks* peaks)
{
    m_peaks = peaks;
    update();
}

void WaveformView::setChapters(ChapterDocument* doc)
{
    m_doc = doc;
    update();
}

void WaveformView::setAudioAvailable(bool available)
{
    m_audioAvailable = available;
    update();
}

void WaveformView::setSelectedRow(int row)
{
    m_selectedRow = row;
    update();
}

void WaveformView::setPlayback(PlaybackController* playback)
{
    m_playback = playback;
    connect(m_playback, &PlaybackController::positionChangedUs,
            this, qOverload<>(&QWidget::update));
    connect(m_playback, &PlaybackController::playbackToggled,
            this, qOverload<>(&QWidget::update));
    update();
}

int WaveformView::timeToX(qint64 t, int width) const
{
    const qint64 span = m_tl->viewSpanUs();
    if (span <= 0)
        return 0;
    return int(double(t - m_tl->viewStartUs()) * width / double(span));
}

qint64 WaveformView::xToTime(int x, int width) const
{
    const qint64 span = m_tl->viewSpanUs();
    if (width <= 0)
        return m_tl->viewStartUs();
    return m_tl->viewStartUs() + qint64(double(x) / width * double(span));
}

int WaveformView::hitMarker(int x) const
{
    if (!m_doc || m_tl->viewSpanUs() <= 0)
        return -1;
    int best = -1;
    int bestDist = kMarkerHitRadius;
    const int w = width();
    for (int row = 0; row < m_doc->chapters.size(); ++row) {
        const int mx = timeToX(m_doc->chapters.at(row).startUs, w);
        const int dist = qAbs(mx - x);
        if (dist <= bestDist) {
            best = row;
            bestDist = dist;
        }
    }
    return best;
}

void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kBackground);

    if (!m_audioAvailable) {
        p.setPen(kTextDim);
        p.drawText(rect(), Qt::AlignCenter, tr("No audio stream to display in this file"));
        return;
    }
    if (!m_peaks || !m_peaks->isReady()) {
        p.setPen(kTextDim);
        p.drawText(rect(), Qt::AlignCenter, tr("Decoding audio…"));
        return;
    }
    if (m_tl->durationUs() <= 0 || m_tl->viewSpanUs() <= 0) {
        p.setPen(kTextDim);
        p.drawText(rect(), Qt::AlignCenter, tr("Open a media file to see the waveform here"));
        return;
    }

    const int w = width();
    const int h = height();
    const QRect rulerRect(0, 0, w, kRulerHeight);
    const QRect waveRect(0, kRulerHeight, w, h - kRulerHeight);

    drawRuler(p, rulerRect);

    p.setPen(kCenterLine);
    p.drawLine(waveRect.left(), waveRect.center().y(), waveRect.right(), waveRect.center().y());

    // ---- waveform ----
    WaveformPeaks::QueryResult qr;
    if (m_peaks->query(m_tl->viewStartUs(), m_tl->viewStartUs() + m_tl->viewSpanUs(),
                       waveRect.width(), &qr)) {
        const int w2 = qMax(1, waveRect.width());
        const qint64 spanSamples = qMax<qint64>(1, m_tl->viewSpanUs() * m_peaks->sampleRate() / 1000000);
        const qint64 samplesPerPixel = qMax<qint64>(1, spanSamples / w2);
        // fewer samples per pixel than a bucket -> columns share buckets -> draw as an interpolated polygon (otherwise blocky)
        const bool interpolated = samplesPerPixel < qr.usedBucketSamples;

        auto isValid = [&qr](int x) {
            const auto &c = qr.columns[x];
            return !(c.lo == 0.f && c.hi == 0.f && c.rms == 0.f);
        };

        if (!interpolated) {
            const int cy = waveRect.center().y();
            const float halfH = waveRect.height() * 0.5f;
            // peak envelope (outer, dark)
            p.setPen(QPen(kWaveformPeak, 1));
            for (int x = 0; x < qr.columns.size(); ++x) {
                if (!isValid(x))
                    continue;
                const float lo = qBound(-1.0f, qr.columns[x].lo, 1.0f);
                const float hi = qBound(-1.0f, qr.columns[x].hi, 1.0f);
                const int yTop = waveRect.top() + int((1.0f - hi) * halfH);
                int yBot = waveRect.top() + int((1.0f - lo) * halfH);
                if (yBot <= yTop)
                    yBot = yTop + 1;
                p.drawLine(x, yTop, x, yBot);
            }
            // RMS core (bright)
            p.setPen(QPen(kWaveformRms, 1));
            for (int x = 0; x < qr.columns.size(); ++x) {
                if (!isValid(x))
                    continue;
                const int dy = int(qBound(0.0f, qr.columns[x].rms, 1.0f) * halfH);
                if (dy <= 0)
                    continue;
                p.drawLine(x, cy - dy, x, cy + dy);
            }
        } else {
            // deep zoom: build closed polygons per valid run (top edge rightward + bottom edge leftward)
            QPainterPath path;
            const int n = qr.columns.size();
            auto yHi = [&qr, &waveRect](int x) {
                return waveRect.top()
                    + int((1.0f - qBound(-1.0f, qr.columns[x].hi, 1.0f)) * 0.5f * waveRect.height());
            };
            auto yLo = [&qr, &waveRect](int x) {
                return waveRect.top()
                    + int((1.0f - qBound(-1.0f, qr.columns[x].lo, 1.0f)) * 0.5f * waveRect.height());
            };
            int x0 = 0;
            while (x0 < n) {
                if (!isValid(x0)) {
                    ++x0;
                    continue;
                }
                int x1 = x0;
                while (x1 + 1 < n && isValid(x1 + 1))
                    ++x1;
                path.moveTo(x0, yHi(x0));
                for (int x = x0 + 1; x <= x1; ++x)
                    path.lineTo(x, yHi(x));
                for (int x = x1; x >= x0; --x)
                    path.lineTo(x, yLo(x));
                path.closeSubpath();
                x0 = x1 + 1;
            }
            p.setPen(Qt::NoPen);
            p.setBrush(kWaveformPeak);
            p.drawPath(path);
        }
    }

    // ---- chapter markers ----
    if (m_doc) {
        const int n = m_doc->chapters.size();
        const bool drawNumbers = n > 0 && w / n >= 40;
        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);
        for (int row = 0; row < n; ++row) {
            const qint64 t = m_doc->chapters.at(row).startUs;
            const int x = timeToX(t, w);
            if (x < -1 || x > w + 1)
                continue;
            const bool selected = (row == m_selectedRow);
            p.setPen(QPen(selected ? kMarkerSelected : kMarker, selected ? 2 : 1));
            p.drawLine(x, 0, x, h - 1);
            if (drawNumbers || selected) {
                p.setPen(selected ? kMarkerSelected : kMarker);
                p.drawText(x + 3, kRulerHeight + 12, QString::number(row + 1));
            }
        }
        // time label while dragging
        if (m_drag == Drag::Marker && m_dragRow >= 0 && m_dragRow < n) {
            const qint64 t = m_doc->chapters.at(m_dragRow).startUs;
            const int x = timeToX(t, w);
            p.setPen(kMarkerSelected);
            p.drawText(qBound(0, x + 5, qMax(0, w - 90)), kRulerHeight + 26,
                       time::formatClockCompact(t));
        }
    }

    // ---- playhead ----
    if (m_playback && m_audioAvailable && m_tl->viewSpanUs() > 0) {
        const qint64 pos = m_playback->positionUs();
        const qint64 vs = m_tl->viewStartUs();
        if (pos >= vs && pos <= vs + m_tl->viewSpanUs()) {
            const int x = timeToX(pos, w);
            p.setPen(QPen(QColor(0xf2, 0xf2, 0xf2), 1));
            p.drawLine(x, 0, x, h - 1);
            // small triangle on the ruler
            QPolygonF tri;
            tri << QPointF(x - 4, 1) << QPointF(x + 4, 1) << QPointF(x, kRulerHeight - 4.0);
            p.setBrush(QColor(0xf2, 0xf2, 0xf2));
            p.setPen(Qt::NoPen);
            p.drawPolygon(tri);
        }
    }
}

void WaveformView::drawRuler(QPainter& p, const QRect& rect)
{
    p.fillRect(rect, kRulerBackground);
    p.setPen(kRulerBorder);
    p.drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());

    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    p.setPen(kText);

    const qint64 start = m_tl->viewStartUs();
    const qint64 span = m_tl->viewSpanUs();
    const int w = rect.width();
    if (span <= 0 || w <= 0)
        return;

    // pick the major tick interval: target pixel spacing >= 80
    qint64 interval = kTickCandidates[sizeof(kTickCandidates) / sizeof(kTickCandidates[0]) - 1];
    for (const qint64 c : kTickCandidates) {
        if (double(c) * w / double(span) >= 80.0) {
            interval = c;
            break;
        }
    }

    // minor ticks (1/5 of the major interval)
    const qint64 firstMinor = ((start / (interval / 5)) + 1) * (interval / 5);
    p.setPen(kRulerBorder);
    for (qint64 t = firstMinor; t < start + span; t += interval / 5) {
        const int x = timeToX(t, w);
        if (t % interval == 0)
            continue; // no minor tick at major ticks
        p.drawLine(x, rect.bottom() - 3, x, rect.bottom());
    }

    const qint64 firstMajor = ((start / interval) + 1) * interval;
    for (qint64 t = firstMajor; t < start + span; t += interval) {
        const int x = timeToX(t, w);
        p.drawLine(x, rect.bottom() - 6, x, rect.bottom());
        p.drawText(x + 3, rect.top(), 120, rect.height() - 8,
                   Qt::AlignLeft | Qt::AlignVCenter, time::formatClockCompact(t));
    }
}

void WaveformView::wheelEvent(QWheelEvent* event)
{
    const QPoint angle = event->angleDelta();
    if (angle.isNull())
        return;
    if (event->modifiers() & Qt::ControlModifier) {
        const double factor = std::pow(1.25, angle.y() / 120.0);
        m_tl->zoomAt(event->position().x() / double(width()), factor);
    } else {
        // wheel pans: scrolling up = toward the start
        m_tl->panByFraction(-angle.y() / 120.0 / 8.0);
    }
    event->accept();
}

void WaveformView::mousePressEvent(QMouseEvent* event)
{
    // middle-button drag: pan (same as left-drag on empty space)
    if (event->button() == Qt::MiddleButton && m_tl->viewSpanUs() > 0) {
        m_drag = Drag::Pan;
        m_grabX = int(event->position().x());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || m_tl->viewSpanUs() <= 0)
        return;
    const QPointF pos = event->position();
    const int x = int(pos.x());

    // ruler area: move the playhead (draggable scrubbing)
    if (pos.y() <= kRulerHeight && m_playback) {
        m_drag = Drag::Seek;
        const qint64 t = qBound<qint64>(0, xToTime(x, width()), m_tl->durationUs());
        m_playback->seekUs(t);
        return;
    }

    const int row = hitMarker(x);
    if (row >= 0) {
        m_drag = Drag::Marker;
        m_dragRow = row;
        m_dragOrigUs = m_doc->chapters.at(row).startUs;
        setCursor(Qt::SizeHorCursor);
        emit chapterClicked(row);
    } else {
        m_drag = Drag::Pan;
        m_grabX = x;
        setCursor(Qt::ClosedHandCursor);
    }
}

void WaveformView::mouseMoveEvent(QMouseEvent* event)
{
    const int x = int(event->position().x());
    if (m_drag == Drag::Seek && m_playback) {
        const qint64 t = qBound<qint64>(0, xToTime(x, width()), m_tl->durationUs());
        m_playback->seekUs(t);
    } else if (m_drag == Drag::Marker && m_doc && m_dragRow >= 0 && m_dragRow < m_doc->chapters.size()) {
        qint64 t = xToTime(x, width());
        const qint64 duration = m_tl->durationUs();
        const qint64 lo = m_dragRow > 0 ? m_doc->chapters.at(m_dragRow - 1).startUs + 1000 : 0;
        const qint64 hi = m_dragRow + 1 < m_doc->chapters.size()
            ? m_doc->chapters.at(m_dragRow + 1).startUs - 1000
            : qMax<qint64>(duration, 1000) - 1000;
        t = qBound(lo, t, qMax(lo, hi));
        emit markerDragged(m_dragRow, t);
        update(); // reflect the drag position immediately (MainWindow only refreshes the overview bar)
    } else if (m_drag == Drag::Pan) {
        const int dx = x - m_grabX;
        m_grabX = x;
        const double fraction = -double(dx) / double(qMax(1, width()));
        m_tl->panByFraction(fraction);
    } else if (event->buttons() == Qt::NoButton) {
        // hover cursor hint
        setCursor(hitMarker(x) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
}

void WaveformView::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    if (m_drag == Drag::Marker) {
        const int row = m_dragRow;
        const qint64 origUs = m_dragOrigUs;
        m_drag = Drag::None;
        m_dragRow = -1;
        if (m_doc && row >= 0 && row < m_doc->chapters.size()) {
            const qint64 curUs = m_doc->chapters.at(row).startUs;
            if (curUs != origUs)
                emit markerEditCommitted(row, origUs, curUs);
        }
    } else {
        m_drag = Drag::None;
    }
    setCursor(Qt::ArrowCursor);
    update();
}

} // namespace pcm
