#include "analyzer/analyzerbeats.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QtDebug>

#include "analyzer/analyzertrack.h"
#include "analyzer/constants.h"
#include "analyzer/plugins/analyzerbeatthis.h"
#include "analyzer/plugins/analyzercustombeats.h"
#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "analyzer/plugins/analyzersoundtouchbeats.h"
#include "library/rekordbox/rekordboxconstants.h"
#include "track/beatfactory.h"
#include "track/track.h"

// static
QList<mixxx::AnalyzerPluginInfo> AnalyzerBeats::availablePlugins() {
    QList<mixxx::AnalyzerPluginInfo> plugins;
    // First one below is the default
    plugins.append(mixxx::AnalyzerQueenMaryBeats::pluginInfo());
    plugins.append(mixxx::AnalyzerSoundTouchBeats::pluginInfo());
    plugins.append(mixxx::AnalyzerBeatThis::pluginInfo());
    plugins.append(mixxx::AnalyzerCustomBeats::pluginInfo());
    return plugins;
}

// static
mixxx::AnalyzerPluginInfo AnalyzerBeats::defaultPlugin() {
    const auto plugins = availablePlugins();
    DEBUG_ASSERT(!plugins.isEmpty());
    return plugins.at(0);
}

AnalyzerBeats::AnalyzerBeats(
        UserSettingsPointer pConfig, bool enforceBpmDetection)
        : m_bpmSettings(pConfig),
          m_resourcePath(pConfig->getResourcePath()),
          m_enforceBpmDetection(enforceBpmDetection),
          m_bPreferencesReanalyzeOldBpm(false),
          m_bPreferencesReanalyzeImported(false),
          m_bPreferencesFixedTempo(true),
          m_bPreferencesFastAnalysis(false),
          m_maxFramesToProcess(0),
          m_currentFrame(0) {
}

