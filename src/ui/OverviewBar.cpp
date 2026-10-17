#include "OverviewBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

#include "TimelineController.h"
#include "PlaybackController.h"

namespace {

const QColor kBackground(0x1e, 0x1e, 0x1e);
const QColor kWaveform(0x46, 0x7a, 0x3a);
const QColor kMarker(0xe8, 0x4c, 0x3c);
const QColor kViewportBorder(0x9a, 0x9a, 0x9a);
const QColor kViewportShade(0, 0, 0, 130);
const QColor kHandle(0xc8, 0xc8, 0xc8);
const QColor kTextDim(0x8a, 0x8a, 0x8a);

} // namespace

namespace pcm {

OverviewBar::OverviewBar(TimelineController* tl, QWidget* parent)
    : QWidget(parent), m_tl(tl)
{
    setMouseTracking(true);
    setFixedHeight(60);
    connect(m_tl, &TimelineController::viewChanged, this, qOverload<>(&QWidget::update));
    connect(m_tl, &TimelineController::durationChanged, this, qOverload<>(&QWidget::update));
}

void OverviewBar::setPeaks(WaveformPeaks* peaks)
{
    m_peaks = peaks;
    update();
}

void OverviewBar::setChapters(ChapterDocument* doc)
{
    m_doc = doc;
    update();
}

void OverviewBar::setAudioAvailable(bool available)
{
    m_audioAvailable = available;
    update();
}

void OverviewBar::setPlayback(PlaybackController* playback)
{
    m_playback = playback;
    connect(m_playback, &PlaybackController::positionChangedUs,
            this, qOverload<>(&QWidget::update));
    connect(m_playback, &PlaybackController::playbackToggled,
            this, qOverload<>(&QWidget::update));
    update();
}

int OverviewBar::timeToX(qint64 t) const
{
    const qint64 duration = m_tl->durationUs();
    if (duration <= 0)
        return 0;
    const int w = width() - 2 * kInnerMargin;
    return kInnerMargin + int(double(t) * w / double(duration));
}

qint64 OverviewBar::xToTime(int x) const
{
    const qint64 duration = m_tl->durationUs();
    const int w = width() - 2 * kInnerMargin;
    if (w <= 0)
        return 0;
    const double ratio = double(qBound(kInnerMargin, x, width() - kInnerMargin) - kInnerMargin) / w;
    return qint64(ratio * double(duration));
}

QRect OverviewBar::viewportRect() const
{
    const qint64 duration = m_tl->durationUs();
    if (duration <= 0)
        return QRect();
    const int x1 = timeToX(m_tl->viewStartUs());
    const int x2 = timeToX(m_tl->viewStartUs() + m_tl->viewSpanUs());
    return QRect(qMin(x1, x2), 0, qAbs(x2 - x1), height());
}

void OverviewBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kBackground);

    if (!m_audioAvailable || m_tl->durationUs() <= 0) {
        p.setPen(kTextDim);
        p.drawText(rect(), Qt::AlignCenter, tr("No audio"));
        return;
    }

    const QRect area(kInnerMargin, 6, width() - 2 * kInnerMargin, height() - 12);

    if (m_peaks && m_peaks->isReady()) {
        WaveformPeaks::QueryResult qr;
        if (m_peaks->query(0, m_tl->durationUs(), area.width(), &qr)) {
            p.setPen(QPen(kWaveform, 1));
            for (int x = 0; x < qr.columns.size(); ++x) {
                const auto &c = qr.columns.at(x);
                if (c.lo == 0.f && c.hi == 0.f)
                    continue;
                const int yTop = area.top() + int((1.0f - c.hi) * 0.5f * area.height());
                int yBot = area.top() + int((1.0f - c.lo) * 0.5f * area.height());
                if (yBot <= yTop)
                    yBot = yTop + 1;
                p.drawLine(area.left() + x, yTop, area.left() + x, yBot);
            }
        }
    }

    // chapter markers (red)
    if (m_doc) {
        p.setPen(QPen(kMarker, 1));
        for (const Chapter &ch : m_doc->chapters) {
            const int x = timeToX(ch.startUs);
            p.drawLine(x, 0, x, height() - 1);
        }
    }

    // viewport box
    const QRect vp = viewportRect();
    if (vp.isValid() && vp.width() > 0) {
        p.fillRect(QRect(0, 0, vp.left(), height()), kViewportShade);
        p.fillRect(QRect(vp.right() + 1, 0, width() - vp.right() - 1, height()), kViewportShade);
        p.setPen(kViewportBorder);
        p.drawRect(vp.adjusted(0, 0, -1, -1));
        p.fillRect(vp.left() - 2, height() / 2 - 9, 4, 18, kHandle);
        p.fillRect(vp.right() - 1, height() / 2 - 9, 4, 18, kHandle);
    }

    // playhead
    if (m_playback && m_tl->durationUs() > 0) {
        const int x = timeToX(qBound<qint64>(0, m_playback->positionUs(), m_tl->durationUs()));
        p.setPen(QPen(QColor(0xf2, 0xf2, 0xf2), 1));
        p.drawLine(x, 0, x, height() - 1);
    }
}

