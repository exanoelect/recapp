#include "BackendClient.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QRegularExpression>
#include <QUuid>

// =============================================================================
// Constructors & Destructor
// =============================================================================

BackendClient::BackendClient(QObject *parent)
    : QObject(parent)
    , m_pingTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_pingTimer->setInterval(25000);
    connect(m_pingTimer, &QTimer::timeout, this, &BackendClient::sendPing);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &BackendClient::attemptReconnect);

    m_queueTimer.setInterval(QUEUE_PROCESS_INTERVAL_MS);
    m_queueTimer.setSingleShot(false);
    connect(&m_queueTimer, &QTimer::timeout, this, &BackendClient::processEventQueue);
}

BackendClient::BackendClient(const QUrl &baseUrl, QObject *parent)
    : BackendClient(parent)
{
    setBaseUrl(baseUrl);
}

BackendClient::BackendClient(const QString &host, quint16 port, bool useSsl, QObject *parent)
    : BackendClient(parent)
{
    setServer(host, port, useSsl);
}

BackendClient::~BackendClient()
{
    abort();
    disconnectFromServer();
}

// =============================================================================
// Konfigurasi Server Terpadu
// =============================================================================

void BackendClient::setServer(const QString &host, quint16 port, bool useSsl)
{
    m_hostDns.clear();
    m_host = host.trimmed();
    m_port = port;
    m_useSsl = useSsl;
    syncUrlFromHostPort();
}

void BackendClient::setServerDns(const QString &hostDns, bool useSsl)
{
    m_hostDns = hostDns.trimmed();
    m_host = m_hostDns;
    m_port = useSsl ? 443 : 80;
    m_useSsl = useSsl;
    syncUrlFromHostPort();
}

void BackendClient::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
    m_useSsl = (baseUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ||
                baseUrl.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0);
    m_host = baseUrl.host();
    m_port = static_cast<quint16>(baseUrl.port(m_useSsl ? 443 : 80));

    if (baseUrl.port() == -1 && !m_host.contains(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+\\.\\d+$")))) {
        m_hostDns = m_host;
    } else {
        m_hostDns.clear();
    }
}

void BackendClient::syncUrlFromHostPort()
{
    QUrl u;
    u.setScheme(m_useSsl ? QStringLiteral("https") : QStringLiteral("http"));
    u.setHost(m_host);
    if (m_port > 0 && !((m_useSsl && m_port == 443) || (!m_useSsl && m_port == 80))) {
        u.setPort(m_port);
    }
    m_baseUrl = u;
}

// =============================================================================
// Koneksi Socket.IO
// =============================================================================

void BackendClient::connectToServer(const QString &ns, SocketIOVersion version)
{
    connectToServer(m_hostDns, m_host, m_port, ns, version);
}

void BackendClient::connectToServer(const QString &host,
                                    quint16 port,
                                    const QString &ns,
                                    SocketIOVersion version)
{
    setServer(host, port, m_useSsl);
    connectToServer(QString(), host, port, ns, version);
}

void BackendClient::connectToServer(const QString &hostDns,
                                    const QString &host,
                                    quint16 port,
                                    const QString &ns,
                                    SocketIOVersion version)
{
    if (!hostDns.isEmpty()) {
        setServerDns(hostDns, true);
    } else if (!host.isEmpty()) {
        setServer(host, port, m_useSsl);
    }

    m_namespace = ns.isEmpty() ? QStringLiteral("/") : ns;
    m_version = version;

    if (m_webSocket) {
        m_webSocket->disconnect(this);
        m_webSocket->deleteLater();
        m_webSocket = nullptr;
    }

    m_isConnected = false;
    setupWebSocket();
    constructWebSocketUrl();
}

void BackendClient::disconnectFromServer()
{
    m_reconnectTimer->stop();
    m_queueTimer.stop();
    if (m_webSocket) {
        m_webSocket->close();
    }
    m_isConnected = false;
}

