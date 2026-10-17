#pragma once

#include <QMutex>
#include <QVector>

namespace pcm {

// Waveform peak pyramid: multi-level min/max + RMS buckets; each level is 8x coarser.
// Level 0 bucket = 256 samples.
//
// Query rule: pick the coarsest level whose bucket <= samples per pixel -- each column aggregates 1..8 buckets,
// giving a per-pixel exact envelope with buckets never shared across columns (no blocky artifacts).
// Deep zoom (fewer samples per pixel than an L0 bucket) falls back to L0, drawn by the view in interpolation mode.
// Written by the builder thread (WaveformBuilder), read by the UI thread; guarded by a mutex,
// so queries/drawing can happen progressively while building.
class WaveformPeaks {
public:
    struct Peak {
        qint16 lo = 0;
        qint16 hi = 0;
        quint16 rms = 0;
    };
    struct Level {
        qint64 bucketSamples = 0; // samples covered by each bucket at this level
        QVector<Peak> data;       // completed buckets
    };
    struct SampleRange { // normalized to [-1, 1]; rms is amplitude 0..1
        float lo = 0.f;
        float hi = 0.f;
        float rms = 0.f;
    };
    struct QueryResult {
        QVector<SampleRange> columns;
        qint64 usedBucketSamples = 0; // bucket width actually used (the view decides interpolation mode from it)
    };

    static constexpr int kFanout = 8;

    void reset();

    bool isReady() const; // true after begin()
    qint64 sampleRate() const;
    qint64 totalSamples() const; // actually decoded sample count (drives the audio duration)
    qint64 durationUs() const;   // 0 when unknown

    // Call once when building starts
    void begin(qint64 estimatedTotalSamples, qint64 sampleRate);
    void setTotalSamples(qint64 samples);
    // Append one completed base bucket (cascades into coarser levels automatically; RMS takes the max)
    void appendBaseBucket(qint16 lo, qint16 hi, quint16 rms);

    // Query the min/max/RMS of each of the columns that evenly split [startUs, endUs).
    // While building is incomplete, not-yet-ready parts return 0.
    bool query(qint64 startUs, qint64 endUs, int columns, QueryResult* out) const;

private:
    mutable QMutex m_mutex;
    qint64 m_sampleRate = 0;
    qint64 m_totalSamples = 0;
    qint64 m_baseBucketSamples = 256;
    QVector<Level> m_levels;
};

} // namespace pcm
