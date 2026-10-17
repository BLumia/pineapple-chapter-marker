#include "MediaProbe.h"

#include <algorithm>

#include <QFileInfo>
#include <QStringList>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

namespace pcm {

namespace {

class FormatContextGuard {
public:
    ~FormatContextGuard()
    {
        if (m_ctx)
            avformat_close_input(&m_ctx);
    }
    AVFormatContext *&get() { return m_ctx; }

private:
    AVFormatContext *m_ctx = nullptr;
};

QString dictValue(const AVDictionary *dict, const char *key)
{
    const AVDictionaryEntry *e = av_dict_get(dict, key, nullptr, 0);
    return e ? QString::fromUtf8(e->value) : QString();
}

QString mediaTypeDisplayName(AVMediaType type)
{
    switch (type) {
    case AVMEDIA_TYPE_VIDEO:    return QObject::tr("Video");
    case AVMEDIA_TYPE_AUDIO:    return QObject::tr("Audio");
    case AVMEDIA_TYPE_SUBTITLE: return QObject::tr("Subtitle");
    case AVMEDIA_TYPE_DATA:     return QObject::tr("Data");
    case AVMEDIA_TYPE_ATTACHMENT: return QObject::tr("Attachment");
    default:                    return QObject::tr("Unknown");
    }
}

} // namespace

ProbeResult probeMedia(const QString& path)
{
    ProbeResult result;
    result.path = path;

    FormatContextGuard guard;
    const QByteArray pathUtf8 = path.toUtf8();
    const int err = avformat_open_input(&guard.get(), pathUtf8.constData(), nullptr, nullptr);
    if (err < 0) {
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(err, buf, sizeof(buf));
        result.errorString = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(buf)).arg(err);
        return result;
    }
    AVFormatContext *ctx = guard.get();

    // find_stream_info failing is not fatal: chapters and container metadata are usually available already
    avformat_find_stream_info(ctx, nullptr);

    result.formatName = QString::fromUtf8(ctx->iformat->name);
    result.formatLongName = QString::fromUtf8(ctx->iformat->long_name ? ctx->iformat->long_name : "");
    // ctx->duration is in AV_TIME_BASE units (microseconds)
    if (ctx->duration != AV_NOPTS_VALUE && ctx->duration > 0)
        result.durationUs = qint64(ctx->duration);
    result.bitrateBps = ctx->bit_rate > 0 ? qint64(ctx->bit_rate) : 0;

    result.hasChapters = ctx->nb_chapters > 0;

    struct RawChapter {
        Chapter ch;
        qint64 endUs = -1; // -1 = container provides none
    };
    QVector<RawChapter> raw;
    raw.reserve(int(ctx->nb_chapters));

    for (unsigned i = 0; i < ctx->nb_chapters; ++i) {
        const AVChapter *c = ctx->chapters[i];
        RawChapter rc;
        rc.ch.startUs = av_rescale_q(c->start, c->time_base, AVRational{1, 1000000});
        if (rc.ch.startUs < 0)
            rc.ch.startUs = 0;
        if (c->end != AV_NOPTS_VALUE)
            rc.endUs = av_rescale_q(c->end, c->time_base, AVRational{1, 1000000});
        rc.ch.title = dictValue(c->metadata, "title");
        raw.append(rc);
    }

    std::stable_sort(raw.begin(), raw.end(),
                     [](const RawChapter &a, const RawChapter &b) { return a.ch.startUs < b.ch.startUs; });

    for (int i = 0; i < raw.size(); ++i) {
        RawChapter &rc = raw[i];
        const qint64 autoEnd = (i + 1 < raw.size())
            ? raw.at(i + 1).ch.startUs
            : (result.durationUs > 0 ? result.durationUs : rc.ch.startUs);
        // When the end is invalid or exactly equals the auto value, store nullopt (auto),
        // otherwise keep the explicit end -- this is how MKV gaps survive.
        if (rc.endUs > rc.ch.startUs && rc.endUs != autoEnd)
            rc.ch.endUs = rc.endUs;
        if (rc.ch.title.isEmpty())
            rc.ch.title = QStringLiteral("Chapter %1").arg(i + 1);
        result.chapters.append(rc.ch);
    }

    // ---- streams ----
    result.streams.reserve(int(ctx->nb_streams));
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        const AVStream *st = ctx->streams[i];
        const AVCodecParameters *par = st->codecpar;

        StreamInfo si;
        si.index = st->index;
        si.typeName = mediaTypeDisplayName(par->codec_type);
        if (par->codec_type == AVMEDIA_TYPE_AUDIO)
            result.hasAudioStream = true;
        si.codecName = QString::fromUtf8(avcodec_get_name(par->codec_id));
        si.language = dictValue(st->metadata, "language");
        si.isCover = par->codec_type == AVMEDIA_TYPE_VIDEO
            && (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && !si.isCover)
            result.hasVideoStream = true;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            si.details = QStringLiteral("%1×%2").arg(par->width).arg(par->height);
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            si.details = QStringLiteral("%1 Hz, %2 ch")
                             .arg(par->sample_rate)
                             .arg(int(par->ch_layout.nb_channels));
        }
        result.streams.append(si);
    }

    // ---- container-level metadata ----
    for (const AVDictionaryEntry *t = nullptr; (t = av_dict_iterate(ctx->metadata, t));)
        result.metadata.insert(QString::fromUtf8(t->key), QString::fromUtf8(t->value));

    result.ok = true;
    return result;
}

QString streamSummaryLine(const ProbeResult& result)
{
    QStringList parts;
    for (const StreamInfo &s : result.streams) {
        const QString name = s.isCover ? QObject::tr("Cover art") : s.typeName;
        QString item = QStringLiteral("%1 %2").arg(name, s.codecName);
        if (!s.details.isEmpty())
            item += QStringLiteral(" (%1)").arg(s.details);
        if (!s.language.isEmpty() && s.language != QLatin1String("und"))
            item += QStringLiteral(" [%1]").arg(s.language);
        parts << item;
    }
    return parts.join(QStringLiteral("，"));
}

} // namespace pcm