void BackendClient::setupWebSocket()
{
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(m_webSocket, &QWebSocket::connected,
            this, &BackendClient::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::disconnected,
            this, &BackendClient::onWebSocketDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &BackendClient::onWebSocketTextMessage);

    CONNECT_WEBSOCKET_ERROR(m_webSocket, this, &BackendClient::onWebSocketError);
}

void BackendClient::constructWebSocketUrl()
{
    QUrl url;
    QUrlQuery query;

    switch (m_version) {
    case SocketIOVersion::V4:
        query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("4"));
        break;
    case SocketIOVersion::V3:
    case SocketIOVersion::V2:
        query.addQueryItem(QStringLiteral("EIO"), QStringLiteral("3"));
        break;
    }

    query.addQueryItem(QStringLiteral("transport"), QStringLiteral("websocket"));
    query.addQueryItem(QStringLiteral("t"), QString::number(QDateTime::currentMSecsSinceEpoch()));

    if (!m_serialNumber.isEmpty()) {
        query.addQueryItem(QStringLiteral("auth"),
                           QStringLiteral("{\"robotId\":\"%1\"}").arg(m_serialNumber));
        query.addQueryItem(QStringLiteral("robotId"), m_serialNumber);
    }

    if (!m_hostDns.isEmpty()) {
        url.setScheme(m_useSsl ? QStringLiteral("wss") : QStringLiteral("ws"));
        url.setHost(m_hostDns);
        url.setPath(QStringLiteral("/socket.io/"));
    } else {
        url.setScheme(m_useSsl ? QStringLiteral("wss") : QStringLiteral("ws"));
        url.setHost(m_host);
        if (m_port > 0) {
            url.setPort(m_port);
        }

        QString basePath = m_baseUrl.path();
        while (basePath.endsWith('/'))
            basePath.chop(1);
        url.setPath(basePath + QStringLiteral("/socket.io/"));
    }

    url.setQuery(query);
    qDebug() << "[BackendClient] Connecting Socket.IO to:" << url.toString()
             << "| HTTP Base:" << m_baseUrl.toString();

    m_webSocket->open(url);
}

void BackendClient::onWebSocketConnected()
{
    // Tunggu paket Engine.IO OPEN ("0{...}") sebelum mengirim Socket.IO CONNECT ("40")
    qDebug() << "[BackendClient] WebSocket transport opened, waiting for Engine.IO handshake...";
}

void BackendClient::sendSocketIoConnectWithAuth()
{
    QJsonObject auth;
    if (!m_serialNumber.isEmpty()) {
        auth.insert(QStringLiteral("robotId"), m_serialNumber);
    }

    const QString authJson = QString::fromUtf8(QJsonDocument(auth).toJson(QJsonDocument::Compact));
    QString packet;

    if (m_namespace.isEmpty() || m_namespace == QLatin1String("/")) {
        packet = QStringLiteral("40") + authJson;
    } else {
        packet = QStringLiteral("40") + m_namespace + QStringLiteral(",") + authJson;
    }

    qDebug() << "[BackendClient] Sending Socket.IO CONNECT:" << packet;
    if (m_webSocket) {
        m_webSocket->sendTextMessage(packet);
    }
}

void BackendClient::onWebSocketDisconnected()
{
    m_isConnected = false;
    m_queueTimer.stop();
    m_ackCallbacks.clear();

    qDebug() << "[BackendClient] Socket.IO disconnected";
    scheduleReconnect();
    emit disconnected();
}

void BackendClient::onWebSocketTextMessage(const QString &message)
{
    qDebug() << "[BackendClient] WS RX:" << message;
    parseSocketIOMessage(message);
}

void BackendClient::parseSocketIOMessage(const QString &message)
{
    if (message.isEmpty())
        return;

    bool ok = false;
    int packetType = QString(message[0]).toInt(&ok);
    if (!ok) {
        qWarning() << "[BackendClient] Invalid Engine.IO packet:" << message;
        return;
    }

    handleSocketIOJsonPacket(packetType, message.mid(1));
}

