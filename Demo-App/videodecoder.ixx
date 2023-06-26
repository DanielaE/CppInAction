module;
#ifdef __MINGW64__
#  include <cwchar> // work around ODR problems with the C standard library
#endif

export module video:decoder;
import std;

import :frame;

namespace video {
export std::generator<video::Frame> makeFrames(std::filesystem::path);
}
