#pragma once
#include <QString>
#include <QStringList>

struct WavSplitOptions {
    double maxMB         = 19.5;   // batas ukuran per file (aman di bawah 20 MB)
    double thresholdDb   = -35.0;  // di bawah ini dianggap hening
    double minSilenceSec = 0.5;    // durasi hening minimal agar dianggap jeda
    double frameSec      = 0.02;   // resolusi analisis (20 ms)
};

struct WavSplitResult {
    bool        ok = false;
    QString     error;
    QStringList files;
};

// Memecah WAV PCM 16-bit (mono/stereo) di titik hening, tiap file <= maxMB.
// Output: <nama>_000.wav, <nama>_001.wav, ... di folder yang sama.
// Blocking: panggil dari thread worker (mis. QtConcurrent::run).
class WavSplitter
{
public:
    static WavSplitResult split(const QString &srcPath,
                                const WavSplitOptions &opt = WavSplitOptions());
};
