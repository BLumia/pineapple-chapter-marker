#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include "ChapterCommands.h"
#include "ChapterTableModel.h"
#include "OverviewBar.h"
#include "PlaybackController.h"
#include "TimelineController.h"
#include "TimestampTextDialog.h"
#include "WaveformView.h"

#include <algorithm>

#include "core/MediaWriter.h"
#include "core/TimeUtils.h"
#include "core/WaveformBuilder.h"
#include "core/WaveformPeaks.h"

#include <QProgressDialog>

namespace pcm {

MainWindow::MainWindow()
{
    m_peaks = new WaveformPeaks();
    m_playback = new PlaybackController(this);
    buildUi();
    updateInfo();
    updateTimeLabel();
    setAcceptDrops(true);
    resize(1200, 840);
}

MainWindow::~MainWindow()
{
    stopWaveformBuild();
    if (m_playback)
        m_playback->stop();
    delete m_peaks;
}

void MainWindow::buildUi()
{
    m_tl = new TimelineController(this);

    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);

    m_info = new QLabel(central);
    m_info->setTextFormat(Qt::RichText);
    m_info->setWordWrap(true);
    m_info->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    lay->addWidget(m_info);

    // Video preview panel: only for files with a real video stream (cover art excluded); toggled with V
    m_videoPanel = new QVideoWidget(central);
    m_videoPanel->setMinimumHeight(140);
    m_playback->attachVideoOutput(m_videoPanel);

    // Waveform area: overview bar (DAW scrollbar) + main waveform view, with the transport bar and chapter table below
    m_overview = new OverviewBar(m_tl, central);
    m_overview->setPeaks(m_peaks);
    m_overview->setChapters(&m_doc);
    m_overview->setPlayback(m_playback);
    m_wave = new WaveformView(m_tl, central);
    m_wave->setPeaks(m_peaks);
    m_wave->setChapters(&m_doc);
    m_wave->setPlayback(m_playback);

