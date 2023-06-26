export module client;
import std;

import asio;
import net;
import gui;
import video;
import executor;

using namespace std::chrono_literals;

namespace client {
static constexpr auto ReceiveTimeBudget = 2s;
static constexpr auto ConnectTimeBudget = 2s;

// a memory resource that owns at least as much memory as it was ever asked to lend out.

struct AdaptiveMemoryResource {
	[[nodiscard]] auto lend(std::size_t Size) -> net::tByteSpan {
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
		const auto & Header = *std::start_lifetime_as<video::FrameHeader>(HeaderBytes);

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
	const auto WatchDog = executor::abort(Socket, Timer);
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

export [[nodiscard]] auto showVideos(asio::io_context & Context, gui::FancyWindow Window,
                                     net::tEndpoints Endpoints) -> asio::awaitable<void> {
	net::tTimer Timer(Context);
	Timer.expires_after(ConnectTimeBudget);
	if (net::tExpectSocket Socket = co_await net::connectTo(Endpoints, Timer)) {
		co_await rollVideos(std::move(Socket).value(), std::move(Timer),
		                    std::move(Window));
	}
	executor::StopAssetOf(Context).request_stop();
}
} // namespace client
