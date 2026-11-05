#include "socketioclient.h"
#include <QUrlQuery>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
//#include <cmath>
//#include <iostream>
#include <QRandomGenerator>  // <-- TAMBAHKAN INI

//ST-2026-04-IND-PRD-V1-000001

SocketIOClient::SocketIOClient(QObject *parent)
    : QObject(parent)
    , m_webSocket(nullptr)
    , m_pingTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_isConnected(false)
    , m_packetId(0)
    , m_reconnectAttempts(0)
    , m_port(3000)
    , m_version(SocketIOVersion::V4)
{
    //m_webSocket = new QWebSocket();


    m_pingTimer->setInterval(25000);
    connect(m_pingTimer, &QTimer::timeout, this, &SocketIOClient::sendPing);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SocketIOClient::attemptReconnect);

    m_queueTimer.setInterval(QUEUE_PROCESS_INTERVAL_MS);
    m_queueTimer.setSingleShot(false);

    connect(&m_queueTimer,&QTimer::timeout,this,&SocketIOClient::processEventQueue);
}

//------------------------------------------------------------------------
SocketIOClient::~SocketIOClient()
{
    disconnectFromServer();
}

//------------------------------------------------------------------------
void SocketIOClient::connectToServer(const QString &hostDns, const QString &host,
                                     quint16 port,
                                     const QString &ns,
                                     SocketIOVersion version)
{    
    m_hostDns = hostDns;
    m_host = host;
    m_port = port;
    m_namespace = ns;
    m_version = version;

    if (m_webSocket) {
        m_webSocket->disconnect(this);
        m_webSocket->deleteLater();
        m_webSocket = nullptr;
    }

    setupWebSocket();
    constructWebSocketUrl();
}

//------------------------------------------------------------------------
void SocketIOClient::disconnectFromServer()
{
    if (m_webSocket) {
        m_webSocket->close();
    }
    m_isConnected = false;
    //m_pingTimer->stop();
    m_reconnectTimer->stop();
}

//------------------------------------------------------------------------
void SocketIOClient::sendSocketIoConnectWithAuth()
{
    QJsonObject auth;
    auth["robotId"] = m_serialNumber; //"TESTING-3";

    QString authJson = QString::fromUtf8(
        QJsonDocument(auth).toJson(QJsonDocument::Compact)
        );

    QString packet;

    if (m_namespace.isEmpty() || m_namespace == "/") {
        // Root namespace
        packet = "40" + authJson;
    } else {
        // Custom namespace, contoh: /robot
        packet = "40" + m_namespace + "," + authJson;
    }

    qDebug() << "Sending Socket.IO CONNECT with auth:" << packet;

    if (m_webSocket) {
        m_webSocket->sendTextMessage(packet);
    }
}

//------------------------------------------------------------------------
void SocketIOClient::enqueueEvent(const QString &eventName, const QJsonObject &data)
{
    /*
      * Queue tetap menerima event walaupun socket sedang disconnect.
      * Event akan dikirim setelah socket kembali connected.
      */

     if (m_eventQueue.size() >= MAX_QUEUE_SIZE) {
         qWarning() << "Event queue penuh. Event tertua dibuang:"
                    << m_eventQueue.head().eventName;

         m_eventQueue.dequeue();
     }

     QueuedEvent event;
     event.eventName = eventName;
     event.data = data;

     m_eventQueue.enqueue(event);

     qDebug() << "Event masuk queue:"
              << eventName
              << "| queue size:"
              << m_eventQueue.size();

     /*
      * Jalankan pemrosesan secara asynchronous.
      * Tidak langsung memanggil fungsi pengiriman pada call stack yang sama.
      */
    if (m_isConnected && !m_queueTimer.isActive()) {
        m_queueTimer.start();
    }
}

//------------------------------------------------------------------------
void SocketIOClient::setupWebSocket()
{
    //m_webSocket = new QWebSocket();

    m_webSocket = new QWebSocket(QString(),
                      QWebSocketProtocol::VersionLatest,
                      this);

    connect(m_webSocket, &QWebSocket::connected,
            this, &SocketIOClient::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::disconnected,
            this, &SocketIOClient::onWebSocketDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &SocketIOClient::onWebSocketTextMessage);

    // Gunakan macro untuk kompatibilitas
    CONNECT_WEBSOCKET_ERROR(m_webSocket, this, &SocketIOClient::onWebSocketError);
}

