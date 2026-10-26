#pragma once

#include <QHash>
#include <QList>
#include <memory>

#include "analyzer/analyzer.h"
#include "analyzer/plugins/analyzerplugin.h"
#include "analyzer/plugins/beatgridevidence.h"
#include "preferences/beatdetectionsettings.h"
#include "preferences/usersettings.h"

class AnalyzerBeats : public Analyzer {
  public:
    explicit AnalyzerBeats(
            UserSettingsPointer pConfig,
            bool enforceBpmDetection = false);
    ~AnalyzerBeats() override = default;

    static QList<mixxx::AnalyzerPluginInfo> availablePlugins();
    static mixxx::AnalyzerPluginInfo defaultPlugin();

    bool initialize(const AnalyzerTrack& track,
            mixxx::audio::SampleRate sampleRate,
            mixxx::audio::ChannelCount channelCount,
            SINT frameLength) override;
    bool processSamples(const CSAMPLE* pIn, SINT count) override;
    void storeResults(TrackPointer tio) override;
    bool hasFinalizationProgress() const override;
    void storeResultsWithProgress(TrackPointer track,
            const AnalyzerProgressCallback& progress) override;
    void cleanup() override;

  private:
    bool shouldAnalyze(TrackPointer pTrack) const;
    std::unique_ptr<mixxx::AnalyzerBeatsPlugin> makePlugin(
            const QString& id, mixxx::BeatGridMode mode) const;
    static QHash<QString, QString> getExtraVersionInfo(const QString& pluginId,
            bool bPreferencesFastAnalysis,
            const QString& modelSha256,
            mixxx::BeatGridMode mode);

    BeatDetectionSettings m_bpmSettings;
    const QString m_resourcePath;
    std::unique_ptr<mixxx::AnalyzerBeatsPlugin> m_pPlugin;
    mixxx::BeatGridEvidence m_audioEvidence;
    const bool m_enforceBpmDetection;
    QString m_pluginId;
    QString m_modelSha256;
    QHash<QString, QString> m_extraVersionInfo;
    mixxx::BeatGridMode m_tempoMode = mixxx::BeatGridMode::Auto;
    bool m_bPreferencesReanalyzeOldBpm;
    bool m_bPreferencesReanalyzeImported;
    bool m_bPreferencesFixedTempo;
    bool m_bPreferencesFastAnalysis;

    mixxx::audio::SampleRate m_sampleRate;
    mixxx::audio::ChannelCount m_channelCount;
    SINT m_maxFramesToProcess;
    SINT m_currentFrame;
};
