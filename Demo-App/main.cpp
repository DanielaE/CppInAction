#include <array>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <print>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <variant>
#include <vector>

#include "c_resource.hpp" // a generic RAII-wrapper for C APIs (constructor, destructor, reference-counting, etc...)
#include "generator.hpp" // reference implementation of generator proposal P2502R2

// import generator;
import boost.program_options; // precompiled module, taken from BMI cache
import asio;                  // precompiled module, taken from BMI cache
import libav;                 // precompiled module, taken from BMI cache
import sdl;                   // precompiled module, taken from BMI cache

using namespace std;         // bad practice - only for presentation!
using namespace std::chrono; // bad practice - only for presentation!

namespace fs  = std::filesystem;
namespace rgs = std::ranges;
namespace vws = rgs::views;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

#define export

//------------------------------------------------------------------------------

//= vvvv move to module 'caboodle'
//==========================================================

// generate an endless stream of paths of the lastest contents of given
// Directory on each iteration step paths are empty if there are none available
export struct EternalDirectoryIterator : rgs::view_base {
	using base              = fs::directory_iterator;
	using iterator_category = input_iterator_tag;
	using difference_type   = ptrdiff_t;
	using value_type        = fs::path;
	using pointer           = const value_type *;
	using reference         = const value_type &;

	struct Sentinel {};

	[[nodiscard]] EternalDirectoryIterator() noexcept = default;
	[[nodiscard]] explicit EternalDirectoryIterator(fs::path Dir) noexcept
	: Directory_{ std::move(Dir) }
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
	EternalDirectoryIterator & operator++(int); // satisfy weakly_incrementable

