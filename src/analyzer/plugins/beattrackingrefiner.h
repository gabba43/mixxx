#pragma once

#include <QVector>
#include <vector>

#include "audio/frame.h"
#include "audio/types.h"

namespace mixxx {

QVector<audio::FramePos> refineBeatTrackingTempo(
        const QVector<audio::FramePos>& beats,
        const std::vector<double>& onsets,
        audio::SampleRate sampleRate,
        int stepSizeFrames);

} // namespace mixxx