bool AnalyzerBeats::initialize(const AnalyzerTrack& track,
        mixxx::audio::SampleRate sampleRate,
        mixxx::audio::ChannelCount channelCount,
        SINT frameLength) {
    if (frameLength <= 0) {
        return false;
    }

    bool bPreferencesBeatDetectionEnabled =
            m_enforceBpmDetection || m_bpmSettings.getBpmDetectionEnabled();
    if (!bPreferencesBeatDetectionEnabled) {
        qDebug() << "Beat calculation is deactivated";
        return false;
    }

    bool bpmLock = track.getTrack()->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    m_tempoMode =
            track.getOptions().tempoMode.value_or(m_bpmSettings.getTempoMode());
    m_bPreferencesFixedTempo = m_tempoMode == mixxx::BeatGridMode::Constant;
    m_bPreferencesReanalyzeOldBpm = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bPreferencesReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bPreferencesFastAnalysis =
            m_bpmSettings.getFastAnalysis() && m_bPreferencesFixedTempo;
    m_modelSha256 = m_bpmSettings.getBeatThisModelSha256().toLower();

    const auto plugins = availablePlugins();
    if (!plugins.isEmpty()) {
        m_pluginId = defaultPlugin().id();
        QString pluginId = m_bpmSettings.getBeatPluginId();
        for (const auto& info : plugins) {
            if (info.id() == pluginId) {
                m_pluginId = pluginId; // configured Plug-In available
                break;
            }
        }
    }

    const auto customChoice = [](const QString& configured, bool constant) {
        if (configured == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id() ||
                configured == mixxx::AnalyzerBeatThis::pluginInfo().id()) {
            return configured;
        }
        return constant ? mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()
                        : mixxx::AnalyzerBeatThis::pluginInfo().id();
    };
    const auto constantId = customChoice(m_bpmSettings.getCustomConstantPlugin(), true);
    const auto variableId = customChoice(m_bpmSettings.getCustomVariablePlugin(), false);
    m_extraVersionInfo = getExtraVersionInfo(m_pluginId,
            m_bPreferencesFastAnalysis,
            m_modelSha256,
            m_tempoMode);
    const auto addQueenMaryVersion = [this](const QString& id, QHash<QString, QString>* version) {
        if (id == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id() &&
                m_bpmSettings.getQueenMaryDownbeats()) {
            version->insert("qm_downbeats", "4/4:1");
        }
    };
    addQueenMaryVersion(m_pluginId, &m_extraVersionInfo);
    if (m_pluginId == mixxx::AnalyzerCustomBeats::pluginInfo().id()) {
        m_extraVersionInfo.insert("custom_backend", "2");
        for (const bool constant : {true, false}) {
            if ((constant && m_tempoMode == mixxx::BeatGridMode::Variable) ||
                    (!constant && m_tempoMode == mixxx::BeatGridMode::Constant)) {
                continue;
            }
            const auto id = constant ? constantId : variableId;
            auto version = getExtraVersionInfo(id, false, m_modelSha256, constant ? mixxx::BeatGridMode::Constant : mixxx::BeatGridMode::Variable);
            addQueenMaryVersion(id, &version);
            const QString prefix = constant ? "constant_" : "variable_";
            for (auto it = version.cbegin(); it != version.cend(); ++it) {
                m_extraVersionInfo.insert(prefix + it.key(), it.value());
            }
        }
    }

    qDebug() << "AnalyzerBeats preference settings:"
             << "\nPlugin:" << m_pluginId
             << "\nFixed tempo assumption:" << m_bPreferencesFixedTempo
             << "\nRe-analyze when settings change:" << m_bPreferencesReanalyzeOldBpm
             << "\nRe-analyze imported from other software:" << m_bPreferencesReanalyzeImported
             << "\nFast analysis:" << m_bPreferencesFastAnalysis;

    m_sampleRate = sampleRate;
    m_audioEvidence.initialize(sampleRate);
    m_channelCount = channelCount;
    // In fast analysis mode, skip processing after
    // kFastAnalysisSecondsToAnalyze seconds are analyzed.
    if (m_bPreferencesFastAnalysis) {
        m_maxFramesToProcess =
                mixxx::kFastAnalysisSecondsToAnalyze * m_sampleRate;
    } else {
        m_maxFramesToProcess = frameLength;
    }
    m_currentFrame = 0;

    // if we can load a stored track don't reanalyze it
    bool bShouldAnalyze = shouldAnalyze(track.getTrack());

    DEBUG_ASSERT(!m_pPlugin);
    if (bShouldAnalyze) {
        if (m_pluginId == mixxx::AnalyzerCustomBeats::pluginInfo().id()) {
            if (m_tempoMode == mixxx::BeatGridMode::Auto && constantId == variableId) {
                m_pPlugin = makePlugin(constantId, m_tempoMode);
            } else {
                m_pPlugin = std::make_unique<mixxx::AnalyzerCustomBeats>(m_tempoMode,
                        m_tempoMode != mixxx::BeatGridMode::Variable
                                ? makePlugin(constantId, mixxx::BeatGridMode::Constant)
                                : nullptr,
                        m_tempoMode != mixxx::BeatGridMode::Constant
                                ? makePlugin(variableId, mixxx::BeatGridMode::Variable)
                                : nullptr,
                        m_tempoMode == mixxx::BeatGridMode::Auto ? &m_audioEvidence : nullptr);
            }
        } else {
            m_pPlugin = makePlugin(m_pluginId, m_tempoMode);
        }

        if (m_pPlugin) {
            if (m_pPlugin->initialize(m_sampleRate)) {
                qDebug() << "Beat calculation started with plugin" << m_pluginId;
            } else {
                qDebug() << "Beat calculation will not start.";
                m_pPlugin.reset();
                bShouldAnalyze = false;
            }
        } else {
            bShouldAnalyze = false;
        }
    }
    return bShouldAnalyze;
}

std::unique_ptr<mixxx::AnalyzerBeatsPlugin> AnalyzerBeats::makePlugin(
        const QString& id, mixxx::BeatGridMode mode) const {
    if (id == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()) {
        return std::make_unique<mixxx::AnalyzerQueenMaryBeats>(
                mode, m_bpmSettings.getQueenMaryDownbeats());
    }
    if (id == mixxx::AnalyzerBeatThis::pluginInfo().id()) {
        return std::make_unique<mixxx::AnalyzerBeatThis>(
                mixxx::AnalyzerBeatThis::Options{m_bpmSettings.getBeatThisPython(),
                        m_resourcePath + "analysis/beat_this_worker.py",
                        m_bpmSettings.getBeatThisModel(),
                        m_modelSha256});
    }
    if (id == mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        return std::make_unique<mixxx::AnalyzerSoundTouchBeats>();
    }
    return nullptr;
}