	friend EternalDirectoryIterator begin(EternalDirectoryIterator it) {
		return std::move(it);
	}
	friend Sentinel end(EternalDirectoryIterator) {
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

template <>
inline constexpr bool rgs::enable_borrowed_range<EternalDirectoryIterator> =
    true;

// models legacy input iterator, range, viewable range, and borrowed range
// concepts
static_assert(input_iterator<EternalDirectoryIterator>);
static_assert(rgs::range<EternalDirectoryIterator>);
static_assert(rgs::viewable_range<EternalDirectoryIterator>);
static_assert(rgs::borrowed_range<EternalDirectoryIterator>);

//------------------------------------------------------------------------------

// showcase of string::resize_and_overwrite(), both fundamental and performant
// showcase of dependency injection of required return type
// showcase of prototype pattern
// showcase of reuse of allocated storage
#ifdef _WIN32
namespace winapi {

extern "C" {
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned, unsigned long,
                                                        const wchar_t *, int,
                                                        char *, int,
                                                        const char *, int *);
}
static constexpr auto UTF8 = 65001;

export template <typename Str = string>
	requires(requires(Str s, size_t r, size_t (*f)(char *, size_t)) {
		{ s.resize_and_overwrite(r, f) };
	})
decltype(auto) toUTF8(wstring_view Utf16, Str && Utf8 = {}) noexcept {
	const auto Length   = static_cast<int>(Utf16.size());
	const auto Required = WideCharToMultiByte(UTF8, 0, Utf16.data(), Length,
	                                          nullptr, 0, nullptr, nullptr);
	Utf8.resize_and_overwrite(Required, [&](char * Buffer, size_t Size) {
		WideCharToMultiByte(UTF8, 0, Utf16.data(), Length, Buffer,
		                    static_cast<int>(Size), nullptr, nullptr);
		return Size;
	});
	return static_cast<Str &&>(Utf8);
}
} // namespace winapi
#endif

//------------------------------------------------------------------------------

// fs::path::string() has unspecified encoding on Windows
// convert from UTF16 to UTF8 with guaranteed semantics
export string u8Path(const fs::path & Path) {
	if constexpr (_WIN32)
		return winapi::toUTF8(Path.native());
	else
		return Path.native();
}

//= ^^^^ move to module 'caboodle'
//==========================================================

//------------------------------------------------------------------------------

//= vvvv move to module 'video'
//=============================================================

// define the decoded video frames to be delivered through the networking layer
export namespace video {
enum PixelFormat : unsigned char { invalid, RGBA, BGRA };

PixelFormat fromLibav(int Format) {
	switch (Format) {
		case AVPixelFormat::AV_PIX_FMT_RGBA: return RGBA;
		case AVPixelFormat::AV_PIX_FMT_BGRA: return BGRA;
		default: return invalid;
	}
}

struct FrameHeader {
	static constexpr auto Size = 16;

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
static_assert(is_trivial_v<FrameHeader>); // guarantee relocatability

struct Frame {
	FrameHeader Header_;
	span<const byte> Pixels_;
};

video::Frame makeFiller(milliseconds D) {
	FrameHeader Header{ 0 };
	Header.Timestamp_ = D;
	return { Header, {} };
}

} // namespace video

//= ^^^^ move to module 'video'
//=============================================================

// wrap the libav (a.k.a. FFmpeg https://ffmpeg.org/) C API types and their
// assorted functions
namespace libav {
using Codec   = stdex::c_resource<AVCodecContext, avcodec_alloc_context3,
                                avcodec_free_context>;
using File    = stdex::c_resource<AVFormatContext, avformat_open_input,
                               avformat_close_input>;
using tFrame  = stdex::c_resource<AVFrame, av_frame_alloc, av_frame_free>;
using tPacket = stdex::c_resource<AVPacket, av_packet_alloc, av_packet_free>;

// frames and packets are reference-counted
struct Frame : tFrame {
	[[nodiscard]] Frame()
	: tFrame(construct){};
	[[nodiscard]] auto dropReference() {
		return Frame::guard<av_frame_unref>(*this);
	}
};
struct Packet : tPacket {
	[[nodiscard]] Packet()
	: tPacket(construct){};
	[[nodiscard]] auto dropReference() {
		return Packet::guard<av_packet_unref>(*this);
	}
};
} // namespace libav

// the setup stages, used in a view-pipeline

static constexpr auto DetectStream  = -1;
static constexpr auto FirstStream   = 0;
static constexpr auto MainSubstream = 0;

libav::File tryOpenFile(const fs::path & Path) {
	libav::File File;
	if (!Path.empty() &&
	    File.replace(u8Path(Path).c_str(), nullptr, nullptr) >= 0) {
		const AVCodec * pCodec;
		if ((av_find_best_stream(File, AVMEDIA_TYPE_VIDEO, DetectStream, -1,
		                         &pCodec, 0) != FirstStream) ||
		    pCodec == nullptr || pCodec->id != AV_CODEC_ID_GIF)
			File.clear();
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
				return make_tuple(std::move(File), std::move(Decoder));
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
		     { bit_cast<const byte *>(Frame->data[MainSubstream]),
		       Header.size() } };
}

// generator, delivers a decoded video frame from a file/decoder pair on each
// call
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
		return p.empty() ||
		       p.extension() == Extension; // ToDo: make comparison robust
	};
}

// the top-level generator, delivers an endless stream of video frames
// - filler frames if there are no GIF files available
// - decoded video frames from the GIF files in the given directory

