#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //Wav recorder
    m_wavPath = QDir::homePath() + "/wav/rec/my_record.wav";
    m_record = new Recorder(m_wavPath, this);

    connect(m_record, &Recorder::recordingFinished, this,
            [this](const QString &path, bool ok) {          // [this] supaya ui bisa dipakai
                qDebug() << "Recording done:" << path << "ok =" << ok;
                ui->btnStart->setEnabled(true);
            });


    // Uploader dibuat sekali; parent = this, jadi dihapus otomatis bersama window
    //m_uploader = new BackendUploader(QUrl(QStringLiteral("http://192.168.1.13:8001")), this);
    QString destination = "http://" + ui->leIP->text() + ":" + ui->lePort->text();
    m_uploader = new BackendUploader(QUrl(destination), this);

    //m_uploader = new BackendUploader(QUrl(QStringLiteral("http://192.168.1.18:8000")), this);

    m_uploader->setTimeoutMs(120000);
    m_uploader->setMergeOnFinish(false);   // biarkan file terpisah (part_000000.wav, dst)

    // Progres file yang sedang dikirim
    connect(m_uploader, &BackendUploader::progress, this,
            [this](qint64 sent, qint64 total) {
        if (total > 0) {
            statusBar()->showMessage(
                QStringLiteral("Mengunggah %1 (%2/%3)... %4%")
                    .arg(m_currentUploadName)
                    .arg(m_currentUploadIndex)
                    .arg(m_uploadTotal)
                    .arg(sent * 100 / total));
        }
    });

    // Satu file selesai -> siapkan info untuk file berikutnya
    connect(m_uploader, &BackendUploader::fileUploaded, this,
            [this](int done, int total, const QString &path) {
        statusBar()->showMessage(QStringLiteral("Terkirim %1/%2: %3")
                                     .arg(done).arg(total).arg(QFileInfo(path).fileName()));
        m_currentUploadIndex = done + 1;
        m_currentUploadName = QFileInfo(m_uploadQueue.value(done)).fileName();
    });

    // SEMUA file selesai (dipanggil sekali di akhir, bukan per file)
    connect(m_uploader, &BackendUploader::finished, this,
            [this](int status, const QByteArray &resp) {
        qInfo().noquote() << "Sukses HTTP" << status << resp;
        statusBar()->showMessage(
            QStringLiteral("Selesai: %1 file diupload ke folder %2")
                .arg(m_uploadTotal).arg(m_uploader->uploadId()), 8000);
        ui->btnUpload->setEnabled(true);
    });

    connect(m_uploader, &BackendUploader::failed, this,
            [this](const QString &err, int status, const QByteArray &resp) {
        qWarning().noquote() << "Gagal:" << err << "(HTTP" << status << ")" << resp;
        statusBar()->showMessage(QStringLiteral("Upload gagal: ") + err, 8000);
        ui->btnUpload->setEnabled(true);
    });

    initSocketIO();

}

//-------------------------------------------------------------------
MainWindow::~MainWindow()
{
    delete ui;
}

//-------------------------------------------------------------------
void MainWindow::on_btnStart_clicked()
{
   m_record->onStartClicked();
   ui->btnStart->setEnabled(false);
}

//-------------------------------------------------------------------
void MainWindow::on_btnStop_clicked()
{
   m_record->onStopClicked();
   ui->btnStop->setEnabled(false);
}

//-------------------------------------------------------------------
void MainWindow::on_btnUpload_clicked()
{
    if (m_uploader->isBusy())
        return;

    // Kumpulkan my_record_000.wav, my_record_001.wav, ... sampai yang terakhir
    //const QDir dir(QStringLiteral("/home/pi/wav/rec"));
   // const QDir dir(QStringLiteral("D:/wav/rec"));
    const QDir dir(ui->leWavPath->text());

    const QRegularExpression re(QStringLiteral("podcast_\\d+\\.wav$"));

    QStringList names;
    const QStringList all = dir.entryList({QStringLiteral("podcast*.wav")}, QDir::Files);
    for (const QString &n : all)
        if (re.match(n).hasMatch())
            names << n;

    // Urut numerik (aman kalau nomor sudah lebih dari 999)
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(names.begin(), names.end(), collator);

    if (names.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Tidak ada file podcast*.wav"));
        return;
    }

    // m_uploadQueue sekarang berisi DAFTAR LENGKAP file (tidak lagi diambil satu-satu)
    m_uploadQueue.clear();
    for (const QString &n : names)
        m_uploadQueue << dir.absoluteFilePath(n);
    m_uploadTotal = m_uploadQueue.size();
    m_currentUploadIndex = 1;
    m_currentUploadName = QFileInfo(m_uploadQueue.first()).fileName();

    // Satu folder baru di server untuk satu kali klik, mis. uploads/rec-20261102-103834/
    // Urutan file terjaga: part_000000.wav = file pertama, dst.
    uploadID = "rec-" + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    //m_uploader->setUploadId(QStringLiteral("rec-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    m_uploader->setUploadId(uploadID);

    ui->btnUpload->setEnabled(false);      // cegah klik ganda selama proses
    m_uploader->uploadFiles(m_uploadQueue);
}


