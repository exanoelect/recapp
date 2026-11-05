#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkReply;

/**
 * Uploader WAV ke backend-meeting-agent (FastAPI).
 *
 * Semua file dalam satu sesi dikirim dengan upload_id yang SAMA, sehingga di
 * server mendarat di satu folder: uploads/<upload_id>/part_000000.wav,
 * part_000001.wav, dst. (index naik otomatis, file dikirim satu per satu).
 *
 * Setelah semua file terkirim, POST /upload/complete SEKALI dengan upload_id dan
 * total (form untuk server asli, JSON untuk server yang dipatch; lihat
 * setCompleteAsJson()).
 *
 * Catatan perilaku server:
 *  - Nama file asli tidak dipertahankan, server menamai part_<index>.wav.
 *  - Server asli menggabungkan semua part jadi final.wav saat complete.
 */
class BackendUploader : public QObject
{
    Q_OBJECT
public:
    explicit BackendUploader(const QUrl &baseUrl, QObject *parent = nullptr);

    void setBaseUrl(const QUrl &baseUrl);
    QUrl baseUrl() const { return m_base; }

    // Timeout per request (ms). 0 = tanpa timeout. Default 60000.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    // Folder tujuan tetap di server, format [a-zA-Z0-9_-]{8,64}.
    //  - Diisi : semua pemanggilan upload*() masuk ke folder ini, index
    //            dilanjutkan dari panggilan sebelumnya (lihat setNextIndex).
    //  - Kosong: tiap pemanggilan upload*() membuat folder baru (UUID).
    void setUploadId(const QString &id);

    // Folder yang sedang/terakhir dipakai.
    QString uploadId() const { return m_currentId; }

    // Index file berikutnya (hanya dipakai bila setUploadId() diisi).
    // Set manual mis. setelah aplikasi restart agar file lama tidak tertimpa.
    void setNextIndex(int index) { m_nextIndex = index; }
    int nextIndex() const { return m_nextIndex; }

    // Format body POST /upload/complete (dipanggil sekali setelah semua file terkirim):
    //  false : form multipart (upload_id, total) -> untuk main.py ASLI. Default.
    //          Server aslinya SELALU menggabungkan part jadi final.wav dan
    //          menghapus part-nya; setMergeOnFinish() tidak berpengaruh.
    //  true  : JSON {"upload_id": "...", "total": N} -> untuk main.py yang sudah
    //          dipatch (endpoint complete menerima JSON, tanpa merge secara default).
    void setCompleteAsJson(bool json) { m_completeAsJson = json; }

    // Hanya berlaku bila setCompleteAsJson(true): tambahkan "merge":true agar
    // server menggabungkan jadi final.wav. false (default) = file tetap terpisah.
    void setMergeOnFinish(bool merge) { m_merge = merge; }

    // Kirim banyak file berurutan ke satu folder.
    void uploadFiles(const QStringList &wavPaths);

    // Kirim satu file.
    void upload(const QString &wavPath);

    // Batalkan upload yang sedang berjalan.
    void abort();

    bool isBusy() const { return m_running; }

signals:
    // Progres file yang sedang dikirim.
    void progress(qint64 sent, qint64 total);

    // Satu file selesai (done dari total file pada panggilan ini).
    void fileUploaded(int done, int total, const QString &path);

    // Semua file selesai (dan merge selesai bila diaktifkan).
    void finished(int httpStatus, const QByteArray &response);

    void failed(const QString &error, int httpStatus, const QByteArray &response);

private:
    QUrl endpoint(const QString &path) const;
    void sendNext();
    void sendComplete();
    void handleError(QNetworkReply *reply);
    void fail(const QString &error);

    QUrl m_base;
    int m_timeoutMs = 60000;
    bool m_merge = false;
    bool m_completeAsJson = false;   // default: kompatibel dengan server asli

    QString m_fixedId;     // dari setUploadId()
    QString m_currentId;   // folder yang dipakai sesi berjalan
    int m_nextIndex = 0;   // lanjutan index untuk folder tetap

    QStringList m_queue;
    int m_total = 0;
    int m_done = 0;
    int m_index = 0;       // index file yang sedang dikirim
    bool m_running = false;
    int m_lastStatus = 0;
    QByteArray m_lastBody;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
};
