#include "record.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

Recorder::Recorder(const QString &wavPath, QWidget *parent)
    : QObject(parent), m_defaultPath(wavPath)
{



}

Recorder::~Recorder()
{
    if (isRecording()) {
        m_recorder->terminate();
        if (!m_recorder->waitForFinished(2000))
            m_recorder->kill();
    }
}

bool Recorder::isRecording() const
{
    return m_recorder && m_recorder->state() != QProcess::NotRunning;
}

void Recorder::onStartClicked()
{
    startRecording(m_defaultPath);
}

void Recorder::onStopClicked()
{
    stopRecording();
}

bool Recorder::startRecording(const QString &wavPath, const QString &micTarget)
{
    if (isRecording()) {
        qWarning() << "Recording already running";
        return false;
    }

    // Pastikan folder tujuan ada
    QDir().mkpath(QFileInfo(wavPath).absolutePath());
    m_currentPath = wavPath;

    QStringList args;
    if (!micTarget.isEmpty())
        args << "--target" << micTarget;
    args << "--rate"     << "48000"   // native mic umumnya 48k, tanpa resampling = paling bersih
         << "--channels" << "1"
         << "--format"   << "s16"     // atau "f32" kalau mau headroom lebih
         << "--quality"  << "10"      // kualitas resampler (0-14, default 4)
         << wavPath;


    m_recorder = new QProcess(this);
    m_recorder->setProgram("pw-record");
    m_recorder->setArguments(args);
    m_recorder->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_recorder, &QProcess::readyReadStandardOutput, this, [this]() {
        qDebug().noquote() << "[REC]" << m_recorder->readAllStandardOutput();
    });

    connect(m_recorder, &QProcess::started, this, [this]() {
        qDebug() << "Recorder started ->" << m_currentPath;
        //m_status->setText(tr("Recording... %1").arg(m_currentPath));
        updateUi();
    });


    connect(m_recorder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning() << "Recorder process error:" << error;
        if (error == QProcess::FailedToStart) {
            const QString path = m_currentPath;   // simpan dulu sebelum objek dibersihkan
            m_recorder->deleteLater();
            m_recorder = nullptr;
            emit recordingFinished(path, false);
        }
    });

    connect(m_recorder,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                qDebug() << "Recorder finished. Exit code:" << exitCode
                         << "Exit status:" << exitStatus;

                const bool ok = QFileInfo::exists(m_currentPath)
                                && QFileInfo(m_currentPath).size() > 44; // > header WAV
                //m_status->setText(ok ? tr("Saved: %1").arg(m_currentPath)
                //                     : tr("Recording failed / empty"));

                m_recorder->deleteLater();
                m_recorder = nullptr;
                updateUi();

                emit recordingFinished(m_currentPath, ok);
            });

    m_recorder->start();
    updateUi();
    return true;
}

void Recorder::stopRecording()
{
    if (!isRecording())
        return;

    //m_status->setText(tr("Stopping..."));

    // SIGTERM: pw-record keluar dengan rapi & finalisasi header WAV
    m_recorder->terminate();

    // Fallback kalau tidak berhenti dalam 3 detik
    QPointer<QProcess> p(m_recorder);
    QTimer::singleShot(3000, this, [p]() {
        if (p && p->state() != QProcess::NotRunning) {
            qWarning() << "pw-record not responding, killing";
            p->kill();
        }
    });
}

void Recorder::updateUi()
{
    const bool rec = isRecording();
    //m_startRecord->setEnabled(!rec);
    //m_stopRecord->setEnabled(rec);
}
