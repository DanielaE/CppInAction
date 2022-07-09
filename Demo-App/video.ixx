module;
#include <chrono>
#include <span>
#include <type_traits>

export module video;
import libav; // precompiled module, taken from BMI cache

using namespace std;
using namespace std::chrono;

export
namespace video {
enum PixelFormat : unsigned char { invalid, RGBA, BGRA };

PixelFormat fromLibav(int Format) {
	switch (Format) {
		case AVPixelFormat::AV_PIX_FMT_RGBA: return RGBA;
		case AVPixelFormat::AV_PIX_FMT_BGRA: return BGRA;
		default: return invalid;
	}
}

struct FrameHeader {
	static constexpr auto Size = 16u;

	using µSeconds = duration<unsigned, micro>;

	int Width_ : 16;
	int Height_ : 16;
	int LinePitch_ : 16;
	int Format_ : 8;
	int Sequence_;
	µSeconds Timestamp_;

	[[nodiscard]] constexpr size_t size() const noexcept {
		return static_cast<size_t>(Height_) * LinePitch_;
	}
	[[nodiscard]] constexpr bool empty() const noexcept {
		return size() == 0;
	}
	[[nodiscard]] constexpr bool filler() const noexcept {
		return Sequence_ == 0 && Timestamp_.count() > 0;
	}
	[[nodiscard]] constexpr bool null() const noexcept {
		return Sequence_ == 0 && Timestamp_.count() == 0;
	}
};
static_assert(sizeof(FrameHeader) == FrameHeader::Size);
static_assert(is_trivial_v<FrameHeader>); // guarantee relocatability!

using tPixels = span<const std::byte>;

struct Frame {
	FrameHeader Header_;
	tPixels Pixels_;
};

video::Frame makeFiller(milliseconds Duration) {
	FrameHeader Header{ 0 };
	Header.Timestamp_ = Duration;
	return { Header, {} };
}

constexpr video::Frame noFrame{ 0 };
} // namespace video