void OverviewBar::wheelEvent(QWheelEvent* event)
{
    // Same as the main view: Ctrl+wheel zoom (centered on the mouse), wheel pans
    const QPoint angle = event->angleDelta();
    if (angle.isNull())
        return;
    if (event->modifiers() & Qt::ControlModifier) {
        const double factor = std::pow(1.25, angle.y() / 120.0);
        m_tl->zoomAt(event->position().x() / double(qMax(1, width())), factor);
    } else {
        m_tl->panByFraction(-angle.y() / 120.0 / 8.0);
    }
    event->accept();
}

void OverviewBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_tl->durationUs() <= 0)
        return;
    const int x = int(event->position().x());
    const QRect vp = viewportRect();
    const qint64 span = m_tl->viewSpanUs();

    if (vp.isValid() && qAbs(x - vp.left()) <= kEdgeGrabRadius) {
        m_drag = Drag::Left;
    } else if (vp.isValid() && qAbs(x - vp.right()) <= kEdgeGrabRadius) {
        m_drag = Drag::Right;
    } else if (vp.isValid() && x > vp.left() && x < vp.right()) {
        m_drag = Drag::Move;
        m_grabOffset = x - vp.left();
    } else {
        // click on empty space: jump centered on that point
        const qint64 center = xToTime(x);
        qint64 newStart = center - span / 2;
        newStart = qBound<qint64>(0, newStart, qMax<qint64>(0, m_tl->durationUs() - span));
        m_tl->setView(newStart, span);
        m_drag = Drag::Move;
        m_grabOffset = vp.width() / 2;
    }
}

void OverviewBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag == Drag::None || m_tl->durationUs() <= 0)
        return;
    const int x = int(event->position().x());
    const qint64 duration = m_tl->durationUs();
    const qint64 span = m_tl->viewSpanUs();
    const qint64 start = m_tl->viewStartUs();
    const qint64 end = start + span;

    switch (m_drag) {
    case Drag::Move: {
        const int w = qMax(1, width() - 2 * kInnerMargin);
        qint64 newStart = xToTime(x) - qint64(double(m_grabOffset) * double(duration) / w);
        newStart = qBound<qint64>(0, newStart, qMax<qint64>(0, duration - span));
        m_tl->setView(newStart, span);
        break;
    }
    case Drag::Left: {
        qint64 newStart = qBound<qint64>(0, xToTime(x), qMax<qint64>(0, end - m_tl->minSpanUs()));
        m_tl->setView(newStart, end - newStart);
        break;
    }
    case Drag::Right: {
        qint64 newEnd = qBound(start + m_tl->minSpanUs(), xToTime(x), duration);
        m_tl->setView(start, newEnd - start);
        break;
    }
    default:
        break;
    }
}

void OverviewBar::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_drag = Drag::None;
}

} // namespace pcm
