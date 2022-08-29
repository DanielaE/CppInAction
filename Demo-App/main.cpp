/* =============================================================================
The server

 - waits for clients to connect at anyone of a list of given endpoints
 - when a client connects, observes a given directory for all files in there,
   repeating this endlessly
 - filters all GIF files which contain a video
 - decodes each video file into individual video frames
 - sends each frame at the correct time to the client
 - sends filler frames if there happen to be no GIF files to process

The client

 - tries to connect to anyone of a list of given server endpoints
 - receives video frames from the network connection
 - presents the video frames in a reasonable manner in a GUI window

The application

 - watches all inputs that the user can interact with for the desire to end
   the application
 - handles timeouts and errors properly and performs a clean shutdown if needed
==============================================================================*/

import std;

import the.whole.caboodle;
import asio; // precompiled module, taken from BMI cache
import executor;
import gui;
import net;
import video;

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace server {

static constexpr auto SendTimeBudget = 100ms;

// create a closure with a call operator that returns an awaitable taylored to each
// given frame.
// the awaitable can then be co_awaited and execution of the awaiter will resume at
// exactly that time that the given frame is supposed to be sent out.

[[nodiscard]] auto makeStartingGate(net::tTimer & Timer) {
	using std::chrono::steady_clock;
	auto StartTime = steady_clock::now();
	auto Timestamp = video::FrameHeader::µSeconds{ 0 };

	return [=, &Timer](const video::Frame & Frame) mutable {
		const auto & Header = Frame.Header_;
		const auto DueTime =
		    StartTime + (Header.isFiller() ? Timestamp : Header.Timestamp_);
		if (Header.isFirstFrame())
			StartTime = steady_clock::now();
		Timestamp = Header.Timestamp_;
		Timer.expires_at(DueTime);
		return Timer.async_wait();
	};
}

// the connection is implemented as an independent coroutine.
// it will be brought down by internal events or from the outside using a
// stop signal.

[[nodiscard]] auto streamVideos(net::tSocket Socket, fs::path Source)
    -> asio::awaitable<void> {
	net::tTimer Timer(Socket.get_executor());
	const auto Sentinel = executor::guard(Socket, Timer);

	auto DueTime = makeStartingGate(Timer);
	for (const auto Frame : video::makeFrames(std::move(Source))) {
		co_await DueTime(Frame);

		net::tSendBuffers<2> Buffers{ net::asBytes(Frame.Header_),
			                          asio::buffer(Frame.Pixels_) };
		Timer.expires_after(SendTimeBudget);
		if (Frame.TotalSize() != co_await net::sendTo(Socket, Timer, Buffers))
			break;
	}
}

// the tcp acceptor is a coroutine.
// it spawns new, independent coroutines on connect.

[[nodiscard]] auto acceptConnections(net::tAcceptor Acceptor, const fs::path Source)
    -> asio::awaitable<void> {
	const auto Sentinel = executor::guard(Acceptor);

	while (Acceptor.is_open()) {
		auto [Error, Socket] = co_await Acceptor.async_accept();
		if (not Error && Socket.is_open())
			executor::commission(Acceptor.get_executor(), streamVideos, std::move(Socket),
			                     Source);
	}
}

// start serving a list of given endpoints.
// each endpoint is served by an independent coroutine.

auto serve(asio::io_context & Context, net::tEndpoints Endpoints, const fs::path Source)
    -> net::tExpectSize {
	std::size_t NumberOfAcceptors = 0;
	auto Error = std::make_error_code(std::errc::function_not_supported);

	for (const auto & Endpoint : Endpoints) {
		try {
			executor::commission(Context, acceptConnections,
			                     net::tAcceptor{ Context, Endpoint }, Source);
			std::println("accept connections at {}", Endpoint.address().to_string());
			++NumberOfAcceptors;
		} catch (const std::system_error & Ex) {
			Error = Ex.code();
		}
	}
	if (NumberOfAcceptors == 0)
		return std::unexpected{ Error };
	return NumberOfAcceptors;
}
} // namespace server