generator<video::Frame> makeFrameGenerator(fs::path Directory) {
	const auto EndlessStreamOfPaths =
	    EternalDirectoryIterator(std::move(Directory));
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

//------------------------------------------------------------------------------

//= vvvv move to module 'net' ?
//=============================================================

using ByteSpan      = span<byte>;
using ConstByteSpan = span<const byte>;

template <typename T>
inline asio::mutable_buffer buffer(span<T> S) noexcept {
	return { S.data(), S.size() };
}
template <typename T>
inline asio::const_buffer buffer(span<const T> S) noexcept {
	return { S.data(), S.size() };
}

template <typename T>
inline ConstByteSpan asBytes(const T & Object) noexcept {
	return as_bytes(span{ &Object, 1 });
}

template <size_t N>
using SendBuffers = array<asio::const_buffer, N>;

//------------------------------------------------------------------------------
namespace aop   = asio::experimental::awaitable_operators;
using await     = asio::experimental::as_tuple_t<asio::use_awaitable_t<>>;
using tSocket   = await::as_default_on_t<asio::ip::tcp::socket>;
using tAcceptor = await::as_default_on_t<asio::ip::tcp::acceptor>;
using tTimer    = await::as_default_on_t<asio::steady_timer>;

using tEndpoint  = asio::ip::tcp::endpoint;
using tEndpoints = span<const tEndpoint>;

// the network layer uses std::expected<T, error_code> as return types

template <typename T>
using tExpected = std::expected<T, error_code>;

// transform the variant return type from asio operator|| into an expected
// as simple as possible to scare away noone. No TMP required here!

template <typename R, typename... Ts>
constexpr tExpected<R> _get(tuple<error_code, Ts...> && tpl) {
	const auto & Error = get<error_code>(tpl);
	if constexpr (sizeof...(Ts) == 0)
		return std::unexpected{ Error };
	else if (Error)
		return std::unexpected{ Error };
	else
		return get<1>(std::move(tpl));
}

template <typename... Ts, typename... Us>
constexpr auto
get(variant<tuple<error_code, Ts...>, tuple<error_code, Us...>> && var) {
	using R = type_identity_t<Ts..., Us...>;
	return visit([](auto && tpl) { return _get<R>(std::move(tpl)); },
	             std::move(var));
}

//= ^^^^ move to module 'net' ?
//=============================================================

//------------------------------------------------------------------------------
// the lowest-level networking routines with support for cancellation and
// timeouts

constexpr bool operator==(const tExpected<size_t> & Actual,
                          size_t rhs) noexcept {
	return Actual && Actual.value() == rhs;
}

asio::awaitable<tExpected<size_t>> send(tSocket & Socket, tTimer & Timer,
                                        span<asio::const_buffer> Data) {
	using aop::operator||;
	co_return get(
	    co_await(asio::async_write(Socket, Data) || Timer.async_wait()));
}

// precondition: !Space.empty()
asio::awaitable<tExpected<size_t>> receive(tSocket & Socket, tTimer & Timer,
                                           ByteSpan Space) {
	using aop::operator||;
	auto Result = get(co_await(asio::async_read(Socket, buffer(Space)) ||
	                           Timer.async_wait()));
	if (Result == 0)
		Result = std::unexpected{ make_error_code(errc::no_message) };
	co_return Result;
}

// precondition: !Endpoints.empty()
asio::awaitable<tExpected<tSocket>>
connectTo(asio::io_context & Ctx, tTimer & Timer, tEndpoints Endpoints) {
	using aop::operator||;
	tSocket Socket(Ctx);
	const auto Result =
	    get(co_await(async_connect(Socket, Endpoints) || Timer.async_wait()));
	if (Result)
		co_return Socket;
	else
		co_return std::unexpected{ Result.error() };
}

void close(tSocket & Socket) {
	error_code Error;
	Socket.shutdown(tSocket::shutdown_both, Error);
	Socket.close(Error);
}

static constexpr auto Local      = "localhost"sv;
static constexpr auto ServerPort = uint16_t{ 34567 };

[[nodiscard]] vector<tEndpoint> resolveHostEndpoints(uint16_t Port,
                                                     string_view HostName,
                                                     milliseconds Timeout) {
	using resolver = asio::ip::tcp::resolver;
	asio::io_context Ctx;
	const bool isLocal = HostName.empty() || HostName == Local;
	resolver::flags Flags{};
	if (isLocal) {
		Flags    = resolver::passive;
		HostName = Local;
	}
	resolver Resolver(Ctx);
	vector<tEndpoint> Result;
	error_code ec;
	for (const auto & EP : Resolver.resolve(HostName, {}, Flags, ec)) {
		const auto & Address = EP.endpoint().address();
		if (!Address.is_unspecified())
			Result.emplace_back(Address, Port);
	}
	Ctx.run_for(Timeout);
	return Result;
}

//------------------------------------------------------------------------------

// the video frame server, nothing to see here
struct Server {
	Server(asio::io_context & Ctx, stop_source Stop)
	: Ctx_{ Ctx }
	, Stop_{ std::move(Stop) } {}

	[[maybe_unused]] error_code serve(tEndpoints, fs::path);

private:
	asio::io_context & Ctx_;
	stop_source Stop_;
};

struct tFrameTimebase {
	steady_clock::time_point StartTime = steady_clock::now();
	video::FrameHeader::µSeconds Timestamp{ 0 };
	int Sequence = INT_MAX;

	auto DueTime(const video::Frame & Frame) {
		const auto T =
		    StartTime + (Frame.Header_.Sequence_ != 0 ? Frame.Header_.Timestamp_
		                                              : Timestamp);
		if (Frame.Header_.Sequence_ == 0 ||
		    Frame.Header_.Sequence_ < Sequence) // start of frame sequence
			StartTime = steady_clock::now();
		Sequence  = Frame.Header_.Sequence_;
		Timestamp = Frame.Header_.Timestamp_;
		return T;
	}
};

// the connection object implemented as a coroutine on the heap
// will be brought down by internal events or from the outside using a
// stop_token

asio::awaitable<void> startStreaming(tSocket Socket, stop_token Stop,
                                     fs::path Source) {
	tTimer Timer(Socket.get_executor());
	tFrameTimebase Timebase;

	stop_callback killMe(Stop, [&] {
		close(Socket);
		Timer.cancel();
	});

	for (const auto & Frame : makeFrameGenerator(std::move(Source))) {
		Timer.expires_at(Timebase.DueTime(Frame));
		co_await Timer.async_wait();

		auto Buffers = SendBuffers<2>{ buffer(asBytes(Frame.Header_)),
			                           buffer(Frame.Pixels_) };
		Timer.expires_after(200ms);
		const tExpected<size_t> Result = co_await send(Socket, Timer, Buffers);
		if (!Result || Stop.stop_requested())
			break;
	}
}

//------------------------------------------------------------------------------

// the tcp acceptor is also a coroutine
// spawns new, independent coroutines on connect

asio::awaitable<void> acceptConnections(tAcceptor Acceptor, stop_token Stop,
                                        const fs::path Source) {
	stop_callback killMe(Stop, [&] { Acceptor.close(); });

	while (Acceptor.is_open()) {
		auto [Error, Socket] = co_await Acceptor.async_accept();
		if (!Stop.stop_requested() && !Error && Socket.is_open())
			co_spawn(Acceptor.get_executor(),
			         startStreaming(std::move(Socket), Stop, Source),
			         asio::detached);
	}
}

// start serving a list of given endpoints
// each endpoint is served by an independent coroutine
// precondition: !Endpoints.empty()
error_code Server::serve(tEndpoints Endpoints, const fs::path Source) {
	error_code Error;
	for (const auto & Endpoint : Endpoints) {
		try {
			co_spawn(Ctx_,
			         acceptConnections({ Ctx_, Endpoint }, Stop_.get_token(),
			                           Source),
			         asio::detached);
		} catch (const system_error & Ex) { Error = Ex.code(); }
	}
	return Error;
}

//------------------------------------------------------------------------------

// wrap the SDL (Simple Directmedia Layer https://www.libsdl.org/) C API types
// and their assorted functions

namespace sdl {
using Window =
    stdex::c_resource<SDL_Window, SDL_CreateWindow, SDL_DestroyWindow>;
using Renderer =
    stdex::c_resource<SDL_Renderer, SDL_CreateRenderer, SDL_DestroyRenderer>;
using Texture =
    stdex::c_resource<SDL_Texture, SDL_CreateTexture, SDL_DestroyTexture>;
} // namespace sdl

// the most minimal GUI
// capable of showing a frame with some decor for user interaction
// renders the video frames

struct GUI {
	GUI(int Width, int Height) {
		SDL_Init(SDL_INIT_VIDEO);
		Window_   = { "",
                    SDL_WINDOWPOS_CENTERED,
                    SDL_WINDOWPOS_CENTERED,
                    Width,
                    Height,
                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN };
		Renderer_ = { Window_, -1, SDL_RENDERER_PRESENTVSYNC };

		SDL_SetWindowMinimumSize(Window_, Width, Height);
		SDL_RenderSetLogicalSize(Renderer_, Width, Height);
		SDL_RenderSetIntegerScale(Renderer_, SDL_TRUE);
	}

	void updateFrom(const video::FrameHeader & Header) {
		if (!Header.Sequence_ || Header.Sequence_ < Sequence_) {
			if (Header.size() > 0) {
				Width_        = Header.Width_;
				Height_       = Header.Height_;
				Pitch_        = Header.LinePitch_;
				SourceFormat_ = Header.Format_ == video::RGBA
				                    ? SDL_PIXELFORMAT_ABGR8888
				                    : SDL_PIXELFORMAT_ARGB8888;
				Texture_ =
				    sdl::Texture(Renderer_, TextureFormat,
				                 SDL_TEXTUREACCESS_STREAMING, Width_, Height_);
				SDL_SetWindowMinimumSize(Window_, Width_, Height_);
				SDL_RenderSetLogicalSize(Renderer_, Width_, Height_);
				SDL_ShowWindow(Window_);
			} else {
				SDL_HideWindow(Window_);
				Texture_ = sdl::Texture{};
			}
		}
		Sequence_ = Header.Sequence_;
	}

	void present(ConstByteSpan Pixels) {
		void * TexturePixels;
		int TexturePitch;

		SDL_SetRenderDrawColor(Renderer_, 240, 240, 240, 240);
		SDL_RenderClear(Renderer_);
		if (!SDL_LockTexture(Texture_, nullptr, &TexturePixels,
		                     &TexturePitch)) {
			SDL_ConvertPixels(Width_, Height_, SourceFormat_, Pixels.data(),
			                  Pitch_, TextureFormat, TexturePixels,
			                  TexturePitch);
			SDL_UnlockTexture(Texture_);
			SDL_RenderCopy(Renderer_, Texture_, nullptr, nullptr);
		}
		SDL_RenderPresent(Renderer_);
	}

	bool processEvents() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				return false;
		}
		return true;
	}

