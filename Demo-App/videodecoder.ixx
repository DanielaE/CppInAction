export module video:decoder;
import std;

import :frame;

namespace video {
export std::generator<video::Frame> makeFrames(std::filesystem::path);
}
