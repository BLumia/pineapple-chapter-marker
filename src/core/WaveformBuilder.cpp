#include "WaveformBuilder.h"

#include <QMutexLocker>

#include "core/TimeUtils.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>

namespace {

inline float norm(uint8_t v) { return (int(v) - 128) / 128.f; }
inline float norm(int16_t v) { return v / 32768.f; }
inline float norm(int32_t v) { return v / 2147483648.f; }
inline float norm(float v) { return v; }
inline float norm(double v) { return float(v); }

class FormatContextGuard {
public:
    explicit FormatContextGuard(AVFormatContext** ctx) : m_ctx(ctx) {}
    ~FormatContextGuard()
    {
        if (m_ctx && *m_ctx)
            avformat_close_input(m_ctx);
    }

private:
    AVFormatContext** m_ctx;
};

template <typename T>
class AllocGuard {
public:
    explicit AllocGuard(T** p) : m_p(p) {}
    ~AllocGuard()
    {
        if (m_p && *m_p) {
            if constexpr (std::is_same_v<T, AVCodecContext>)
                avcodec_free_context(m_p);
            else if constexpr (std::is_same_v<T, AVPacket>)
                av_packet_free(m_p);
            else if constexpr (std::is_same_v<T, AVFrame>)
                av_frame_free(m_p);
            *m_p = nullptr;
        }
    }

private:
    T** m_p;
};

} // namespace

