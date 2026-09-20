module;
#include "cli/shared/result.hpp"

export module caudio.cli:result;

export namespace caudio::cli {
  using ::caudio::cli::Status;
  using ::caudio::cli::QueueTracks;
  using ::caudio::cli::VolumeInfo;
  using ::caudio::cli::LibraryStatsData;
  using ::caudio::cli::LibraryStatsDetailedData;
  using ::caudio::cli::Tracks;
  using ::caudio::cli::Playlists;
  using ::caudio::cli::PlaylistData;
  using ::caudio::cli::ConfigValue;
  using ::caudio::cli::ConfigValues;
  using ::caudio::cli::SingleTrack;
  using ::caudio::cli::TrackInfo;
  using ::caudio::cli::HistoryEntry;
  using ::caudio::cli::History;
  using ::caudio::cli::DeviceInfo;
  using ::caudio::cli::Devices;
  using ::caudio::cli::Empty;
  using ::caudio::cli::CliError;
  using ::caudio::cli::Result;
  using ::caudio::cli::ReplyExpected;
}