//------------------------------------------------------------------------
void SocketIOClient::constructWebSocketUrl()
{
    if(m_hostDns != ""){
        //m_webSocket->open(QUrl("wss://elderly-care-socket-io-server.online/socket.io/?EIO=4&transport=websocket"));
        m_webSocket->open(QUrl("wss://" + m_hostDns + "/socket.io/?EIO=4&transport=websocket"));
    }else{
       QUrl url;

       qDebug() << "Debug cuk " << m_host << "-" << m_port;
       url.setScheme("ws");
       url.setHost(m_host);
       url.setPort(m_port);
       url.setPath("/socket.io/");

       QUrlQuery query;

        // Parameter EIO berdasarkan versi
        switch (m_version) {
        case SocketIOVersion::V4:
            query.addQueryItem("EIO", "4");
            break;
        case SocketIOVersion::V3:
            query.addQueryItem("EIO", "3");
            break;
        case SocketIOVersion::V2:
            query.addQueryItem("EIO", "3"); // v2 juga pakai EIO=3
            break;
        }

        query.addQueryItem("transport", "websocket");

        // Tambahkan timestamp untuk avoid cache
        query.addQueryItem("t", QString::number(QDateTime::currentMSecsSinceEpoch()));
        //query.addQueryItem("auth", "{\"robotId\":\"TESTING-3\"}");
        query.addQueryItem("auth",QString("{\"robotId\":\"%1\"}").arg(m_serialNumber));
        //query.addQueryItem("robotId","TESTING-3");m_serialNumber
        query.addQueryItem("robotId",m_serialNumber);

        // Tambahkan namespace ke query jika bukan root namespace
        //if (m_namespace != "/") {
        //    query.addQueryItem("namespace", m_namespace);
        //}

        url.setQuery(query);

        QString urlString = url.toString();
        qDebug() << "Connecting to:" << urlString; //"wss://elderly-care-socket-io-server.online/socket.io/?EIO=4&transport=websocket";/
        qDebug() << "Namespace:" << m_namespace;

        m_webSocket->open(url);
    }
}

//------------------------------------------------------------------------
void SocketIOClient::onWebSocketConnected()
{
    qDebug() << "WebSocket connected, waiting for Socket.IO handshake...";
    //m_webSocket->sendTextMessage("40");
    //qDebug() << "Sent namespaces";

    qDebug() << "WebSocket connected, sending CONNECT with auth";

    //QString payload = "40{\"robotId\":\"TESTING-3\"}";
    //m_webSocket->sendTextMessage(payload);

    QJsonObject authObject;
    authObject.insert(QStringLiteral("robotId"),"");

    authObject.insert(QStringLiteral("processor_id"), "");
    const QByteArray jsonData = QJsonDocument(authObject).toJson(QJsonDocument::Compact);
    const QString payload = QStringLiteral("40") + QString::fromUtf8(jsonData);

    m_webSocket->sendTextMessage(payload);

    qDebug() << "Socket.IO CONNECT payload:" << payload;

    /*
     * Kirim event-event yang sebelumnya tertahan.
     */
    if (!m_eventQueue.isEmpty() && !m_queueTimer.isActive()) {
        m_queueTimer.start();
    }
}

//------------------------------------------------------------------------
void SocketIOClient::onWebSocketDisconnected()
{
    m_isConnected = false;
    //m_pingTimer->stop();
    qDebug() << "WebSocket disconnected";
    m_queueTimer.stop();

    m_ackCallbacks.clear();
    scheduleReconnect();
    emit disconnected();
}

//------------------------------------------------------------------------
void SocketIOClient::onWebSocketTextMessage(const QString &message)
{
    qDebug() << "Received message:" << message;

    parseSocketIOMessage(message);
}

//------------------------------------------------------------------------
void SocketIOClient::parseSocketIOMessage(const QString &message)
{
    if (message.isEmpty()) {
        qDebug() << "Empty message received";
        return;
    }

    bool ok;
    int packetType = QString(message[0]).toInt(&ok);
    if (!ok) {
        qWarning() << "Invalid packet type:" << message;
        return;
    }

    QString data = message.mid(1);
    handleSocketIOJsonPacket(packetType, data);
}

