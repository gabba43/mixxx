#include "analyzer/analyzerbeats.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QtDebug>
#include <algorithm>
#include <exception>
#include <limits>

#include "analyzer/analyzertrack.h"
#include "analyzer/constants.h"
#include "analyzer/plugins/analyzerphasegrid.h"
#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "analyzer/plugins/analyzersoundtouchbeats.h"
#include "library/rekordbox/rekordboxconstants.h"
#include "track/beatfactory.h"
#include "track/track.h"
#include "util/fpclassify.h"

// static
QList<mixxx::AnalyzerPluginInfo> AnalyzerBeats::availablePlugins() {
    QList<mixxx::AnalyzerPluginInfo> plugins;
    // First one below is the default
    plugins.append(mixxx::AnalyzerQueenMaryBeats::pluginInfo());
    plugins.append(mixxx::AnalyzerSoundTouchBeats::pluginInfo());
    plugins.append(mixxx::AnalyzerPhaseGrid::pluginInfo());
    return plugins;
}

// static
mixxx::AnalyzerPluginInfo AnalyzerBeats::defaultPlugin() {
    const auto plugins = availablePlugins();
    DEBUG_ASSERT(!plugins.isEmpty());
    return plugins.at(0);
}

AnalyzerBeats::AnalyzerBeats(UserSettingsPointer pConfig, bool enforceBpmDetection)
        : m_bpmSettings(pConfig),
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

    m_tempoMode = track.getOptions().tempoMode.value_or(m_bpmSettings.getTempoMode());
    m_bPreferencesFixedTempo = m_tempoMode == mixxx::BeatGridMode::Constant;
    m_bPreferencesReanalyzeOldBpm = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bPreferencesReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bPreferencesFastAnalysis = m_bpmSettings.getFastAnalysis() && m_bPreferencesFixedTempo;

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

    if (m_pluginId == mixxx::AnalyzerPhaseGrid::pluginInfo().id()) {
        m_phaseGridInputFailed = false;
        m_phaseGridStereoBuffer.clear();
        if (!sampleRate.isValid() || sampleRate.value() < 1000 || sampleRate.value() > 384000 ||
                (channelCount != mixxx::audio::ChannelCount::mono() &&
                        channelCount != mixxx::audio::ChannelCount::stereo() &&
                        channelCount != mixxx::audio::ChannelCount::stem())) {
            return false;
        }
        m_tempoMode = mixxx::BeatGridMode::Constant;
        m_bPreferencesFixedTempo = true;
        m_bPreferencesFastAnalysis = false;
    }

    qDebug() << "AnalyzerBeats preference settings:"
             << "\nPlugin:" << m_pluginId
             << "\nFixed tempo assumption:" << m_bPreferencesFixedTempo
             << "\nRe-analyze when settings change:" << m_bPreferencesReanalyzeOldBpm
             << "\nRe-analyze imported from other software:" << m_bPreferencesReanalyzeImported
             << "\nFast analysis:" << m_bPreferencesFastAnalysis;

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    m_audioEvidence.initialize(sampleRate);
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
        if (m_pluginId == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerQueenMaryBeats>(m_tempoMode);
        } else if (m_pluginId == mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerSoundTouchBeats>();
        } else if (m_pluginId == mixxx::AnalyzerPhaseGrid::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerPhaseGrid>();
        } else {
            // This must not happen, because we have already verified above
            // that the PlugInId is valid
            DEBUG_ASSERT(false);
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
    if (m_pluginId == mixxx::AnalyzerPhaseGrid::pluginInfo().id() && subVersion.isEmpty() &&
            !m_bPreferencesReanalyzeOldBpm) {
        return false;
    }

    if (subVersion.isEmpty() && pBeats->firstBeat() <= mixxx::audio::kStartFramePos &&
            m_pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        // This happens if the beat grid was created from the metadata BPM value.
        qDebug() << "First beat is 0 for grid so analyzing track to find first beat.";
        return true;
    }

    QString version = pBeats->getVersion();
    QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
            m_pluginId,
            m_bPreferencesFastAnalysis);
    QString newVersion = BeatFactory::getPreferredVersion(
            m_bPreferencesFixedTempo);
    if (m_tempoMode == mixxx::BeatGridMode::Auto) {
        newVersion = version;
    }
    QString newSubVersion = BeatFactory::getPreferredSubVersion(
            extraVersionInfo);

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

    const bool phaseGrid = m_pluginId == mixxx::AnalyzerPhaseGrid::pluginInfo().id();
    if (phaseGrid) {
        if (m_phaseGridInputFailed || count < 0 || count % m_channelCount.value() != 0 ||
                (count && !pIn)) {
            m_phaseGridInputFailed = true;
            return false;
        }
        if (count == 0) {
            return true;
        }
        if (!std::all_of(pIn, pIn + count,
                    [](CSAMPLE value) { return util_isfinite(static_cast<double>(value)); })) {
            m_phaseGridInputFailed = true;
            return false;
        }
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

    if (phaseGrid && m_channelCount == mixxx::audio::ChannelCount::mono()) {
        if (numFrames > std::numeric_limits<SINT>::max() / 2) {
            m_phaseGridInputFailed = true;
            return false;
        }
        try {
            m_phaseGridStereoBuffer.resize(static_cast<std::size_t>(numFrames) * 2);
        } catch (const std::exception&) {
            m_phaseGridInputFailed = true;
            return false;
        }
        for (SINT frame = 0; frame < numFrames; ++frame) {
            m_phaseGridStereoBuffer[frame * 2] = pIn[frame];
            m_phaseGridStereoBuffer[frame * 2 + 1] = pIn[frame];
        }
        count = numFrames * 2;
        pBeatInput = m_phaseGridStereoBuffer.data();
    } else if (m_channelCount == mixxx::audio::ChannelCount::stem()) {
        // We have an 8 channel soundsource. The only implemented soundsource with
        // 8ch is the NI STEM file format.
        // TODO: If we add other soundsources with 8ch, we need to rework this condition.
        //
        // For NI STEM we mix all the stems together except the first one,
        // which contains drums or beats by convention.
        count = numFrames * mixxx::audio::ChannelCount::stereo();
        pDrumChannel = SampleUtil::alloc(count);

        VERIFY_OR_DEBUG_ASSERT(pDrumChannel) {
            if (phaseGrid) {
                m_phaseGridInputFailed = true;
            }
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
    if (ret && !phaseGrid && m_tempoMode != mixxx::BeatGridMode::Variable &&
            m_pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id() &&
            m_channelCount != mixxx::audio::ChannelCount::mono()) {
        ret = m_audioEvidence.processSamples(pBeatInput, count);
    }
    if (pDrumChannel) {
        SampleUtil::free(pDrumChannel);
    }
    if (phaseGrid && !ret) {
        m_phaseGridInputFailed = true;
    }
    return ret;
}

void AnalyzerBeats::cleanup() {
    m_pPlugin.reset();
    std::vector<CSAMPLE>().swap(m_phaseGridStereoBuffer);
    m_phaseGridInputFailed = false;
}

void AnalyzerBeats::storeResults(TrackPointer pTrack) {
    VERIFY_OR_DEBUG_ASSERT(m_pPlugin) {
        return;
    }

    if (m_pPlugin->providesResolvedBeatGrid()) {
        if (!m_phaseGridInputFailed && m_currentFrame == m_maxFramesToProcess &&
                m_pPlugin->finalize()) {
            const auto pBeats = m_pPlugin->resolvedBeatGrid(BeatFactory::getPreferredSubVersion(
                    getExtraVersionInfo(m_pluginId, false)));
            if (pBeats) {
                pTrack->trySetBeats(pBeats);
            }
        }
        return;
    }

    if (!m_pPlugin->finalize()) {
        qWarning() << "Beat/BPM analysis failed";
        return;
    }

    m_audioEvidence.prepare();
    mixxx::BeatsPointer pBeats;
    if (m_pPlugin->supportsBeatTracking()) {
        QVector<mixxx::audio::FramePos> beats = m_pPlugin->getBeats();
        QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
                m_pluginId, m_bPreferencesFastAnalysis);
        pBeats = BeatFactory::makePreferredBeats(
                beats,
                extraVersionInfo,
                m_tempoMode,
                m_sampleRate);
        if (pBeats && m_tempoMode == mixxx::BeatGridMode::Auto && m_audioEvidence.hasEvidence()) {
            const auto candidate = [&](bool constant) {
                return pBeats->hasConstantTempo() == constant
                        ? pBeats
                        : BeatFactory::makePreferredBeats(beats,
                                  extraVersionInfo,
                                  constant,
                                  m_sampleRate);
            };
            const auto constant = m_audioEvidence.evaluate(candidate(true), true);
            const auto variable = m_audioEvidence.evaluate(candidate(false), true);
            pBeats = mixxx::BeatGridEvidence::preferConstant(
                             constant, variable, pBeats->hasConstantTempo())
                    ? constant.grid
                    : variable.grid;
        } else if (pBeats && m_tempoMode == mixxx::BeatGridMode::Constant) {
            pBeats = m_audioEvidence.evaluate(pBeats, false).grid;
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
    }
}

QHash<QString, QString> AnalyzerBeats::getExtraVersionInfo(
        const QString& pluginId, bool bPreferencesFastAnalysis) const {
    QHash<QString, QString> extraVersionInfo;
    extraVersionInfo["vamp_plugin_id"] = pluginId;
    if (pluginId == mixxx::AnalyzerPhaseGrid::pluginInfo().id()) {
        const auto version = mixxx::AnalyzerPhaseGrid::extraVersionInfo(
                m_sampleRate, m_channelCount);
        for (auto it = version.cbegin(); it != version.cend(); ++it) {
            extraVersionInfo.insert(it.key(), it.value());
        }
        if (m_channelCount == mixxx::audio::ChannelCount::stem()) {
            extraVersionInfo.insert(QStringLiteral("phase_grid_stem_strategy"),
                    QString::number(static_cast<int>(m_bpmSettings.getStemStrategy())));
        }
        return extraVersionInfo;
    }
    if (m_tempoMode == mixxx::BeatGridMode::Auto) {
        extraVersionInfo["tempo_mode"] = "auto:3";
    }
    if (m_tempoMode != mixxx::BeatGridMode::Variable &&
            pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        extraVersionInfo["audio_grid_evidence"] = "3";
    }
    if (pluginId == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()) {
        extraVersionInfo["beat_tracker_version"] = "3";
    }
    if (bPreferencesFastAnalysis) {
        extraVersionInfo["fast_analysis"] = "1";
    }
    return extraVersionInfo;
}
