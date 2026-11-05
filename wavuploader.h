#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QByteArray>
#include <QString>

/**
 * WavUploader
 * Mengirim file .wav ke RESTful server via HTTP POST (Qt6).
 *
 * Mode:
 *  - Multipart : multipart/form-data (field file + field tambahan opsional)
 *  - RawBinary : body = isi file mentah, Content-Type: audio/wav
 */
class WavUploader : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Multipart, RawBinary };
    Q_ENUM(Mode)

    explicit WavUploader(QObject *parent = nullptr);
    ~WavUploader() override;

    // --- Konfigurasi ---
    void setUrl(const QUrl &url)                     { m_url = url; }
    void setMode(Mode mode)                          { m_mode = mode; }
    void setFileFieldName(const QString &name)       { m_fieldName = name; }   // default "file"
    void setBearerToken(const QString &token)        { m_bearerToken = token; }
    void setRawHeader(const QByteArray &name, const QByteArray &value);
    void setTimeoutMs(int ms)                        { m_timeoutMs = ms; }     // default 30000
    void setValidateWav(bool enable)                 { m_validateWav = enable; }

    bool isBusy() const { return !m_reply.isNull(); }

public slots:
    /**
     * Mulai upload.
     * @param filePath     path file .wav
     * @param extraFields  field form tambahan (hanya mode Multipart), mis. {"user_id": 12}
     */
    void upload(const QString &filePath, const QVariantMap &extraFields = {});

    /// Batalkan upload yang sedang berjalan.
    void abort();

signals:
    /// Progress pengiriman (bytes terkirim / total).
    void progress(qint64 bytesSent, qint64 bytesTotal);

    /// Sukses (HTTP 2xx). `response` = body balasan server.
    void finished(int httpStatus, const QByteArray &response);

    /// Gagal (validasi, jaringan, atau HTTP non-2xx).
    void failed(const QString &error, int httpStatus, const QByteArray &response);

private slots:
    void onReplyFinished();

private:
    static bool checkWavHeader(const QString &path, QString *error);
    void fail(const QString &msg, int status = 0, const QByteArray &body = {});

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;

    QUrl        m_url;
    Mode        m_mode        = Mode::Multipart;
    QString     m_fieldName   = QStringLiteral("file");
    QString     m_bearerToken;
    QMap<QByteArray, QByteArray> m_headers;
    int         m_timeoutMs   = 30000;
    bool        m_validateWav = true;
    bool        m_aborted     = false;
};
