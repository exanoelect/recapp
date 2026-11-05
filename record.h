#pragma once

#include <QWidget>
#include <QString>
#include <QProcess>


class Recorder : public QObject
{
    Q_OBJECT
public:
    explicit Recorder(const QString &wavPath, QWidget *parent = nullptr);
    ~Recorder() override;

    // Mulai recording ke wavPath. micTarget kosong = default input PipeWire.
    bool startRecording(const QString &wavPath, const QString &micTarget = QString());
    // Hentikan recording (graceful, supaya header WAV ditulis dengan benar).
    void stopRecording();

    bool isRecording() const;

signals:
    void recordingFinished(const QString &wavPath, bool success);

public slots:
    void onStartClicked();
    void onStopClicked();

private:
    void updateUi();

    QProcess *m_recorder = nullptr;
    QString m_defaultPath;   // path default yang dipakai tombol Start
    QString m_currentPath;   // path recording yang sedang berjalan
};