//------------------------------------------------------------------------
void SocketIOClient::handleSocketIOJsonPacket(int type, const QString &data)
{
    switch (type) {
    case 0: { // Engine.IO OPEN
        if (!data.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());

            if (doc.isObject()) {
                QJsonObject obj = doc.object();

                QString engineSid = obj["sid"].toString();
                int pingInterval = obj["pingInterval"].toInt(25000);
                int pingTimeout = obj["pingTimeout"].toInt(20000);

                qDebug() << "Engine.IO opened. SID:" << engineSid;
                qDebug() << "Ping interval:" << pingInterval
                         << "Ping timeout:" << pingTimeout;

                m_reconnectAttempts = 0;

                // BELUM Socket.IO connected.
                // Jangan set m_isConnected = true di sini.
                m_isConnected = false;

                // Setelah Engine.IO OPEN, kirim Socket.IO CONNECT + auth robotId
                sendSocketIoConnectWithAuth();
            }
        }
        break;
    }

    case 1: // DISCONNECT
        qDebug() << "Server requested disconnect";
        if (m_webSocket) {
            m_webSocket->close();
        }
        break;

    case 2: // PING
        sendSocketIOPacket(3); // PONG
        qDebug() << "Received PING, sent PONG";
        break;

    case 3: // PONG
        qDebug() << "Received PONG";
        break;

    case 4: { // MESSAGE (Engine.IO)
        if (data.isEmpty()) break;

        // Data diawali subtype Socket.IO:
        // 0{...} -> CONNECT
        // 2["EVENT","DATA"]
        // 3[ackId,"RESPONSE"]

        QChar subTypeChar = data[0];
        QString subData = data.mid(1);

        int subType = subTypeChar.digitValue();

        if (subType == 0) { // Socket.IO CONNECT accepted
            QString sid;

            if (!subData.isEmpty()) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(subData.toUtf8(), &err);

                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    sid = obj["sid"].toString();
                } else {
                    qWarning() << "Socket.IO CONNECT payload is not JSON object:" << subData;
                }
            }

            m_socketId = sid;
            m_isConnected = true;

            qDebug() << "Socket.IO connected. SID:" << sid;

            emit connected(sid);

            // Baru aman kirim DEVICE_READY di sini
            QTimer::singleShot(300, this, [this]() {
                if (!m_isConnected) {
                    qWarning() << "Skip DEVICE_READY, Socket.IO not connected";
                    return;
                }

                sendDeviceReady();
                emit deviceready();
            });
        }

        else if (subType == 2) { // EVENT
            QJsonDocument doc = QJsonDocument::fromJson(subData.toUtf8());
            if (!doc.isArray()) {
                qWarning() << "Invalid event JSON:" << subData;
                return;
            }

            QJsonArray arr = doc.array();
            if (arr.size() < 2) {
                qWarning() << "Invalid event format:" << arr;
                return;
            }

            QString eventName = arr[0].toString();
            QJsonValue dataValue = arr[1];

            int ackId = -1;
            if (arr.size() >= 3 && arr[2].isDouble()) {
                ackId = arr[2].toInt();
            }

            qDebug() << "Parse Event:" << eventName
                     << "Data:" << dataValue
                     << "AckId:" << ackId;

            // ===== DEDUPLICATION LOGIC =====
            if (eventName == m_lastEventName && dataValue == m_lastEventData) {
                qDebug() << "Duplicate event skipped:" << eventName << dataValue;
                return;
            }

            // Update last values
            m_lastEventName = eventName;
            m_lastEventData = dataValue;
            // =================================

            //handleIncomingEvent(eventName, dataValue.toObject(), ackId);
            emit eventReceived(eventName, dataValue);
        }

        else if (subType == 3) { // ACK

            int pos = 0;

            // cari posisi '[' pertama
            while (pos < subData.size() && subData[pos].isDigit())
                pos++;

            int ackId = subData.left(pos).toInt();
            QString jsonPart = subData.mid(pos);

            QJsonDocument doc = QJsonDocument::fromJson(jsonPart.toUtf8());

            if (!doc.isArray()) {
                qWarning() << "Invalid ACK JSON:" << jsonPart;
                return;
            }

            QJsonArray arr = doc.array();
            QJsonValue ackData = arr.isEmpty() ? QJsonValue() : arr[0];

            qDebug() << "ACK received:" << ackId << ackData;

            handleIncomingAck(ackId, ackData);
        }
        else if (subType == 1) { // DISCONNECT
            qWarning() << "Socket.IO namespace disconnected by server. Data:" << subData;

            m_isConnected = false;

            emit disconnected(); // atau socketIoDisconnected(), sesuai signal kamu

            return; // penting: jangan lanjut emit apa pun
        }else {
            qDebug() << "Unhandled Socket.IO subtype:" << subType << "Data:" << subData;
        }

        break;
    }


    case 5: // UPGRADE (WebSocket upgrade)
        qDebug() << "Upgrade packet received";
        break;

    case 6: // NOOP
        qDebug() << "NOOP packet received";
        break;

    default:
        qWarning() << "Unknown packet type:" << type;
        break;
    }
}


