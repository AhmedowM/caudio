module;
#include "cli/cli.hpp"

export module caudio.cli;
export import :command;
export import :result;
export import :protocol;
export import :config;

export namespace caudio::cli {
  // command
  using ::caudio::cli::Play;
  using ::caudio::cli::Pause;
  using ::caudio::cli::Resume;
  using ::caudio::cli::Restart;
  using ::caudio::cli::Stop;
  using ::caudio::cli::Next;
  using ::caudio::cli::Prev;
  using ::caudio::cli::Seek;
  using ::caudio::cli::StatusReq;
  using ::caudio::cli::VolumeSet;
  using ::caudio::cli::Command;
  // result
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
  // protocol
  using ::caudio::cli::IpcRequest;
  using ::caudio::cli::IpcReply;
  using ::caudio::cli::toJson;
  using ::caudio::cli::commandFromJson;
  using ::caudio::cli::resultFromJson;
  using ::caudio::cli::serializeRequest;
  using ::caudio::cli::deserializeRequest;
  using ::caudio::cli::serializeReply;
  using ::caudio::cli::deserializeReply;
  using ::caudio::cli::frame;
  using ::caudio::cli::deframe;
  // config
  using ::caudio::cli::Config;
  using ::caudio::cli::loadConfig;
  using ::caudio::cli::saveConfig;
  using ::caudio::cli::socketPathFor;
  using ::caudio::cli::pidPathFor;
  using ::caudio::cli::lockPathFor;
}
