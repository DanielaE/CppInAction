export module video:frame;
import std;

import libav;

export namespace video {
enum class PixelFormat : unsigned char { invalid, RGBA, BGRA, _largest = BGRA };

consteval auto FormatBits() {
	return std::bit_width(std::to_underlying(PixelFormat::_largest));
}

constexpr PixelFormat fromLibav(int Format) {
	using enum PixelFormat;
	switch (Format) {
		case AV_PIX_FMT_RGBA: return RGBA;
		case AV_PIX_FMT_BGRA: return BGRA;
		default: return invalid;
	}
}

namespace chrono = std::chrono;

struct FrameHeader {
	static constexpr auto SizeBytes = 12u;

	using µSeconds = chrono::duration<unsigned, std::micro>;

	int Width_     : 16;
	int Height_    : 16;
	int LinePitch_ : 16;
	int Format_ : FormatBits();
	int Sequence_ : 16 - FormatBits();
	µSeconds Timestamp_;

	[[nodiscard]] constexpr size_t SizePixels() const noexcept {
		return static_cast<size_t>(Height_) * LinePitch_;
	}
	constexpr bool hasNoPixels() const noexcept { return SizePixels() == 0; }
	constexpr bool isFiller() const noexcept {
		return Sequence_ == 0 and Timestamp_.count() > 0;
	}
	constexpr bool isNoFrame() const noexcept {
		return Sequence_ == 0 and Timestamp_.count() <= 0;
	}
	constexpr bool isFirstFrame() const noexcept { return Sequence_ <= 1; }
};
static_assert(sizeof(FrameHeader) == FrameHeader::SizeBytes);
static_assert(std::is_trivial_v<FrameHeader>,
              "Please keep me trivial"); // guarantee relocatability!
static_assert(std::is_trivially_destructible_v<FrameHeader>,
              "Please keep me 'implicit lifetime'");

using tPixels = std::span<const std::byte>;

struct Frame {
	FrameHeader Header_;
	tPixels Pixels_;

	[[nodiscard]] constexpr std::size_t TotalSize() const noexcept {
		return FrameHeader::SizeBytes + Pixels_.size_bytes();
	}
};

constexpr inline video::Frame noFrame{ 0 };

constexpr video::Frame makeFillerFrame(chrono::milliseconds Duration) {
	auto Filler               = noFrame;
	Filler.Header_.Timestamp_ = chrono::duration_cast<FrameHeader::µSeconds>(Duration);
	return Filler;
}
} // namespace video
