// Vendored nothings/stb v2.30 decoder (public domain, see stb_image.h). Implementation TU.
// No stdio: the UI reads files through LinuxPlatformApi and decodes from memory buffers.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
