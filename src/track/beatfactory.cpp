#include "track/beatfactory.h"

#include <QStringList>
#include <QtDebug>
#include <algorithm>
#include <cmath>

#include "track/beatutils.h"

namespace {

const QString kRoundingVersion = QStringLiteral("V4");

} // namespace

// static
QString BeatFactory::getPreferredVersion(bool fixedTempo) {
    if (fixedTempo) {
        return BEAT_GRID_2_VERSION;
    }
    return BEAT_MAP_VERSION;
}

QString BeatFactory::getPreferredSubVersion(
        const QHash<QString, QString>& extraVersionInfo) {
    const char* kSubVersionKeyValueSeparator = "=";
    const char* kSubVersionFragmentSeparator = "|";
    QStringList fragments;

    QHashIterator<QString, QString> it(extraVersionInfo);
    while (it.hasNext()) {
        it.next();
        if (it.key().contains(kSubVersionKeyValueSeparator) ||
                it.key().contains(kSubVersionFragmentSeparator) ||
                it.value().contains(kSubVersionKeyValueSeparator) ||
                it.value().contains(kSubVersionFragmentSeparator)) {
            qDebug() << "ERROR: Your analyzer key/value contains invalid "
                        "characters:"
                     << it.key() << ":" << it.value() << "Skipping.";
            continue;
        }
        fragments << QString("%1%2%3").arg(
                it.key(), kSubVersionKeyValueSeparator, it.value());
    }

    fragments << QString("rounding%1%2")
                         .arg(kSubVersionKeyValueSeparator, kRoundingVersion);

    std::sort(fragments.begin(), fragments.end());
    return (fragments.size() > 0) ? fragments.join(kSubVersionFragmentSeparator)
                                  : "";
}

mixxx::BeatsPointer BeatFactory::makePreferredBeats(
        const QVector<mixxx::audio::FramePos>& beats,
        const QHash<QString, QString>& extraVersionInfo,
        bool fixedTempo,
        mixxx::audio::SampleRate sampleRate,
        bool preserveBeatPositions) {
    DEBUG_ASSERT(sampleRate.isValid());
    if (preserveBeatPositions && !fixedTempo) {
        if (beats.size() < 2) {
            return nullptr;
        }
        return mixxx::Beats::fromBeatPositions(
                sampleRate, beats, getPreferredSubVersion(extraVersionInfo));
    }
#ifdef DEBUG_PRINT_BEATS
    for (mixxx::audio::FramePos beat : beats) {
        qDebug().noquote() << QString::number(beat.value(), 'g', 8);
    }
#endif

    QVector<BeatUtils::ConstRegion> constantRegions =
            BeatUtils::retrieveConstRegions(beats, sampleRate);
#ifdef DEBUG_PRINT_BEATS
    for (auto& region : constantRegions) {
        qDebug().noquote() << QString::number(region.firstBeat.value(), 'g', 8)
                           << QString::number(region.beatLength, 'g', 8);
    }
#endif
    if (constantRegions.isEmpty()) {
        return nullptr;
    }

    const QString version = getPreferredVersion(fixedTempo);
    const QString subVersion = getPreferredSubVersion(extraVersionInfo);

    if (version == BEAT_GRID_2_VERSION) {
        mixxx::audio::FramePos firstBeat = mixxx::audio::kStartFramePos;
        const mixxx::Bpm constBPM = BeatUtils::makeConstBpm(
                constantRegions, sampleRate, &firstBeat);
        if (firstBeat.isValid() && constBPM.isValid()) {
            firstBeat = BeatUtils::adjustPhase(firstBeat, constBPM, sampleRate, beats);
            auto pGrid = mixxx::Beats::fromConstTempo(
                    sampleRate, firstBeat.toNearestFrameBoundary(), constBPM, subVersion);
            return pGrid;
        } else {
            qWarning() << "Failed to create beat grid: Invalid first beat";
        }
    } else if (version == BEAT_MAP_VERSION) {
        QVector<mixxx::audio::FramePos> ironedBeats = BeatUtils::getBeats(constantRegions);
        auto pBeatMap = mixxx::Beats::fromBeatPositions(sampleRate, ironedBeats, subVersion);
        return pBeatMap;
    } else {
        qDebug() << "ERROR: Could not determine what type of beatgrid to create.";
    }
    return nullptr;
}

mixxx::BeatsPointer BeatFactory::makePreferredBeats(
        const QVector<mixxx::audio::FramePos>& beats,
        const QHash<QString, QString>& extraVersionInfo,
        mixxx::BeatGridMode mode,
        mixxx::audio::SampleRate sampleRate,
        bool preserveBeatPositions) {
    if (mode != mixxx::BeatGridMode::Auto) {
        return makePreferredBeats(beats,
                extraVersionInfo,
                mode == mixxx::BeatGridMode::Constant,
                sampleRate,
                preserveBeatPositions);
    }
    if (beats.size() >= 32) {
        const auto constant =
                makePreferredBeats(beats, extraVersionInfo, true, sampleRate);
        if (constant) {
            const double period = 60.0 * sampleRate /
                    constant->getBpmAroundPosition(beats.front(), 1).value();
            const double tolerance = std::min(0.04 * sampleRate, period * 0.1);
            int outliers = 0;
            int blockOutliers = 0;
            int consecutiveBadBlocks = 0;
            bool sustainedDeviation = false;
            for (int i = 0; i < beats.size(); ++i) {
                if (std::abs(constant->findClosestBeat(beats[i]) - beats[i]) >
                        tolerance) {
                    ++outliers;
                    ++blockOutliers;
                }
                if ((i + 1) % 16 == 0) {
                    consecutiveBadBlocks =
                            blockOutliers >= 4 ? consecutiveBadBlocks + 1 : 0;
                    sustainedDeviation |= consecutiveBadBlocks >= 2;
                    blockOutliers = 0;
                }
            }
            if (outliers <= 0.1 * beats.size() && !sustainedDeviation) {
                qDebug() << "Auto beat grid: constant;" << outliers
                         << "outliers of" << beats.size();
                return constant;
            }
        }
    }
    qDebug() << "Auto beat grid: variable;" << beats.size() << "detected beats";
    return makePreferredBeats(
            beats, extraVersionInfo, false, sampleRate, preserveBeatPositions);
}