//------------------------------------------------------------------------
void SocketIOClient::handleIncomingAck(int ackId, const QJsonValue &data)
{
    qDebug() << "ACK received for id:" << ackId << "Data:" << data;

    auto it = m_ackCallbacks.find(ackId);
    if (it != m_ackCallbacks.end()) {
        auto callback = it->second;
        m_ackCallbacks.erase(it);
        if(callback){
          callback(true, data); //success = true
        }
    } else {
        qWarning() << "No callback found for ACK id:" << ackId;
    }
}

//------------------------------------------------------------------------
void SocketIOClient::sendAcknowledgment(const QString &eventName,
                                        int ackId,
                                        const QJsonValue &originalData)
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "Cannot send acknowledgment, socket not connected";
        return;
    }

    QJsonValue ackPayload;

    // Buat data acknowledgment berdasarkan event
    if (eventName == "SCREEN_BRIGHTNESS_SET" && originalData.isObject()) {
        QJsonObject obj;
        obj["level"] = originalData.toObject()["level"];
        ackPayload = obj;
    }
    else if (eventName == "VOLUME_SET" && originalData.isObject()) {
        QJsonObject obj;
        obj["volume"] = originalData.toObject()["volume"];
        ackPayload = obj;
    }
    else {
        // Default ACK payload
        QJsonObject obj;
        obj["ack"] = true;
        obj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        ackPayload = obj;
    }

    // Format Socket.IO ACK packet:
    // 42["EVENT_ACK", data, ackId]
    QJsonArray eventArray;
    eventArray.append(eventName + "_ACK");
    eventArray.append(ackPayload);
    eventArray.append(ackId);

    QJsonDocument doc(eventArray);
    QString message = "42" + doc.toJson(QJsonDocument::Compact);

    m_webSocket->sendTextMessage(message);
    qDebug() << "Sent acknowledgment for" << eventName << "ackId:" << ackId;
}


//------------------------------------------------------------------------
void SocketIOClient::sendSocketIOPacket(int type, const QString &data)
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "Cannot send packet, socket not connected";
        return;
    }

    QString packet = QString::number(type);
    if (!data.isEmpty()) {
        packet += data;
    }

    m_webSocket->sendTextMessage(packet);
}

//------------------------------------------------------------------------
void SocketIOClient::emitEvent(const QString &eventName,
                               const QJsonValue &data,
                               std::function<void(QJsonValue)> ackCallback)
{
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "Not connected, cannot emit:" << eventName;
        return;
    }

    QJsonValue fixedData = data;

    // 🔥 Normalize data agar sesuai Zod
    if (data.isObject()) {
        QJsonObject obj = data.toObject();

        if (obj.contains("timestamp")) {
            QJsonValue ts = obj.value("timestamp");

            // Jika string ISO → convert ke number (ms)
            if (ts.isString()) {
                QDateTime dt = QDateTime::fromString(ts.toString(), Qt::ISODate);
                if (dt.isValid()) {
                    obj["timestamp"] = dt.toMSecsSinceEpoch(); // jadi number
                } else {
                    qWarning() << "Invalid timestamp string format";
                    return; // stop kirim, biar ga kena validation failed
                }
            }

            // Kalau bukan number juga → reject
            else if (!ts.isDouble()) {
                qWarning() << "Invalid timestamp type (must be number)";
                return;
            }
        }

        fixedData = obj;
    } else {
        qWarning() << "Data must be JSON object for event:" << eventName;
        return;
    }

    // Format Socket.IO: 42["event", {...}]
    QJsonArray eventArray;
    eventArray.append(eventName);
    eventArray.append(fixedData);

    QJsonDocument doc(eventArray);
    QString message = "42" + QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    m_webSocket->sendTextMessage(message);

    qDebug() << "Emitted event:" << eventName
             << "Payload:" << message;
}


