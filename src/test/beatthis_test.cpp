#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>
#include <vector>

#include "analyzer/analyzerbeats.h"
#include "analyzer/plugins/analyzerbeatthis.h"
#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "preferences/beatdetectionsettings.h"
#include "preferences/dialog/dlgprefbeats.h"
#include "test/mixxxtest.h"
#include "track/beatfactory.h"
#include "track/beats.h"
#include "track/track.h"

namespace {

const auto kRate = mixxx::audio::SampleRate(44100);
const QString kHash(64, 'a');

QByteArray resultJson(
        QJsonArray beats = {0.1, 0.6, 1.1, 1.6}, QJsonArray downbeats = {0.6}) {
    return QJsonDocument(QJsonObject{{"version", 1},
                                 {"model_sha256", kHash},
                                 {"beats", beats},
                                 {"downbeats", downbeats}})
            .toJson();
}

TEST(BeatThisProtocolTest, AcceptsModelIdentityAndBeatDownbeatPositions) {
    QVector<mixxx::audio::FramePos> beats;
    QVector<mixxx::audio::FramePos> downbeats;
    ASSERT_TRUE(mixxx::AnalyzerBeatThis::parseResults(
            resultJson(), kHash, kRate, 2 * 44100, &beats, &downbeats));
    EXPECT_EQ(4, beats.size());
    ASSERT_EQ(1, downbeats.size());
    EXPECT_DOUBLE_EQ(26460, downbeats.front().value());
}

TEST(BeatThisProtocolTest,
        RejectsMalformedOrMismatchedResultsWithoutPartialCommit) {
    using mixxx::audio::FramePos;
    QVector<FramePos> beats{FramePos(42)};
    QVector<FramePos> downbeats{FramePos(42)};
    const auto original = beats;
    for (const auto& payload : {QByteArray("not json"),
                 resultJson({0.6, 0.1}),
                 resultJson({0.1, 0.1}),
                 resultJson({0.1, 0.100001}),
                 resultJson({0.1, 2.5}),
                 resultJson({0.1, "0.6"}),
                 resultJson({0.1, 0.6}, {0.3}),
                 resultJson({-0.1, 0.6})}) {
        EXPECT_FALSE(mixxx::AnalyzerBeatThis::parseResults(
                payload, kHash, kRate, 2 * 44100, &beats, &downbeats));
        EXPECT_EQ(original, beats);
        EXPECT_EQ(original, downbeats);
    }
    EXPECT_FALSE(mixxx::AnalyzerBeatThis::parseResults(resultJson(),
            QString(64, 'b'),
            kRate,
            2 * 44100,
            &beats,
            &downbeats));
}

TEST(DownbeatStorageTest, ConstantAndVariableGridsRoundTrip) {
    using mixxx::audio::FramePos;
    QVector<FramePos> positions;
    for (int i = 0; i < 24; ++i) {
        positions.push_back(
                FramePos(4410 + i * 22050 + (i > 10 ? (i - 10) * 300 : 0)));
    }
    const auto constant = mixxx::Beats::fromConstTempo(
            kRate, FramePos(4410), mixxx::Bpm(120));
    const auto fractional = mixxx::Beats::fromConstTempo(
            kRate, FramePos(4410), mixxx::Bpm(174));
    const auto variable = mixxx::Beats::fromBeatPositions(kRate, positions);
    for (const auto& source : {constant, fractional, variable}) {
        auto first = source->iteratorFrom(source->firstBeat());
        const QVector<FramePos> downbeats{*(first + 1), *(first + 5)};
        auto annotated = source->withDownbeats(downbeats);
        ASSERT_EQ(2, annotated->getDownbeats().size());
        EXPECT_TRUE(annotated->isDownbeat(downbeats[0]));
        EXPECT_FALSE(annotated->isDownbeat(positions[3]));
        auto restored = annotated;
        for (int cycle = 0; cycle < 10; ++cycle) {
            restored = mixxx::Beats::fromByteArray(kRate,
                    restored->getVersion(),
                    restored->getSubVersion(),
                    restored->toByteArray());
            ASSERT_TRUE(restored);
            EXPECT_EQ(annotated->getDownbeats(), restored->getDownbeats());
            EXPECT_EQ(*annotated, *restored);
        }
    }
}

TEST(DownbeatStorageTest, GridShiftPreservesDownbeatsAndTempoEditsClearThem) {
    using mixxx::audio::FramePos;
    auto source = mixxx::Beats::fromConstTempo(
            kRate, FramePos(4410), mixxx::Bpm(120));
    source = source->withDownbeats({FramePos(4410), FramePos(92610)});
    const auto translated = source->tryTranslate(1234);
    ASSERT_TRUE(translated);
    EXPECT_EQ(FramePos(5644), translated.value()->getDownbeats().front());
    const auto shifted = source->tryTranslateBeats(0.5);
    ASSERT_TRUE(shifted);
    EXPECT_EQ(FramePos(15435), shifted.value()->getDownbeats().front());
    const auto scaled = source->tryScale(mixxx::Beats::BpmScale::Double);
    ASSERT_TRUE(scaled);
    EXPECT_TRUE(scaled.value()->getDownbeats().isEmpty());
    EXPECT_TRUE(source->trySetBpm(mixxx::Bpm(121))
                    .value()
                    ->getDownbeats()
                    .isEmpty());
    const auto legacy = mixxx::Beats::fromConstTempo(
            kRate, FramePos(4410), mixxx::Bpm(120));
    EXPECT_TRUE(mixxx::Beats::fromByteArray(kRate,
            legacy->getVersion(),
            legacy->getSubVersion(),
            legacy->toByteArray())
                    ->getDownbeats()
                    .isEmpty());
}

class BeatThisAnalyzerTest : public MixxxTest {};

TEST(BeatThisVariableGridTest, AcceleratingGridRetainsEveryModelPosition) {
    using mixxx::audio::FramePos;
    QVector<FramePos> positions;
    double time = 0.2;
    for (int beat = 0; beat < 96; ++beat) {
        positions.push_back(FramePos(std::round(time * 50) * 882));
        time += 60.0 / (100 + beat * 0.9);
    }
    const auto grid = BeatFactory::makePreferredBeats(
            positions, {{"vamp_plugin_id", "beat-this:1"}}, false, kRate, true);
    ASSERT_TRUE(grid);
    EXPECT_FALSE(grid->hasConstantTempo());
    const auto annotated = grid->withDownbeats(
            {positions[0], positions[4], positions[40], positions[92]});
    const auto restored = mixxx::Beats::fromByteArray(kRate,
            annotated->getVersion(),
            annotated->getSubVersion(),
            annotated->toByteArray());
    ASSERT_TRUE(restored);
    for (const auto& position : positions) {
        EXPECT_NEAR(position.value(),
                restored->findClosestBeat(position).value(),
                1.0);
    }
    EXPECT_EQ(annotated->getDownbeats(), restored->getDownbeats());
    const auto firstBpm = restored->getBpmAroundPosition(positions[8], 4);
    const auto lastBpm = restored->getBpmAroundPosition(positions[80], 4);
    EXPECT_GT(lastBpm.value() - firstBpm.value(), 40);
}

TEST(BeatThisVariableGridTest, ShortPredictionDoesNotRequireConstantRegions) {
    using mixxx::audio::FramePos;
    const QVector<FramePos> positions{
            FramePos(4410), FramePos(26460), FramePos(48510)};
    const auto grid =
            BeatFactory::makePreferredBeats(positions, {}, false, kRate, true);
    ASSERT_TRUE(grid);
    EXPECT_NEAR(120,
            grid->getBpmInRange(positions.front(), positions.back()).value(),
            0.01);
    EXPECT_FALSE(BeatFactory::makePreferredBeats({}, {}, false, kRate, true));
}

TEST(BeatThisVariableGridTest, QuantizedPredictionsKeepAccuratePredominantBpm) {
    for (const int sampleRate : {44100, 48000}) {
        for (const double bpm : {133.15, 138.0}) {
            SCOPED_TRACE(testing::Message()
                    << sampleRate << " Hz, " << bpm << " BPM");
            QVector<mixxx::audio::FramePos> positions;
            for (int i = 0; i < 512; ++i) {
                const double time = std::round((0.2 + i * 60 / bpm) * 50) / 50;
                positions.push_back(
                        mixxx::audio::FramePos(std::round(time * sampleRate)));
            }
            const auto grid = BeatFactory::makePreferredBeats(positions,
                    {},
                    false,
                    mixxx::audio::SampleRate(sampleRate),
                    true);
            ASSERT_TRUE(grid);
            EXPECT_NEAR(bpm,
                    grid->getBpmInRange(positions.front(), positions.back())
                            .value(),
                    0.25);
        }
    }
}

TEST(BeatThisRealTrackTest,
        UploadedTracksPreserveModelPositionsAndReportGridModes) {
    const auto directory =
            qEnvironmentVariable("MIXXX_TEST_BEAT_THIS_TRACK_DIRECTORY");
    if (directory.isEmpty()) {
        GTEST_SKIP() << "Set MIXXX_TEST_BEAT_THIS_TRACK_DIRECTORY for "
                        "uploaded-track regression";
    }
    for (const auto& name : {QStringLiteral("hmc"), QStringLiteral("gnosis")}) {
        SCOPED_TRACE(name.toStdString());
        QFile audio(QDir(directory).filePath(name + ".f32"));
        QFile predictions(QDir(directory).filePath(name + "-baseline.json"));
        ASSERT_TRUE(audio.open(QIODevice::ReadOnly));
        ASSERT_TRUE(predictions.open(QIODevice::ReadOnly));
        QVector<mixxx::audio::FramePos> positions;
        QVector<mixxx::audio::FramePos> downbeats;
        const auto frames = audio.size() / static_cast<qint64>(sizeof(float));
        ASSERT_TRUE(mixxx::AnalyzerBeatThis::parseResults(predictions.readAll(),
                "8c328b45f59d8dd3dff219253ff6a8d6482be57d0133a29140e2febbf8eb83"
                "31",
                kRate,
                frames,
                &positions,
                &downbeats));
        const auto variable = BeatFactory::makePreferredBeats(
                positions, {}, mixxx::BeatGridMode::Variable, kRate, true);
        ASSERT_TRUE(variable);
        const auto annotated = variable->withDownbeats(downbeats);
        const auto restored = mixxx::Beats::fromByteArray(kRate,
                annotated->getVersion(),
                annotated->getSubVersion(),
                annotated->toByteArray());
        ASSERT_TRUE(restored);
        EXPECT_EQ(downbeats, restored->getDownbeats());
        for (const auto position : positions) {
            EXPECT_NEAR(position.value(),
                    restored->findClosestBeat(position).value(),
                    1);
        }
        const auto automatic = BeatFactory::makePreferredBeats(
                positions, {}, mixxx::BeatGridMode::Auto, kRate, true);
        const auto constant = BeatFactory::makePreferredBeats(
                positions, {}, mixxx::BeatGridMode::Constant, kRate, true);
        const auto smoothed =
                BeatFactory::makePreferredBeats(positions, {}, false, kRate);
        ASSERT_TRUE(automatic);
        ASSERT_TRUE(constant);
        ASSERT_TRUE(smoothed);
        QJsonObject report{{"model_beats", positions.size()},
                {"downbeats", downbeats.size()},
                {"auto_mode",
                        automatic->hasConstantTempo() ? "constant"
                                                      : "variable"},
                {"constant_bpm",
                        constant->getBpmAroundPosition(
                                        positions[positions.size() / 2], 4)
                                .value()}};
        report["variable_bpm"] =
                restored->getBpmInRange(positions.front(), positions.back())
                        .value();
        std::vector<double> oldErrors;
        for (const auto position : positions) {
            oldErrors.push_back(
                    std::abs(smoothed->findClosestBeat(position) - position) /
                    44100.0);
        }
        std::sort(oldErrors.begin(), oldErrors.end());
        report["old_variable_max_error_ms"] = oldErrors.back() * 1000;
        report["old_variable_median_error_ms"] =
                oldErrors[oldErrors.size() / 2] * 1000;

        mixxx::AnalyzerQueenMaryBeats original(false);
        mixxx::AnalyzerQueenMaryBeats refined(true);
        ASSERT_TRUE(original.initialize(kRate));
        ASSERT_TRUE(refined.initialize(kRate));
        std::vector<CSAMPLE> stereo(8192);
        std::vector<float> mono(4096);
        while (!audio.atEnd()) {
            const auto bytes = audio.read(
                    reinterpret_cast<char*>(mono.data()), 4096 * sizeof(float));
            ASSERT_GT(bytes, 0);
            ASSERT_EQ(0, bytes % sizeof(float));
            const int count = static_cast<int>(bytes / sizeof(float));
            for (int i = 0; i < count; ++i) {
                stereo[2 * i] = stereo[2 * i + 1] = mono[i];
            }
            ASSERT_TRUE(original.processSamples(stereo.data(), count * 2));
            ASSERT_TRUE(refined.processSamples(stereo.data(), count * 2));
        }
        ASSERT_TRUE(original.finalize());
        ASSERT_TRUE(refined.finalize());
        for (const auto& entry : {std::make_pair(QStringLiteral("qm_original"),
                                          original.getBeats()),
                     std::make_pair(QStringLiteral("qm_refined"),
                             refined.getBeats())}) {
            const auto grid = BeatFactory::makePreferredBeats(
                    entry.second, {}, true, kRate);
            ASSERT_TRUE(grid);
            std::vector<double> errors;
            for (const auto beat : positions) {
                if (beat.value() > 30 * 44100 &&
                        beat.value() < (frames - 30 * 44100)) {
                    errors.push_back(
                            std::abs(grid->findClosestBeat(beat) - beat) /
                            44100.0);
                }
            }
            std::sort(errors.begin(), errors.end());
            report[entry.first + "_median_distance_to_model_ms"] =
                    errors[errors.size() / 2] * 1000;
            report[entry.first + "_bpm"] =
                    grid->getBpmAroundPosition(
                                positions[positions.size() / 2], 4)
                            .value();
        }
        QFile output(QDir(directory).filePath(name + "-grid-report.json"));
        ASSERT_TRUE(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(report).toJson());
        qInfo().noquote() << name
                          << QJsonDocument(report).toJson(
                                     QJsonDocument::Compact);
    }
}

TEST(BeatThisGridModeTest,
        AutoKeepsStableGridDespiteModelJitterAndIsolatedOutliers) {
    QVector<mixxx::audio::FramePos> positions;
    for (int i = 0; i < 256; ++i) {
        const double jitter = i % 73 == 24 ? 0.12 : (i % 3 - 1) * 0.01;
        positions.push_back(
                mixxx::audio::FramePos((0.2 + i * 0.5 + jitter) * 44100));
    }
    const auto grid = BeatFactory::makePreferredBeats(
            positions, {}, mixxx::BeatGridMode::Auto, kRate, true);
    ASSERT_TRUE(grid);
    EXPECT_TRUE(grid->hasConstantTempo());
    EXPECT_NEAR(120, grid->getBpmAroundPosition(positions[80], 4).value(), 0.1);
}

TEST(BeatThisGridModeTest, AutoRetainsSustainedTempoChangesAndManualOverride) {
    QVector<mixxx::audio::FramePos> positions;
    double time = 0.2;
    for (int i = 0; i < 160; ++i) {
        positions.push_back(
                mixxx::audio::FramePos(std::round(time * 50) * 882));
        time += 60.0 / (i < 80 ? 120 : 128);
    }
    const auto automatic = BeatFactory::makePreferredBeats(
            positions, {}, mixxx::BeatGridMode::Auto, kRate, true);
    const auto forced = BeatFactory::makePreferredBeats(
            positions, {}, mixxx::BeatGridMode::Constant, kRate, true);
    ASSERT_TRUE(automatic);
    ASSERT_TRUE(forced);
    EXPECT_FALSE(automatic->hasConstantTempo());
    EXPECT_TRUE(forced->hasConstantTempo());
    for (const auto position : positions) {
        EXPECT_NEAR(position.value(),
                automatic->findClosestBeat(position).value(),
                1);
    }
}

class BeatThisPreferencesTest : public MixxxTest {};

TEST_F(BeatThisPreferencesTest,
        LegacyTempoSettingsMigrateAndAllModesSurviveRestart) {
    using Mode = mixxx::BeatGridMode;
    BeatDetectionSettings settings(config());
    EXPECT_EQ(Mode::Auto, settings.getTempoMode());
    settings.setFixedTempoAssumption(true);
    EXPECT_EQ(Mode::Constant, settings.getTempoMode());
    settings.setFixedTempoAssumption(false);
    EXPECT_EQ(Mode::Variable, settings.getTempoMode());
    settings.setBeatPluginId("beat-this:1");
    for (const auto mode : {Mode::Auto, Mode::Constant, Mode::Variable}) {
        settings.setTempoMode(mode);
        DlgPrefBeats dialog(nullptr, config());
        EXPECT_EQ(static_cast<int>(mode),
                dialog.comboBoxTempoMode->currentData().toInt());
        EXPECT_EQ(mode == Mode::Constant,
                dialog.checkBoxFastAnalysis->isEnabled());
        dialog.slotApply();
        const auto reloaded = UserSettingsPointer(
                new UserSettings(getTestDataDir().filePath("test.cfg")));
        BeatDetectionSettings restored(reloaded);
        EXPECT_EQ(mode, restored.getTempoMode());
        EXPECT_EQ("beat-this:1", restored.getBeatPluginId());
    }
}

TEST_F(BeatThisPreferencesTest,
        AppliedSelectionPersistsBeforeApplicationShutdown) {
    {
        DlgPrefBeats dialog(nullptr, config());
        dialog.comboBoxBeatPlugin->setCurrentIndex(
                dialog.comboBoxBeatPlugin->findData("beat-this:1"));
        dialog.lineEditBeatThisPython->setText("/path/to/python");
        dialog.lineEditBeatThisModel->setText("/path/to/final0.ckpt");
        dialog.comboBoxTempoMode->setCurrentIndex(
                dialog.comboBoxTempoMode->findData(
                        static_cast<int>(mixxx::BeatGridMode::Variable)));
        dialog.slotApply();
    }
    const auto reloaded = UserSettingsPointer(
            new UserSettings(getTestDataDir().filePath("test.cfg")));
    BeatDetectionSettings settings(reloaded);
    EXPECT_EQ("beat-this:1", settings.getBeatPluginId());
    EXPECT_EQ("/path/to/python", settings.getBeatThisPython());
    EXPECT_EQ("/path/to/final0.ckpt", settings.getBeatThisModel());
    EXPECT_FALSE(settings.getFixedTempoAssumption());
    DlgPrefBeats reopened(nullptr, reloaded);
    EXPECT_EQ("beat-this:1",
            reopened.comboBoxBeatPlugin->currentData().toString());
    reopened.slotApply();
    EXPECT_EQ("beat-this:1", settings.getBeatPluginId());
}

TEST_F(BeatThisPreferencesTest, RefreshDoesNotEmitSelectionChanges) {
    DlgPrefBeats dialog(nullptr, config());
    int selectionChanges = 0;
    QObject::connect(dialog.comboBoxBeatPlugin,
            &QComboBox::currentIndexChanged,
            [&selectionChanges](int) { ++selectionChanges; });
    BeatDetectionSettings settings(config());
    settings.setBeatPluginId("beat-this:1");
    settings.setFixedTempoAssumption(false);
    dialog.slotUpdate();
    EXPECT_EQ(0, selectionChanges);
    EXPECT_EQ(
            "beat-this:1", dialog.comboBoxBeatPlugin->currentData().toString());
    dialog.slotApply();
    EXPECT_EQ("beat-this:1", settings.getBeatPluginId());
}

TEST_F(BeatThisPreferencesTest,
        DisabledAnalysisDisplaysTheSavedProviderAndMode) {
    BeatDetectionSettings settings(config());
    settings.setBeatPluginId("beat-this:1");
    settings.setTempoMode(mixxx::BeatGridMode::Variable);
    settings.setBpmDetectionEnabled(false);
    DlgPrefBeats dialog(nullptr, config());
    EXPECT_FALSE(dialog.comboBoxBeatPlugin->isEnabled());
    EXPECT_EQ(
            "beat-this:1", dialog.comboBoxBeatPlugin->currentData().toString());
    EXPECT_EQ(static_cast<int>(mixxx::BeatGridMode::Variable),
            dialog.comboBoxTempoMode->currentData().toInt());
    dialog.slotApply();
    const auto reloaded = UserSettingsPointer(
            new UserSettings(getTestDataDir().filePath("test.cfg")));
    BeatDetectionSettings restored(reloaded);
    EXPECT_EQ("beat-this:1", restored.getBeatPluginId());
    EXPECT_EQ(mixxx::BeatGridMode::Variable, restored.getTempoMode());
    EXPECT_FALSE(restored.getBpmDetectionEnabled());
}

TEST_F(BeatThisAnalyzerTest, InvalidCheckpointPreservesExistingGrid) {
    const auto python = QStandardPaths::findExecutable("python3");
    if (python.isEmpty()) {
        GTEST_SKIP() << "Python 3 is required to exercise process failure";
    }
    QTemporaryFile checkpoint;
    ASSERT_TRUE(checkpoint.open());
    ASSERT_EQ(11, checkpoint.write("not a model"));
    checkpoint.close();
    BeatDetectionSettings settings(config());
    settings.setBeatPluginId(mixxx::AnalyzerBeatThis::pluginInfo().id());
    settings.setBeatThisPython(python);
    settings.setBeatThisModel(checkpoint.fileName());
    settings.setBeatThisModelSha256(QString(64, '0'));
    settings.setReanalyzeWhenSettingsChange(true);
    auto track = Track::newTemporary();
    track->setAudioProperties(mixxx::audio::ChannelCount::stereo(),
            kRate,
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(1));
    const auto original = mixxx::Beats::fromConstTempo(
            kRate, mixxx::audio::FramePos(4410), mixxx::Bpm(120));
    ASSERT_TRUE(track->trySetBeats(original));
    AnalyzerBeats analyzer(config());
    ASSERT_TRUE(analyzer.initialize(AnalyzerTrack(track),
            kRate,
            mixxx::audio::ChannelCount::stereo(),
            44100));
    std::vector<CSAMPLE> samples(44100 * 2, 0.0f);
    ASSERT_TRUE(analyzer.processSamples(samples.data(), samples.size()));
    analyzer.storeResults(track);
    analyzer.cleanup();
    EXPECT_EQ(original, track->getBeats());
}

TEST_F(BeatThisAnalyzerTest,
        PretrainedModelCompletesAnalyzerAndPersistencePath) {
    const auto python = qEnvironmentVariable("MIXXX_TEST_BEAT_THIS_PYTHON");
    const auto model = qEnvironmentVariable("MIXXX_TEST_BEAT_THIS_MODEL");
    if (python.isEmpty() || model.isEmpty()) {
        GTEST_SKIP() << "Set MIXXX_TEST_BEAT_THIS_PYTHON and "
                        "MIXXX_TEST_BEAT_THIS_MODEL for real-model validation";
    }
    BeatDetectionSettings settings(config());
    settings.setBeatPluginId(mixxx::AnalyzerBeatThis::pluginInfo().id());
    settings.setBeatThisPython(python);
    settings.setBeatThisModel(model);
    const auto modelHash = settings.getBeatThisModelSha256();
    settings.setFixedTempoAssumption(false);
    auto track = Track::newTemporary();
    track->setAudioProperties(mixxx::audio::ChannelCount::stereo(),
            kRate,
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(12));
    AnalyzerBeats analyzer(config());
    ASSERT_TRUE(analyzer.initialize(AnalyzerTrack(track),
            kRate,
            mixxx::audio::ChannelCount::stereo(),
            12 * 44100));
    std::vector<CSAMPLE> samples(12 * 44100 * 2);
    for (int beat = 0; beat < 24; ++beat) {
        const int start = 4410 + beat * 22050;
        for (int frame = 0; frame < 1200 && start + frame < 12 * 44100;
                ++frame) {
            const double time = frame / 44100.;
            const auto pulse = static_cast<CSAMPLE>(
                    std::exp(-180 * time) * std::cos(2 * M_PI * 100 * time));
            samples[(start + frame) * 2] = pulse;
            samples[(start + frame) * 2 + 1] = pulse;
        }
    }
    for (size_t i = 0; i < samples.size(); i += 8192) {
        ASSERT_TRUE(analyzer.processSamples(samples.data() + i,
                static_cast<SINT>(std::min(size_t(8192), samples.size() - i))));
    }
    settings.setBeatThisModelSha256(QString(64, '0'));
    QVector<AnalyzerProgress> progress;
    analyzer.storeResultsWithProgress(track,
            [&progress](AnalyzerProgress value) { progress.push_back(value); });
    ASSERT_GE(progress.size(), 5);
    EXPECT_TRUE(std::is_sorted(progress.begin(), progress.end()));
    EXPECT_GE(progress.front(), 0.0);
    EXPECT_EQ(1.0, progress.back());
    analyzer.cleanup();
    const auto beats = track->getBeats();
    ASSERT_TRUE(beats);
    EXPECT_NEAR(120, track->getBpm(), 3);
    EXPECT_FALSE(beats->getDownbeats().isEmpty());
    EXPECT_TRUE(beats->getSubVersion().contains("model_sha256=" + modelHash));
    const auto restored = mixxx::Beats::fromByteArray(kRate,
            beats->getVersion(),
            beats->getSubVersion(),
            beats->toByteArray());
    ASSERT_TRUE(restored);
    EXPECT_EQ(beats->getDownbeats(), restored->getDownbeats());
    settings.setBeatThisModelSha256(modelHash);
    settings.setReanalyzeWhenSettingsChange(true);
    EXPECT_FALSE(analyzer.initialize(AnalyzerTrack(track),
            kRate,
            mixxx::audio::ChannelCount::stereo(),
            12 * 44100));
    AnalyzerTrack::Options options;
    options.tempoMode = mixxx::BeatGridMode::Auto;
    ASSERT_TRUE(analyzer.initialize(AnalyzerTrack(track, options),
            kRate,
            mixxx::audio::ChannelCount::stereo(),
            12 * 44100));
    analyzer.cleanup();
    EXPECT_EQ(mixxx::BeatGridMode::Variable, settings.getTempoMode());
}

TEST_F(BeatThisAnalyzerTest,
        PretrainedModelFollowsTempoChangeWithFastAnalysisDisabled) {
    const auto python = qEnvironmentVariable("MIXXX_TEST_BEAT_THIS_PYTHON");
    const auto model = qEnvironmentVariable("MIXXX_TEST_BEAT_THIS_MODEL");
    if (python.isEmpty() || model.isEmpty()) {
        GTEST_SKIP() << "Set the Beat This! runtime and model paths";
    }
    BeatDetectionSettings settings(config());
    settings.setBeatPluginId("beat-this:1");
    settings.setBeatThisPython(python);
    settings.setBeatThisModel(model);
    settings.setTempoMode(mixxx::BeatGridMode::Variable);
    settings.setFastAnalysis(true);
    constexpr int seconds = 76;
    std::vector<CSAMPLE> samples(seconds * 44100 * 2);
    double time = 0.1;
    int beat = 0;
    while (time < seconds) {
        const int start = static_cast<int>(std::round(time * 44100));
        for (int frame = 0; frame < 1600 && start + frame < seconds * 44100;
                ++frame) {
            const double t = frame / 44100.0;
            const auto pulse =
                    static_cast<CSAMPLE>((beat % 4 == 0 ? 0.9 : 0.6) *
                            std::exp(-150 * t) * std::cos(2 * M_PI * 100 * t));
            samples[(start + frame) * 2] = samples[(start + frame) * 2 + 1] =
                    pulse;
        }
        time += 60.0 / (time < 38 ? 120 : 144);
        ++beat;
    }
    auto track = Track::newTemporary();
    track->setAudioProperties(mixxx::audio::ChannelCount::stereo(),
            kRate,
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(seconds));
    AnalyzerBeats analyzer(config());
    ASSERT_TRUE(analyzer.initialize(AnalyzerTrack(track),
            kRate,
            mixxx::audio::ChannelCount::stereo(),
            seconds * 44100));
    for (size_t i = 0; i < samples.size(); i += 8192) {
        ASSERT_TRUE(analyzer.processSamples(samples.data() + i,
                static_cast<SINT>(std::min(size_t(8192), samples.size() - i))));
    }
    analyzer.storeResults(track);
    analyzer.cleanup();
    const auto grid = track->getBeats();
    ASSERT_TRUE(grid);
    EXPECT_FALSE(grid->hasConstantTempo());
    EXPECT_FALSE(grid->getSubVersion().contains("fast_analysis"));
    EXPECT_NEAR(120,
            grid->getBpmAroundPosition(mixxx::audio::FramePos(10 * 44100), 8)
                    .value(),
            4);
    EXPECT_NEAR(144,
            grid->getBpmAroundPosition(mixxx::audio::FramePos(68 * 44100), 8)
                    .value(),
            4);
}

} // namespace
