module;
#include <bit>
#include <chrono>
#include <coroutine>
#include <filesystem>
#include <print>
#include <ranges>
#include <span>

#include "c_resource.hpp"

module video.decoder;

import the.whole.caboodle;
import libav; // precompiled module, taken from BMI cache

using namespace std;         // bad practice - only for presentation!
using namespace std::chrono; // bad practice - only for presentation!

using namespace std::chrono_literals;

namespace fs  = std::filesystem;
namespace rgs = std::ranges;
namespace vws = rgs::views;

// video frame generator
// wrap the libav (a.k.a. FFmpeg https://ffmpeg.org/) C API types and their
// assorted functions
namespace libav {
using Codec   = stdex::c_resource<AVCodecContext, avcodec_alloc_context3,
                                avcodec_free_context>;
using File    = stdex::c_resource<AVFormatContext, avformat_open_input,
                               avformat_close_input>;
using tFrame  = stdex::c_resource<AVFrame, av_frame_alloc, av_frame_free>;
using tPacket = stdex::c_resource<AVPacket, av_packet_alloc, av_packet_free>;

// frames and packets are reference-counted and always constructed non-empty
struct Frame : tFrame {
	[[nodiscard]] Frame()
	: tFrame(constructed){};
	[[nodiscard]] auto dropReference() {
		return Frame::guard<av_frame_unref>(*this);
	}
};
struct Packet : tPacket {
	[[nodiscard]] Packet()
	: tPacket(constructed){};
	[[nodiscard]] auto dropReference() {
		return Packet::guard<av_packet_unref>(*this);
	}
};
} // namespace libav

namespace videodecoder {

//------------------------------------------------------------------------------
// generate an endless stream of paths of the lastest contents of given
// Directory on each iteration step paths are empty if there are none available

struct EternalDirectoryIterator : rgs::view_base {
	using base              = fs::directory_iterator;
	using iterator_category = input_iterator_tag;
	using difference_type   = ptrdiff_t;
	using value_type        = fs::path;
	using pointer           = const value_type *;
	using reference         = const value_type &;

	struct Sentinel {};

	[[nodiscard]] EternalDirectoryIterator() noexcept = default;
	[[nodiscard]] explicit EternalDirectoryIterator(fs::path Dir) noexcept
	: Directory_{ move(Dir) }
	, Iter_{ restart(Directory_) }
	, None_{ Iter_ == End_ } {}

	[[nodiscard]] bool
	operator==(const EternalDirectoryIterator & rhs) const noexcept {
		return Iter_ == rhs.Iter_;
	}
	[[nodiscard]] bool operator==(Sentinel) const noexcept {
		return false;
	}

	[[nodiscard]] fs::path operator*() const noexcept {
		if (None_)
			return {};
		return Iter_->path();
	}

	[[maybe_unused]] EternalDirectoryIterator & operator++() {
		error_code Error;
		if (Iter_ == End_ || Iter_.increment(Error) == End_)
			Iter_ = restart(Directory_);
		None_ = Error || Iter_ == End_;
		return *this;
	}
	EternalDirectoryIterator & operator++(int);

	[[nodiscard]] friend inline EternalDirectoryIterator
	begin(EternalDirectoryIterator it) {
		return move(it);
	}
	[[nodiscard]] friend inline Sentinel end(EternalDirectoryIterator) {
		return {};
	}

private:
	static base restart(const fs::path & Directory) {
		error_code Error;
		return base{ Directory, fs::directory_options::skip_permission_denied,
			         Error };
	}

	fs::path Directory_;
	base Iter_;
	base End_;
	bool None_ = false;
};
} // namespace videodecoder

using videodecoder::EternalDirectoryIterator;

template <>
constexpr bool rgs::enable_borrowed_range<EternalDirectoryIterator> =
    rgs::enable_borrowed_range<EternalDirectoryIterator::base>;

static_assert(input_iterator<EternalDirectoryIterator>);
static_assert(rgs::range<EternalDirectoryIterator>);
static_assert(rgs::viewable_range<EternalDirectoryIterator>);
static_assert(rgs::borrowed_range<EternalDirectoryIterator>);

