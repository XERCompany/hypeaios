#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_COMMENTS
#define STB_VORBIS_MAX_CHANNELS 2
#define alloca(sz) __builtin_alloca(sz)
#include "stb_vorbis.h"
