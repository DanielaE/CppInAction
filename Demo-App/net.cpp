﻿import std;

import asio; // precompiled module, taken from BMI cache
import net;

// the lowest-level networking routines with support for cancellation and timeouts

namespace net {
using namespace asio;

// precondition: not Data.empty()
auto sendTo(tSocket & Socket, tTimer & Timer, tConstBuffers Data)
    -> awaitable<tExpectSize> {
	co_return expect(co_await (async_write(Socket, Data) || Timer.async_wait()));
}

// precondition: not Space.empty()
auto receiveFrom(tSocket & Socket, tTimer & Timer, tByteSpan Space)
    -> awaitable<tExpectSize> {
	co_return expect(co_await (async_read(Socket, buffer(Space)) || Timer.async_wait()));
}

// precondition: not Endpoints.empty()
auto connectTo(tEndpoints Endpoints, tTimer & Timer) -> awaitable<tExpectSocket> {
	tSocket Socket(Timer.get_executor());
	co_return replace(
	    expect(co_await (async_connect(Socket, Endpoints) || Timer.async_wait())),
	    std::move(Socket));
}

void close(tSocket & Socket) noexcept {
	std::error_code Error;
	Socket.shutdown(tSocket::shutdown_both, Error);
	Socket.close(Error);
}

using namespace std::string_view_literals;
static constexpr auto Local = "localhost"sv;

// precondition: TimeBudget > 0
auto resolveHostEndpoints(std::string_view HostName, uint16_t Port,
                          std::chrono::milliseconds TimeBudget)
    -> std::vector<tEndpoint> {
	using tResolver = asio::ip::tcp::resolver;
	auto Flags      = tResolver::numeric_service;
	if (HostName.empty() || HostName == Local) {
		Flags |= tResolver::passive;
		HostName = Local;
	}

	std::vector<tEndpoint> Endpoints;
	const auto toEndpoints = [&](const auto &, auto Resolved) {
		Endpoints.reserve(Resolved.size());
		for (const auto & Element : Resolved)
			Endpoints.push_back(Element.endpoint());
	};

	asio::io_context Executor;
	tResolver Resolver(Executor);
	Resolver.async_resolve(HostName, std::to_string(Port), Flags, toEndpoints);
	Executor.run_for(TimeBudget);
	return Endpoints;
}

} // namespace net