/* =============================================================================
The server

 - waits for clients to connect at any of a list of given endpoints
 - when a client connects, observes a given directory for all files in there
   repeating this endlessly
 - filters all GIF files which contain a video
 - decodes each video file into individual video frames
 - sends each frame at the correct time to the client
 - sends filler frames if there happen to be no GIF files to process

The client

 - tries to connect to any of a list of given server endpoints
 - receives video frames from the network connection
 - presents the video frames in a reasonable manner in a GUI window

The application

 - performs a clean shutdown from all inputs that the user can interact with
 - handles timeouts and errors properly and performs a clean shutdown
==============================================================================*/

#include <chrono>
#include <coroutine>
#include <csignal>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <print>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

#include "c_resource.hpp"

import the.whole.caboodle;
import asio;          // precompiled module, taken from BMI cache
import net.types;
import sdl;           // precompiled module, taken from BMI cache
import video;
import video.decoder;

using namespace std;         // bad practice - only for presentation!
using namespace std::chrono; // bad practice - only for presentation!
using namespace std::chrono_literals;
using namespace net;

namespace fs = std::filesystem;

// networking
namespace {
//------------------------------------------------------------------------------
// the lowest-level networking routines with support for cancellation and
// timeouts

asio::awaitable<tExpected<size_t>> sendTo(tSocket & Socket, tTimer & Timer,
                                          tConstBuffers Data) {
	co_return get(
	    co_await(asio::async_write(Socket, Data) || Timer.async_wait()));
}

// precondition: !Space.empty()
asio::awaitable<tExpected<size_t>> receiveFrom(tSocket & Socket, tTimer & Timer,
                                               ByteSpan Space) {
	co_return get(co_await(asio::async_read(Socket, buffer(Space)) ||
	                       Timer.async_wait()));
}

// precondition: !Endpoints.empty()
asio::awaitable<tExpected<tSocket>> connectTo(tEndpoints Endpoints,
                                              tTimer & Timer) {
	tSocket Socket(Timer.get_executor());
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

[[nodiscard]] vector<tEndpoint> resolveHostEndpoints(string_view HostName,
                                                     uint16_t Port,
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

template <typename T>
void _close(T & Obj) {
	if constexpr (requires(T t) { { close(t) }; })
		close(Obj);
	else if constexpr (requires(T t) { { t.close() }; })
		Obj.close();
	else if constexpr (requires(T t) { { t.cancel() }; })
		Obj.cancel();
	else
		static_assert(is_fundamental_v<remove_cv_t<T>>);
}

template <typename S, typename T, typename... Ts>
[[nodiscard]] auto killMe(S & Stop, T & Obj, Ts &... Objs) {
	stop_token Token;
	if constexpr (is_base_of_v<stop_source, S>)
		Token = Stop.get_token();
	else if constexpr (is_base_of_v<stop_token, S>)
		Token = Stop;
	else
		static_assert(is_void_v<S>, "gimme a break!");

	return stop_callback{ std::move(Token), [&] {
		                     (_close(Obj), ..., _close(Objs));
		                 } };
}

} // namespace

// server
namespace {

auto makeTimedBarrier(tTimer & Timer) {
	auto StartTime = steady_clock::now();
	auto Timestamp = video::FrameHeader::µSeconds{ 0 };
	int Sequence   = INT_MAX;

	return [=, &Timer](const video::Frame & Frame) mutable {
		const auto & Header = Frame.Header_;
		Timer.expires_at(StartTime + (Header.Sequence_ != 0 ? Header.Timestamp_
		                                                    : Timestamp));
		if (Header.Sequence_ == 0 ||
		    Header.Sequence_ < Sequence) // start of frame sequence
			StartTime = steady_clock::now();
		Sequence  = Header.Sequence_;
		Timestamp = Header.Timestamp_;
		return Timer.async_wait();
	};
}

// the connection object implemented as a coroutine on the heap
// will be brought down by internal events or from the outside using a
// stop_token

asio::awaitable<void> startStreaming(tSocket Socket, stop_token Stop,
                                     fs::path Source) {
	tTimer Timer(Socket.get_executor());
	const auto _ = killMe(Stop, Socket, Timer);
	auto DueTime = makeTimedBarrier(Timer);

	for (const auto & Frame : videodecoder::makeFrames(std::move(Source))) {
		co_await DueTime(Frame);

		auto Buffers = SendBuffers<2>{ buffer(asBytes(Frame.Header_)),
			                           buffer(Frame.Pixels_) };
		Timer.expires_after(100ms);
		if (!co_await sendTo(Socket, Timer, Buffers) || Stop.stop_requested())
			break;
	}
}

// the tcp acceptor is also a coroutine
// spawns new, independent coroutines on connect

asio::awaitable<void> acceptConnections(tAcceptor Acceptor, stop_token Stop,
                                        const fs::path Source) {
	const auto _ = killMe(Stop, Acceptor);

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

error_code serve(asio::io_context & Ctx, stop_source Stop, tEndpoints Endpoints,
                 const fs::path Source) {
	error_code Error;
	for (const auto & Endpoint : Endpoints) {
		try {
			co_spawn(
			    Ctx,
			    acceptConnections({ Ctx, Endpoint }, Stop.get_token(), Source),
			    asio::detached);
		} catch (const system_error & Ex) { Error = Ex.code(); }
	}
	return Error;
}
} // namespace

// GUI
namespace {
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
			if (Header.empty()) {
				SDL_HideWindow(Window_);
				Texture_ = sdl::Texture{};
			} else {
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
			}
		}
		Sequence_ = Header.Sequence_;
	}

	void present(video::tPixels Pixels) {
		void * TexturePixels;
		int TexturePitch;

		SDL_SetRenderDrawColor(Renderer_, 240, 240, 240, 240);
		SDL_RenderClear(Renderer_);
		if (SDL_LockTexture(Texture_, nullptr, &TexturePixels, &TexturePitch) ==
		    0) {
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

} // namespace

// client
namespace {
struct GrowingSpace {
	[[nodiscard]] ByteSpan get(size_t Size) noexcept {
		if (Size > Capacity_) {
			Capacity_ = Size;
			Bytes_    = make_unique_for_overwrite<std::byte[]>(Capacity_);
		}
		return { Bytes_.get(), Size };
	}

private:
	unique_ptr<std::byte[]> Bytes_;
	size_t Capacity_ = 0;
};

asio::awaitable<video::Frame> receiveFrame(tSocket & Socket, tTimer & Timer,
                                           GrowingSpace & PixelSpace) {
	alignas(video::FrameHeader) std::byte Header[video::FrameHeader::Size];
	tExpected<size_t> Result = co_await receiveFrom(Socket, Timer, Header);
	if (Result == video::FrameHeader::Size) {
		const auto & FrameHeader = *new (Header) video::FrameHeader;
		auto Pixels              = PixelSpace.get(FrameHeader.size());
		if (!Pixels.empty())
			Result = co_await receiveFrom(Socket, Timer, Pixels);
		if (FrameHeader.filler() || Result == Pixels.size())
			co_return video::Frame{ FrameHeader, Pixels };
	}
	co_return video::noFrame;
}

asio::awaitable<void> rollVideos(stop_token Stop, tSocket & Socket,
                                 tTimer & Timer, GUI & UI) {
	GrowingSpace PixelSpace;

	while (!Stop.stop_requested()) {
		Timer.expires_after(2s); // time budget for the *whole* operation,
		// not just for single tcp socket reads!
		const auto Frame    = co_await receiveFrame(Socket, Timer, PixelSpace);
		const auto & Header = Frame.Header_;
		if (Header.null())
			co_return;

		UI.updateFrom(Header);
		UI.present(Frame.Pixels_);

		if (Header.filler())
			println("filler");
		else
			println("frame {:3} {}x{}", Header.Sequence_, Header.Width_,
			        Header.Height_);
	}
}

// the video receive-render-present loop, implemented as coroutine on the heap
// brought down by internal events or through a stop-token

asio::awaitable<void> showVideos(asio::io_context & Ctx, stop_source Stop,
                                 GUI & UI, tEndpoints Endpoints) {
	tTimer Timer(Ctx);
	Timer.expires_after(2s);
	if (tExpected<tSocket> Connection = co_await connectTo(Endpoints, Timer);
	    Connection) {
		auto Socket  = std::move(Connection).value();
		const auto _ = killMe(Stop, Socket, Timer);

		co_await rollVideos(Stop.get_token(), Socket, Timer, UI);
	}
	Stop.request_stop();
}
} // namespace

// user interaction
namespace {
// stop not only through a window button press but also from the command line
// there is a copy of the shared stop_source

asio::awaitable<void> stopOnSignal(asio::io_context & Ctx, stop_source Stop) {
	asio::signal_set Signals(Ctx, SIGINT);
	const auto _ = killMe(Stop, Signals);

	co_await Signals.async_wait(asio::use_awaitable);
	Stop.request_stop();
}

// the GUI interaction is a separate coroutine
// there is a copy of the shared stop_source

asio::awaitable<void> handleGUIEvents(asio::io_context & Ctx, stop_source Stop,
                                      GUI & UI) {
	tTimer Poll(Ctx);
	const auto _ = killMe(Stop, Poll);

	while (!Stop.stop_requested()) {
		if (UI.processEvents()) {
			Poll.expires_after(50ms);
			co_await Poll.async_wait();
		} else {
			Stop.request_stop();
		}
	}
}
} // namespace

int main() {
	const auto [MediaDirectory, ServerName] = caboodle::getOptions();
	if (MediaDirectory.empty())
		return -2;
	const auto ServerEndpoints =
	    resolveHostEndpoints(ServerName, ServerPort, 1s);
	if (ServerEndpoints.empty())
		return -3;

	asio::io_context Ctx;
	stop_source Stop; // the mother of all stops

	const auto Error =
	    serve(Ctx, Stop, ServerEndpoints, std::move(MediaDirectory));
	if (Error)
		return -4;

	GUI UI(1280, 1024);

	co_spawn(Ctx, stopOnSignal(Ctx, Stop), asio::detached);
	co_spawn(Ctx, handleGUIEvents(Ctx, Stop, UI), asio::detached);
	co_spawn(Ctx, showVideos(Ctx, Stop, UI, ServerEndpoints), asio::detached);

	Ctx.run();
}
