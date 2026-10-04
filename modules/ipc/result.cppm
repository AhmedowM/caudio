module;
#include <caudio/ipc/result.hpp>

export module caudio.ipc:result;

export namespace caudio::ipc {
using ::caudio::ipc::CliError;
using ::caudio::ipc::ConfigValue;
using ::caudio::ipc::ConfigValues;
using ::caudio::ipc::DeviceInfo;
using ::caudio::ipc::Devices;
using ::caudio::ipc::Empty;
using ::caudio::ipc::History;
using ::caudio::ipc::HistoryEntry;
using ::caudio::ipc::LibraryStatsData;
using ::caudio::ipc::LibraryStatsDetailedData;
using ::caudio::ipc::PlaylistData;
using ::caudio::ipc::Playlists;
using ::caudio::ipc::QueueEntry;
using ::caudio::ipc::QueueTracks;
using ::caudio::ipc::Queues;
using ::caudio::ipc::ReplyExpected;
using ::caudio::ipc::Result;
using ::caudio::ipc::SingleTrack;
using ::caudio::ipc::Status;
using ::caudio::ipc::TrackInfo;
using ::caudio::ipc::Tracks;
using ::caudio::ipc::VolumeInfo;
} // namespace caudio::ipc