bool AnalyzerBeats::shouldAnalyze(TrackPointer pTrack) const {
    bool bpmLock = pTrack->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    // If the track already has a Beats object then we need to decide whether to
    // analyze this track or not.
    const mixxx::BeatsPointer pBeats = pTrack->getBeats();
    if (!pBeats) {
        return true;
    }
    if (!pBeats->getBpmInRange(mixxx::audio::kStartFramePos,
                       mixxx::audio::FramePos{
                               pTrack->getDuration() * pBeats->getSampleRate()})
                    .isValid()) {
        // Tracks with an invalid bpm <= 0 should be re-analyzed,
        // independent of the preference settings. We expect that
        // all tracks have a bpm > 0 when analyzed. Users that want
        // to keep their zero bpm tracks could lock them to prevent
        // this re-analysis (see the check above).
        qDebug() << "Re-analyzing track with invalid BPM despite preference settings.";
        return true;
    }

    QString subVersion = pBeats->getSubVersion();
    if (subVersion == mixxx::rekordboxconstants::beatsSubversion) {
        return m_bPreferencesReanalyzeImported;
    }

    if (subVersion.isEmpty() && pBeats->firstBeat() <= mixxx::audio::kStartFramePos &&
            m_pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        // This happens if the beat grid was created from the metadata BPM value.
        qDebug() << "First beat is 0 for grid so analyzing track to find first beat.";
        return true;
    }

    QString version = pBeats->getVersion();
    QString newVersion = BeatFactory::getPreferredVersion(
            m_bPreferencesFixedTempo);
    if (m_tempoMode == mixxx::BeatGridMode::Auto ||
            ((m_pluginId == mixxx::AnalyzerBeatThis::pluginInfo().id() ||
                     m_pluginId == mixxx::AnalyzerCustomBeats::pluginInfo().id()) &&
                    m_tempoMode == mixxx::BeatGridMode::Variable)) {
        newVersion = version;
    }
    QString newSubVersion = BeatFactory::getPreferredSubVersion(
            m_extraVersionInfo);

    if (version == newVersion && subVersion == newSubVersion) {
        // If the version and settings have not changed then if the world is
        // sane, re-analyzing will do nothing.
        return false;
    }
    // Beat grid exists but version and settings differ
    if (!m_bPreferencesReanalyzeOldBpm) {
        qDebug() << "Beat calculation skips analyzing because the track has"
                << "a BPM computed by a previous Mixxx version and user"
                << "preferences indicate we should not change it.";
        return false;
    }

    return true;
}

bool AnalyzerBeats::processSamples(const CSAMPLE* pIn, SINT count) {
    VERIFY_OR_DEBUG_ASSERT(m_pPlugin) {
        return false;
    }

    if (m_currentFrame >= m_maxFramesToProcess) {
        return true;
    }
    const SINT numFrames = std::min(
            count / m_channelCount, m_maxFramesToProcess - m_currentFrame);
    count = numFrames * m_channelCount;
    m_currentFrame += numFrames;
    const CSAMPLE* pBeatInput = pIn;
    CSAMPLE* pDrumChannel = nullptr;

    if (m_channelCount == mixxx::audio::ChannelCount::stem()) {
        // We have an 8 channel soundsource. The only implemented soundsource with
        // 8ch is the NI STEM file format.
        // TODO: If we add other soundsources with 8ch, we need to rework this condition.
        //
        // For NI STEM we mix all the stems together except the first one,
        // which contains drums or beats by convention.
        count = numFrames * mixxx::audio::ChannelCount::stereo();
        pDrumChannel = SampleUtil::alloc(count);

        VERIFY_OR_DEBUG_ASSERT(pDrumChannel) {
            return false;
        }

        if (m_bpmSettings.getStemStrategy() == BeatDetectionSettings::StemStrategy::Enforced) {
            SampleUtil::copyOneStereoFromMulti(pDrumChannel, pIn, numFrames, m_channelCount, 0);
        } else {
            SampleUtil::mixMultichannelToStereo(pDrumChannel, pIn, numFrames, m_channelCount);
        }

        pBeatInput = pDrumChannel;
    } else if (m_channelCount > mixxx::audio::ChannelCount::stereo()) {
        DEBUG_ASSERT(!"Unsupported channel count");
        return false;
    }

    bool ret = m_pPlugin->processSamples(pBeatInput, count);
    if (ret && m_tempoMode != mixxx::BeatGridMode::Variable &&
            m_pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id() &&
            m_channelCount != mixxx::audio::ChannelCount::mono()) {
        ret = m_audioEvidence.processSamples(pBeatInput, count);
    }
    if (pDrumChannel) {
        SampleUtil::free(pDrumChannel);
    }
    return ret;
}

void AnalyzerBeats::cleanup() {
    m_pPlugin.reset();
}

void AnalyzerBeats::storeResults(TrackPointer pTrack) {
    storeResultsWithProgress(pTrack, {});
}

bool AnalyzerBeats::hasFinalizationProgress() const {
    return m_pPlugin && m_pPlugin->hasFinalizationProgress();
}