void BackendClient::handleSocketIOJsonPacket(int type, const QString &data)
{
    switch (type) {
    case 0: { // Engine.IO OPEN
        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            qDebug() << "[BackendClient] Engine.IO OPEN. SID:" << obj["sid"].toString();
            m_reconnectAttempts = 0;
            m_isConnected = false;
            sendSocketIoConnectWithAuth();
        }
        break;
    }
    case 1: // DISCONNECT
        if (m_webSocket)
            m_webSocket->close();
        break;

    case 2: // PING -> balas PONG
        sendSocketIOPacket(3);
        break;

    case 3: // PONG
        break;

    case 4: { // Engine.IO MESSAGE -> Socket.IO Packet
        if (data.isEmpty())
            break;

        QString payloadData = data;
        int subType = payloadData[0].digitValue();
        QString subData = payloadData.mid(1);

        if (subData.startsWith('/')) {
            int commaIdx = subData.indexOf(',');
            subData = (commaIdx != -1) ? subData.mid(commaIdx + 1) : QString();
        }

        if (subType == 0) { // Socket.IO CONNECT accepted
            QString sid;
            if (!subData.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(subData.toUtf8());
                if (doc.isObject())
                    sid = doc.object().value(QStringLiteral("sid")).toString();
            }

            m_socketId = sid;
            m_isConnected = true;
            qDebug() << "[BackendClient] Socket.IO connected. SID:" << sid;

            emit connected(sid);

            // Jika sebelumnya sudah ada upload_id aktif & autoJoin aktif, otomatis join room
            if (m_autoJoinOnUpload && !m_currentId.isEmpty()) {
                joinRoom(m_currentId);
            }

            if (!m_eventQueue.isEmpty() && !m_queueTimer.isActive()) {
                m_queueTimer.start();
            }

            QTimer::singleShot(300, this, [this]() {
                if (!m_isConnected)
                    return;
                sendDeviceReady();
                emit deviceready();
            });
        } else if (subType == 2) { // EVENT
            QJsonDocument doc = QJsonDocument::fromJson(subData.toUtf8());
            if (!doc.isArray())
                return;

            QJsonArray arr = doc.array();
            if (arr.size() < 2)
                return;

            QString eventName = arr[0].toString();
            QJsonValue dataValue = arr[1];

            if (eventName == m_lastEventName && dataValue == m_lastEventData) {
                return;
            }
            m_lastEventName = eventName;
            m_lastEventData = dataValue;

            emit eventReceived(eventName, dataValue);
        } else if (subType == 3) { // ACK
            int pos = 0;
            while (pos < subData.size() && subData[pos].isDigit())
                pos++;

            int ackId = subData.left(pos).toInt();
            QJsonDocument doc = QJsonDocument::fromJson(subData.mid(pos).toUtf8());
            if (!doc.isArray())
                return;

            QJsonArray arr = doc.array();
            QJsonValue ackData = arr.isEmpty() ? QJsonValue() : arr[0];
            handleIncomingAck(ackId, ackData);
        } else if (subType == 1) { // Namespace DISCONNECT
            m_isConnected = false;
            emit disconnected();
        }
        break;
    }
    default:
        break;
    }
}

void BackendClient::handleIncomingAck(int ackId, const QJsonValue &data)
{
    auto it = m_ackCallbacks.find(ackId);
    if (it != m_ackCallbacks.end()) {
        auto callback = it->second;
        m_ackCallbacks.erase(it);
        if (callback)
            callback(true, data);
    }
}

void BackendClient::sendAcknowledgment(const QString &eventName,
                                       int ackId,
                                       const QJsonValue &originalData)
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject obj;
    if (eventName == QLatin1String("SCREEN_BRIGHTNESS_SET") && originalData.isObject()) {
        obj[QStringLiteral("level")] = originalData.toObject()[QStringLiteral("level")];
    } else if (eventName == QLatin1String("VOLUME_SET") && originalData.isObject()) {
        obj[QStringLiteral("volume")] = originalData.toObject()[QStringLiteral("volume")];
    } else {
        obj[QStringLiteral("ack")] = true;
        obj[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    }

    QJsonArray eventArray;
    eventArray.append(eventName + QStringLiteral("_ACK"));
    eventArray.append(obj);
    eventArray.append(ackId);

    m_webSocket->sendTextMessage(QStringLiteral("42") +
                                 QString::fromUtf8(QJsonDocument(eventArray).toJson(QJsonDocument::Compact)));
}