namespace videodecoder {
// the setup stages, used in a view-pipeline

static constexpr auto DetectStream  = -1;
static constexpr auto FirstStream   = 0;
static constexpr auto MainSubstream = 0;

libav::File tryOpenFile(const fs::path & Path) {
	libav::File File;
	if (!Path.empty() &&
	    File.replace(caboodle::utf8Path(Path).c_str(), nullptr, nullptr) >= 0) {
		const AVCodec * pCodec;
		if ((av_find_best_stream(File, AVMEDIA_TYPE_VIDEO, DetectStream, -1,
		                         &pCodec, 0) != FirstStream) ||
		    pCodec == nullptr || pCodec->id != AV_CODEC_ID_GIF)
			File = {};
	}
	return File;
}

tuple<libav::File, libav::Codec> tryOpenDecoder(libav::File File) {
	if (File.empty())
		return {};

	const AVCodec * pCodec;
	avformat_find_stream_info(File, nullptr);
	av_find_best_stream(File, AVMEDIA_TYPE_VIDEO, FirstStream, -1, &pCodec, 0);
	if (File->duration > 0) {
		if (libav::Codec Decoder(pCodec); Decoder) {
			avcodec_parameters_to_context(Decoder,
			                              File->streams[FirstStream]->codecpar);
			if (avcodec_open2(Decoder, pCodec, nullptr) >= 0)
				return make_tuple(move(File), move(Decoder));
		}
	}
	return {};
}

microseconds getTickDuration(const libav::File & File) {
	static_assert(is_same_v<microseconds::period, ratio<1, AV_TIME_BASE>>);
	return microseconds{ av_rescale_q(1, File->streams[FirstStream]->time_base,
		                              { 1, AV_TIME_BASE }) };
}

video::Frame makeVideoFrame(const libav::Frame & Frame, int FrameNumber,
                            microseconds Tick) {
	video::FrameHeader Header = { .Width_     = Frame->width,
		                          .Height_    = Frame->height,
		                          .LinePitch_ = Frame->linesize[MainSubstream],
		                          .Format_    = video::fromLibav(Frame->format),
		                          .Sequence_  = FrameNumber,
		                          .Timestamp_ = Tick * Frame->pts };
	return { Header,
		     { bit_cast<const std::byte *>(Frame->data[MainSubstream]),
		       Header.size() } };
}

generator<video::Frame> decodeFrames(libav::File File, libav::Codec Decoder) {
	libav::Packet Packet;
	libav::Frame Frame;
	const auto Tick = getTickDuration(File);

	while (av_read_frame(File, Packet) >= 0) {
		const auto PGuard = Packet.dropReference();
		if (Packet->stream_index != FirstStream)
			continue;
		for (auto rc = avcodec_send_packet(Decoder, Packet); rc >= 0;) {
			rc                = avcodec_receive_frame(Decoder, Frame);
			const auto FGuard = Frame.dropReference();
			if (rc >= 0)
				co_yield makeVideoFrame(Frame, Decoder->frame_number, Tick);
			else if (rc == AVERROR_EOF)
				co_return;
		}
	}
}

auto hasExtension(string_view Extension) {
	return [=](const fs::path & p) {
		return p.empty() || p.extension() == Extension;
	};
}

generator<video::Frame> makeFrames(fs::path Directory) {
	const auto EndlessStreamOfPaths = EternalDirectoryIterator(move(Directory));
	// clang-format off
	auto MisEnPlace = EndlessStreamOfPaths
		            | vws::filter(hasExtension(".gif"))
		            | vws::transform(tryOpenFile)
		            | vws::transform(tryOpenDecoder)
		            ;
	// clang-format on

	for (auto [File, Decoder] : MisEnPlace) {
		if (Decoder) {
			println("decoding <{}>", File->url);
			co_yield rgs::elements_of(
			    decodeFrames(std::move(File), std::move(Decoder)));
		} else {
			co_yield video::makeFiller(100ms);
		}
	}
}
} // namespace videodecoder