private:
	sdl::Window Window_;
	sdl::Renderer Renderer_;
	sdl::Texture Texture_;
	int Sequence_ = INT_MAX;
	int Width_;
	int Height_;
	int Pitch_;
	int SourceFormat_;

	static constexpr auto TextureFormat = SDL_PIXELFORMAT_ARGB8888;
};

//------------------------------------------------------------------------------

// the receive-side network layer

struct GrowingSpace {
	[[nodiscard]] ByteSpan get(size_t Size) noexcept {
		if (Size > Size_) {
			Size_  = Size;
			Bytes_ = make_unique_for_overwrite<byte[]>(Size_);
		}
		return { Bytes_.get(), Size };
	}

private:
	unique_ptr<byte[]> Bytes_;
	size_t Size_ = 0;
};

// the video receive-render-present loop, implemented as coroutine on the heap
// brought down by internal events or through a stop-token

asio::awaitable<video::Frame> receiveFrame(tSocket & Socket, tTimer & Timer,
                                           GrowingSpace & PixelSpace) {
	byte HeaderSpace[video::FrameHeader::Size];
	tExpected<size_t> Result = co_await receive(Socket, Timer, HeaderSpace);
	if (Result == video::FrameHeader::Size) {
		const auto & Header = *new (HeaderSpace) video::FrameHeader;
		auto Pixels         = PixelSpace.get(Header.size());
		if (!Pixels.empty())
			Result = co_await receive(Socket, Timer, Pixels);
		if (Header.filler() || Result == Pixels.size())
			co_return video::Frame{ Header, Pixels };
	}
	co_return video::Frame{ 0 };
}