    m_model = new ChapterTableModel(m_doc, this);
    m_model->setUndoStack(&m_undoStack);
    m_table = new QTableView(central);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::SelectedClicked);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(26);
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setStretchLastSection(true);
    hh->setSectionResizeMode(ChapterTableModel::ColIndex, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ChapterTableModel::ColStart, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ChapterTableModel::ColEnd, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ChapterTableModel::ColLength, QHeaderView::ResizeToContents);

    auto* splitter = new QSplitter(Qt::Vertical, central);
    splitter->addWidget(m_wave);
    splitter->addWidget(m_table);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({420, 240});

    // Transport bar: play/pause, stop, time display, volume
    auto* playBar = new QWidget(central);
    auto* pb = new QHBoxLayout(playBar);
    pb->setContentsMargins(0, 0, 0, 0);
    pb->setSpacing(8);
    m_playBtn = new QToolButton(playBar);
    m_playBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    m_playBtn->setToolTip(tr("Play / Pause (Space)"));
    m_playBtn->setFocusPolicy(Qt::NoFocus);
    m_stopBtn = new QToolButton(playBar);
    m_stopBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    m_stopBtn->setToolTip(tr("Stop"));
    m_stopBtn->setFocusPolicy(Qt::NoFocus);
    auto *addChBtn = new QToolButton(playBar);
    addChBtn->setText(tr("+ Chapter"));
    addChBtn->setToolTip(tr("Add chapter at playhead (M)"));
    addChBtn->setFocusPolicy(Qt::NoFocus);
    connect(addChBtn, &QToolButton::clicked, this, &MainWindow::addChapterAtPlayhead);
    m_timeLabel = new QLabel(QStringLiteral("00:00:00.000 / 00:00:00.000"), playBar);
    QFont mono = m_timeLabel->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("Consolas"));
    m_timeLabel->setFont(mono);
    auto* volLabel = new QLabel(tr("Volume"), playBar);
    m_volumeSlider = new QSlider(Qt::Horizontal, playBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(110);
    m_volumeSlider->setFocusPolicy(Qt::NoFocus);
    m_volumeSlider->setToolTip(tr("Volume"));
    pb->addWidget(m_playBtn);
    pb->addWidget(m_stopBtn);
    pb->addWidget(addChBtn);
    pb->addWidget(m_timeLabel);
    pb->addStretch(1);
    pb->addWidget(volLabel);
    pb->addWidget(m_volumeSlider);

    // Lower half: overview bar + waveform/table splitter + transport bar
    auto* lower = new QWidget(central);
    auto* lowerLay = new QVBoxLayout(lower);
    lowerLay->setContentsMargins(0, 0, 0, 0);
    lowerLay->setSpacing(6);
    lowerLay->addWidget(m_overview);
    lowerLay->addWidget(splitter, 1);
    lowerLay->addWidget(playBar);

    // Outer splitter: video monitor on top, timeline below (resizable video panel)
    auto* outerSplitter = new QSplitter(Qt::Vertical, central);
    outerSplitter->setChildrenCollapsible(false);
    outerSplitter->addWidget(m_videoPanel);
    outerSplitter->addWidget(lower);
    outerSplitter->setStretchFactor(0, 0);
    outerSplitter->setStretchFactor(1, 1);
    outerSplitter->setSizes({240, 560});
    m_videoPanel->setVisible(false);

    lay->addWidget(outerSplitter, 1);
    setCentralWidget(central);

    connect(m_playBtn, &QToolButton::clicked, m_playback, &PlaybackController::toggle);
    connect(m_stopBtn, &QToolButton::clicked, m_playback, &PlaybackController::stop);
    connect(m_volumeSlider, &QSlider::valueChanged, m_playback, &PlaybackController::setVolume);
    connect(m_playback, &PlaybackController::playbackToggled, this, [this](bool playing) {
        m_playBtn->setIcon(style()->standardIcon(
            playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(m_playback, &PlaybackController::positionChangedUs, this, [this](qint64 us) {
        updateTimeLabel();
        // playhead leaves the viewport while playing -> page-follow
        if (m_playback->isPlaying() && m_tl->viewSpanUs() > 0) {
            const qint64 vs = m_tl->viewStartUs();
            const qint64 span = m_tl->viewSpanUs();
            if (us < vs || us >= vs + span) {
                const qint64 newStart = qBound<qint64>(0, us - span / 4,
                                                       qMax<qint64>(0, m_tl->durationUs() - span));
                m_tl->setView(newStart, span);
            }
        }
    });
    connect(m_playback, &PlaybackController::errorOccurred, this, [this](const QString& msg) {
        statusBar()->showMessage(tr("Playback failed: %1").arg(msg), 8000);
    });

    // Space = play/pause
    QAction *playToggleAct = new QAction(this);
    playToggleAct->setShortcut(QKeySequence(Qt::Key_Space));
    connect(playToggleAct, &QAction::triggered, m_playback, &PlaybackController::toggle);
    addAction(playToggleAct);

    connect(m_model, &ChapterTableModel::documentEdited, this, [this] {
        m_modified = true;
        updateInfo();
        // The waveform view draws markers straight from the document, not via the model's dataChanged,
        // so every document change (edit/undo/redo/import) must trigger a repaint explicitly
        m_wave->update();
        m_overview->update();
    });

    // table selection -> highlight in the waveform view + scroll into view when needed
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &, const QItemSelection &) {
        const QModelIndexList rows = m_table->selectionModel()->selectedRows();
        const int row = rows.isEmpty() ? -1 : rows.first().row();
        m_wave->setSelectedRow(row);
        if (row >= 0 && row < m_doc.chapters.size()) {
            const qint64 t = m_doc.chapters.at(row).startUs;
            const qint64 vs = m_tl->viewStartUs();
            const qint64 span = m_tl->viewSpanUs();
            if (span <= 0 || t < vs || t > vs + span) {
                const qint64 newStart = qBound<qint64>(0, t - span / 2,
                                                       qMax<qint64>(0, m_tl->durationUs() - span));
                m_tl->setView(newStart, span);
            }
        }
    });

    // waveform marker click/drag -> table and document
    connect(m_wave, &WaveformView::chapterClicked, this, [this](int row) {
        m_table->selectRow(row);
    });

    // Table context menu: delete the selected chapter
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const QModelIndex idx = m_table->indexAt(pos);
        if (!idx.isValid())
            return;
        m_table->selectRow(idx.row());
        QMenu menu(this);
        menu.addAction(tr("Delete This Chapter"), this, &MainWindow::deleteSelectedChapter);
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
    connect(m_wave, &WaveformView::markerDragged, this, [this](int row, qint64 newStartUs) {
        if (row < 0 || row >= m_doc.chapters.size())
            return;
        Chapter &ch = m_doc.chapters[row];
        if (ch.startUs == newStartUs)
            return;
        ch.startUs = newStartUs; // the drag end already clamps against neighbors; no crossing possible
        m_model->notifyRowsChanged(qMax(0, row - 1), row);
        m_modified = true;
        updateInfo();
        m_wave->update();
        m_overview->update();
    });

    connect(m_wave, &WaveformView::markerEditCommitted, this,
            [this](int row, qint64 oldStartUs, qint64 newStartUs) {
        if (oldStartUs != newStartUs)
            m_undoStack.push(new StartEditCommand(m_model, row, oldStartUs, newStartUs));
    });

    // ---- actions ----
    QAction *openAct = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                   tr("&Open…"), this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Open Media File"), QString(),
            tr("Media Files (*.mkv *.mka *.mp4 *.m4a *.m4b *.mp3 *.flac *.ogg *.opus *.wav *.wv *.ape);;All Files (*)"));
        if (!file.isEmpty())
            openFile(file);
    });

    m_importAct = new QAction(tr("&Import Timestamp Text…"), this);
    m_importAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(m_importAct, &QAction::triggered, this, &MainWindow::onImportTimestamps);

    m_exportAct = new QAction(tr("&Export Timestamp Text…"), this);
    m_exportAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_exportAct, &QAction::triggered, this, &MainWindow::onExportTimestamps);

    m_saveAct = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                            tr("&Save"), this);
    m_saveAct->setShortcut(QKeySequence::Save);
    connect(m_saveAct, &QAction::triggered, this, &MainWindow::saveCurrent);

    QAction *saveAsAct = new QAction(tr("Save &As…"), this);
    saveAsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveAsCurrent);

    // Video preview toggle: only for media with a real video stream; detaching when hidden stops video decoding
    m_videoAct = new QAction(tr("&Video Preview"), this);
    m_videoAct->setCheckable(true);
    m_videoAct->setShortcut(QKeySequence(QStringLiteral("V")));
    m_videoAct->setEnabled(false);
    connect(m_videoAct, &QAction::toggled, this, [this](bool on) {
        m_videoPanel->setVisible(on);
        m_playback->attachVideoOutput(on ? static_cast<QObject*>(m_videoPanel) : nullptr);
    });

    // Frame stepping (±33ms while paused, roughly one frame)
    QAction *stepBackAct = new QAction(this);
    stepBackAct->setShortcut(QKeySequence(QStringLiteral(",")));
    connect(stepBackAct, &QAction::triggered, m_playback, [this] {
        if (m_playback->isPlaying())
            m_playback->pause();
        m_playback->seekUs(qMax<qint64>(0, m_playback->positionUs() - 33366));
    });
    QAction *stepFwdAct = new QAction(this);
    stepFwdAct->setShortcut(QKeySequence(QStringLiteral(".")));
    connect(stepFwdAct, &QAction::triggered, m_playback, [this] {
        if (m_playback->isPlaying())
            m_playback->pause();
        m_playback->seekUs(qMin(m_tl->durationUs(), m_playback->positionUs() + 33366));
    });
    addAction(stepBackAct);
    addAction(stepFwdAct);

    // Add a chapter at the playhead (creates the first/last pair when no chapters exist)
    QAction *addChapterAct = new QAction(tr("Add Chapter at &Playhead"), this);
    addChapterAct->setShortcut(QKeySequence(QStringLiteral("M")));
    connect(addChapterAct, &QAction::triggered, this, &MainWindow::addChapterAtPlayhead);
    addAction(addChapterAct);

    // Delete the selected chapter: Del key / Edit menu / table context menu
    QAction *deleteChapterAct = new QAction(tr("&Delete Selected Chapter"), this);
    deleteChapterAct->setShortcut(QKeySequence::Delete);
    connect(deleteChapterAct, &QAction::triggered, this, &MainWindow::deleteSelectedChapter);
    addAction(deleteChapterAct);

    QAction *exitAct = new QAction(tr("E&xit"), this);
    exitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(exitAct, &QAction::triggered, this, &MainWindow::close);

    QAction *aboutAct = new QAction(tr("&About…"), this);
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About Pineapple Chapter Marker"),
            tr("Audio/video chapter marker editor built on Qt 6 + FFmpeg.\n\n"
               "Waveform view: Ctrl+wheel to zoom / wheel to pan / drag to pan\n"
               "Overview bar: drag the viewport to pan, drag its edges to zoom, click to jump\n"
               "Drag the red chapter markers to adjust chapter starts\n\n"
               "Saving remuxes the file with stream copy (no re-encoding) and\n"
               "automatically backs up the original as *.bak when overwriting.\n"
               "Avoid keeping the target file locked in another player while saving."));
    });

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAct);
    fileMenu->addAction(m_saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addSeparator();
    fileMenu->addAction(m_importAct);
    fileMenu->addAction(m_exportAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);

    // Edit: undo / redo
    QAction *undoAct = m_undoStack.createUndoAction(this, tr("&Undo"));
    undoAct->setShortcuts(QKeySequence::Undo);
    QAction *redoAct = m_undoStack.createRedoAction(this, tr("&Redo"));
    redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Y"))});
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);
    editMenu->addSeparator();
    editMenu->addAction(addChapterAct);
    editMenu->addAction(deleteChapterAct);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_videoAct);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAct);

    QToolBar *tb = addToolBar(tr("Main Toolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(openAct);
    tb->addAction(m_saveAct);
    tb->addAction(undoAct);
    tb->addAction(redoAct);
    tb->addAction(m_importAct);
    tb->addAction(m_exportAct);
    tb->addAction(m_videoAct);
    undoAct->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    redoAct->setIcon(QIcon::fromTheme(QStringLiteral("edit-redo")));

    m_importAct->setEnabled(false);
    m_exportAct->setEnabled(false);
    m_saveAct->setEnabled(false);

    statusBar()->showMessage(tr("Ready"));
}

bool MainWindow::openFile(const QString& path)
{
    const ProbeResult r = probeMedia(path);
    if (!r.ok) {
        QMessageBox::warning(this, tr("Open Failed"),
                             tr("Cannot open file:\n%1\n\n%2").arg(path, r.errorString));
        return false;
    }

    stopWaveformBuild();
    m_peaks->reset();

    m_probe = r;
    m_currentPath = path;
    m_doc.durationUs = r.durationUs;
    setChapters(r.chapters);
    m_modified = false;
    m_undoStack.clear();
    m_importAct->setEnabled(true);
    m_exportAct->setEnabled(true);

    // Switch the player to the new file
    m_playback->stop();
    m_playback->setSource(path);
    updateTimeLabel();

    // Video preview: enabled only with a real video stream (embedded covers excluded)
    const bool hasVideo = r.hasVideoStream;
    m_videoAct->setEnabled(hasVideo);
    m_videoPanel->setVisible(hasVideo);
    m_videoAct->setChecked(hasVideo);
    if (hasVideo)
        m_playback->attachVideoOutput(m_videoPanel);
    else
        m_playback->attachVideoOutput(nullptr);

    // Timeline: reset to the full range
    m_tl->setDuration(qMax<qint64>(r.durationUs, 1));
    m_tl->setView(0, qMax<qint64>(r.durationUs, 1));
    m_wave->setAudioAvailable(r.hasAudioStream);
    m_overview->setAudioAvailable(r.hasAudioStream);
    m_wave->setSelectedRow(-1);
    m_wave->update();
    m_overview->update();

    startWaveformBuild(path, r.hasAudioStream);
    updateInfo();

    const QString msg = r.hasChapters
        ? tr("Loaded %1: read %2 chapters").arg(QFileInfo(path).fileName()).arg(r.chapters.size())
        : tr("Loaded %1: the file has no embedded chapters; import from text to add them").arg(QFileInfo(path).fileName());
    statusBar()->showMessage(msg, 8000);
    return true;
}

void MainWindow::startWaveformBuild(const QString& path, bool hasAudio)
{
    if (!hasAudio)
        return;
    stopWaveformBuild();
    m_builder = new WaveformBuilder(path, m_peaks, this);
    connect(m_builder, &WaveformBuilder::progressChanged, this, [this](int percent) {
        statusBar()->showMessage(percent >= 0
            ? tr("Analyzing audio… %1%").arg(percent)
            : tr("Analyzing audio…"));
        m_wave->update();
        m_overview->update();
    });
    connect(m_builder, &WaveformBuilder::buildFinished, this, [this](bool ok, const QString& msg) {
        statusBar()->showMessage(msg, 8000);
        // fall back to the real audio duration when the container duration is missing
        if (ok && m_doc.durationUs <= 0 && m_peaks->durationUs() > 0) {
            m_doc.durationUs = m_peaks->durationUs();
            m_tl->setDuration(m_doc.durationUs);
            m_tl->setView(0, m_doc.durationUs);
            updateInfo();
        }
        m_wave->update();
        m_overview->update();
    });
    m_builder->start();
}

void MainWindow::stopWaveformBuild()
{
    if (!m_builder)
        return;
    if (m_builder->isRunning()) {
        m_builder->cancel();
        m_builder->wait();
    }
    delete m_builder;
    m_builder = nullptr;
}

void MainWindow::setChapters(const QVector<Chapter>& chapters)
{
    m_doc.chapters = chapters;
    m_doc.sortByStart();
    m_model->refresh();
}

void MainWindow::updateInfo()
{
    if (m_currentPath.isEmpty()) {
        m_info->setText(tr("<b>Drop a media file here</b> or use File → Open…<br>"
                           "Supports MKV / MKA / MP4 / M4A / M4B / MP3 and more."));
        setWindowTitle(QStringLiteral("Pineapple Chapter Marker"));
        return;
    }

    const QString fmt = m_probe.formatLongName.isEmpty() ? m_probe.formatName : m_probe.formatLongName;
    const QString dur = m_doc.durationUs > 0 ? time::formatClockFixed(m_doc.durationUs) : tr("Unknown");
    QString text = tr("<b>%1</b> · %2 · Duration %3 · <b>%4</b> chapters")
                       .arg(QFileInfo(m_currentPath).fileName(), fmt, dur)
                       .arg(m_doc.chapters.size());
    const QString streams = streamSummaryLine(m_probe);
    if (!streams.isEmpty())
        text += QStringLiteral("<br><span style=\"color:gray\">%1</span>").arg(streams.toHtmlEscaped());
    m_info->setText(text);

    m_saveAct->setEnabled(m_modified && !m_saver);
    setWindowTitle(QStringLiteral("%1%2 - Pineapple Chapter Marker")
                       .arg(QFileInfo(m_currentPath).fileName(),
                            m_modified ? QStringLiteral("[*]") : QString()));
}

void MainWindow::updateTimeLabel()
{
    m_timeLabel->setText(QStringLiteral("%1 / %2")
                             .arg(time::formatClockFixed(m_playback->positionUs()),
                                  time::formatClockFixed(m_doc.durationUs)));
}

void MainWindow::onImportTimestamps()
{
    TimestampTextDialog dlg(TimestampTextDialog::Import, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QVector<Chapter> oldList = m_doc.chapters;
    setChapters(dlg.importedChapters());
    if (oldList != m_doc.chapters)
        m_undoStack.push(new ReplaceAllCommand(m_model, oldList, m_doc.chapters));
    m_modified = true;
    updateInfo();
    m_wave->update();
    m_overview->update();
    statusBar()->showMessage(tr("Imported %1 chapters").arg(m_doc.chapters.size()), 5000);
}

void MainWindow::onExportTimestamps()
{
    TimestampTextDialog dlg(TimestampTextDialog::Export, this);
    dlg.setExportText(time::toYouTubeText(m_doc.chapters),
                      time::youTubeRuleWarnings(m_doc.chapters));
    dlg.exec();
}

void MainWindow::addChapterAtPlayhead()
{
    if (m_currentPath.isEmpty() || m_doc.durationUs <= 2000) {
        statusBar()->showMessage(tr("Media duration unknown; wait for waveform analysis before adding chapters"), 5000);
        return;
    }
    // clamp the playhead position to [1ms, duration-1ms]
    const qint64 pos = qBound<qint64>(1000, m_playback->positionUs(), m_doc.durationUs - 1000);

    QVector<Chapter> next = m_doc.chapters;
    int insertRow;

    if (next.isEmpty()) {
        // no chapters yet: create the first/last pair (0 -> playhead -> end)
        Chapter first;
        first.startUs = 0;
        first.title = QStringLiteral("Chapter 1");
        Chapter second;
        second.startUs = pos;
        second.title = QStringLiteral("Chapter 2");
        next = {first, second};
        insertRow = 1;
    } else {
        for (const Chapter &c : next) {
            if (qAbs(c.startUs - pos) < 1000) {
                statusBar()->showMessage(tr("A chapter already exists at this position"), 4000);
                return;
            }
        }
        insertRow = static_cast<int>(
            std::count_if(next.cbegin(), next.cend(),
                          [pos](const Chapter &c) { return c.startUs < pos; }));
        Chapter c;
        c.startUs = pos;
        c.title = QStringLiteral("Chapter %1").arg(next.size() + 1);
        next.insert(insertRow, c);
        // split semantics: a previous explicit end past the new start becomes auto (inherits the new start)
        if (insertRow > 0) {
            Chapter &prev = next[insertRow - 1];
            if (prev.endUs && *prev.endUs > pos)
                prev.endUs = std::nullopt;
        }
    }

    const QVector<Chapter> oldList = m_doc.chapters;
    m_model->applyChapters(next);
    auto *cmd = new ReplaceAllCommand(m_model, oldList, m_doc.chapters);
    cmd->setText(tr("Add chapter at playhead"));
    m_undoStack.push(cmd);

    m_table->selectRow(insertRow);
    updateInfo();
    statusBar()->showMessage(tr("Chapter added at %1").arg(time::formatClockCompact(pos)), 4000);
}

void MainWindow::deleteSelectedChapter()
{
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        statusBar()->showMessage(tr("Select a chapter in the table or waveform first"), 4000);
        return;
    }
    const int row = rows.first().row();
    if (row < 0 || row >= m_doc.chapters.size())
        return;

    const QVector<Chapter> oldList = m_doc.chapters;
    QVector<Chapter> next = oldList;
    const Chapter removed = next.takeAt(row);

    m_model->applyChapters(next);
    auto *cmd = new ReplaceAllCommand(m_model, oldList, m_doc.chapters);
    cmd->setText(tr("Delete chapter \"%1\"").arg(removed.title));
    m_undoStack.push(cmd);

    m_wave->setSelectedRow(-1);
    updateInfo();
    statusBar()->showMessage(tr("Deleted chapter \"%1\"").arg(removed.title), 4000);
}

void MainWindow::saveCurrent()
{
    if (m_currentPath.isEmpty())
        return;
    if (!m_modified) {
        statusBar()->showMessage(tr("No unsaved changes"), 4000);
        return;
    }
    doSave(m_currentPath, true);
}

void MainWindow::saveAsCurrent()
{
    if (m_currentPath.isEmpty())
        return;
    const QString target = QFileDialog::getSaveFileName(
        this, tr("Save Chapters As"), m_currentPath,
        tr("MKV (*.mkv);;MKA (*.mka);;MP4 (*.mp4);;M4A (*.m4a);;M4B (*.m4b);;MP3 (*.mp3);;All Files (*)"));
    if (target.isEmpty())
        return;
    doSave(target, false);
}

bool MainWindow::doSave(const QString& targetPath, bool makeBackup)
{
    if (m_saver) {
        QMessageBox::information(this, tr("Save"), tr("A save task is already in progress."));
        return false;
    }
    m_playback->pause(); // pause playback while writing the file
    if (m_doc.chapters.isEmpty() && !QFileInfo(targetPath).exists()) {
        QMessageBox::information(this, tr("Save"), tr("There are no chapters to write."));
        return false;
    }

    // Container capability check and warning
    const SaveCapability cap = saveCapabilityFor(targetPath);
    if (!cap.canWriteChapters) {
        QMessageBox::warning(this, tr("Cannot Write Chapters"),
            tr("The target container does not support writing chapters.\n"
               "Supported: MKV / MKA / WebM / MP4 / M4A / M4B / MP3.\n"
               "Use File → Save As to pick one of these formats."));
        return false;
    }
    const QVector<int> losing = chaptersLosingEnds(m_doc.chapters, m_doc.durationUs, cap);
    if (!losing.isEmpty()) {
        QStringList preview;
        for (int k = 0; k < losing.size() && k < 5; ++k) {
            const Chapter &ch = m_doc.chapters.at(losing.at(k));
            preview << tr("Chapter %1 \"%2\"").arg(losing.at(k) + 1).arg(ch.title);
        }
        if (losing.size() > 5)
            preview << tr("…and %1 more").arg(losing.size());
        const auto btn = QMessageBox::warning(
            this, tr("Chapter Gaps Will Be Truncated"),
            tr("The target container (%1) cannot represent \"chapter end ≠ next chapter start\".\n"
               "The explicit end times of these %2 chapters will be removed:\n%3\n\nSave anyway?")
                .arg(cap.containerDescription).arg(losing.size()).arg(preview.join(QStringLiteral("; "))),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Ok)
            return false;
    }
    if (makeBackup && m_doc.chapters.isEmpty() && m_probe.hasChapters) {
        const auto btn = QMessageBox::warning(this, tr("Remove Chapters"),
            tr("The chapter list is empty; saving will remove all chapters from the original file.\nContinue anyway?"),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Ok)
            return false;
    }

    // Run in the background + modal progress
    SaveRequest req;
    req.sourcePath = m_currentPath;
    req.targetPath = targetPath;
    req.chapters = m_doc.chapters;
    req.durationUs = m_doc.durationUs;
    req.makeBackup = makeBackup;
    m_saver = new SaveWorker(req, this);

    QProgressDialog dlg(tr("Writing (stream copy, no re-encoding)…"), tr("Cancel"), 0, 100, this);
    dlg.setWindowTitle(tr("Save"));
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);

    bool ok = false;
    bool canceled = false;
    QString message;
    QString backupPath;
    connect(m_saver, &SaveWorker::progressChanged, &dlg,
            [&dlg](int percent) { if (percent >= 0) dlg.setValue(percent); });
    connect(m_saver, &SaveWorker::saveFinished, &dlg,
            [&](bool o, const QString &m, const QString &b) {
                ok = o;
                message = m;
                backupPath = b;
                dlg.accept();
            });
    connect(&dlg, &QProgressDialog::canceled, m_saver, &SaveWorker::cancel);
    m_saver->start();
    dlg.exec();
    m_saver->wait();
    canceled = m_saver->wasCanceled();
    m_saver->deleteLater();
    m_saver = nullptr;

    if (ok) {
        if (!makeBackup && targetPath != m_currentPath) {
            // Save as: the document moves to the new file (re-probe to refresh the header info)
            m_currentPath = targetPath;
            const ProbeResult r = probeMedia(targetPath);
            if (r.ok) {
                m_probe = r;
                m_doc.durationUs = r.durationUs;
            }
        }
        m_modified = false;
        updateInfo();
        statusBar()->showMessage(message, 10000);
        return true;
    }
    if (canceled) {
        statusBar()->showMessage(tr("Save canceled"), 4000);
        return false;
    }
    QMessageBox::critical(this, tr("Save Failed"), message);
    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &u : urls) {
        const QString local = u.toLocalFile();
        if (!local.isEmpty()) {
            openFile(local);
            break;
        }
    }
}

} // namespace pcm