void BackendClient::sendSocketIOPacket(int type, const QString &data)
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState)
        return;

    QString packet = QString::number(type) + data;
    m_webSocket->sendTextMessage(packet);
}

void BackendClient::joinRoom(const QString &roomId)
{
    if (roomId.isEmpty())
        return;

    if (m_isConnected) {
        emitEventQstringMsg(QStringLiteral("join"), roomId);
    } else {
        enqueueEvent(QStringLiteral("join"), roomId);
    }
}

void BackendClient::enqueueEvent(const QString &eventName, const QJsonObject &data)
{
    if (m_eventQueue.size() >= MAX_QUEUE_SIZE) {
        m_eventQueue.dequeue();
    }
    m_eventQueue.enqueue({eventName, data, false});

    if (m_isConnected && !m_queueTimer.isActive()) {
        m_queueTimer.start();
    }
}

void BackendClient::enqueueEvent(const QString &eventName, const QString &data)
{
    if (m_eventQueue.size() >= MAX_QUEUE_SIZE) {
        m_eventQueue.dequeue();
    }
    m_eventQueue.enqueue({eventName, data, true});

    if (m_isConnected && !m_queueTimer.isActive()) {
        m_queueTimer.start();
    }
}

void BackendClient::emitEvent(const QString &eventName,
                              const QJsonValue &data,
                              std::function<void(QJsonValue)> ackCallback)
{
    Q_UNUSED(ackCallback);
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "[BackendClient] Not connected, cannot emit:" << eventName;
        return;
    }

    QJsonValue fixedData = data;
    if (data.isObject()) {
        QJsonObject obj = data.toObject();
        if (obj.contains(QStringLiteral("timestamp"))) {
            QJsonValue ts = obj.value(QStringLiteral("timestamp"));
            if (ts.isString()) {
                QDateTime dt = QDateTime::fromString(ts.toString(), Qt::ISODate);
                if (dt.isValid()) {
                    obj[QStringLiteral("timestamp")] = dt.toMSecsSinceEpoch();
                } else {
                    return;
                }
            } else if (!ts.isDouble()) {
                return;
            }
        }
        fixedData = obj;
    } else {
        return;
    }

    QJsonArray eventArray;
    eventArray.append(eventName);
    eventArray.append(fixedData);

    const QString nsPrefix = (m_namespace == QLatin1String("/")) ? QString() : (m_namespace + QStringLiteral(","));
    const QString message = QStringLiteral("42") + nsPrefix +
                            QString::fromUtf8(QJsonDocument(eventArray).toJson(QJsonDocument::Compact));

    m_webSocket->sendTextMessage(message);
}

void BackendClient::emitEventQstringMsg(const QString &eventName, const QString &message)
{
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "[BackendClient] Not connected, cannot emit:" << eventName;
        return;
    }

    QString safeMsg = message;
    safeMsg.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    safeMsg.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    const QString nsPrefix = (m_namespace == QLatin1String("/")) ? QString() : (m_namespace + QStringLiteral(","));
    const QString data = QStringLiteral("42%1[\"%2\",\"%3\"]").arg(nsPrefix, eventName, safeMsg);

    m_webSocket->sendTextMessage(data);
    qDebug() << "[BackendClient] Emitted:" << data;
}

