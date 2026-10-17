#include "WaveformPeaks.h"

#include <QMutexLocker>

#include <algorithm>

namespace pcm {

void WaveformPeaks::reset()
{
    const QMutexLocker lock(&m_mutex);
    m_sampleRate = 0;
    m_totalSamples = 0;
    m_levels.clear();
}

bool WaveformPeaks::isReady() const
{
    const QMutexLocker lock(&m_mutex);
    return m_sampleRate > 0;
}

qint64 WaveformPeaks::sampleRate() const
{
    const QMutexLocker lock(&m_mutex);
    return m_sampleRate;
}

qint64 WaveformPeaks::totalSamples() const
{
    const QMutexLocker lock(&m_mutex);
    return m_totalSamples;
}

qint64 WaveformPeaks::durationUs() const
{
    const QMutexLocker lock(&m_mutex);
    return m_sampleRate > 0 ? (m_totalSamples * 1000000) / m_sampleRate : 0;
}

void WaveformPeaks::begin(qint64 estimatedTotalSamples, qint64 sampleRate)
{
    const QMutexLocker lock(&m_mutex);
    m_sampleRate = sampleRate;
    m_totalSamples = 0;
    m_levels.clear();

    // Level 0 + 7 coarser levels (256 * 8^7 ≈ 1.1e9 samples/bucket; covers very long audio)
    Level l0;
    l0.bucketSamples = m_baseBucketSamples;
    const qint64 expectedBuckets = estimatedTotalSamples / m_baseBucketSamples + 1;
    l0.data.reserve(int(qBound<qint64>(0, expectedBuckets, qint64(20000000))));
    m_levels.append(l0);
    qint64 bucket = m_baseBucketSamples;
    for (int k = 1; k < 8; ++k) {
        bucket *= kFanout;
        Level lk;
        lk.bucketSamples = bucket;
        m_levels.append(lk);
    }
}

void WaveformPeaks::setTotalSamples(qint64 samples)
{
    const QMutexLocker lock(&m_mutex);
    m_totalSamples = samples;
}

void WaveformPeaks::appendBaseBucket(qint16 lo, qint16 hi, quint16 rms)
{
    const QMutexLocker lock(&m_mutex);
    if (m_levels.isEmpty())
        return;
    m_levels[0].data.append(Peak{lo, hi, rms});

    // Cascade: whenever the level below accumulates kFanout buckets, merge one up (RMS takes the max)
    for (int k = 1; k < m_levels.size(); ++k) {
        const Level &prev = m_levels[k - 1];
        if (prev.data.size() % kFanout != 0)
            break;
        const int n = prev.data.size();
        qint16 lo2 = prev.data[n - 1].lo;
        qint16 hi2 = prev.data[n - 1].hi;
        quint16 rms2 = prev.data[n - 1].rms;
        for (int i = n - kFanout; i < n; ++i) {
            lo2 = std::min(lo2, prev.data[i].lo);
            hi2 = std::max(hi2, prev.data[i].hi);
            rms2 = std::max(rms2, prev.data[i].rms);
        }
        m_levels[k].data.append(Peak{lo2, hi2, rms2});
    }
}

bool WaveformPeaks::query(qint64 startUs, qint64 endUs, int columns, QueryResult* out) const
{
    const QMutexLocker lock(&m_mutex);
    if (m_levels.isEmpty() || m_sampleRate <= 0 || columns <= 0 || endUs <= startUs || !out)
        return false;

    out->columns.assign(columns, SampleRange{});

    const qint64 startSamples = qMax<qint64>(0, startUs) * m_sampleRate / 1000000;
    const qint64 spanSamples = qMax<qint64>(1, (endUs - startUs) * m_sampleRate / 1000000);
    const qint64 samplesPerPixel = qMax<qint64>(1, spanSamples / columns);

    // Pick the coarsest level whose bucket <= samples per pixel: each column aggregates 1..kFanout buckets,
    // giving a per-pixel exact envelope without adjacent columns sharing one bucket (blocky artifacts).
    // Deep zoom: no level is fine enough -> use L0 (the view switches to interpolated drawing).
    int li = 0;
    for (int k = 0; k < m_levels.size(); ++k) {
        if (m_levels[k].bucketSamples <= samplesPerPixel)
            li = k;
        else
            break;
    }

    const Level &lv = m_levels[li];
    out->usedBucketSamples = lv.bucketSamples;
    const qint64 bucket = lv.bucketSamples;
    const qint64 filled = lv.data.size();

    for (int c = 0; c < columns; ++c) {
        const qint64 s0 = startSamples + spanSamples * c / columns;
        const qint64 s1 = startSamples + spanSamples * (c + 1) / columns;
        qint64 b0 = s0 / bucket;
        qint64 b1 = s1 / bucket;
        if (b1 <= b0)
            b1 = b0 + 1;
        const qint64 bEnd = std::min<qint64>(b1, filled);

        qint16 lo = 32767;
        qint16 hi = -32768;
        quint16 rms = 0;
        bool any = false;
        for (qint64 b = qMax<qint64>(0, b0); b < bEnd; ++b) {
            const Peak &pk = lv.data[int(b)];
            lo = std::min(lo, pk.lo);
            hi = std::max(hi, pk.hi);
            rms = std::max(rms, pk.rms);
            any = true;
        }
        if (any)
            (*out).columns[c] = SampleRange{lo / 32767.f, hi / 32767.f, rms / 32767.f};
    }
    return true;
}

} // namespace pcm