asio::awaitable<void> showVideos(asio::io_context & Ctx, stop_source Stop,
                                 GUI & UI, tEndpoints Endpoints) {
	tTimer Timer(Ctx);
	Timer.expires_after(2s);
	if (tExpected<tSocket> Connection =
	        co_await connectTo(Ctx, Timer, Endpoints);
	    Connection) {
		auto & Socket = Connection.value();
		stop_callback killme(Stop.get_token(), [&] {
			close(Socket);
			Timer.cancel();
		});

		GrowingSpace PixelSpace;

		while (!Stop.stop_requested()) {
			Timer.expires_after(2s); // time budget for the *whole* operation,
			                         // not just for single tcp socket reads!
			const auto Frame = co_await receiveFrame(Socket, Timer, PixelSpace);
			if (const auto & Header = Frame.Header_; !Header.null()) {
				UI.updateFrom(Header);
				UI.present(Frame.Pixels_);
				if (Header.filler())
					println("filler");
				else
					println("frame {:3} {}x{}", Header.Sequence_, Header.Width_,
					        Header.Height_);
			} else {
				break;
			}
		}
	}
	Stop.request_stop();
}

//------------------------------------------------------------------------------

// stop not only through a window button press but also from the command line
// there is a copy of the shared stop_source

asio::awaitable<void> stopOnSignal(asio::io_context & Ctx, stop_source Stop) {
	asio::signal_set Signals(Ctx, SIGINT);
	stop_callback killme(Stop.get_token(), [&] { Signals.cancel(); });

	co_await Signals.async_wait(asio::use_awaitable);
	Stop.request_stop();
}

