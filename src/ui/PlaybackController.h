#pragma once

#include <QObject>
#include <QTimer>

class QMediaPlayer;
class QAudioOutput;

namespace pcm {

// Playback preview controller: wraps QMediaPlayer/QAudioOutput.
// A 33ms poll produces positionChangedUs, driving the playhead on the waveform views.
class PlaybackController : public QObject {
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);

    void setSource(const QString& path);
    qint64 positionUs() const;
    bool isPlaying() const;

    // Attach/detach the video output (QVideoWidget or another QObject); pass nullptr to detach,
    // detaching while the video panel is hidden avoids continuous video decoding cost.
    void attachVideoOutput(QObject* output);

public slots:
    void toggle();
    void pause();
    void stop();
    void seekUs(qint64 us);
    void setVolume(int percent); // 0..100

signals:
    void playbackToggled(bool playing);
    void positionChangedUs(qint64 us);
    void errorOccurred(const QString& message);

private:
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audio = nullptr;
    QTimer m_pollTimer;
};

} // namespace pcm
