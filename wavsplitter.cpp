#include "wavsplitter.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

struct WavInfo {
    quint16 format     = 0;
    quint16 channels   = 0;
    quint32 rate       = 0;
    quint16 blockAlign = 0;
    quint16 bits       = 0;
    qint64  dataOffset = 0;
    qint64  dataSize   = 0;
};

bool parseWav(QFile &f, WavInfo &w, QString &err)
{
    char riff[12];
    if (f.read(riff, 12) != 12 || std::memcmp(riff, "RIFF", 4) != 0
        || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        err = "Bukan file WAV yang valid";
        return false;
    }

    bool haveFmt = false;
    while (!f.atEnd()) {
        char id[4];
        char szBuf[4];
        if (f.read(id, 4) != 4 || f.read(szBuf, 4) != 4)
            break;
        const quint32 sz = qFromLittleEndian<quint32>(szBuf);

        if (std::memcmp(id, "fmt ", 4) == 0) {
            const QByteArray b = f.read(sz);
            if (b.size() < 16) {
                err = "Chunk fmt rusak";
                return false;
            }
            const char *p = b.constData();
            w.format     = qFromLittleEndian<quint16>(p);
            w.channels   = qFromLittleEndian<quint16>(p + 2);
            w.rate       = qFromLittleEndian<quint32>(p + 4);
            w.blockAlign = qFromLittleEndian<quint16>(p + 12);
            w.bits       = qFromLittleEndian<quint16>(p + 14);
            haveFmt = true;
            if (sz & 1) f.seek(f.pos() + 1);
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!haveFmt) {
                err = "Chunk data muncul sebelum fmt";
                return false;
            }
            w.dataOffset = f.pos();
            const qint64 remaining = f.size() - w.dataOffset;
            // Header hasil pw-record yang diputus paksa bisa punya size 0 / 0xFFFFFFFF
            w.dataSize = (sz == 0 || sz == 0xFFFFFFFFu || qint64(sz) > remaining)
                             ? remaining : qint64(sz);
            return true;
        } else {
            f.seek(f.pos() + sz + (sz & 1));
        }
    }
    err = "Chunk data tidak ditemukan";
    return false;
}

QByteArray makeHeader(const WavInfo &w, quint32 dataBytes)
{
    QByteArray h;
    QDataStream s(&h, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4);
    s << quint32(36 + dataBytes);
    s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << w.channels << w.rate
      << quint32(w.rate * w.blockAlign) << w.blockAlign << w.bits;
    s.writeRawData("data", 4);
    s << dataBytes;
    return h;
}

} // namespace

WavSplitResult WavSplitter::split(const QString &srcPath, const WavSplitOptions &opt)
{
    WavSplitResult res;

    QFile in(srcPath);
    if (!in.open(QIODevice::ReadOnly)) {
        res.error = "Gagal membuka: " + srcPath;
        return res;
    }

    WavInfo w;
    if (!parseWav(in, w, res.error))
        return res;

    if (w.bits != 16 || (w.format != 1 && w.format != 0xFFFE)
        || w.channels == 0 || w.blockAlign != w.channels * 2) {
        res.error = "Hanya mendukung PCM 16-bit (--format s16)";
        return res;
    }

    const qint64 totalSamples = w.dataSize / w.blockAlign;   // sample-frame
    const qint64 frameSamples = std::max<qint64>(1, qint64(w.rate * opt.frameSec));
    const qint64 frameBytes   = frameSamples * w.blockAlign;

    // ---------- Pass 1: level (dB) per frame 20 ms ----------
    std::vector<float> levelDb;
    levelDb.reserve(size_t(totalSamples / frameSamples) + 1);

    in.seek(w.dataOffset);
    qint64 left = totalSamples * w.blockAlign;
    QByteArray buf;
    while (left > 0) {
        buf = in.read(std::min(frameBytes, left));
        const int n = buf.size() / 2;                         // jumlah nilai int16
        if (n <= 0) break;
        const qint16 *p = reinterpret_cast<const qint16 *>(buf.constData());
        double sum = 0;
        for (int i = 0; i < n; ++i) {
            const double v = qFromLittleEndian<qint16>(p + i) / 32768.0;
            sum += v * v;
        }
        const double ms = sum / n;
        levelDb.push_back(float(10.0 * std::log10(std::max(ms, 1e-12))));
        left -= buf.size();
    }

    // ---------- Cari jeda hening, simpan titik tengahnya ----------
    const size_t minFrames = size_t(std::max(1.0, opt.minSilenceSec / opt.frameSec));
    std::vector<qint64> mids;   // dalam satuan sample-frame
    size_t runStart = 0;
    bool inRun = false;
    for (size_t i = 0; i <= levelDb.size(); ++i) {
        const bool quiet = i < levelDb.size() && levelDb[i] < opt.thresholdDb;
        if (quiet && !inRun) {
            inRun = true;
            runStart = i;
        } else if (!quiet && inRun) {
            inRun = false;
            if (i - runStart >= minFrames)
                mids.push_back(qint64((runStart + i) / 2) * frameSamples);
        }
    }

    // ---------- Tentukan titik potong ----------
    const qint64 maxSamples = qint64((opt.maxMB * 1'000'000.0 - 44) / w.blockAlign);
    const qint64 minSamples = qint64(maxSamples * 0.3);       // hindari potongan terlalu pendek
    if (maxSamples <= 0) {
        res.error = "maxMB terlalu kecil";
        return res;
    }

    std::vector<qint64> cuts{0};
    qint64 start = 0;
    while (totalSamples - start > maxSamples) {
        qint64 cut = start + maxSamples;                      // fallback: potong paksa
        auto it = std::upper_bound(mids.begin(), mids.end(), start + maxSamples);
        if (it != mids.begin()) {
            --it;
            if (*it > start + minSamples)
                cut = *it;                                    // jeda terakhir sebelum batas
        }
        cuts.push_back(cut);
        start = cut;
    }
    cuts.push_back(totalSamples);

    // ---------- Pass 2: tulis tiap potongan ----------
    const QFileInfo fi(srcPath);
    const QString base = fi.absoluteDir().filePath(fi.completeBaseName());

    constexpr qint64 kChunk = 1 << 20;
    QByteArray chunk;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        const qint64 nSamples = cuts[i + 1] - cuts[i];
        if (nSamples <= 0) continue;
        const qint64 nBytes = nSamples * w.blockAlign;

        const QString outPath = QString("%1_%2.wav").arg(base).arg(int(i), 3, 10, QChar('0'));
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            res.error = "Gagal menulis: " + outPath;
            return res;
        }
        out.write(makeHeader(w, quint32(nBytes)));

        in.seek(w.dataOffset + cuts[i] * w.blockAlign);
        qint64 remain = nBytes;
        while (remain > 0) {
            chunk = in.read(std::min(kChunk, remain));
            if (chunk.isEmpty()) break;
            out.write(chunk);
            remain -= chunk.size();
        }
        out.close();
        res.files << outPath;
    }

    res.ok = true;
    return res;
}
