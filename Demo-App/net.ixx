export module net;
import std;

import asio; // precompiled module, taken from BMI cache

// Convenience types and functions to deal with the networking part of the
// Asio library https://think-async.com/Asio/

// The Asio library is also the reference implementation of the
// Networking TS (ISO/IEC TS 19216:2018, C++ Extensions for Library Fundamentals)
// Draft https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/n4771.pdf
// plus P0958, P1322, P1943, P2444
// plus executors as decribed in P0443, P1348, and P1393

namespace aex = asio::experimental;
namespace net {

// basic networking types

export {
	// customize the regular Asio types and functions such that they can be
	//  - conveniently used in await expressions
	//  - composed into higher level, augmented operations

	template <typename... Ts>
	using tResult   = std::tuple<std::error_code, Ts...>;
	using use_await = asio::as_tuple_t<asio::use_awaitable_t<>>;
	using tSocket   = use_await::as_default_on_t<asio::ip::tcp::socket>;
	using tAcceptor = use_await::as_default_on_t<asio::ip::tcp::acceptor>;
	using tTimer    = use_await::as_default_on_t<asio::steady_timer>;

	using tEndpoint      = asio::ip::tcp::endpoint;
	using tEndpoints     = std::span<const tEndpoint>;
	using tByteSpan      = std::span<std::byte>;
	using tConstByteSpan = std::span<const std::byte>;

	template <std::size_t N>
	using tSendBuffers  = std::array<asio::const_buffer, N>;
	using tConstBuffers = std::span<asio::const_buffer>;

	enum tPort : uint16_t {};

	// the network layer uses std::expected<T, error_code> as return types

	template <typename T>
	using tExpected     = std::expected<T, std::error_code>;
	using tExpectSize   = tExpected<std::size_t>;
	using tExpectSocket = tExpected<tSocket>;
} // export

// transform the 'variant' return type from asio operator|| into an 'expected'
// as simply as possible to scare away no one. No TMP required here!

template <typename R, typename... Ts>
constexpr auto _map(tResult<Ts...> && Tuple) -> net::tExpected<R> {
	const auto & Error = std::get<std::error_code>(Tuple);
	if constexpr (sizeof...(Ts) == 0)
		return std::unexpected{ Error };
	else if (Error)
		return std::unexpected{ Error };
	else
		return std::get<R>(std::move(Tuple));
}

export {
	using aex::awaitable_operators::operator||;

	template <typename... Ts, typename... Us>
	constexpr auto flatten(std::variant<tResult<Ts...>, tResult<Us...>> && Variant) {
		using net::_map;
		using tReturn = std::type_identity<Ts..., Us...>::type;
		return std::visit(
		    [](auto && Tuple) {
			    return _map<tReturn>(std::move(Tuple));
		    },
		    std::move(Variant));
	}

	template <typename Out, typename In>
	auto replace(net::tExpected<In> && Input, Out && Replacement)->net::tExpected<Out> {
		return std::move(Input).transform([&](In &&) {
			return std::forward<Out>(Replacement);
		});
	}

	constexpr auto asBytes(const auto & Object) noexcept -> asio::const_buffer {
		const auto Bytes = std::as_bytes(std::span{ &Object, 1 });
		return { Bytes.data(), Bytes.size() };
	}

	auto sendTo(tSocket & Socket, tTimer & Timer, tConstBuffers DataToSend)
	    ->asio::awaitable<tExpectSize>;
	auto receiveFrom(tSocket & Socket, tTimer & Timer, tByteSpan SpaceToFill)
	    ->asio::awaitable<tExpectSize>;
	auto connectTo(tEndpoints EndpointsToTry, tTimer & Timer)
	    ->asio::awaitable<tExpectSocket>;
	auto expired(tTimer & Timer) noexcept -> asio::awaitable<bool>;

	void close(tSocket & Socket) noexcept;
	auto resolveHostEndpoints(std::string_view HostName, tPort Port,
	                          std::chrono::milliseconds TimeBudget)
	    ->std::vector<tEndpoint>;
} // export
} // namespace net
