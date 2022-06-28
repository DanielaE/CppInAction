module;

#include <filesystem>
#include <print>
#include <ranges>
#include <string>

export module caboodle;
import boost.program_options;

namespace fs  = std::filesystem;
namespace rgs = std::ranges;
namespace vws = rgs::views;

namespace caboodle {

//--------------------------------------------------------------------------------------------

export struct EternalDirectoryIterator : rgs::view_base {
	using base              = fs::directory_iterator;
	using iterator_category = std::input_iterator_tag;
	using difference_type   = ptrdiff_t;
	using value_type        = fs::path;
	using pointer           = const value_type *;
	using reference         = const value_type &;

	static constexpr auto Options = fs::directory_options::skip_permission_denied;
	struct Sentinel {};

	EternalDirectoryIterator() noexcept = default;
	explicit EternalDirectoryIterator(fs::path Dir) noexcept
	: Directory_{ std::move(Dir) } {
		Iter_  = restart(Directory_);
		Error_ = Iter_ == End_;
	}

	bool operator==(const EternalDirectoryIterator & rhs) const noexcept { return Iter_ == rhs.Iter_; }
	bool operator==(Sentinel) const noexcept { return false; }

	fs::path operator*() const noexcept {
		if (Error_)
			return {};
		return Iter_->path();
	}

	EternalDirectoryIterator & operator++() {
		std::error_code ec;
		if (Iter_ == End_ || Iter_.increment(ec) == End_)
			Iter_ = restart(Directory_);
		Error_ = ec || Iter_ == End_;
		return *this;
	}

	EternalDirectoryIterator & operator++(int); // just declare to satisfy std::weakly_incementable

	friend inline EternalDirectoryIterator begin(EternalDirectoryIterator it) {
		return static_cast<EternalDirectoryIterator &&>(it);
	}
	friend inline Sentinel end(EternalDirectoryIterator) { return {}; }

private:
	static base restart(const fs::path & Directory) {
		std::error_code err;
		return base{ Directory, Options, err };
	}

	fs::path Directory_;
	base Iter_;
	base End_;
	bool Error_ = false;
};

} // namespace caboodle

template <>
inline constexpr bool rgs::enable_borrowed_range<caboodle::EternalDirectoryIterator> = true;

static_assert(rgs::range<caboodle::EternalDirectoryIterator>);
static_assert(rgs::viewable_range<caboodle::EternalDirectoryIterator>);
static_assert(rgs::borrowed_range<caboodle::EternalDirectoryIterator>);

//--------------------------------------------------------------------------------------------

namespace caboodle {

#ifdef _WIN32
extern "C" {
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned, unsigned long, const wchar_t *, int, char *, int,
                                                        const char *, int *);
}
#	pragma comment(lib, "kernel32")
namespace winapi {
static constexpr auto UTF8 = 65001;
static constexpr auto NFC  = 1;

export template <typename Str = std::string>
	requires(std::is_same_v<typename Str::value_type, char> && !std::is_const_v<Str> &&
	         requires(Str s, size_t r, size_t (*f)(char *, size_t)) { { s.resize_and_overwrite(r, f) }; })
decltype(auto) toUTF8(std::wstring_view Utf16, Str && Result = {}) noexcept {
	Result.resize_and_overwrite(
	    WideCharToMultiByte(UTF8, 0, Utf16.data(), static_cast<int>(Utf16.size()), nullptr, 0, nullptr, nullptr),
	    [&](char * Buffer, size_t Size) {
		    WideCharToMultiByte(UTF8, 0, Utf16.data(), static_cast<int>(Utf16.size()), Buffer, static_cast<int>(Size),
		                        nullptr, nullptr);
		    return Size;
	    });
	return static_cast<Str &&>(Result);
}
} // namespace winapi
#endif

//--------------------------------------------------------------------------------------------

export std::string u8Path(const fs::path & Path) {
	if constexpr (_WIN32)
		return winapi::toUTF8(Path.native());
	else
		return Path.native();
}

//--------------------------------------------------------------------------------------------

export auto getOptions() {
	namespace po = boost::program_options;
	po::options_description OptionsDescription("Options available");
	// clang-format off
	OptionsDescription.add_options()
		("help", "produce help message")
		("media", po::value<std::string>()->default_value("media"), "media directory")
		("server", po::value<std::string>()->default_value(""), "server name or ip")
		;
	// clang-format on
	po::variables_map Option;
	po::ext::parseCommandline(Option, OptionsDescription);
	if (Option.count("help")) {
		std::println("{}", po::ext::getHelpText(OptionsDescription));
		exit(-1);
	}
	return std::make_tuple(Option["media"].as<std::string>(), Option["server"].as<std::string>());
}

} // namespace caboodle
