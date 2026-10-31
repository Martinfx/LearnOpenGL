/* Implementation unit for the vendored miniaudio and stb_vorbis (3rdparty/, unmodified). */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MA_NO_GENERATION /* waveform/noise generators are not used */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