void BackendClient::emitEventStringMsgJsoned(const QString &eventName, const QJsonValue &data)
{
    if (!m_isConnected || !m_webSocket)
        return;

    QJsonValue fixedData = data;
    if (data.isObject()) {
        QJsonObject obj = data.toObject();
        if (obj.contains(QStringLiteral("timestamp")) && obj.value(QStringLiteral("timestamp")).isString()) {
            QDateTime dt = QDateTime::fromString(obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
            if (dt.isValid())
                obj[QStringLiteral("timestamp")] = dt.toMSecsSinceEpoch();
        }
        fixedData = obj;
    }

    QJsonArray packetArray;
    packetArray.append(eventName);
    packetArray.append(fixedData);

    const QString nsPrefix = (m_namespace == QLatin1String("/")) ? QString() : (m_namespace + QStringLiteral(","));
    const QString message = QStringLiteral("42") + nsPrefix +
                            QString::fromUtf8(QJsonDocument(packetArray).toJson(QJsonDocument::Compact));

    m_webSocket->sendTextMessage(message);
}

void BackendClient::emitEventWithAck(const QString &eventName,
                                     const QJsonObject &data,
                                     std::function<void(bool, QJsonValue)> callback,
                                     int timeoutMs)
{
    if (!m_isConnected || !m_webSocket)
        return;

    quint64 ackId = 0;
    do {
        ackId = m_nextAckId++;
        if (m_nextAckId == 0)
            m_nextAckId = 1;
    } while (m_ackCallbacks.count(ackId));

    m_ackCallbacks.emplace(ackId, std::move(callback));

    if (m_ackCallbacks.size() > 1000) {
        m_ackCallbacks.clear();
    }

    QJsonArray arr;
    arr.append(eventName);
    arr.append(data);

    const QString nsPrefix = (m_namespace == QLatin1String("/")) ? QString() : (m_namespace + QStringLiteral(","));
    const QString message = QStringLiteral("42") + nsPrefix + QString::number(ackId) +
                            QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

    m_webSocket->sendTextMessage(message);

    QTimer::singleShot(timeoutMs, this, [this, ackId]() {
        auto it = m_ackCallbacks.find(ackId);
        if (it == m_ackCallbacks.end())
            return;
        auto cb = it->second;
        m_ackCallbacks.erase(it);
        if (cb)
            cb(false, QJsonValue(QStringLiteral("ack timeout")));
    });
}

void BackendClient::emitEventWithAckqString(const QString &eventName,
                                            const QString &data,
                                            std::function<void(const QString &)> callback,
                                            int timeoutMs)
{
    emitEventWithAck(
        eventName,
        QJsonObject{{QStringLiteral("data"), data}},
        [callback](bool ok, QJsonValue value) {
            if (!ok) {
                callback(QString());
                return;
            }
            callback(value.isString() ? value.toString() : value.toVariant().toString());
        },
        timeoutMs);
}

void BackendClient::emitEventWithAckQJsonValue(const QString &eventName,
                                               const QJsonObject &data,
                                               std::function<void(QJsonValue)> callback)
{
    emitEventWithAck(
        eventName,
        data,
        [callback](bool ok, QJsonValue value) {
            callback((ok && value.isObject()) ? value : QJsonObject());
        },
        5000);
}

void BackendClient::sendDeviceReady()
{
    if (!m_isConnected || !m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject payload{
        {QStringLiteral("robotId"), m_serialNumber},
        {QStringLiteral("brightness"), QString()},
        {QStringLiteral("volume"), QString()}
    };
    emitEvent(QStringLiteral("DEVICE_READY"), payload);
}

void BackendClient::onWebSocketError(QAbstractSocket::SocketError error)
{
    QString errorStr = QStringLiteral("WebSocket error [%1]: %2")
                           .arg(error)
                           .arg(m_webSocket ? m_webSocket->errorString() : QStringLiteral("No socket"));
    qWarning() << "[BackendClient]" << errorStr;
    emit connectionError(errorStr);
}

void BackendClient::sendPing()
{
    sendSocketIOPacket(2);
}

void BackendClient::scheduleReconnect()
{
    m_reconnectAttempts++;
    int delay = qMin(30000, (1 << qMin(m_reconnectAttempts, 5)) * 1000);
    m_reconnectTimer->start(delay);
}

void BackendClient::attemptReconnect()
{
    if (!m_webSocket || m_webSocket->state() == QAbstractSocket::UnconnectedState) {
        constructWebSocketUrl();
    }
}

void BackendClient::processEventQueue()
{
    if (m_processingQueue)
        return;

    if (!m_isConnected || m_eventQueue.isEmpty()) {
        m_queueTimer.stop();
        return;
    }

    m_processingQueue = true;
    const QueuedEvent event = m_eventQueue.head();

    if (event.isRawString) {
        emitEventQstringMsg(event.eventName, event.data.toString());
    } else {
        emitEventStringMsgJsoned(event.eventName, event.data);
    }

    m_eventQueue.dequeue();
    m_processingQueue = false;

    if (m_eventQueue.isEmpty()) {
        m_queueTimer.stop();
    }
}

// =============================================================================
// HTTP WAV Chunk Uploader
// =============================================================================

void BackendClient::setUploadId(const QString &id)
{
    if (id != m_fixedId)
        m_nextIndex = 0;
    m_fixedId = id;
}

QUrl BackendClient::endpoint(const QString &path) const
{
    QUrl u = m_baseUrl;
    QString p = u.path();
    while (p.endsWith('/'))
        p.chop(1);
    u.setPath(p + path);
    return u;
}

static void addFormTextPart(QHttpMultiPart *mp, const QByteArray &name, const QByteArray &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QByteArray("form-data; name=\"") + name + "\""));
    part.setBody(value);
    mp->append(part);
}

