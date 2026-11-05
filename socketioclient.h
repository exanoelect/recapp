#pragma once

#include <QObject>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>     // DITAMBAHKAN
#include <QJsonValue>     // Optional, tapi baik untuk dittambah
#include <QTimer>
#include <QUrlQuery>
#include <QDebug>
#include <functional>
#include <map>
#include <QMutexLocker>
#include <QMutex>
#include <QProcess>
#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QTimer>

// Macro untuk kompatibilitas Qt version
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#define CONNECT_WEBSOCKET_ERROR(obj, receiver, slot) \
connect(obj, &QWebSocket::errorOccurred, receiver, slot)
#else
#define CONNECT_WEBSOCKET_ERROR(obj, receiver, slot) \
connect(obj, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), receiver, slot)
#endif

    class SocketIOClient : public QObject
{
    Q_OBJECT

public:
    enum class SocketIOVersion {
        V4,  // EIO=4
        V3,  // EIO=3
        V2   // EIO=3 (v2 juga pakai EIO=3)
    };

    explicit SocketIOClient(QObject *parent = nullptr);

    ~SocketIOClient();

    //void connectToServer(const QString &host,
    //                     quint16 port = 3000,
    //                     const QString &ns = "/",
    //                     SocketIOVersion version = SocketIOVersion::V4);

    void connectToServer(const QString &hostDns,
                         const QString &host,
                         quint16 port = 3000,
                         const QString &ns = "/",
                         SocketIOVersion version = SocketIOVersion::V4);

    void disconnectFromServer();
    bool isConnected() const { return m_isConnected; }
    void sendSocketIoConnectWithAuth();

    // Fungsi baru untuk memasukkan event ke queue
    void enqueueEvent(const QString &eventName, const QJsonObject &data);

    //void emitEvent(const QString &eventName, const QJsonObject &data = QJsonObject());
    void emitEvent(const QString &eventName,const QJsonValue &data,std::function<void(QJsonValue)> ackCallback);
    void emitEventQstringMsg(const QString &eventName, const QString message);
    void emitEventStringMsgJsoned(const QString &eventName, const QJsonValue &data);

    void emitEventWithAck(const QString &eventName,
                          const QJsonObject &data,
                          std::function<void (bool, QJsonValue)> callback, int timeoutMs);

    void emitEventWithAckqString(const QString &eventName,
                          const QString &data,
                          std::function<void(const QString&)> callback,
                                 int timeoutMs);

    void emitEventWithAckQJsonValue(const QString &eventName,
                          const QJsonObject &data,
                          std::function<void(QJsonValue)> callback);

    // Robot commands
    void sendDeviceReady();

    // Utility
    QString getSocketId() const { return m_socketId; }
    QString getNamespace() const { return m_namespace; }
    QString getHost() const { return m_host; }
    quint16 getPort() const { return m_port; }

signals:
    void connected(const QString &socketId);
    void deviceready();
    void disconnected();
    void connectionError(const QString &error);
    //void eventReceived(const QString &eventName, const QJsonObject &data);
    //void eventReceived(const QString &eventName, const QString &message);
    void eventReceived(const QString &eventName, const QJsonValue &payload);

private slots:
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketTextMessage(const QString &message);
    void onWebSocketError(QAbstractSocket::SocketError error);
    void sendPing();
    void attemptReconnect();
    void processEventQueue();

private:
    struct QueuedEvent{
        QString eventName;
        QJsonObject data;
    };

    QQueue<QueuedEvent> m_eventQueue;
    QTimer m_queueTimer;

    bool m_processingQueue = false;
    bool m_isConnected = false;

    static constexpr int QUEUE_PROCESS_INTERVAL_MS = 100;
    static constexpr int MAX_QUEUE_SIZE = 1000;

    QWebSocket *m_webSocket;
    QTimer *m_pingTimer;
    QTimer *m_reconnectTimer;
    QString m_socketId;
    QString m_namespace;
    int m_packetId;
    int m_reconnectAttempts;
    bool namespaceConnected = false;
    int m_nextAckId;
    QMutex m_mutex;

    QString m_lastEventName;
    QJsonValue m_lastEventData;

    QString m_serialNumber;
    QString m_hostDns;
    QString m_host;
    quint16 m_port;
    SocketIOVersion m_version;

    //std::map<int, std::function<void(const QString&)>> m_ackCallbacksQString;
    //std::map<int, std::function<void(QJsonValue)>> m_ackCallbacks;
    std::map<quint64, std::function<void(bool, QJsonValue)>> m_ackCallbacks;


    void setupWebSocket();
    void constructWebSocketUrl();
    void parseSocketIOMessage(const QString &message);
    void handleSocketIOJsonPacket(int type, const QString &data);
    void handleIncomingAck(int ackId, const QJsonValue &data);

    void sendSocketIOPacket(int type, const QString &data = QString());
    void handleIncomingEvent(const QString &eventName,
                             const QJsonValue &payload,
                             int ackId);

    void sendAcknowledgment(const QString &eventName,
                            int ackId,
                            const QJsonValue &originalData);

    void scheduleReconnect();
};
