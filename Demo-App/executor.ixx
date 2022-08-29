export module executor;
import std;

import asio; // precompiled module, taken from BMI cache

// Convenience types and functions to deal with the executor part of the
// Asio library https://think-async.com/Asio/

// The Asio library is also the reference implementation of the
// Networking TS (ISO/IEC TS 19216:2018, C++ Extensions for Library Fundamentals)
// Draft https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/n4771.pdf
// plus P0958, P1322, P1943, P2444
// plus executors as decribed in P0443, P1348, and P1393

export namespace Off {
struct Indicator : std::stop_token {
	using base = std::stop_token;

	[[nodiscard]] Indicator(base Stop) noexcept
	: base{ std::move(Stop) } {}

	explicit operator bool() const noexcept { return stop_requested(); }
};

struct Switch : std::stop_source {
	using base = std::stop_source;

	using base::base;
	[[nodiscard]] Switch(base Stop) noexcept
	: base{ std::move(Stop) } {}

	void stop() noexcept { request_stop(); }

	explicit operator bool() const noexcept { return stop_requested(); }
	[[nodiscard]] operator Indicator() const noexcept { return get_token(); }
};
} // namespace Off

namespace executor {
template <typename T>
constexpr inline bool Unfortunate = false;
template <typename T>
constexpr inline bool isExecutionContext = std::is_base_of_v<asio::execution_context, T>;
template <typename T>
constexpr inline bool isExecutor =
    asio::is_executor<T>::value || asio::execution::is_executor<T>::value;
template <typename T>
constexpr inline bool hasExecutor = requires(T t) {
	{ t.get_executor() };
};

auto onException(Off::Switch Stop) {
	return [Stop_ = std::move(Stop)](std::exception_ptr pEx) mutable {
		if (pEx)
			Stop_.stop();
	};
}

using ServiceBase = asio::execution_context::service;
struct StopService : ServiceBase {
	using key_type = StopService;

	static asio::io_context::id id;

	using ServiceBase::ServiceBase;
	StopService(asio::execution_context & Ctx, Off::Switch Stop)
	: ServiceBase(Ctx)
	, Stop_(std::move(Stop)) {}

	operator Off::Switch() const { return Stop_; }

private:
	void shutdown() noexcept override {}
	Off::Switch Stop_;
};

void addStopService(asio::execution_context & Executor, std::stop_source & Stop) {
	asio::make_service<StopService>(Executor, Stop);
}

// precondition: 'Context' owns a 'StopService'

Off::Switch getStop(asio::execution_context & Context) {
	return asio::use_service<StopService>(Context);
}

template <typename T>
asio::execution_context & getContext(T & Object) {
	if constexpr (isExecutionContext<T>)
		return Object;
	else if constexpr (isExecutor<T>)
		return Object.context();
	else if constexpr (hasExecutor<T>)
		return Object.get_executor().context();
	else
		static_assert(Unfortunate<T>, "Please give me an execution context");
}

// return the off-switch that is 'wired' to the given 'Object'

export [[nodiscard]] auto StopAssetOf(auto & Object) {
	return executor::getStop(executor::getContext(Object));
}

// A compile-time function that answers the questions if a given callable 'Func'
//  - can be called with a set of argument types 'Ts'
//  - returns an awaitable
//  - must be called synchronously or asynchronously

template <typename Func, typename... Ts>
struct isCallable {
	static constexpr bool invocable = std::is_invocable_v<Func, Ts...>;
	using ReturnType                = std::invoke_result_t<Func, Ts...>;

	template <typename T>
	static consteval bool isAwaitable(asio::awaitable<T> *) {
		return true;
	}
	static consteval bool isAwaitable(auto *) { return false; }
	static consteval ReturnType * returned() { return nullptr; }

	static constexpr bool returnsAwaitable = isAwaitable(returned());
	static constexpr bool synchronously    = invocable && not returnsAwaitable;
	static constexpr bool asynchronously   = invocable && returnsAwaitable;
};

// initiate independent asynchronous execution of a piece of work on a given executor.
// signal stop if an exception escapes that piece of work.

#define WORKITEM std::invoke(std::forward<Func>(Work), std::forward<Ts>(Args)...)

export template <typename Exec, typename Func, typename... Ts>
	requires(isCallable<Func, Ts...>::asynchronously)
void commission(Exec && Executor, Func && Work, Ts &&... Args) {
	auto Stop = executor::StopAssetOf(Executor);
	asio::co_spawn(Executor, WORKITEM, executor::onException(Stop));
}

// create a scheduler that can issue pieces of work onto the given execution context.
// depending on the kind of work, it is scheduled for synchronous or asynchronous
// execution.
// the execution context is augmented by a stop service related to the given
// stop_source.

#define WORK std::forward<Func>(Work), Context, std::forward<Ts>(Args)...

export [[nodiscard]] auto makeScheduler(asio::io_context & Context,
                                        std::stop_source & Stop) {
	executor::addStopService(Context, Stop);

	return [&]<typename Func, typename... Ts>(Func && Work, Ts &&... Args) {
		using toBeCalled = isCallable<Func, asio::io_context &, Ts...>;
		if constexpr (toBeCalled::asynchronously)
			executor::commission(Context, WORK);
		else if constexpr (toBeCalled::synchronously)
			return std::invoke(WORK);
		else
			static_assert(Unfortunate<Func>,
			              "Please help, I don't know how to execute this 'Work'");
	};
}

// close a given object depending on its capabilities.
// the close operation is customizable to cater for more involved closing requirements.

#define THIS_WORKS(x) (requires(T Object) { { x }; }) x;

// clang-format off
template <typename T>
void _close(T & Object) {
	if      constexpr THIS_WORKS( close(Object)   )
	else if constexpr THIS_WORKS( Object.close()  )
	else if constexpr THIS_WORKS( Object.cancel() )
	else
		static_assert(Unfortunate<T>, "Please tell me how to close this 'Object'");
}
// clang-format on

// create an object that is wired up to close all given objects
// whenever a stop is indicated.

export [[nodiscard]] auto breakOn(Off::Indicator Stop, auto & Object,
                                  auto &... moreObjects) {
	using executor::_close;
	return std::stop_callback{ std::move(Stop), [&] {
		                          (_close(Object), ..., _close(moreObjects));
		                      } };
}

export [[nodiscard]] auto guard(auto & Object, auto &... moreObjects) {
	return breakOn(StopAssetOf(Object), Object, moreObjects...);
}

} // namespace executor
