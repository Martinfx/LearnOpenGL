/* Implementation unit for the vendored miniaudio and stb_vorbis (3rdparty/, unmodified).
   miniaudio is used only to decode files (WAV, Ogg Vorbis, MP3, FLAC); playback is OpenAL. */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
