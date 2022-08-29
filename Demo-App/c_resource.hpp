#pragma once
#include <concepts>
#include <type_traits>

// Wrap C-style 'things' that are allocated in dynamic memory and their related APIs
//
// Instead of passing around (const) 'thing' pointers, and manually constructing or
// destructing them, provide proper C++ 'thing' object types with move-only value
// semantics that bundles the respective managing functions from the C-style API. These
// wrappers act as drop-in replacements with correct constness and uniqueness wherever
// formerly the bare 'thing' pointers were used. The API sembles for the most part a
// std::unique_ptr<thing> but goes beyond that in terms of semantic correctness and
// convenience.

namespace stdex {

// custumization point to support different 'null' values
template <typename T>
constexpr inline T * c_resource_null_value = nullptr;

// constructors and destructors support two API schemata of the underlying managing
// functions:
//
// schema 1:
// constructor functions return the constructed entity as an output value,
//   e.g. thing * construct();
// destructor functions take the disposable entity by an input value,
//   e.g. void destruct(thing *);
//
// schema 2:
// both constructor and destructor functions take an input-output reference to the thing
// pointer,
//   e.g. void construct(thing **);
//        void destruct(thing **);
// and modify the referenced thing pointer accordingly. If the constructor functions for
// some call signatures are non-void, no constructors exist for these signatures.
//
// modifiers,
//   e.g. replace(...);
// exist only for schema 2 and act like constructors but return the return value of the
// underlying construct function,
//   e.g. auto construct(thing **);

template <typename T, auto * ConstructFunction, auto * DestructFunction>
struct c_resource {
	using pointer       = T *;
	using const_pointer = std::add_const_t<T> *;
	using element_type  = T;

private:
	using Constructor = decltype(ConstructFunction);
	using Destructor  = decltype(DestructFunction);

	static_assert(std::is_function_v<std::remove_pointer_t<Constructor>>,
	              "I need a C function");
	static_assert(std::is_function_v<std::remove_pointer_t<Destructor>>,
	              "I need a C function");

	static constexpr Constructor construct = ConstructFunction;
	static constexpr Destructor destruct   = DestructFunction;
	static constexpr T * null              = c_resource_null_value<T>;

	struct construct_t {};

public:
	static constexpr construct_t constructed = {};

	[[nodiscard]] constexpr c_resource() noexcept = default;
	[[nodiscard]] constexpr explicit c_resource(
	    construct_t) noexcept requires std::is_invocable_r_v<T *, Constructor>
	: ptr_{ construct() } {}

	template <typename... Ts>
		requires(sizeof...(Ts) > 0 && std::is_invocable_r_v<T *, Constructor, Ts...>)
	[[nodiscard]] constexpr explicit(sizeof...(Ts) == 1)
	    c_resource(Ts &&... Args) noexcept
	: ptr_{ construct(static_cast<Ts &&>(Args)...) } {}

	template <typename... Ts>
		requires(sizeof...(Ts) > 0 && requires(T * p, Ts... Args) {
			{ construct(&p, Args...) } -> std::same_as<void>;
		})
	[[nodiscard]] constexpr explicit(sizeof...(Ts) == 1)
	    c_resource(Ts &&... Args) noexcept
	: ptr_{ null } {
		construct(&ptr_, static_cast<Ts &&>(Args)...);
	}

	template <typename... Ts>
		requires(std::is_invocable_v<Constructor, T **, Ts...>)
	[[nodiscard]] constexpr auto emplace(Ts &&... Args) noexcept {
		_destruct(ptr_);
		ptr_ = null;
		return construct(&ptr_, static_cast<Ts &&>(Args)...);
	}

	[[nodiscard]] constexpr c_resource(c_resource && other) noexcept {
		ptr_       = other.ptr_;
		other.ptr_ = null;
	};
	constexpr c_resource & operator=(c_resource && rhs) noexcept {
		if (this != &rhs) {
			_destruct(ptr_);
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

	static constexpr bool destructible =
	    std::is_invocable_v<Destructor, T *> || std::is_invocable_v<Destructor, T **>;

	constexpr ~c_resource() noexcept = delete;
	constexpr ~c_resource() noexcept requires destructible { _destruct(ptr_); }
	constexpr void clear() noexcept requires destructible {
		_destruct(ptr_);
		ptr_ = null;
	}
	constexpr c_resource & operator=(std::nullptr_t) noexcept {
		clear();
		return *this;
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return ptr_ != null;
	}
	[[nodiscard]] constexpr bool empty() const noexcept { return ptr_ == null; }
	[[nodiscard]] constexpr friend bool have(const c_resource & r) noexcept {
		return r.ptr_ != null;
	}

#if defined(__cpp_explicit_this_parameter)
	template <typename U, typename V>
	static constexpr bool less_const = std::is_const_v<U> < std::is_const_v<V>;
	template <typename U, typename V>
	static constexpr bool similar = std::is_same_v<std::remove_const_t<U>, T>;

	template <typename U, typename Self>
		requires(similar<U, T> && !less_const<U, Self>)
	[[nodiscard]] constexpr operator U *(this Self && self) noexcept {
		return like(self);
	}
	[[nodiscard]] constexpr auto operator->(this auto && self) noexcept {
		return like(self);
	}
	[[nodiscard]] constexpr auto get(this auto && self) noexcept {
		return like(self);
	}
#else
	[[nodiscard]] constexpr operator pointer() noexcept {
		return like(*this);
	}
	[[nodiscard]] constexpr operator const_pointer() const noexcept {
		return like(*this);
	}
	[[nodiscard]] constexpr pointer operator->() noexcept {
		return like(*this);
	}
	[[nodiscard]] constexpr const_pointer operator->() const noexcept {
		return like(*this);
	}
	[[nodiscard]] constexpr pointer get() noexcept {
		return like(*this);
	}
	[[nodiscard]] constexpr const_pointer get() const noexcept {
		return like(*this);
	}
#endif

	constexpr void reset(pointer ptr = null) noexcept {
		_destruct(ptr_);
		ptr_ = ptr;
	};

	constexpr pointer release() noexcept {
		auto ptr = ptr_;
		ptr_     = null;
		return ptr;
	};

	template <auto * CleanupFunction>
	struct guard {
		using cleaner = decltype(CleanupFunction);

		static_assert(std::is_function_v<std::remove_pointer_t<cleaner>>,
		              "I need a C function");
		static_assert(std::is_invocable_v<cleaner, pointer>, "Please check the function");

		constexpr guard(c_resource & Obj) noexcept
		: ptr_{ Obj.ptr_ } {}
		constexpr ~guard() noexcept {
			if (ptr_ != null)
				CleanupFunction(ptr_);
		}

	private:
		pointer ptr_;
	};

private:
	constexpr static void
	_destruct(pointer & p) noexcept requires std::is_invocable_v<Destructor, T *> {
		if (p != null)
			destruct(p);
	}
	constexpr static void
	_destruct(pointer & p) noexcept requires std::is_invocable_v<Destructor, T **> {
		if (p != null)
			destruct(&p);
	}

	static constexpr auto like(c_resource & self) noexcept {
		return self.ptr_;
	}
	static constexpr auto like(const c_resource & self) noexcept {
		return static_cast<const_pointer>(self.ptr_);
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
