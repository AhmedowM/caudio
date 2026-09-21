module;
#include "cli/shared/result.hpp"

export module caudio.cli:result;

export namespace caudio::cli {
using ::caudio::cli::CliError;
using ::caudio::cli::ConfigValue;
using ::caudio::cli::ConfigValues;
using ::caudio::cli::DeviceInfo;
using ::caudio::cli::Devices;
using ::caudio::cli::Empty;
using ::caudio::cli::History;
using ::caudio::cli::HistoryEntry;
using ::caudio::cli::LibraryStatsData;
using ::caudio::cli::LibraryStatsDetailedData;
using ::caudio::cli::PlaylistData;
using ::caudio::cli::Playlists;
using ::caudio::cli::QueueTracks;
using ::caudio::cli::ReplyExpected;
using ::caudio::cli::Result;
using ::caudio::cli::SingleTrack;
using ::caudio::cli::Status;
using ::caudio::cli::TrackInfo;
using ::caudio::cli::Tracks;
using ::caudio::cli::VolumeInfo;
} // namespace caudio::cli
