#include "PlaybackController.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

namespace pcm {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(1.0f);

    // Smooth playhead motion: high-frequency polling
    m_pollTimer.setInterval(33);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        emit positionChangedUs(positionUs());
    });

    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState)
            m_pollTimer.start();
        else {
            m_pollTimer.stop();
            emit positionChangedUs(positionUs());
        }
        emit playbackToggled(state == QMediaPlayer::PlayingState);
    });

    // instant feedback when seeking while paused/stopped
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (!m_pollTimer.isActive())
            emit positionChangedUs(ms * 1000);
    });

    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& errorString) {
        emit errorOccurred(errorString);
    });
}

void PlaybackController::attachVideoOutput(QObject* output)
{
    m_player->setVideoOutput(output);
}

void PlaybackController::setSource(const QString& path)
{
    m_player->setSource(QUrl::fromLocalFile(path));
}

qint64 PlaybackController::positionUs() const
{
    return m_player->position() * 1000;
}

bool PlaybackController::isPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

void PlaybackController::toggle()
{
    if (isPlaying())
        m_player->pause();
    else
        m_player->play();
}

void PlaybackController::pause()
{
    m_player->pause();
}

void PlaybackController::stop()
{
    m_player->stop();
}

void PlaybackController::seekUs(qint64 us)
{
    m_player->setPosition(int(us / 1000));
    emit positionChangedUs(us); // instant seek feedback
}

void PlaybackController::setVolume(int percent)
{
    m_audio->setVolume(qBound(0, percent, 100) / 100.0f);
}

} // namespace pcm
