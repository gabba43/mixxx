#pragma once

#include <optional>

#include "track/beatgridmode.h"
#include "track/track_decl.h"

/// A scheduled not-null track with additional options for analysis.
class AnalyzerTrack {
  public:
    struct Options {
        std::optional<mixxx::BeatGridMode> tempoMode;
    };

    explicit AnalyzerTrack(TrackPointer track, Options options = Options());

    /// Fetches the (not-null) track to be analyzed.
    const TrackPointer& getTrack() const;

    /// Fetches the additional options.
    const Options& getOptions() const;

  private:
    /// The (not-null) track to be analyzed.
    TrackPointer m_track;
    /// The additional options.
    Options m_options;
};
