module;
#ifdef __MINGW64__
#  include <cwchar> // work around ODR problems with the C standard library
#endif

export module server;
import std;

import asio;
import net;
import video;
import executor;

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
	const auto WatchDog = executor::abort(Socket, Timer);

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
	const auto WatchDog = executor::abort(Acceptor);

	while (Acceptor.is_open()) {
		auto [Error, Socket] = co_await Acceptor.async_accept();
		if (not Error and Socket.is_open())
			executor::commission(Acceptor.get_executor(), streamVideos, std::move(Socket),
			                     Source);
	}
}

// start serving a list of given endpoints.
// each endpoint is served by an independent coroutine.

export auto serve(asio::io_context & Context, net::tEndpoints Endpoints,
                  const fs::path Source) -> net::tExpectSize {
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
