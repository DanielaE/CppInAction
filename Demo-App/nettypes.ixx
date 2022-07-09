module;
#include <chrono>
#include <concepts>
#include <expected>
#include <span>
#include <tuple>
#include <variant>

export module net.types;
import asio; // precompiled module, taken from BMI cache

using namespace std; // bad practice - only for presentation!

// basic networking types

namespace net {

// Convenience types and functions to deal with the
// asio library https://think-async.com/Asio/

// The asio library is also the reference implementation of the
// Networking TS (ISO/IEC TS 19216:2018, C++ Extensions for Library
// Fundamentals) Draft
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/n4771.pdf

// customize the regular asio types and functions such that they can be
//  - conveniently used in await expressions
//  - composed into higher level, augmented operations

export {
	namespace asioe = asio::experimental;

	using asioe::awaitable_operators::operator||;
	using await = asioe::as_tuple_t<asio::use_awaitable_t<>>;

	using tSocket   = await::as_default_on_t<asio::ip::tcp::socket>;
	using tAcceptor = await::as_default_on_t<asio::ip::tcp::acceptor>;
	using tTimer    = await::as_default_on_t<asio::steady_timer>;

	using tEndpoint     = asio::ip::tcp::endpoint;
	using tEndpoints    = span<const tEndpoint>;
	using tConstBuffers = span<asio::const_buffer>;

	using ByteSpan      = span<std::byte>;
	using ConstByteSpan = span<const std::byte>;

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

	// the network layer uses std::expected<T, error_code> as return types

	template <typename T>
	using tExpected = std::expected<T, error_code>;
} // export

// transform the variant return type from asio operator|| into an expected
// as simple as possible to scare away no one. No TMP required here!

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

export {
	template <typename... Ts, typename... Us>
	constexpr auto get(
	    variant<tuple<error_code, Ts...>, tuple<error_code, Us...>> && var) {
		using net::_get;
		using R = type_identity_t<Ts..., Us...>;
		return visit(
		    [](auto && tpl) {
			    return _get<R>(std::move(tpl));
		    },
		    std::move(var));
	}

	template <typename T>
	constexpr bool operator==(const tExpected<T> & Actual,
	                          const convertible_to<T> auto & rhs) noexcept {
		return Actual && Actual.value() == rhs;
	}
} // export
} // namespace net
