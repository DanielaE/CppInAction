export module video:frame;
import std;

import libav; // precompiled module, taken from BMI cache

export namespace video {
enum class PixelFormat : unsigned char { invalid, RGBA, BGRA };

constexpr PixelFormat fromLibav(int Format) {
	using enum PixelFormat;
	switch (Format) {
		case AVPixelFormat::AV_PIX_FMT_RGBA: return RGBA;
		case AVPixelFormat::AV_PIX_FMT_BGRA: return BGRA;
		default: return invalid;
	}
}

struct FrameHeader {
	static constexpr auto SizeBytes = 16u;

	using µSeconds = std::chrono::duration<unsigned, std::micro>;

	int Width_ : 16;
	int Height_ : 16;
	int LinePitch_ : 16;
	int Format_ : 8;
	int Sequence_;
	µSeconds Timestamp_;

	[[nodiscard]] constexpr size_t SizePixels() const noexcept {
		return static_cast<size_t>(Height_) * LinePitch_;
	}
	constexpr bool hasNoPixels() const noexcept { return SizePixels() == 0; }
	constexpr bool isFiller() const noexcept {
		return Sequence_ == 0 && Timestamp_.count() > 0;
	}
	constexpr bool isNoFrame() const noexcept {
		return Sequence_ == 0 && Timestamp_.count() == 0;
	}
	constexpr bool isFirstFrame() const noexcept { return Sequence_ <= 1; }
};
static_assert(sizeof(FrameHeader) == FrameHeader::SizeBytes);
static_assert(std::is_trivial_v<FrameHeader>,
              "Please keep me trivial"); // guarantee relocatability!

using tPixels = std::span<const std::byte>;

struct Frame {
	FrameHeader Header_;
	tPixels Pixels_;

	[[nodiscard]] constexpr std::size_t TotalSize() const noexcept {
		return FrameHeader::SizeBytes + Pixels_.size_bytes();
	}
};

constexpr video::Frame makeFillerFrame(std::chrono::milliseconds Duration) {
	FrameHeader Header{ 0 };
	Header.Timestamp_ = std::chrono::duration_cast<FrameHeader::µSeconds>(Duration);
	return { Header, {} };
}

constexpr inline video::Frame noFrame{ 0 };
} // namespace video
