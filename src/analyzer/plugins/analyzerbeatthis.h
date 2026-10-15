#pragma once

#include <QObject>
#include <QTemporaryFile>

#include "analyzer/plugins/analyzerplugin.h"

namespace mixxx {

class AnalyzerBeatThis final : public AnalyzerBeatsPlugin {
  public:
    struct Options {
        QString python;
        QString script;
        QString checkpoint;
        QString sha256;
    };

    static AnalyzerPluginInfo pluginInfo() {
        return AnalyzerPluginInfo("beat-this:1",
                "CPJKU",
                QObject::tr("Beat This! (local model)"),
                true);
    }

    explicit AnalyzerBeatThis(Options options);
    AnalyzerPluginInfo info() const override {
        return pluginInfo();
    }
    bool initialize(audio::SampleRate sampleRate) override;
    bool processSamples(const CSAMPLE* input, SINT count) override;
    bool finalize() override;
    bool supportsBeatTracking() const override {
        return true;
    }
    QVector<audio::FramePos> getBeats() const override {
        return m_beats;
    }
    QVector<audio::FramePos> getDownbeats() const override {
        return m_downbeats;
    }

    static bool parseResults(const QByteArray& json,
            const QString& sha256,
            audio::SampleRate sampleRate,
            SINT frames,
            QVector<audio::FramePos>* beats,
            QVector<audio::FramePos>* downbeats);

  private:
    Options m_options;
    QTemporaryFile m_audio;
    audio::SampleRate m_sampleRate;
    SINT m_frames = 0;
    bool m_failed = false;
    bool m_initialized = false;
    QVector<audio::FramePos> m_beats;
    QVector<audio::FramePos> m_downbeats;
};

} // namespace mixxx