void AnalyzerBeats::storeResultsWithProgress(
        TrackPointer pTrack, const AnalyzerProgressCallback& progress) {
    VERIFY_OR_DEBUG_ASSERT(m_pPlugin) {
        return;
    }

    if (m_tempoMode != mixxx::BeatGridMode::Variable) {
        m_audioEvidence.prepare();
    }
    const auto pluginProgress = [&progress, this](double value) {
        if (progress) {
            progress(value * (m_audioEvidence.hasEvidence() ? 0.85 : 1.0));
        }
    };
    if (!m_pPlugin->finalizeWithProgress(pluginProgress)) {
        qWarning() << "Beat/BPM analysis failed";
        return;
    }

    mixxx::BeatsPointer pBeats;
    if (m_pPlugin->supportsBeatTracking()) {
        QVector<mixxx::audio::FramePos> beats = m_pPlugin->getBeats();
        const auto* custom = dynamic_cast<const mixxx::AnalyzerCustomBeats*>(m_pPlugin.get());
        const auto prepared = custom ? custom->preparedGrid() : mixxx::BeatsPointer{};
        pBeats = prepared ? mixxx::Beats::fromByteArray(m_sampleRate,
                                    prepared->getVersion(),
                                    BeatFactory::getPreferredSubVersion(m_extraVersionInfo),
                                    prepared->toByteArray())
                          : BeatFactory::makePreferredBeats(beats,
                                    m_extraVersionInfo,
                                    custom ? custom->resolvedTempoMode() : m_tempoMode,
                                    m_sampleRate,
                                    m_pPlugin->preservesBeatPositions());
        if (pBeats && !prepared && !m_pPlugin->getDownbeats().isEmpty()) {
            pBeats = pBeats->withDownbeats(m_pPlugin->getDownbeats());
        }
        if (pBeats && !prepared && m_tempoMode != mixxx::BeatGridMode::Variable) {
            if (m_tempoMode == mixxx::BeatGridMode::Auto && m_audioEvidence.hasEvidence()) {
                const auto makeCandidate = [&](bool constant) {
                    if (pBeats->hasConstantTempo() == constant) {
                        return pBeats;
                    }
                    auto candidate = BeatFactory::makePreferredBeats(beats,
                            m_extraVersionInfo,
                            constant,
                            m_sampleRate,
                            m_pPlugin->preservesBeatPositions());
                    return candidate && !m_pPlugin->getDownbeats().isEmpty()
                            ? candidate->withDownbeats(m_pPlugin->getDownbeats())
                            : candidate;
                };
                const auto constant = m_audioEvidence.evaluate(makeCandidate(true), true);
                const auto variable = m_audioEvidence.evaluate(makeCandidate(false), true);
                pBeats = mixxx::BeatGridEvidence::preferConstant(
                                 constant, variable, pBeats->hasConstantTempo())
                        ? constant.grid
                        : variable.grid;
            } else {
                pBeats = m_audioEvidence.evaluate(pBeats, false).grid;
            }
        }
        qDebug() << "AnalyzerBeats plugin detected" << beats.size()
                 << "beats. Predominant BPM:"
                 << (pBeats ? pBeats->getBpmInRange(
                                      mixxx::audio::kStartFramePos,
                                      mixxx::audio::FramePos{
                                              pTrack->getDuration() *
                                              pBeats->getSampleRate()})
                            : mixxx::Bpm());
    } else {
        mixxx::Bpm bpm = m_pPlugin->getBpm();
        qDebug() << "AnalyzerBeats plugin detected constant BPM: " << bpm;
        pBeats = mixxx::Beats::fromConstTempo(m_sampleRate, mixxx::audio::kStartFramePos, bpm);
    }

    if (pBeats) {
        pTrack->trySetBeats(pBeats);
    } else {
        qWarning() << "Beat/BPM analysis produced no usable grid";
    }
    if (progress) {
        progress(1.0);
    }
}

// static
QHash<QString, QString> AnalyzerBeats::getExtraVersionInfo(
        const QString& pluginId,
        bool bPreferencesFastAnalysis,
        const QString& modelSha256,
        mixxx::BeatGridMode mode) {
    QHash<QString, QString> extraVersionInfo;
    extraVersionInfo["vamp_plugin_id"] = pluginId;
    if (mode == mixxx::BeatGridMode::Auto) {
        extraVersionInfo["tempo_mode"] = "auto:3";
    }
    if (mode != mixxx::BeatGridMode::Variable &&
            pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        extraVersionInfo["audio_grid_evidence"] = "2";
    }
    if (pluginId == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()) {
        extraVersionInfo["beat_tracker_version"] = "3";
    }
    if (pluginId == mixxx::AnalyzerBeatThis::pluginInfo().id()) {
        extraVersionInfo["model_sha256"] = modelSha256.toLower();
        extraVersionInfo["beat_this_backend"] = "3";
    }
    if (bPreferencesFastAnalysis) {
        extraVersionInfo["fast_analysis"] = "1";
    }
    return extraVersionInfo;
}
