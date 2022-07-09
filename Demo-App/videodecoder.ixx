module;
#include <filesystem>

export module video.decoder;
import generator;
import video;

namespace videodecoder {
export std::generator<video::Frame> makeFrames(std::filesystem::path);
}
