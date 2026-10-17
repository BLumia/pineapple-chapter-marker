#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "Chapter.h"

namespace pcm {

struct StreamInfo {
    int index = 0;
    QString typeName;   // video / audio / subtitle / data / attachment
    QString codecName;  // h264 / aac / ...
    QString language;   // und / chi / ... (from stream metadata)
    QString details;    // "1920×1080" or "48000 Hz, 2 ch"
    bool isCover = false; // video stream whose disposition has attached_pic (embedded cover art)
};

// probeMedia's result: when ok=false only errorString is valid.
// Chapters are converted to the unified model: ends equal to the auto value become nullopt,
// so genuine gaps in MKV are preserved as explicit ends.
struct ProbeResult {
    bool ok = false;
    QString errorString;

    QString path;
    QString formatName;      // "matroska,webm"
    QString formatLongName;  // "Matroska / WebM"
    qint64 durationUs = 0;   // 0 when unknown
    qint64 bitrateBps = 0;

    QVector<StreamInfo> streams;
    QVector<Chapter> chapters;
    QHash<QString, QString> metadata; // container-level tags

    bool hasChapters = false; // whether the container has embedded chapters
    bool hasAudioStream = false; // whether an audio stream exists (drives waveform analysis)
    bool hasVideoStream = false; // whether a real video stream exists (cover art excluded; drives the video preview)
};

// Opens and probes a media file (chapters, streams, metadata); no decoding.
ProbeResult probeMedia(const QString& path);

// Builds a one-line human-readable stream summary: "video h264 (1920×1080), audio aac (48000 Hz, 2 ch), cover art mjpeg"
QString streamSummaryLine(const ProbeResult& result);

} // namespace pcm