// the GUI interaction is a separate coroutine
// there is a copy of the shared stop_source

asio::awaitable<void> handleGUIEvents(asio::io_context & Ctx, stop_source Stop,
                                      GUI & UI) {
	tTimer Poll(Ctx);
	stop_callback killme(Stop.get_token(), [&] { Poll.cancel(); });

	while (!Stop.stop_requested()) {
		if (UI.processEvents()) {
			Poll.expires_after(50ms);
			co_await Poll.async_wait();
		} else {
			Stop.request_stop();
		}
	}
}

//= vvvv move to module 'caboodle'
//==========================================================

// an option parser implemented with Boost.Program_options (C++03)

export auto getOptions() {
	namespace po = boost::program_options;
	po::options_description OptionsDescription("Options available");
	// clang-format off
	OptionsDescription.add_options()
		("help", "produce help message")
		("media", po::value<string>()->default_value("media"), "media directory")
		("server", po::value<string>()->default_value(""), "server name or ip")
		;
	// clang-format on
	po::variables_map Option;
	po::ext::parseCommandline(Option, OptionsDescription);
	if (Option.count("help")) {
		println("{}", po::ext::getHelpText(OptionsDescription));
		exit(-1);
	}
	return make_tuple(Option["media"].as<string>(),
	                  Option["server"].as<string>());
}

//= ^^^^ move to module 'caboodle'
//==========================================================

// the main routine just orchestrates and initiates the coroutines, and provides
// the central stop resource

int main() {
	const auto [MediaDirectory, ServerName] = getOptions();
	if (MediaDirectory.empty())
		return -2;

	asio::io_context Ctx; // this thread's root of the asio framework
	stop_source Stop;     // the mother of all stops

	const auto ServerEndpoints =
	    resolveHostEndpoints(ServerPort, ServerName, 1s);
	if (ServerEndpoints.empty())
		return -3;
	Server Srv(Ctx, Stop);
	Srv.serve(ServerEndpoints, MediaDirectory);

	GUI UI(1280, 720);

	asio::co_spawn(Ctx, stopOnSignal(Ctx, Stop), asio::detached);
	asio::co_spawn(Ctx, handleGUIEvents(Ctx, Stop, UI), asio::detached);
	asio::co_spawn(Ctx, showVideos(Ctx, Stop, UI, ServerEndpoints),
	               asio::detached);

	Ctx.run();
}