namespace pcm {

WaveformBuilder::WaveformBuilder(QString path, WaveformPeaks* peaks, QObject* parent)
    : QThread(parent), m_path(std::move(path)), m_peaks(peaks)
{
}

WaveformBuilder::~WaveformBuilder() = default;

void WaveformBuilder::addSample(float lo, float hi, float meanSquare)
{
    // Counted once per sample instant (channels are merged here) so bucket width is in instants
    // and consistent with the sampleRate timebase -- otherwise stereo would count as double length.
    const qint16 sLo = qint16(qBound(-32768, int(lo * 32767.f), 32767));
    const qint16 sHi = qint16(qBound(-32768, int(hi * 32767.f), 32767));
    if (m_accEmpty) {
        m_accLo = sLo;
        m_accHi = sHi;
        m_accSumsq = 0.0;
        m_accEmpty = false;
        m_accCount = 0;
    } else {
        m_accLo = std::min(m_accLo, sLo);
        m_accHi = std::max(m_accHi, sHi);
    }
    m_accSumsq += double(meanSquare) * (32767.0 * 32767.0);
    ++m_accCount;
    ++m_realSamples;
    if (m_accCount >= m_baseBucket) {
        const auto rms = quint16(qBound(0, int(std::sqrt(m_accSumsq / double(m_accCount))), 32767));
        m_peaks->appendBaseBucket(m_accLo, m_accHi, rms);
        m_accEmpty = true;
    }

    // progress reporting (roughly every 1%)
    if (m_estimatedSamples > 0) {
        const int pct = int(qMin<qint64>(99, m_realSamples * 100 / m_estimatedSamples));
        if (pct != m_lastPercent) {
            m_lastPercent = pct;
            emit progressChanged(pct);
        }
    }
}

void WaveformBuilder::flushPartialBucket()
{
    if (!m_accEmpty) {
        const auto rms = quint16(qBound(0, int(std::sqrt(m_accSumsq / double(m_accCount))), 32767));
        m_peaks->appendBaseBucket(m_accLo, m_accHi, rms);
        m_accEmpty = true;
    }
}

template<typename T>
void WaveformBuilder::processFrameData(AVFrame* frame, int nbSamples, int channels, bool planar)
{
    // Aggregate channels per sample instant: min/max for the envelope, power averaged,
    // producing exactly one peak sample per instant (channels are not separate time points).
    if (planar) {
        for (int i = 0; i < nbSamples; ++i) {
            float lo = 0.f, hi = 0.f, meanSquare = 0.f;
            for (int c = 0; c < channels; ++c) {
                const T *p = reinterpret_cast<const T*>(frame->data[c]);
                const float v = norm(p[i]);
                if (c == 0) {
                    lo = hi = v;
                } else {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                meanSquare += v * v;
            }
            addSample(lo, hi, meanSquare / float(channels));
        }
    } else {
        const T *p = reinterpret_cast<const T*>(frame->data[0]);
        for (int i = 0; i < nbSamples; ++i) {
            float lo = 0.f, hi = 0.f, meanSquare = 0.f;
            for (int c = 0; c < channels; ++c) {
                const float v = norm(p[i * channels + c]);
                if (c == 0) {
                    lo = hi = v;
                } else {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                meanSquare += v * v;
            }
            addSample(lo, hi, meanSquare / float(channels));
        }
    }
}

void WaveformBuilder::run()
{
    AVFormatContext *fmt = nullptr;
    FormatContextGuard fmtGuard(&fmt);
    const QByteArray pathUtf8 = m_path.toUtf8();
    int ret = avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, buf, sizeof(buf));
        emit buildFinished(false, tr("Waveform analysis failed: cannot open file: %1").arg(QString::fromUtf8(buf)));
        return;
    }
    avformat_find_stream_info(fmt, nullptr);

    int audioIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = int(i);
            break;
        }
    }
    if (audioIdx < 0) {
        emit buildFinished(false, tr("Waveform analysis failed: no audio stream found"));
        return;
    }

    const AVCodecParameters *par = fmt->streams[audioIdx]->codecpar;
    const qint64 sampleRate = par->sample_rate;
    if (sampleRate <= 0) {
        emit buildFinished(false, tr("Waveform analysis failed: invalid sample rate"));
        return;
    }

    // Sample format whitelist (hand-written normalization covering the common formats)
    switch (par->format) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
        break;
    default:
        emit buildFinished(false, tr("Waveform analysis failed: unsupported sample format %1")
                                   .arg(QString::fromUtf8(av_get_sample_fmt_name(AVSampleFormat(par->format)))));
        return;
    }

    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        emit buildFinished(false, tr("Waveform analysis failed: decoder not found: %1")
                                     .arg(QString::fromUtf8(avcodec_get_name(par->codec_id))));
        return;
    }
    AVCodecContext *cc = avcodec_alloc_context3(dec);
    AllocGuard<AVCodecContext> ccGuard(&cc);
    if (avcodec_parameters_to_context(cc, par) < 0 || avcodec_open2(cc, dec, nullptr) < 0) {
        emit buildFinished(false, tr("Waveform analysis failed: cannot initialize the audio decoder"));
        return;
    }

    // Estimate the total sample count (for progress; unknown when duration is missing)
    m_estimatedSamples = fmt->duration > 0
        ? av_rescale(fmt->duration, sampleRate, AV_TIME_BASE)
        : 0;
    m_peaks->begin(m_estimatedSamples, sampleRate);
    emit progressChanged(m_estimatedSamples > 0 ? 0 : -1);

    AVPacket *pkt = av_packet_alloc();
    AllocGuard<AVPacket> pktGuard(&pkt);
    AVFrame *frame = av_frame_alloc();
    AllocGuard<AVFrame> frameGuard(&frame);

    bool eofSent = false;
    bool failed = false;
    QString failMessage;

    while (!m_cancelled.load()) {
        ret = av_read_frame(fmt, pkt);
        const bool eof = ret < 0;
        if (!eof) {
            if (pkt->stream_index != audioIdx) {
                av_packet_unref(pkt);
                continue;
            }
        }

        ret = avcodec_send_packet(cc, eof ? nullptr : pkt);
        if (!eof)
            av_packet_unref(pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            failed = true;
            failMessage = tr("Waveform analysis failed: audio decode error (%1)").arg(ret);
            break;
        }
        if (eof)
            eofSent = true;

        while (true) {
            ret = avcodec_receive_frame(cc, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                failed = true;
                failMessage = tr("Waveform analysis failed: audio decode error (%1)").arg(ret);
                break;
            }

            const int nb = frame->nb_samples;
            const int ch = frame->ch_layout.nb_channels;
            if (nb > 0 && ch > 0) {
                switch (frame->format) {
                case AV_SAMPLE_FMT_U8:   processFrameData<uint8_t>(frame, nb, ch, false); break;
                case AV_SAMPLE_FMT_U8P:  processFrameData<uint8_t>(frame, nb, ch, true);  break;
                case AV_SAMPLE_FMT_S16:  processFrameData<int16_t>(frame, nb, ch, false); break;
                case AV_SAMPLE_FMT_S16P: processFrameData<int16_t>(frame, nb, ch, true);  break;
                case AV_SAMPLE_FMT_S32:  processFrameData<int32_t>(frame, nb, ch, false); break;
                case AV_SAMPLE_FMT_S32P: processFrameData<int32_t>(frame, nb, ch, true);  break;
                case AV_SAMPLE_FMT_FLT:  processFrameData<float>(frame, nb, ch, false);   break;
                case AV_SAMPLE_FMT_FLTP: processFrameData<float>(frame, nb, ch, true);    break;
                case AV_SAMPLE_FMT_DBL:  processFrameData<double>(frame, nb, ch, false);  break;
                case AV_SAMPLE_FMT_DBLP: processFrameData<double>(frame, nb, ch, true);   break;
                default: break;
                }
            }
            av_frame_unref(frame);
        }
        if (failed)
            break;
        if (eofSent)
            break;
    }

    if (m_cancelled.load())
        return; // silent abort
    if (failed) {
        emit buildFinished(false, failMessage);
        return;
    }

    flushPartialBucket();
    m_peaks->setTotalSamples(m_realSamples);
    emit progressChanged(100);
    emit buildFinished(true, QStringLiteral("Audio analysis finished (audio duration %1)")
                               .arg(time::formatClockCompact(m_realSamples * 1000000 / sampleRate)));
}

} // namespace pcm