//------------------------------------------------------------------------
void SocketIOClient::emitEventQstringMsg(const QString &eventName, const QString message)
{
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "Not connected, cannot emit:" << eventName;
        return;
    }

    // Format Socket.IO message: 42["eventName"]
    QString data = "42[\"" + message + "\"" + "]";

    m_webSocket->sendTextMessage(data);
    qDebug() << "Emitted event:" << eventName << "message:" << data;
}

//------------------------------------------------------------------------
void SocketIOClient::emitEventStringMsgJsoned(const QString &eventName,
                                              const QJsonValue &data)
{
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "Not connected, cannot emit:" << eventName;
        return;
    }

    QJsonValue fixedData = data;

    // 🔥 Jika object, cek field timestamp
    if (data.isObject()) {
        QJsonObject obj = data.toObject();

        if (obj.contains("timestamp")) {
            QJsonValue ts = obj.value("timestamp");

            // ❌ kalau string → convert ke number (ms)
            if (ts.isString()) {
                QDateTime dt = QDateTime::fromString(ts.toString(), Qt::ISODate);
                if (dt.isValid()) {
                    obj["timestamp"] = dt.toMSecsSinceEpoch(); // jadi number
                } else {
                    qWarning() << "Invalid timestamp string format";
                }
            }
        }

        fixedData = obj;
    }

    QJsonArray packetArray;
    packetArray.append(eventName);
    packetArray.append(fixedData);

    QJsonDocument doc(packetArray);
    QString message = "42" + QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    m_webSocket->sendTextMessage(message);

    qDebug() << "Emitted event:" << eventName
             << "Payload:" << message;
}



//------------------------------------------------------------------------
void SocketIOClient::emitEventWithAck(const QString &eventName,
                                       const QJsonObject &data,
                                       std::function<void(bool, QJsonValue)> callback, int timeoutMs)
{
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "Not connected, cannot emit:" << eventName;
        return;
    }

    quint64 ackId;

    do {
        ackId = m_nextAckId++;
        if (m_nextAckId == 0)
            m_nextAckId = 1;
    } while (m_ackCallbacks.count(ackId));

    m_ackCallbacks.emplace(ackId, std::move(callback));

    if(m_ackCallbacks.size() > 1000) {
        qWarning() << "ACK map too large! Possible leak:" << m_ackCallbacks.size();
        qWarning() << "FORCE CLEAR ACK MAP!";
        m_ackCallbacks.clear();
    }

    QJsonArray arr;
    arr.append(eventName);
    arr.append(data);

    QString message = "42" + QString::number(ackId) +
                      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

    m_webSocket->sendTextMessage(message);

    // timeout handler
    QTimer::singleShot(timeoutMs, this, [this, ackId]() {

        auto it = m_ackCallbacks.find(ackId);
        if (it == m_ackCallbacks.end())
            return; // ACK sudah datang

        auto cb = it->second;
        m_ackCallbacks.erase(it);

        if (cb) {
            qWarning() << "SocketIO ACK timeout:" << ackId;
            qDebug() << "SocketIO ACK timeout debug:" << ackId;
            cb(false, QJsonValue("ack timeout"));
        }
    });
}