namespace client {
static constexpr auto ReceiveTimeBudget = 2s;
static constexpr auto ConnectTimeBudget = 2s;

// a memory resource that owns at least as much memory as it was ever asked to lend out.

struct AdaptiveMemoryResource {
	[[nodiscard]] net::tByteSpan lend(std::size_t Size) noexcept {
		if (Size > Capacity_) {
			Capacity_ = Size;
			Bytes_    = std::make_unique_for_overwrite<std::byte[]>(Capacity_);
		}
		return { Bytes_.get(), Size };
	}

private:
	std::unique_ptr<std::byte[]> Bytes_;
	std::size_t Capacity_ = 0;
};

// receive a single video frame.
// it returns either
//  - a well-formed frame with visible content
//  - a well-formed frame without visible content
//  - a 'noFrame' placeholder to express disappointment in case of problems

[[nodiscard]] auto receiveFrame(net::tSocket & Socket, net::tTimer & Timer,
                                AdaptiveMemoryResource & Memory)
    -> asio::awaitable<video::Frame> {
	alignas(video::FrameHeader) std::byte HeaderBytes[video::FrameHeader::SizeBytes];

	auto Got = co_await net::receiveFrom(Socket, Timer, HeaderBytes);
	if (Got == video::FrameHeader::SizeBytes) {
		const auto & Header = *new (HeaderBytes) video::FrameHeader;

		auto Pixels = Memory.lend(Header.SizePixels());
		if (not Pixels.empty()) {
			Got    = co_await net::receiveFrom(Socket, Timer, Pixels);
			Pixels = Pixels.first(Got.value_or(0));
		}
		if (Pixels.size() == Header.SizePixels())
			co_return video::Frame{ Header, Pixels };
	}
	co_return video::noFrame;
}

// present a possibly infinite sequence of video frames until the spectator
// gets bored or problems arise.

[[nodiscard]] auto rollVideos(net::tSocket Socket, net::tTimer Timer,
                              gui::FancyWindow Window) -> asio::awaitable<void> {
	const auto Sentinel = executor::guard(Socket, Timer);
	AdaptiveMemoryResource PixelMemory;

	while (Socket.is_open()) {
		Timer.expires_after(ReceiveTimeBudget);
		const auto Frame    = co_await receiveFrame(Socket, Timer, PixelMemory);
		const auto & Header = Frame.Header_;
		if (Header.isNoFrame())
			break;

		Window.updateFrom(Header);
		Window.present(Frame.Pixels_);

		using namespace std::chrono;

		if (Header.isFiller())
			std::println("filler frame");
		else
			std::println("frame {:3} {}x{} @ {:>6%Q%q}", Header.Sequence_, Header.Width_,
			             Header.Height_, round<milliseconds>(Header.Timestamp_));
	}
}

// connects to the server and starts the top-level video receive-render-present loop.
// initiates an application stop in case of communication problems.

[[nodiscard]] auto showVideos(asio::io_context & Context, gui::FancyWindow Window,
                              net::tEndpoints Endpoints) -> asio::awaitable<void> {
	net::tTimer Timer(Context);
	Timer.expires_after(ConnectTimeBudget);
	if (net::tExpectSocket Socket = co_await net::connectTo(Endpoints, Timer)) {
		co_await rollVideos(std::move(Socket).value(), std::move(Timer),
		                    std::move(Window));
	}
	executor::StopAssetOf(Context).stop();
}
} // namespace client

// user interaction
namespace handleEvents {
static constexpr auto EventPollInterval = 50ms;

// watch out for an interrupt signal (e.g. from the command line).
// initiate an application stop in that case.

[[nodiscard]] auto fromTerminal(asio::io_context & Context) -> asio::awaitable<void> {
	asio::signal_set Signals(Context, SIGINT, SIGTERM);
	const auto Sentinel = executor::guard(Signals);

	co_await Signals.async_wait(asio::use_awaitable);
	executor::StopAssetOf(Context).stop();
}

// the GUI interaction is a separate coroutine.
// initiate an application stop if the spectator closes the window.

[[nodiscard]] auto fromGUI(asio::io_context & Context) -> asio::awaitable<void> {
	net::tTimer Timer(Context);
	auto Stop               = executor::StopAssetOf(Context);
	const auto BreakCircuit = executor::breakOn(Stop, Timer);

	while (not Stop && gui::processEvents()) {
		Timer.expires_after(EventPollInterval);
		co_await Timer.async_wait();
	}
	Stop.stop();
}

} // namespace handleEvents

static constexpr auto ServerPort        = uint16_t{ 34567 };
static constexpr auto ResolveTimeBudget = 1s;

int main() {
	auto [MediaDirectory, ServerName] = caboodle::getOptions();
	if (MediaDirectory.empty())
		return -2;
	const auto ServerEndpoints =
	    net::resolveHostEndpoints(ServerName, ServerPort, ResolveTimeBudget);
	if (ServerEndpoints.empty())
		return -3;

	asio::io_context ExecutionContext; // we have executors at home
	std::stop_source Stop;             // the mother of all stops
	const auto schedule = executor::makeScheduler(ExecutionContext, Stop);

	const auto Listening =
	    schedule(server::serve, ServerEndpoints, std::move(MediaDirectory));
	if (not Listening)
		return -4;

	schedule(client::showVideos,
	         gui::FancyWindow(gui::width{ 1280 }, gui::height{ 1024 }), ServerEndpoints);
	schedule(handleEvents::fromTerminal);
	schedule(handleEvents::fromGUI);

	ExecutionContext.run();
}
