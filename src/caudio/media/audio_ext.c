#include <caudio/media/audio_ext.h>

#include <ctype.h>
#include <string.h>

bool ca_media_has_audio_ext(const char* path) {
    if (!path) return false;

    const char* dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') return false;

    // Lowercase the extension in place (stack buffer)
    char ext[16];
    size_t i = 0;
    const char* p = dot + 1;
    while (*p && i + 1 < sizeof(ext)) {
        ext[i++] = (char)tolower((unsigned char)*p++);
    }
    ext[i] = '\0';

    return strcmp(ext, "mp3") == 0 || strcmp(ext, "flac") == 0 ||
           strcmp(ext, "ogg") == 0 || strcmp(ext, "opus") == 0 ||
           strcmp(ext, "oga") == 0 || strcmp(ext, "wav") == 0 ||
           strcmp(ext, "m4a") == 0 || strcmp(ext, "mp4") == 0 ||
           strcmp(ext, "wma") == 0 || strcmp(ext, "asf") == 0 ||
           strcmp(ext, "aiff") == 0 || strcmp(ext, "aif") == 0 ||
           strcmp(ext, "ape") == 0 || strcmp(ext, "mpc") == 0 ||
           strcmp(ext, "wv") == 0;
}
