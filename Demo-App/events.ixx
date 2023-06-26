module;
#include <csignal>

export module events;
import std;

import asio;
import executor;
import gui;
import net;

using namespace std::chrono_literals;

static constexpr auto EventPollInterval = 50ms;

// user interaction
export namespace handleEvents {

// watch out for an interrupt signal (e.g. from the command line).
// initiate an application stop in that case.

[[nodiscard]] auto fromTerminal(asio::io_context & Context) -> asio::awaitable<void> {
	asio::signal_set Signals(Context, SIGINT, SIGTERM);
	const auto WatchDog = executor::abort(Signals);

	co_await Signals.async_wait(asio::use_awaitable);
	executor::StopAssetOf(Context).request_stop();
}

// the GUI interaction is a separate coroutine.
// initiate an application stop if the spectator closes the window.

[[nodiscard]] auto fromGUI(asio::io_context & Context) -> asio::awaitable<void> {
	net::tTimer Timer(Context);
	const auto WatchDog = executor::abort(Timer);

	do {
		Timer.expires_after(EventPollInterval);
	} while (co_await net::expired(Timer) and gui::isAlive());
	executor::StopAssetOf(Context).request_stop();
}

} // namespace handleEvents
