module;
#include "caudio/ipc/command.hpp"

export module caudio.cli:command;

export namespace caudio::cli {
using ::caudio::cli::Command;
using ::caudio::cli::ConfigExport;
using ::caudio::cli::ConfigGet;
using ::caudio::cli::ConfigImport;
using ::caudio::cli::ConfigList;
using ::caudio::cli::ConfigReset;
using ::caudio::cli::ConfigSet;
using ::caudio::cli::DeviceList;
using ::caudio::cli::DeviceSet;
using ::caudio::cli::DeviceTest;
using ::caudio::cli::HistoryClear;
using ::caudio::cli::HistoryList;
using ::caudio::cli::Info;
using ::caudio::cli::LibraryAdd;
using ::caudio::cli::LibraryList;
using ::caudio::cli::LibraryRemove;
using ::caudio::cli::LibraryScan;
using ::caudio::cli::LibrarySearch;
using ::caudio::cli::LibraryStats;
using ::caudio::cli::LibraryStatsDetailed;
using ::caudio::cli::Next;
using ::caudio::cli::Pause;
using ::caudio::cli::Play;
using ::caudio::cli::PlaylistDelete;
using ::caudio::cli::PlaylistExport;
using ::caudio::cli::PlaylistImport;
using ::caudio::cli::PlaylistList;
using ::caudio::cli::PlaylistLoad;
using ::caudio::cli::PlaylistRename;
using ::caudio::cli::PlaylistSave;
using ::caudio::cli::PlaylistTracks;
using ::caudio::cli::Prev;
using ::caudio::cli::Preview;
using ::caudio::cli::QueueAdd;
using ::caudio::cli::QueueClear;
using ::caudio::cli::QueueList;
using ::caudio::cli::QueueMove;
using ::caudio::cli::QueueQueues;
using ::caudio::cli::QueueRemove;
using ::caudio::cli::QueueRepeat;
using ::caudio::cli::QueueShuffle;
using ::caudio::cli::QueueSwitch;
using ::caudio::cli::Restart;
using ::caudio::cli::Resume;
using ::caudio::cli::Seek;
using ::caudio::cli::Shutdown;
using ::caudio::cli::StatusReq;
using ::caudio::cli::Stop;
using ::caudio::cli::TagEdit;
using ::caudio::cli::TagGet;
using ::caudio::cli::VolumeSet;
} // namespace caudio::cli
