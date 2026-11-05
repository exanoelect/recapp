#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "record.h"
#include <QDir>
#include <QProcess>
#include "wavuploader.h"
#include "BackendUploader.h"
#include <QDebug>
#include <QStatusBar>
#include <QUrl>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDir>
#include <QFileInfo>
#include <QCollator>
#include <QRegularExpression>
#include <QTimer>
#include "socketioclient.h"
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
   //void onUploadFinished();
   // void onUploadFailed(const QString &err);

    // ---------------------------------------------------------------------
    // Socket.IO and device communication
    // ---------------------------------------------------------------------
    void onSocketEventReceived(const QString &eventName, const QJsonValue &data);
    void onDeviceReadyConnected(int vol, int bright);

    void on_btnPost_clicked();
    void onTranscriptionResultReceived(QString status, int percent);


private:
    Ui::MainWindow *ui;
    Recorder *m_record = nullptr;
    QString m_wavPath;
    WavUploader *uploader = nullptr;
    BackendUploader *m_uploader = nullptr;   // anggota kelas, bukan variabel lokal

    void splitRecording(const QString &path);

    QStringList m_uploadQueue;
    int m_uploadTotal = 0;
    //void uploadNext();

    QString m_currentUploadName;   // nama file yang sedang diupload
       int m_currentUploadIndex = 0;  // urutan file (1-based)

    // ---------------------------------------------------------------------
    // Socket.IO and payload workers
    // ---------------------------------------------------------------------
    SocketIOClient *client;
    SocketEventWorker *m_worker;
    QThread *m_workerThread;

    QString uploadID;

    // ---------------------------------------------------------------------
    // Initialization
    // ---------------------------------------------------------------------
    void initSocketIO();

};
#endif // MAINWINDOW_H
