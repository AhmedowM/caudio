#include <caudio/types/wire.h>
#include <string.h>

const char* ca_repeat_mode_name(ca_repeat_mode mode) {
    switch (mode) {
    case CA_REPEAT_OFF:
        return "Off";
    case CA_REPEAT_ALL:
        return "All";
    case CA_REPEAT_ONE:
        return "One";
    default:
        return "Unknown";
    }
}

bool ca_repeat_mode_parse(const char* text, ca_repeat_mode* out) {
    if (!text || !out) {
        return false;
    }
    if (strcmp(text, "Off") == 0) {
        *out = CA_REPEAT_OFF;
        return true;
    }
    if (strcmp(text, "All") == 0) {
        *out = CA_REPEAT_ALL;
        return true;
    }
    if (strcmp(text, "One") == 0) {
        *out = CA_REPEAT_ONE;
        return true;
    }
    return false;
}

const char* ca_playback_state_name(ca_playback_state state) {
    switch (state) {
    case CA_PLAYBACK_STOPPED:
        return "Stopped";
    case CA_PLAYBACK_READY:
        return "Ready";
    case CA_PLAYBACK_PLAYING:
        return "Playing";
    case CA_PLAYBACK_PAUSED:
        return "Paused";
    default:
        return "Unknown";
    }
}

bool ca_playback_state_parse(const char* text, ca_playback_state* out) {
    if (!text || !out) {
        return false;
    }
    if (strcmp(text, "Stopped") == 0) {
        *out = CA_PLAYBACK_STOPPED;
        return true;
    }
    if (strcmp(text, "Ready") == 0) {
        *out = CA_PLAYBACK_READY;
        return true;
    }
    if (strcmp(text, "Playing") == 0) {
        *out = CA_PLAYBACK_PLAYING;
        return true;
    }
    if (strcmp(text, "Paused") == 0) {
        *out = CA_PLAYBACK_PAUSED;
        return true;
    }
    return false;
}

const char* ca_source_kind_name(ca_source_kind kind) {
    switch (kind) {
    case CA_SOURCE_PATH:
        return "path";
    case CA_SOURCE_EXTERNAL:
        return "external";
    case CA_SOURCE_IMPORTED:
        return "imported";
    case CA_SOURCE_UNAVAILABLE:
        return "unavailable";
    default:
        return "path";
    }
}

bool ca_source_kind_parse(const char* text, ca_source_kind* out) {
    if (!text || !out) {
        return false;
    }
    if (strcmp(text, "path") == 0) {
        *out = CA_SOURCE_PATH;
        return true;
    }
    if (strcmp(text, "external") == 0) {
        *out = CA_SOURCE_EXTERNAL;
        return true;
    }
    if (strcmp(text, "imported") == 0) {
        *out = CA_SOURCE_IMPORTED;
        return true;
    }
    if (strcmp(text, "unavailable") == 0) {
        *out = CA_SOURCE_UNAVAILABLE;
        return true;
    }
    return false;
}

const char* ca_engine_event_type_name(ca_engine_event_type type) {
    switch (type) {
    case CA_ENGINE_EVENT_NONE:
        return "None";
    case CA_ENGINE_EVENT_TRACK_STARTED:
        return "TrackStarted";
    case CA_ENGINE_EVENT_TRACK_ENDED:
        return "TrackEnded";
    case CA_ENGINE_EVENT_QUEUE_CHANGED:
        return "QueueChanged";
    case CA_ENGINE_EVENT_PROGRESS:
        return "Progress";
    case CA_ENGINE_EVENT_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

bool ca_engine_event_type_parse(const char* text, ca_engine_event_type* out) {
    if (!text || !out) {
        return false;
    }
    if (strcmp(text, "None") == 0) {
        *out = CA_ENGINE_EVENT_NONE;
        return true;
    }
    if (strcmp(text, "TrackStarted") == 0) {
        *out = CA_ENGINE_EVENT_TRACK_STARTED;
        return true;
    }
    if (strcmp(text, "TrackEnded") == 0) {
        *out = CA_ENGINE_EVENT_TRACK_ENDED;
        return true;
    }
    if (strcmp(text, "QueueChanged") == 0) {
        *out = CA_ENGINE_EVENT_QUEUE_CHANGED;
        return true;
    }
    if (strcmp(text, "Progress") == 0) {
        *out = CA_ENGINE_EVENT_PROGRESS;
        return true;
    }
    if (strcmp(text, "Error") == 0) {
        *out = CA_ENGINE_EVENT_ERROR;
        return true;
    }
    return false;
}
