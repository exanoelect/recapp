#pragma once

#include <QObject>
#include <QQueue>
#include <QPair>
#include <QMutex>
#include <QWaitCondition>
#include <QJsonValue>

class SocketEventWorker : public QObject
{
    Q_OBJECT
public:
    explicit SocketEventWorker(QObject *parent = nullptr);
    ~SocketEventWorker();

    void enqueue(const QString &eventName, const QJsonValue &data);
    void stop();

signals:
    //Robot mode
    void transcriptionResult(QString status, int percent);

public slots:
    void process();

private:
    QQueue<QPair<QString, QJsonValue>> m_queue;
    QMutex m_mutex;
    QWaitCondition m_cond;
    bool m_running;
};
