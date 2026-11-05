#include "wavuploader.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QNetworkRequest>

WavUploader::WavUploader(QObject *parent)
    : QObject(parent)
{

}

WavUploader::~WavUploader()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void WavUploader::setRawHeader(const QByteArray &name, const QByteArray &value)
{
    m_headers.insert(name, value);
}

bool WavUploader::checkWavHeader(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Tidak bisa membuka file: %1").arg(f.errorString());
        return false;
    }
    const QByteArray h = f.read(12);
    // Format WAV: "RIFF" <size> "WAVE"
    if (h.size() < 12 || !h.startsWith("RIFF") || h.mid(8, 4) != "WAVE") {
        if (error) *error = QStringLiteral("File bukan WAV valid (header RIFF/WAVE tidak ditemukan)");
        return false;
    }
    return true;
}

void WavUploader::fail(const QString &msg, int status, const QByteArray &body)
{
    emit failed(msg, status, body);
}

void WavUploader::upload(const QString &filePath, const QVariantMap &extraFields)
{
    if (isBusy()) {
        fail(QStringLiteral("Upload sebelumnya masih berjalan"));
        return;
    }
    if (!m_url.isValid() || m_url.isEmpty()) {
        fail(QStringLiteral("URL server belum diset / tidak valid"));
        return;
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        fail(QStringLiteral("File tidak ditemukan: %1").arg(filePath));
        return;
    }

    QString err;
    if (m_validateWav && !checkWavHeader(filePath, &err)) {
        fail(err);
        return;
    }

    // --- Request ---
    QNetworkRequest req(m_url);
    req.setTransferTimeout(m_timeoutMs);
    if (!m_bearerToken.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_bearerToken.toUtf8());
    for (auto it = m_headers.cbegin(); it != m_headers.cend(); ++it)
        req.setRawHeader(it.key(), it.value());

    m_aborted = false;

    if (m_mode == Mode::Multipart) {
        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

        // Field tambahan (teks)
        for (auto it = extraFields.cbegin(); it != extraFields.cend(); ++it) {
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QVariant(QStringLiteral("form-data; name=\"%1\"").arg(it.key())));
            part.setBody(it.value().toString().toUtf8());
            multi->append(part);
        }

        // Field file
        auto *file = new QFile(filePath, multi);   // dimiliki oleh multi
        if (!file->open(QIODevice::ReadOnly)) {
            const QString msg = QStringLiteral("Gagal membuka file: %1").arg(file->errorString());
            delete multi;
            fail(msg);
            return;
        }

        QString fileName = info.fileName();
        fileName.replace('"', QStringLiteral("\\\""));

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/wav"));
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QVariant(QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                                        .arg(m_fieldName, fileName)));
        filePart.setBodyDevice(file);
        multi->append(filePart);

        m_reply = m_nam.post(req, multi);
        multi->setParent(m_reply);                 // ikut dihapus bersama reply
    } else {
        auto *file = new QFile(filePath);
        if (!file->open(QIODevice::ReadOnly)) {
            const QString msg = QStringLiteral("Gagal membuka file: %1").arg(file->errorString());
            delete file;
            fail(msg);
            return;
        }
        req.setHeader(QNetworkRequest::ContentTypeHeader, "audio/wav");
        req.setHeader(QNetworkRequest::ContentLengthHeader, file->size());

        m_reply = m_nam.post(req, file);
        file->setParent(m_reply);
    }

    connect(m_reply, &QNetworkReply::uploadProgress, this, &WavUploader::progress);
    connect(m_reply, &QNetworkReply::finished, this, &WavUploader::onReplyFinished);
}

void WavUploader::abort()
{
    if (m_reply) {
        m_aborted = true;
        m_reply->abort();
    }
}

void WavUploader::onReplyFinished()
{
    QNetworkReply *reply = m_reply.data();
    if (!reply)
        return;
    m_reply.clear();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const auto netError = reply->error();
    const QString netErrorStr = reply->errorString();
    reply->deleteLater();

    if (m_aborted) {
        fail(QStringLiteral("Upload dibatalkan"), status, body);
    } else if (netError != QNetworkReply::NoError) {
        fail(netErrorStr, status, body);
    } else if (status < 200 || status >= 300) {
        fail(QStringLiteral("Server membalas HTTP %1").arg(status), status, body);
    } else {
        emit finished(status, body);
    }
}
