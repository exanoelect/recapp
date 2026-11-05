#include "BackendUploader.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUuid>

BackendUploader::BackendUploader(const QUrl &baseUrl, QObject *parent)
    : QObject(parent), m_base(baseUrl)
{
}

void BackendUploader::setBaseUrl(const QUrl &baseUrl)
{
    m_base = baseUrl;
}

void BackendUploader::setUploadId(const QString &id)
{
    if (id != m_fixedId)
        m_nextIndex = 0;          // folder berbeda -> mulai dari 0
    m_fixedId = id;
}

QUrl BackendUploader::endpoint(const QString &path) const
{
    QUrl u = m_base;
    QString p = u.path();
    while (p.endsWith('/'))
        p.chop(1);
    u.setPath(p + path);
    return u;
}

static void addTextPart(QHttpMultiPart *mp, const QByteArray &name, const QByteArray &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QByteArray("form-data; name=\"") + name + "\""));
    part.setBody(value);
    mp->append(part);
}

void BackendUploader::upload(const QString &wavPath)
{
    uploadFiles(QStringList{wavPath});
}

void BackendUploader::uploadFiles(const QStringList &wavPaths)
{
    if (m_running) {
        emit failed(QStringLiteral("Upload sebelumnya masih berjalan"), 0, {});
        return;
    }
    if (wavPaths.isEmpty()) {
        emit failed(QStringLiteral("Tidak ada file untuk diunggah"), 0, {});
        return;
    }

    // Validasi semua file di awal supaya tidak gagal di tengah antrian
    for (const QString &p : wavPaths) {
        if (!QFileInfo::exists(p)) {
            emit failed(QStringLiteral("File tidak ditemukan: ") + p, 0, {});
            return;
        }
    }

    const QString id = m_fixedId.isEmpty()
                           ? QUuid::createUuid().toString(QUuid::WithoutBraces)   // 36 karakter
                           : m_fixedId;

    static const QRegularExpression safeId(QStringLiteral("^[a-zA-Z0-9_-]{8,64}$"));
    if (!safeId.match(id).hasMatch()) {
        emit failed(QStringLiteral("upload_id tidak valid (harus [a-zA-Z0-9_-]{8,64})"), 0, {});
        return;
    }

    m_currentId = id;
    m_index = m_fixedId.isEmpty() ? 0 : m_nextIndex;
    m_queue = wavPaths;
    m_total = wavPaths.size();
    m_done = 0;
    m_lastStatus = 0;
    m_lastBody.clear();
    m_running = true;

    sendNext();
}

void BackendUploader::sendNext()
{
    if (m_queue.isEmpty()) {
        sendComplete();          // semua file terkirim -> beri tahu server lewat JSON
        return;
    }

    const QString path = m_queue.first();

    auto *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        fail(QStringLiteral("Tidak bisa membuka ") + path);
        return;
    }

    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    addTextPart(mp, "upload_id", m_currentId.toUtf8());
    addTextPart(mp, "index", QByteArray::number(m_index));

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/wav"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"chunk\"; filename=\"%1\"")
                                    .arg(QFileInfo(path).fileName())));
    filePart.setBodyDevice(file);
    file->setParent(mp);              // file dihapus bersama mp
    mp->append(filePart);

    QNetworkRequest req(endpoint(QStringLiteral("/upload/chunk")));
    req.setTransferTimeout(m_timeoutMs);

    m_reply = m_nam.post(req, mp);
    mp->setParent(m_reply);           // mp dihapus bersama reply

    connect(m_reply, &QNetworkReply::uploadProgress, this, &BackendUploader::progress);
    connect(m_reply, &QNetworkReply::finished, this, [this, path] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            handleError(reply);
            return;
        }

        m_lastStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_lastBody = reply->readAll();
        reply->deleteLater();

        m_queue.removeFirst();
        ++m_done;
        ++m_index;
        if (!m_fixedId.isEmpty())
            m_nextIndex = m_index;    // sesi berikutnya melanjutkan di folder yang sama

        emit fileUploaded(m_done, m_total, path);
        sendNext();
    });
}

void BackendUploader::sendComplete()
{
    QNetworkRequest req(endpoint(QStringLiteral("/upload/complete")));
    req.setTransferTimeout(m_timeoutMs);

    if (m_completeAsJson) {
        // Server baru: {"upload_id":"<folder>","total":<jumlah file>}
        // "merge":true hanya disertakan bila setMergeOnFinish(true).
        QJsonObject obj;
        obj.insert(QStringLiteral("upload_id"), m_currentId);
        obj.insert(QStringLiteral("total"), m_done);    // jumlah file yang berhasil diupload
        if (m_merge)
            obj.insert(QStringLiteral("merge"), true);

        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        m_reply = m_nam.post(req, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    } else {
        // Server lama (main.py asli): form multipart dengan field upload_id dan total.
        // Server akan menggabungkan semua part jadi final.wav lalu menghapus part-nya.
        auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        addTextPart(mp, "upload_id", m_currentId.toUtf8());
        addTextPart(mp, "total", QByteArray::number(m_done));

        m_reply = m_nam.post(req, mp);
        mp->setParent(m_reply);
    }

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            handleError(reply);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        m_running = false;
        emit finished(status, body);
    });
}

void BackendUploader::handleError(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QString err = reply->errorString();
    reply->deleteLater();

    m_running = false;
    m_queue.clear();
    emit failed(err, status, body);
}

void BackendUploader::fail(const QString &error)
{
    m_running = false;
    m_queue.clear();
    emit failed(error, 0, {});
}

void BackendUploader::abort()
{
    if (m_reply)
        m_reply->abort();   // memicu finished() dengan error -> failed()
}
