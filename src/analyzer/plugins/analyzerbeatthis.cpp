#include "analyzer/plugins/analyzerbeatthis.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QtDebug>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "analyzer/constants.h"
#include "util/workerthread.h"

namespace mixxx {

AnalyzerBeatThis::AnalyzerBeatThis(Options options)
        : m_options(std::move(options)) {
}

bool AnalyzerBeatThis::initialize(audio::SampleRate sampleRate) {
    m_beats.clear();
    m_downbeats.clear();
    m_frames = 0;
    m_failed = false;
    m_initialized = false;
    m_audio.remove();
    m_sampleRate = sampleRate;
    const QString python = QStandardPaths::findExecutable(m_options.python);
    if (!sampleRate.isValid() || m_options.python.isEmpty() ||
            python.isEmpty() || !QFileInfo(m_options.script).isFile() ||
            !QFileInfo(m_options.checkpoint).isFile() ||
            m_options.sha256.size() != 64) {
        qWarning() << "Beat This! requires a Python runtime, script, local "
                      "checkpoint and SHA-256";
        return false;
    }
    m_options.python = python;
    m_initialized = m_audio.open();
    return m_initialized;
}

bool AnalyzerBeatThis::processSamples(const CSAMPLE* input, SINT count) {
    if (!m_initialized || m_failed || !input || count < 0 || count % 2 != 0) {
        return false;
    }
    constexpr SINT kChunk = 4096;
    std::vector<float> mono(kChunk);
    for (SINT offset = 0; offset < count / 2; offset += kChunk) {
        const SINT frames = std::min(kChunk, count / 2 - offset);
        for (SINT i = 0; i < frames; ++i) {
            mono[i] = input[(offset + i) * 2] * 0.5f +
                    input[(offset + i) * 2 + 1] * 0.5f;
            if (!std::isfinite(mono[i])) {
                m_failed = true;
                return false;
            }
        }
        const qint64 bytes = frames * static_cast<qint64>(sizeof(float));
        if (m_audio.write(reinterpret_cast<const char*>(mono.data()), bytes) !=
                bytes) {
            m_failed = true;
            return false;
        }
        m_frames += frames;
    }
    return true;
}

bool AnalyzerBeatThis::finalize() {
    if (!m_initialized || m_failed || m_frames == 0 || !m_audio.flush()) {
        return false;
    }
    m_initialized = false;
    const QString audioPath = m_audio.fileName();
    m_audio.close();
    QProcess process;
    process.setProgram(m_options.python);
    process.setArguments({m_options.script,
            "--audio",
            audioPath,
            "--sample-rate",
            QString::number(m_sampleRate.value()),
            "--checkpoint",
            m_options.checkpoint,
            "--sha256",
            m_options.sha256});
    process.start();
    const bool started = process.waitForStarted(10000);
    QElapsedTimer timer;
    timer.start();
    auto* worker = qobject_cast<WorkerThread*>(QThread::currentThread());
    while (started && process.state() != QProcess::NotRunning &&
            timer.elapsed() < 600000 && !(worker && worker->isStopping())) {
        process.waitForFinished(200);
    }
    if (!started || process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(5000);
        m_audio.remove();
        qWarning() << "Beat This! process failed to start, was cancelled or "
                      "timed out";
        return false;
    }
    m_audio.remove();
    if (process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
        qWarning().noquote()
                << "Beat This!:"
                << QString::fromUtf8(process.readAllStandardError()).left(4096);
        return false;
    }
    return parseResults(process.readAllStandardOutput(),
            m_options.sha256,
            m_sampleRate,
            m_frames,
            &m_beats,
            &m_downbeats);
}

bool AnalyzerBeatThis::parseResults(const QByteArray& json,
        const QString& sha256,
        audio::SampleRate sampleRate,
        SINT frames,
        QVector<audio::FramePos>* beats,
        QVector<audio::FramePos>* downbeats) {
    if (!sampleRate.isValid() || frames <= 0 || json.size() > 8 * 1024 * 1024) {
        return false;
    }
    const auto document = QJsonDocument::fromJson(json);
    const auto object = document.object();
    if (!document.isObject() || object.value("version").toInt() != 1 ||
            object.value("model_sha256").toString() != sha256.toLower()) {
        return false;
    }
    auto parse = [&](const QJsonValue& value,
                         QVector<audio::FramePos>* positions) {
        if (!value.isArray()) {
            return false;
        }
        for (const auto& entry : value.toArray()) {
            const double seconds = entry.toDouble(-1);
            const audio::FramePos rawPosition(seconds * sampleRate);
            const auto position = rawPosition.isValid()
                    ? rawPosition.toNearestFrameBoundary()
                    : rawPosition;
            if (!entry.isDouble() || !std::isfinite(seconds) || seconds < 0 ||
                    position.value() >= frames ||
                    (!positions->isEmpty() && position <= positions->back())) {
                return false;
            }
            positions->push_back(position);
        }
        return true;
    };
    QVector<audio::FramePos> parsedBeats;
    QVector<audio::FramePos> parsedDownbeats;
    if (!parse(object.value("beats"), &parsedBeats) || parsedBeats.size() < 2 ||
            !parse(object.value("downbeats"), &parsedDownbeats)) {
        return false;
    }
    for (const auto downbeat : parsedDownbeats) {
        if (!std::binary_search(
                    parsedBeats.begin(), parsedBeats.end(), downbeat)) {
            return false;
        }
    }
    *beats = std::move(parsedBeats);
    *downbeats = std::move(parsedDownbeats);
    return true;
}

} // namespace mixxx
