module video:decoder.pipeline;
import std;

import :frame;
import "c_resource.hpp";
import the.whole.caboodle;
import libav; // precompiled module, taken from BMI cache

namespace fs  = std::filesystem;
namespace rgs = std::ranges;
namespace vws = rgs::views;

// video frame generator
// wrap the libav (a.k.a. FFmpeg https://ffmpeg.org/) C API types and their
// assorted functions
namespace libav {
using Codec =
    stdex::c_resource<AVCodecContext, avcodec_alloc_context3, avcodec_free_context>;
using File =
    stdex::c_resource<AVFormatContext, avformat_open_input, avformat_close_input>;
using tFrame  = stdex::c_resource<AVFrame, av_frame_alloc, av_frame_free>;
using tPacket = stdex::c_resource<AVPacket, av_packet_alloc, av_packet_free>;

// frames and packets are reference-counted and always constructed non-empty
struct Frame : tFrame {
	[[nodiscard]] Frame()
	: tFrame(constructed){};
	[[nodiscard]] auto dropReference() { return Frame::guard<av_frame_unref>(*this); }
};
struct Packet : tPacket {
	[[nodiscard]] Packet()
	: tPacket(constructed){};
	[[nodiscard]] auto dropReference() { return Packet::guard<av_packet_unref>(*this); }
};
} // namespace libav

namespace video {

// generate an endless stream of paths of the lastest contents of given Directory on each
// iteration step.
// the returned paths are empty if there are no directory contents.

auto InfinitePathSource(fs::path Directory) -> std::generator<fs::path> {
	using fs::directory_options::skip_permission_denied;
	std::error_code Error;
	for (fs::directory_iterator atEnd{}, Iterator = atEnd; true;) {
		if (Iterator == atEnd or Iterator.increment(Error) == atEnd)
			Iterator = fs::directory_iterator{ Directory, skip_permission_denied, Error };
		co_yield Error or Iterator == atEnd ? fs::path{} : Iterator->path();
	}
}

static_assert(rgs::range<decltype(InfinitePathSource({}))>);
static_assert(rgs::viewable_range<decltype(InfinitePathSource({}))>);

static constexpr auto DetectStream  = -1;
static constexpr auto FirstStream   = 0;
static constexpr auto MainSubstream = 0;

constexpr bool successful(int Code) {
	return Code >= 0;
}

constexpr bool atEndOfFile(int Code) {
	return Code == AVERROR_EOF;
}

auto acceptOnlyGIF(libav::File File) -> libav::File {
	const AVCodec * pCodec;
	if (av_find_best_stream(File, AVMEDIA_TYPE_VIDEO, DetectStream, -1, &pCodec, 0) !=
	        FirstStream or
	    pCodec == nullptr or pCodec->id != AV_CODEC_ID_GIF)
		File = {};
	return File;
}

auto tryOpenAsGIF(fs::path Path) -> libav::File {
	const auto Filename = caboodle::utf8Path(std::move(Path));
	libav::File File;
	if (not Filename.empty() and
	    successful(File.emplace(Filename.c_str(), nullptr, nullptr))) {
		File = acceptOnlyGIF(std::move(File));
	}
	return File;
}

auto tryOpenVideoDecoder(libav::File File) -> std::tuple<libav::File, libav::Codec> {
	if (not have(File))
		return {};

	const AVCodec * pCodec;
	avformat_find_stream_info(File, nullptr);
	av_find_best_stream(File, AVMEDIA_TYPE_VIDEO, FirstStream, -1, &pCodec, 0);
	if (File->duration <= 0)
		return {}; // refuse still images

	libav::Codec Decoder(pCodec);
	if (have(Decoder)) {
		avcodec_parameters_to_context(Decoder, File->streams[FirstStream]->codecpar);
		if (successful(avcodec_open2(Decoder, pCodec, nullptr)))
			return { std::move(File), std::move(Decoder) };
	}
	return {};
}

using std::chrono::microseconds;

static constexpr bool isSameTimeUnit =
    std::is_same_v<microseconds::period, std::ratio<1, AV_TIME_BASE>>;
static_assert(isSameTimeUnit, "libav uses different time units");

auto getTickDuration(const libav::File & File) {
	return microseconds{ av_rescale_q(1, File->streams[FirstStream]->time_base,
		                              { 1, AV_TIME_BASE }) };
}

constexpr auto makeVideoFrame(const libav::Frame & Frame, int FrameNumber,
                              microseconds TickDuration) {
	FrameHeader Header = { .Width_     = Frame->width,
		                   .Height_    = Frame->height,
		                   .LinePitch_ = Frame->linesize[MainSubstream],
		                   .Format_    = std::to_underlying(fromLibav(Frame->format)),
		                   .Sequence_  = FrameNumber,
		                   .Timestamp_ = TickDuration * Frame->pts };

	tPixels Pixels = { std::bit_cast<const std::byte *>(Frame->data[MainSubstream]),
		               Header.SizePixels() };
	return video::Frame{ Header, Pixels };
}

auto decodeFrames(libav::File File, libav::Codec Decoder)
    -> std::generator<video::Frame> {
	const auto TickDuration = getTickDuration(File);
	libav::Packet Packet;
	libav::Frame Frame;

	int Result = 0;
	while (not atEndOfFile(Result) and successful(av_read_frame(File, Packet))) {
		const auto PacketReferenceGuard = Packet.dropReference();
		if (Packet->stream_index != FirstStream)
			continue;
		Result = avcodec_send_packet(Decoder, Packet);
		while (successful(Result)) {
			Result = avcodec_receive_frame(Decoder, Frame);
			if (successful(Result))
				co_yield makeVideoFrame(Frame, static_cast<int>(Decoder->frame_num),
				                        TickDuration);
		}
	}
}

// "borrowing" is safe due to 'consteval' 😊
consteval auto hasExtension(std::string_view Extension) {
	return [=](const fs::path & p) {
		return p.empty() or p.extension() == Extension;
	};
}

using namespace std::chrono_literals;

// clang-format off
auto makeFrames(fs::path Directory) -> std::generator<video::Frame> {
	auto PreprocessedMediaFiles = InfinitePathSource(std::move(Directory))
		                        | vws::filter(hasExtension(".gif"))
		                        | vws::transform(tryOpenAsGIF)
		                        | vws::transform(tryOpenVideoDecoder)
		                        ;
	for (auto [File, Decoder] : PreprocessedMediaFiles) {
		if (have(Decoder)) {
			std::println("decoding <{}>", File->url);
			co_yield rgs::elements_of(
			    decodeFrames(std::move(File), std::move(Decoder)));
		} else {
			co_yield video::makeFillerFrame(100ms);
		}
	}
}
} // namespace video
