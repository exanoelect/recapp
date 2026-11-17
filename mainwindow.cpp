#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Wav recorder
    m_wavPath = QDir::homePath() + "/wav/rec/my_record.wav";
    m_record = new Recorder(m_wavPath, this);

    connect(m_record, &Recorder::recordingFinished, this,
            [this](const QString &path, bool ok) {
                qDebug() << "Recording done:" << path << "ok =" << ok;
                ui->btnStart->setEnabled(true);
            });

    // Inisialisasi klien terpadu (HTTP Uploader + Socket.IO) & Worker Thread
    initBackendClient();
}

//-------------------------------------------------------------------
MainWindow::~MainWindow()
{
    if (m_worker && m_workerThread) {
        m_worker->stop();
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete ui;
}

//-------------------------------------------------------------------
void MainWindow::initBackendClient()
{
    const QString serverIp = ui->leIP->text().trimmed();
    const quint16 serverPort = ui->lePort->text().toUShort();

    m_backend = new BackendClient(serverIp, serverPort > 0 ? serverPort : 8000, false, this);
    m_backend->setTimeoutMs(120000);
    m_backend->setMergeOnFinish(false);
    m_backend->setAutoJoinOnUpload(true);

    // Progres file yang sedang dikirim
    connect(m_backend, &BackendClient::progress, this,
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
    connect(m_backend, &BackendClient::fileUploaded, this,
            [this](int done, int total, const QString &path) {
        statusBar()->showMessage(QStringLiteral("Terkirim %1/%2: %3")
                                     .arg(done).arg(total).arg(QFileInfo(path).fileName()));
        m_currentUploadIndex = done + 1;
        m_currentUploadName = QFileInfo(m_uploadQueue.value(done)).fileName();
    });

    // SEMUA file selesai (dipanggil sekali di akhir, bukan per file)
    connect(m_backend, &BackendClient::finished, this,
            [this](int status, const QByteArray &resp) {
        qInfo().noquote() << "Sukses HTTP" << status << resp;

        const QString currentUploadId = m_backend ? m_backend->uploadId() : QString();
        statusBar()->showMessage(
            QStringLiteral("Selesai: %1 file diupload ke folder %2")
                .arg(m_uploadTotal).arg(currentUploadId), 8000);

        ui->btnUpload->setEnabled(true);
    });

    connect(m_backend, &BackendClient::failed, this,
            [this](const QString &err, int status, const QByteArray &resp) {
        qWarning().noquote() << "Gagal:" << err << "(HTTP" << status << ")" << resp;
        statusBar()->showMessage(QStringLiteral("Upload gagal: ") + err, 8000);
        ui->btnUpload->setEnabled(true);
    });

    // Hubungkan sinyal Socket.IO ke Worker
    connect(m_backend, &BackendClient::eventReceived, this, &MainWindow::onSocketEventReceived);

    m_workerThread = new QThread(this);
    m_worker = new SocketEventWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &SocketEventWorker::process);
    connect(m_worker, &SocketEventWorker::transcriptionResult, this, &MainWindow::onTranscriptionResultReceived);
    connect(m_worker, &SocketEventWorker::joinReply, this, &MainWindow::onJoinRet);

    m_workerThread->start();
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
    if (!m_backend || m_backend->isBusy())
        return;

    // Pastikan alamat IP & port terbaru dari UI digunakan
    const QString serverIp = ui->leIP->text().trimmed();
    const quint16 serverPort = ui->lePort->text().toUShort();
    m_backend->setServer(serverIp, serverPort > 0 ? serverPort : 8000, false);

    const QDir dir(ui->leWavPath->text());
    const QRegularExpression re(QStringLiteral("_\\d+\\.wav$"));

    QStringList names;
    const QStringList all = dir.entryList({QStringLiteral("*.wav")}, QDir::Files);
    for (const QString &n : all) {
        if (re.match(n).hasMatch())
            names << n;
    }

    // Urut numerik
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(names.begin(), names.end(), collator);

    if (names.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Tidak ada file *_000.wav"));
        return;
    }

    m_uploadQueue.clear();
    for (const QString &n : names)
        m_uploadQueue << dir.absoluteFilePath(n);
    m_uploadTotal = m_uploadQueue.size();
    m_currentUploadIndex = 1;
    m_currentUploadName = QFileInfo(m_uploadQueue.first()).fileName();

    uploadID = "rec-" + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    m_backend->setUploadId(uploadID);

    ui->btnUpload->setEnabled(false);
    m_backend->uploadFiles(m_uploadQueue);
}

//-------------------------------------------------------------------
void MainWindow::on_btnSplitter_clicked()
{
    ui->btnSplitter->setEnabled(false);
    splitRecording(ui->leWavPath->text());
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
            QFile::remove(path);
            qDebug() << "Split selesai:" << r.files;
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
    if (m_worker) {
        m_worker->enqueue(eventName, data);
    }
}

// -----------------------------------------------------------------------------
void MainWindow::onDeviceReadyConnected(int vol, int bright)
{
    Q_UNUSED(vol);
    Q_UNUSED(bright);
}

// -----------------------------------------------------------------------------
void MainWindow::on_btnPost_clicked()
{
    if (m_backend && !uploadID.isEmpty()) {
        m_backend->joinRoom(uploadID);
    }
}

// -----------------------------------------------------------------------------
void MainWindow::onTranscriptionResultReceived(QString status, int percent)
{
    qDebug() << "resp server " << status << "-" << percent;
    ui->progressBar->setValue(percent);
}

// =============================================================================
void MainWindow::onJoinRet(QString ret)
{
    qDebug() << "Join reply " << ret;
}

// =============================================================================
void MainWindow::on_btntest_clicked()
{
    if (!m_backend)
        return;

    const QString serverIp = ui->leIP->text().trimmed();
    const quint16 serverPort = ui->lePort->text().toUShort();

    qDebug() << "Connecting unified BackendClient to:" << serverIp << ":" << serverPort;
    m_backend->connectToServer(serverIp, serverPort > 0 ? serverPort : 8000);
}