void BackendClient::upload(const QString &wavPath)
{
    uploadFiles(QStringList{wavPath});
}

void BackendClient::uploadFiles(const QStringList &wavPaths)
{
    if (m_running) {
        emit failed(QStringLiteral("Upload sebelumnya masih berjalan"), 0, {});
        return;
    }
    if (wavPaths.isEmpty()) {
        emit failed(QStringLiteral("Tidak ada file untuk diunggah"), 0, {});
        return;
    }

    for (const QString &p : wavPaths) {
        if (!QFileInfo::exists(p)) {
            emit failed(QStringLiteral("File tidak ditemukan: ") + p, 0, {});
            return;
        }
    }

    const QString id = m_fixedId.isEmpty()
                           ? QUuid::createUuid().toString(QUuid::WithoutBraces)
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

    // Jika auto-join aktif, langsung bergabung ke room upload_id di Socket.IO
    if (m_autoJoinOnUpload) {
        joinRoom(m_currentId);
    }

    sendNext();
}

void BackendClient::sendNext()
{
    if (m_queue.isEmpty()) {
        sendComplete();
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
    addFormTextPart(mp, "upload_id", m_currentId.toUtf8());
    addFormTextPart(mp, "index", QByteArray::number(m_index));

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/wav"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"chunk\"; filename=\"%1\"")
                                    .arg(QFileInfo(path).fileName())));
    filePart.setBodyDevice(file);
    file->setParent(mp);
    mp->append(filePart);

    QNetworkRequest req(endpoint(QStringLiteral("/upload/chunk")));
    req.setTransferTimeout(m_timeoutMs);

    m_reply = m_nam.post(req, mp);
    mp->setParent(m_reply);

    connect(m_reply, &QNetworkReply::uploadProgress, this, &BackendClient::progress);
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
            m_nextIndex = m_index;

        emit fileUploaded(m_done, m_total, path);
        sendNext();
    });
}

void BackendClient::sendComplete()
{
    QNetworkRequest req(endpoint(QStringLiteral("/upload/complete")));
    req.setTransferTimeout(m_timeoutMs);

    if (m_completeAsJson) {
        QJsonObject obj;
        obj.insert(QStringLiteral("upload_id"), m_currentId);
        obj.insert(QStringLiteral("total"), m_done + 1);
        if (m_merge)
            obj.insert(QStringLiteral("merge"), true);

        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        m_reply = m_nam.post(req, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    } else {
        auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        addFormTextPart(mp, "upload_id", m_currentId.toUtf8());
        addFormTextPart(mp, "total", QByteArray::number(m_done));

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

void BackendClient::handleError(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QString err = reply->errorString();
    reply->deleteLater();

    m_running = false;
    m_queue.clear();
    emit failed(err, status, body);
}

void BackendClient::fail(const QString &error)
{
    m_running = false;
    m_queue.clear();
    emit failed(error, 0, {});
}

void BackendClient::abort()
{
    if (m_reply)
        m_reply->abort();
}