//-------------------------------------------------------------------
void MainWindow::on_btnSplitter_clicked()
{
    // Kalau path disimpan di MainWindow:
    ui->btnSplitter->setEnabled(false);
    //splitRecording("/home/pi/wav/rec/my_record.wav");
    splitRecording("D:/wav/rec/podcast.wav");

    // Kalau proses rekam ada di kelas lain (mis. Recorder *m_recorder),
    // ambil path-nya dari situ, atau hubungkan lewat signal:
    //connect(m_recorder, &Recorder::finished, this, &MainWindow::splitRecording);
}

//-------------------------------------------------------------------
void MainWindow::splitRecording(const QString &path)
{
    WavSplitOptions opt;
     opt.maxMB = 19.5;
     opt.thresholdDb = -35.0;
     opt.minSilenceSec = 0.5;

     auto *watcher = new QFutureWatcher<WavSplitResult>(this);

     connect(watcher, &QFutureWatcher<WavSplitResult>::finished, this, [this, watcher, path]() {
         const WavSplitResult r = watcher->result();
         if (r.ok) {
             QFile::remove(path);   // hapus file asli, hapus baris ini kalau mau disimpan
             qDebug() << "Split selesai:" << r.files;
             // TODO: proses r.files (upload / transkripsi / tampilkan di UI)
             ui->btnSplitter->setEnabled(true);
         } else {
             qWarning() << "Split gagal:" << r.error;
             ui->btnSplitter->setEnabled(true);
         }
         watcher->deleteLater();
     });

     watcher->setFuture(QtConcurrent::run([path, opt]() {
         return WavSplitter::split(path, opt);
     }));

}



// -----------------------------------------------------------------------------
void MainWindow::onSocketEventReceived(const QString &eventName, const QJsonValue &data)
{
    qDebug() << "UI received event:" << eventName << "data:" << data;
    // m_eventQueue.enqueue(qMakePair(eventName, data));

    qDebug() << "UI received event:" << eventName << "data:" << data;
    m_worker->enqueue(eventName, data);
}

// -----------------------------------------------------------------------------
void MainWindow::onDeviceReadyConnected(int vol, int bright) {}

// -----------------------------------------------------------------------------
void MainWindow::on_btnPost_clicked()
{
    //QString timestamp = QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss");
    QJsonObject obj;
    obj["upload_id"] = uploadID;//"folderX";
    client->enqueueEvent("join", obj);
}

// -----------------------------------------------------------------------------
void MainWindow::onTranscriptionResultReceived(QString status, int percent)
{
    qDebug() << "resp server " << status << "-" << percent;
    ui->progressBar->setValue(percent);
}


// =============================================================================
// Socket.IO and event worker
// =============================================================================
void MainWindow::initSocketIO()
{
    client = new SocketIOClient();

    connect(client, &SocketIOClient::eventReceived, this, &MainWindow::onSocketEventReceived);

    // Worker setup
    m_workerThread = new QThread(this);
    m_worker = new SocketEventWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &SocketEventWorker::process);

    // Hubungkan signal worker ke aksi UI / device
    connect(m_worker, &SocketEventWorker::transcriptionResult, this, &MainWindow::onTranscriptionResultReceived);

    m_workerThread->start();

    QString serverIpDns = ""; //ConfigManager::getServerIpDNS();
    QString serverIp =  ui->leIP->text();//"192.168.1.22" ;//ConfigManager::getServerIp(); //"203.194.114.21"; ////"https://elderly-care-socket-io-server.online";
    int serverPort = ui->lePortSIO->text().toInt();//3000; //ConfigManager::getServerPort(); // 4000;//

    qDebug() << "Server IP DNS:" << serverIpDns;
    qDebug() << "Server IP:" << serverIp;
    qDebug() << "Server Port:" << serverPort;

    client->connectToServer(serverIpDns, serverIp, serverPort);
}
