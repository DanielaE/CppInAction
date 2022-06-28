module;
#include <chrono>
#include <span>

export module video;
import libav;

export namespace video {
enum PixelFormat : unsigned char { invalid, RGBA, BGRA };

PixelFormat fromLibav(int Format) {
	switch (Format) {
		case AVPixelFormat::AV_PIX_FMT_RGBA: return RGBA;
		case AVPixelFormat::AV_PIX_FMT_BGRA: return BGRA;
		default: return invalid;
	}
}

#pragma pack(push, 1)
struct FrameHeader {
	static constexpr auto Size = 16;

	using MicroSeconds = std::chrono::duration<unsigned, std::micro>;

	int Width_ : 16;
	int Height_ : 16;
	int LinePitch_ : 16;
	int PixelSize_ : 8;
	int Format_ : 8;
	int Sequence_;
	MicroSeconds Timestamp_;

	[[nodiscard]] constexpr size_t size() const noexcept { return static_cast<size_t>(Height_) * LinePitch_; }
	[[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }
	[[nodiscard]] constexpr bool filler() const noexcept { return Sequence_ == 0 && Timestamp_.count() > 0; }
	[[nodiscard]] constexpr bool null() const noexcept { return Sequence_ == 0 && Timestamp_.count() == 0; }
};
static_assert(sizeof(FrameHeader) == FrameHeader::Size);
#pragma pack(pop)

struct Frame {
	FrameHeader Header_;
	std::span<const std::byte> Pixels_;
};

video::Frame makeFiller(std::chrono::milliseconds D) {
	FrameHeader Header{ 0 };
	Header.Timestamp_ = D;
	return { Header, {} };
}

} // namespace video
