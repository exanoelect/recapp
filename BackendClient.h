#pragma once

#include <QByteArray>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <functional>
#include <map>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#define CONNECT_WEBSOCKET_ERROR(obj, receiver, slot) \
    connect(obj, &QWebSocket::errorOccurred, receiver, slot)
#else
#define CONNECT_WEBSOCKET_ERROR(obj, receiver, slot) \
    connect(obj, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), receiver, slot)
#endif

class BackendClient : public QObject
{
    Q_OBJECT

public:
    enum class SocketIOVersion {
        V4,  // EIO=4
        V3,  // EIO=3
        V2   // EIO=3
    };

    explicit BackendClient(QObject *parent = nullptr);
    explicit BackendClient(const QUrl &baseUrl, QObject *parent = nullptr);
    BackendClient(const QString &host, quint16 port = 8000, bool useSsl = false, QObject *parent = nullptr);
    ~BackendClient() override;

    // =========================================================================
    // Konfigurasi Server Terpadu (HTTP & Socket.IO)
    // =========================================================================
    void setServer(const QString &host, quint16 port = 8000, bool useSsl = false);
    void setServerDns(const QString &hostDns, bool useSsl = true);
    void setBaseUrl(const QUrl &baseUrl);

    QUrl baseUrl() const { return m_baseUrl; }
    QString getHost() const { return m_host; }
    quint16 getPort() const { return m_port; }
    QString getNamespace() const { return m_namespace; }
    QString getSocketId() const { return m_socketId; }

    void setSerialNumber(const QString &serial) { m_serialNumber = serial; }
    QString serialNumber() const { return m_serialNumber; }

    // =========================================================================
    // Koneksi & Event Socket.IO
    // =========================================================================
    void connectToServer(const QString &ns = QStringLiteral("/"),
                         SocketIOVersion version = SocketIOVersion::V4);

    void connectToServer(const QString &host,
                         quint16 port,
                         const QString &ns = QStringLiteral("/"),
                         SocketIOVersion version = SocketIOVersion::V4);

    void connectToServer(const QString &hostDns,
                         const QString &host,
                         quint16 port = 8000,
                         const QString &ns = QStringLiteral("/"),
                         SocketIOVersion version = SocketIOVersion::V4);

    void disconnectFromServer();
    bool isConnected() const { return m_isConnected; }
    void sendSocketIoConnectWithAuth();

    // Helper khusus join room upload_id
    void joinRoom(const QString &roomId);

    void enqueueEvent(const QString &eventName, const QJsonObject &data);
    void enqueueEvent(const QString &eventName, const QString &data);

    void emitEvent(const QString &eventName,
                   const QJsonValue &data,
                   std::function<void(QJsonValue)> ackCallback = nullptr);
    void emitEventQstringMsg(const QString &eventName, const QString &message);
    void emitEventStringMsgJsoned(const QString &eventName, const QJsonValue &data);

    void emitEventWithAck(const QString &eventName,
                          const QJsonObject &data,
                          std::function<void(bool, QJsonValue)> callback,
                          int timeoutMs = 5000);

    void emitEventWithAckqString(const QString &eventName,
                                 const QString &data,
                                 std::function<void(const QString &)> callback,
                                 int timeoutMs = 5000);

    void emitEventWithAckQJsonValue(const QString &eventName,
                                    const QJsonObject &data,
                                    std::function<void(QJsonValue)> callback);

    void sendDeviceReady();

    // =========================================================================
    // HTTP WAV Chunk Uploader
    // =========================================================================
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void setUploadId(const QString &id);
    QString uploadId() const { return m_currentId; }

    void setNextIndex(int index) { m_nextIndex = index; }
    int nextIndex() const { return m_nextIndex; }

    void setCompleteAsJson(bool json) { m_completeAsJson = json; }
    void setMergeOnFinish(bool merge) { m_merge = merge; }
    void setAutoJoinOnUpload(bool enable) { m_autoJoinOnUpload = enable; }

    void uploadFiles(const QStringList &wavPaths);
    void upload(const QString &wavPath);
    void abort();
    bool isBusy() const { return m_running; }

signals:
    // Signals Socket.IO
    void connected(const QString &socketId);
    void deviceready();
    void disconnected();
    void connectionError(const QString &error);
    void eventReceived(const QString &eventName, const QJsonValue &payload);
    void joinAckReceived(bool ok);

    // Signals HTTP Uploader
    void progress(qint64 sent, qint64 total);
    void fileUploaded(int done, int total, const QString &path);
    void finished(int httpStatus, const QByteArray &response);
    void failed(const QString &error, int httpStatus, const QByteArray &response);

private slots:
    // Slots Socket.IO
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketTextMessage(const QString &message);
    void onWebSocketError(QAbstractSocket::SocketError error);
    void sendPing();
    void attemptReconnect();
    void processEventQueue();

private:
    // --- Server State ---
    QUrl m_baseUrl;
    QString m_hostDns;
    QString m_host;
    quint16 m_port = 8000;
    bool m_useSsl = false;

    void syncUrlFromHostPort();

    // --- Socket.IO State & Helpers ---
    struct QueuedEvent {
        QString eventName;
        QJsonValue data;
        bool isRawString = false;
    };

    QQueue<QueuedEvent> m_eventQueue;
    QTimer m_queueTimer;
    bool m_processingQueue = false;
    bool m_isConnected = false;

    static constexpr int QUEUE_PROCESS_INTERVAL_MS = 100;
    static constexpr int MAX_QUEUE_SIZE = 1000;

    QWebSocket *m_webSocket = nullptr;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QString m_socketId;
    QString m_namespace = QStringLiteral("/");
    int m_packetId = 0;
    int m_reconnectAttempts = 0;
    quint64 m_nextAckId = 1;
    QMutex m_mutex;

    QString m_lastEventName;
    QJsonValue m_lastEventData;
    QString m_serialNumber;
    SocketIOVersion m_version = SocketIOVersion::V4;
    std::map<quint64, std::function<void(bool, QJsonValue)>> m_ackCallbacks;

    void setupWebSocket();
    void constructWebSocketUrl();
    void parseSocketIOMessage(const QString &message);
    void handleSocketIOJsonPacket(int type, const QString &data);
    void handleIncomingAck(int ackId, const QJsonValue &data);
    void sendSocketIOPacket(int type, const QString &data = QString());
    void sendAcknowledgment(const QString &eventName, int ackId, const QJsonValue &originalData);
    void scheduleReconnect();

    // --- HTTP Uploader State & Helpers ---
    QUrl endpoint(const QString &path) const;
    void sendNext();
    void sendComplete();
    void handleError(QNetworkReply *reply);
    void fail(const QString &error);

    int m_timeoutMs = 60000;
    bool m_merge = false;
    bool m_completeAsJson = false;
    bool m_autoJoinOnUpload = true;

    QString m_fixedId;
    QString m_currentId;
    int m_nextIndex = 0;

    QStringList m_queue;
    int m_total = 0;
    int m_done = 0;
    int m_index = 0;
    bool m_running = false;
    int m_lastStatus = 0;
    QByteArray m_lastBody;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
};
