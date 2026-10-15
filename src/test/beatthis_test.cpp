#include <gtest/gtest.h>

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
#include "preferences/beatdetectionsettings.h"
#include "test/mixxxtest.h"
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
    analyzer.storeResults(track);
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
}

} // namespace
