#pragma once

#include <QMainWindow>
#include <QUndoStack>
#include <QVector>
#include <QVideoWidget>

#include "core/ChapterDocument.h"
#include "core/MediaProbe.h"

class QLabel;
class QTableView;
class QAction;
class QToolButton;
class QLabel;
class QSlider;

namespace pcm {

class ChapterTableModel;
class TimelineController;
class WaveformView;
class OverviewBar;
class WaveformPeaks;
class WaveformBuilder;
class SaveWorker;
class PlaybackController;

// Main window: overview bar + main waveform view + chapter table + transport bar.
// Background waveform analysis (WaveformBuilder) + two-way view/table linkage + save pipeline (SaveWorker)
// + playback preview (PlaybackController) + undo/redo (QUndoStack).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    bool openFile(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    void updateInfo();
    void updateTimeLabel();
    void setChapters(const QVector<Chapter>& chapters);
    void onImportTimestamps();
    void onExportTimestamps();
    void startWaveformBuild(const QString& path, bool hasAudio);
    void stopWaveformBuild();
    void saveCurrent();
    void saveAsCurrent();
    bool doSave(const QString& targetPath, bool makeBackup);
    void addChapterAtPlayhead();
    void deleteSelectedChapter();

    ProbeResult m_probe;
    ChapterDocument m_doc;
    ChapterTableModel* m_model = nullptr;
    QTableView* m_table = nullptr;
    QLabel* m_info = nullptr;
    QAction* m_importAct = nullptr;
    QAction* m_exportAct = nullptr;
    QAction* m_saveAct = nullptr;
    QAction* m_videoAct = nullptr;
    QVideoWidget* m_videoPanel = nullptr;
    QString m_currentPath;
    bool m_modified = false;

    QUndoStack m_undoStack;

    TimelineController* m_tl = nullptr;
    WaveformPeaks* m_peaks = nullptr;
    WaveformBuilder* m_builder = nullptr;
    WaveformView* m_wave = nullptr;
    OverviewBar* m_overview = nullptr;
    SaveWorker* m_saver = nullptr;
    PlaybackController* m_playback = nullptr;
    QToolButton* m_playBtn = nullptr;
    QToolButton* m_stopBtn = nullptr;
    QLabel* m_timeLabel = nullptr;
    QSlider* m_volumeSlider = nullptr;
};

} // namespace pcm
