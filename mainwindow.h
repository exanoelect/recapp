#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "record.h"
#include <QDir>
#include <QProcess>
#include "wavuploader.h"
#include "BackendClient.h"
#include <QDebug>
#include <QStatusBar>
#include <QUrl>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QCollator>
#include <QRegularExpression>
#include <QTimer>
#include <QThread>
#include "socketeventworker.h"
#include "wavsplitter.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnStart_clicked();
    void on_btnStop_clicked();
    void on_btnUpload_clicked();
    void on_btnSplitter_clicked();

    // ---------------------------------------------------------------------
    // Socket.IO and device communication
    // ---------------------------------------------------------------------
    void onSocketEventReceived(const QString &eventName, const QJsonValue &data);
    void onDeviceReadyConnected(int vol, int bright);

    void on_btnPost_clicked();
    void onTranscriptionResultReceived(QString status, int percent);
    void onJoinRet(QString ret);

    void on_btntest_clicked();

private:
    Ui::MainWindow *ui;
    Recorder *m_record = nullptr;
    QString m_wavPath;
    WavUploader *uploader = nullptr;

    void splitRecording(const QString &path);

    QStringList m_uploadQueue;
    int m_uploadTotal = 0;

    QString m_currentUploadName;   // nama file yang sedang diupload
    int m_currentUploadIndex = 0;  // urutan file (1-based)

    // ---------------------------------------------------------------------
    // Unified BackendClient (HTTP Uploader + Socket.IO) & Event Worker
    // ---------------------------------------------------------------------
    BackendClient *m_backend = nullptr;
    SocketEventWorker *m_worker = nullptr;
    QThread *m_workerThread = nullptr;

    QString uploadID;

    // ---------------------------------------------------------------------
    // Initialization
    // ---------------------------------------------------------------------
    void initBackendClient();
};
#endif // MAINWINDOW_H
