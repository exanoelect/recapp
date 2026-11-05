#include "socketeventworker.h"
#include <QJsonObject>
#include <QDebug>

SocketEventWorker::SocketEventWorker(QObject *parent)
    : QObject(parent), m_running(true)
{
}

SocketEventWorker::~SocketEventWorker(){

}

//------------------------------------------------------------------------
void SocketEventWorker::stop()
{
    QMutexLocker locker(&m_mutex);
    m_running = false;
    m_cond.wakeAll();
}

//------------------------------------------------------------------------
void SocketEventWorker::enqueue(const QString &eventName, const QJsonValue &data)
{
    QMutexLocker locker(&m_mutex);
    m_queue.enqueue(qMakePair(eventName, data));
    m_cond.wakeOne();
}

//------------------------------------------------------------------------
void SocketEventWorker::process()
{
    while (true) {
        m_mutex.lock();
        while (m_queue.isEmpty() && m_running) {
            m_cond.wait(&m_mutex);   // tidur sampai ada data
        }

        if (!m_running) {
            m_mutex.unlock();
            break;
        }

        auto event = m_queue.dequeue();
        m_mutex.unlock();

        const QString &eventName = event.first;
        const QJsonValue &data = event.second;

        qDebug() << "Worker processing event:" << eventName << "data:" << data;

        //Robot mode
        if (eventName == "transcription") {
            if (data.isObject()) {
                QJsonObject obj = data.toObject();
                QString uid = obj["upload_id"].toString();
                QString status = obj["status"].toString();
                int percent = obj["percent"].toInt();
                qDebug() << "reply " << eventName << obj;
                emit transcriptionResult(status,percent);
            }
        }
    }
}
