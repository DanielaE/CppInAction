#pragma once
#include <concepts>
#include <type_traits>

namespace stdex {
template <typename T>
inline constexpr T * c_resource_null_value = nullptr;

template <typename T, auto * ConstructFunction, auto * DestructFunction>
	requires(std::is_function_v<
	         std::remove_pointer_t<decltype(ConstructFunction)>> &&
	             std::is_function_v<
	                 std::remove_pointer_t<decltype(DestructFunction)>>)
struct c_resource {
	using pointer      = T *;
	using element_type = T;

private:
	using constructor = decltype(ConstructFunction);
	using destructor  = decltype(DestructFunction);

	static constexpr constructor c_ = ConstructFunction;
	static constexpr destructor d_  = DestructFunction;

	static constexpr T * null = c_resource_null_value<T>;

	struct construct_t {};

public:
	static constexpr inline construct_t construct = {};

	constexpr c_resource() noexcept = default;
	constexpr explicit c_resource(
	    construct_t) noexcept requires std::is_invocable_r_v<T *, constructor>
	: ptr_{ c_() } {};

	template <typename... Ts>
		requires(sizeof...(Ts) > 0 &&
		         std::is_invocable_r_v<T *, constructor, Ts...>)
	[[nodiscard]] constexpr explicit(sizeof...(Ts) == 1)
	    c_resource(Ts &&... Args) noexcept
	: ptr_{ c_(static_cast<Ts &&>(Args)...) } {}

	template <typename... Ts>
		requires(sizeof...(Ts) > 0 &&
		         std::is_invocable_v<constructor, T **, Ts...> &&
		         requires(T * p, Ts... Args) {
			         { c_(&p, Args...) } -> std::same_as<void>;
		         })
	[[nodiscard]] constexpr explicit(sizeof...(Ts) == 1)
	    c_resource(Ts &&... Args) noexcept
	: ptr_{ null } {
		c_(&ptr_, static_cast<Ts &&>(Args)...);
	}

	template <typename... Ts>
		requires(std::is_invocable_v<constructor, T **, Ts...>)
	[[nodiscard]] constexpr auto replace(Ts &&... Args) noexcept {
		destruct(ptr_);
		ptr_ = null;
		return c_(&ptr_, static_cast<Ts &&>(Args)...);
	}

	template <typename... Ts>
		requires(std::is_invocable_v<constructor, T **, Ts...> && requires(
		    T * p, Ts... Args) {
			{ c_(&p, Args...) } -> std::same_as<void>;
		})
	constexpr void replace(Ts &&... Args) noexcept {
		destruct(ptr_);
		ptr_ = null;
		c_(&ptr_, static_cast<Ts &&>(Args)...);
	}

	[[nodiscard]] constexpr c_resource(c_resource && other) noexcept {
		ptr_       = other.ptr_;
		other.ptr_ = null;
	};
	constexpr c_resource & operator=(c_resource && rhs) noexcept {
		if (this != &rhs) {
			destruct(ptr_);
			ptr_     = rhs.ptr_;
			rhs.ptr_ = null;
		}
		return *this;
	};
	constexpr void swap(c_resource & other) noexcept {
		auto ptr   = ptr_;
		ptr_       = other.ptr_;
		other.ptr_ = ptr;
	}

	static constexpr bool destructible = std::is_invocable_v<destructor, T *> ||
	                                     std::is_invocable_v<destructor, T **>;

	constexpr ~c_resource() noexcept = delete;
	constexpr ~c_resource() noexcept requires destructible {
		destruct(ptr_);
	}
	constexpr void clear() noexcept requires destructible {
		destruct(ptr_);
		ptr_ = null;
	}
	constexpr c_resource & operator=(std::nullptr_t) noexcept {
		clear();
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return ptr_ != null;
	}
	[[nodiscard]] constexpr bool empty() const noexcept {
		return ptr_ == null;
	}

	template <typename U>
	using const_as =
	    std::conditional_t<std::is_const_v<std::remove_reference_t<U>>,
	                       const T *, T *>;
	template <typename U, typename V>
	static constexpr bool less_const = std::is_const_v<U> < std::is_const_v<V>;

	template <typename U, typename Self>
		requires(std::is_same_v<std::remove_cv_t<U>, T> && !less_const<U, Self>)
	[[nodiscard]] constexpr operator U *(this Self && self) noexcept {
		return const_as<Self>{ self.ptr_ };
	}
	[[nodiscard]] constexpr auto operator->(this auto && self) noexcept {
		return const_as<decltype(self)>{ self.ptr_ };
	}
	[[nodiscard]] constexpr auto get(this auto && self) noexcept {
		return const_as<decltype(self)>{ self.ptr_ };
	}

	constexpr void reset(pointer ptr = pointer()) noexcept {
		destruct(ptr_);
		ptr_ = ptr;
	};

	constexpr pointer release() noexcept {
		auto ptr = ptr_;
		ptr_     = null;
		return ptr;
	};

	template <auto * AtEndOfScopeFunction>
		requires(
		    std::is_function_v<
		        std::remove_pointer_t<decltype(AtEndOfScopeFunction)>> &&
		        std::is_invocable_v<decltype(AtEndOfScopeFunction), pointer>)
	struct guard {
		constexpr guard(c_resource & Obj) noexcept
		: ptr_{ Obj.ptr_ } {}
		constexpr ~guard() noexcept {
			if (ptr_ != null)
				AtEndOfScopeFunction(ptr_);
		}

	private:
		pointer ptr_;
	};

private:
	constexpr static void destruct(
	    pointer & p) noexcept requires std::is_invocable_v<destructor, T *> {
		if (p != null)
			d_(p);
	}
	constexpr static void destruct(
	    pointer & p) noexcept requires std::is_invocable_v<destructor, T **> {
		if (p != null)
			d_(&p);
	}
	pointer ptr_ = null;
};

} // namespace stdex

#ifdef UNITTEST

namespace unit_test {
struct S {};
extern "C" {
S * conS1();      // API schema 1: return value
void desS1(S *);  // API schema 1: value as input parameter
void conS2(S **); // API schema 2: reference as input-output parameter
void desS2(S **); // API schema 2: reference as input-output parameter
}
using w1 = stdex::c_resource<S, conS1, desS1>;
using w2 = stdex::c_resource<S, conS2, desS2>;

static_assert(std::is_nothrow_default_constructible_v<w1>);
static_assert(!std::is_copy_constructible_v<w1>);
static_assert(std::is_nothrow_move_constructible_v<w1>);
static_assert(!std::is_copy_assignable_v<w1>);
static_assert(std::is_nothrow_move_assignable_v<w1>);
static_assert(std::is_nothrow_destructible_v<w1>);
static_assert(std::is_nothrow_swappable_v<w1>);

static_assert(std::is_nothrow_default_constructible_v<w2>);
static_assert(!std::is_copy_constructible_v<w2>);
static_assert(std::is_nothrow_move_constructible_v<w2>);
static_assert(!std::is_copy_assignable_v<w2>);
static_assert(std::is_nothrow_move_assignable_v<w2>);
static_assert(std::is_nothrow_destructible_v<w2>);
static_assert(std::is_nothrow_swappable_v<w2>);
} // namespace unit_test

#endif