//------------------------------------------------------------------------
void SocketIOClient::emitEventWithAckqString(const QString &eventName,
                                      const QString &data,
                                      std::function<void(const QString&)> callback, int timeoutMs)
{
    if (!m_isConnected || !m_webSocket) {
        qWarning() << "Not connected, cannot emit:" << eventName;
        return;
    }

    emitEventWithAck(
        eventName,
        QJsonObject{{"data", data}},
        [callback](bool ok, QJsonValue value){
            if (!ok) {
                callback(QString());   // timeout / error
                return;
            }
            if (value.isString())
                callback(value.toString());
            else
                callback(value.toVariant().toString());
        },
        timeoutMs
    );
}

//------------------------------------------------------------------------
void SocketIOClient::emitEventWithAckQJsonValue(const QString &eventName,
                                      const QJsonObject &data,
                                      std::function<void(QJsonValue)> callback)
{
    emitEventWithAck(
        eventName,
        data,
        [callback](bool ok, QJsonValue value)
        {
            if (!ok) {
                callback(QJsonObject());
                return;
            }

            if (value.isObject())
                callback(value);
            else
                callback(QJsonObject());
        },
        5000
    );
}

//------------------------------------------------------------------------
void SocketIOClient::sendDeviceReady()
{
    // Gunakan QRandomGenerator untuk Qt 6
    // Generate device ID yang unik
    //QString deviceId = "sentikoo_" +
    //                   QString::number(QRandomGenerator::global()->generate() % 1000);

    if (!m_isConnected || !m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "Cannot send DEVICE_READY, Socket.IO is not connected";
        return;
    }

    QJsonObject payload{
        //{"device_id", deviceId},
        //{"device_id", "ST-2026-04-IND-PRD-V1-000001"}, //"raspberry"},
        //{"robotId", "TESTING-3"}, //"raspberry"},
        {"robotId", m_serialNumber}, //"raspberry"},
        {"brightness", ""},
        {"volume", ""}
    };
    //emitEvent("DEVICE_READY", payload);
    emitEvent("DEVICE_READY", payload, [](QJsonValue ackVal) {
        qDebug() << ackVal;
    });
}



//------------------------------------------------------------------------
void SocketIOClient::onWebSocketError(QAbstractSocket::SocketError error)
{
    QString errorStr = QString("WebSocket error [%1]: %2")
    .arg(error)
        .arg(m_webSocket ? m_webSocket->errorString() : "No socket");
    qWarning() << errorStr;
    emit connectionError(errorStr);
}

//------------------------------------------------------------------------
void SocketIOClient::sendPing()
{
    sendSocketIOPacket(2); // Type 2 = PING
    qDebug() << "Sent PING";
}

//------------------------------------------------------------------------
void SocketIOClient::scheduleReconnect()
{
    m_reconnectAttempts++;

    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, max 30s
    int delay = qMin(30000, (1 << qMin(m_reconnectAttempts, 5)) * 1000);

    qDebug() << "Reconnecting in" << delay << "ms (attempt"
             << m_reconnectAttempts << ")";

    m_reconnectTimer->start(delay);
}

//------------------------------------------------------------------------
void SocketIOClient::attemptReconnect()
{
    if ((!m_webSocket) || (m_webSocket->state() == QAbstractSocket::UnconnectedState)) {
        qDebug() << "Attempting reconnect to" << m_host << ":" << m_port;
        constructWebSocketUrl();
    }
}

//------------------------------------------------------------------------
void SocketIOClient::processEventQueue()
{
    if (m_processingQueue) {
        return;
    }

    if (!m_isConnected) {
        m_queueTimer.stop();
        return;
    }

    if (m_eventQueue.isEmpty()) {
        m_queueTimer.stop();
        return;
    }

    m_processingQueue = true;

    /*
     * Ambil event paling depan.
     *
     * Jangan langsung dequeue sebelum fungsi pengiriman dipanggil.
     * Event baru dihapus dari queue setelah perintah pengiriman dijalankan.
     */
    const QueuedEvent event = m_eventQueue.head();

    emitEventStringMsgJsoned(event.eventName, event.data);

    m_eventQueue.dequeue();

    qDebug() << "Event dikirim dari queue:"
             << event.eventName
             << "| queue tersisa:"
             << m_eventQueue.size();

    m_processingQueue = false;

    if (m_eventQueue.isEmpty()) {
        m_queueTimer.stop();
    }
}



