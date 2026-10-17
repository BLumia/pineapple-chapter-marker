#pragma once

#include <QThread>
#include <atomic>
#include <functional>

#include "Chapter.h"

namespace pcm {

// Chapter writing capability of the target container
struct SaveCapability {
    bool canWriteChapters = false;
    bool preservesGaps = false; // whether "chapter end ≠ next chapter start" can be preserved
    QString containerDescription;
};

// Determines the writing capability from the target file's extension
SaveCapability saveCapabilityFor(const QString& targetPath);

// Which chapters lose their explicit end when saving to targetPath (gap-losing containers only)
QVector<int> chaptersLosingEnds(const QVector<Chapter>& chapters, qint64 durationUs,
                                const SaveCapability& cap);

struct SaveRequest {
    QString sourcePath;  // A/V data source (the file being remuxed)
    QString targetPath;  // Output file (may equal sourcePath = save over the original)
    QVector<Chapter> chapters;
    qint64 durationUs = 0; // media duration needed for chapter auto ends
    bool makeBackup = false; // whether to back up an existing target as *.bak
};

struct SaveResult {
    bool ok = false;
    QString message;
    QString backupPath;
};

// Synchronous: remux (stream copy) -> validate -> backup/replace.
// Set cancelled to abort; progress callback 0..100 (may be invoked on any thread).
SaveResult executeSave(const SaveRequest& request, const std::atomic_bool& cancelled,
                       const std::function<void(int)>& progress);

// Thread wrapper around executeSave: progress signal plus cancellation
class SaveWorker : public QThread {
    Q_OBJECT
public:
    SaveWorker(SaveRequest request, QObject* parent = nullptr);
    ~SaveWorker() override;

    void cancel() { m_cancelled.store(true); }
    bool wasCanceled() const { return m_cancelled.load(); }

    void run() override;

signals:
    void progressChanged(int percent);
    void saveFinished(bool ok, const QString& message, const QString& backupPath);

private:
    SaveRequest m_request;
    std::atomic_bool m_cancelled{false};
};

} // namespace pcm
