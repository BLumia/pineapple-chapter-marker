#pragma once

#include <QThread>
#include <atomic>

#include "core/WaveformPeaks.h"

class AVFrame;

namespace pcm {

// Background thread: decodes the audio stream and streams out the peak pyramid.
// Cancel anytime via cancel() (when opening a new file / quitting).
class WaveformBuilder : public QThread {
    Q_OBJECT
public:
    WaveformBuilder(QString path, WaveformPeaks* peaks, QObject* parent = nullptr);
    ~WaveformBuilder() override;

    void cancel() { m_cancelled.store(true); }

    void run() override;

signals:
    void progressChanged(int percent); // -1 = unknown progress
    void buildFinished(bool ok, const QString& message);

private:
    template<typename T>
    void processFrameData(AVFrame* frame, int nbSamples, int channels, bool planar);
    // Input is one sample instant's per-channel min/max and mean power (mean square).
    // Each instant counts once so the peak timebase matches sampleRate (independent of channel count).
    void addSample(float lo, float hi, float meanSquare);
    void flushPartialBucket();

    QString m_path;
    WaveformPeaks* m_peaks = nullptr;
    std::atomic_bool m_cancelled{false};

    // Level-0 partial bucket accumulator
    const qint64 m_baseBucket = 256;
    qint64 m_accCount = 0;
    qint16 m_accLo = 0;
    qint16 m_accHi = 0;
    double m_accSumsq = 0.0;
    bool m_accEmpty = true;
    qint64 m_realSamples = 0;
    qint64 m_estimatedSamples = 0;
    int m_lastPercent = -1;
};

} // namespace pcm
