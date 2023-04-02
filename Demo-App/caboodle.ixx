export module the.whole.caboodle;
import std;

import boost.program_options; // precompiled module, taken from BMI cache

namespace caboodle {

// fs::path::string() has unspecified encoding on Windows.
// convert from UTF16 to UTF8 with guaranteed semantics.
export auto utf8Path(const std::filesystem::path & Path) -> std::string;

//------------------------------------------------------------------------------

boost::program_options::variables_map parseOptions();

export auto getOptions() {
	const auto Option = parseOptions();
	return std::tuple{ Option["media"].as<std::string>(),
		               Option["server"].as<std::string>() };
}

} // namespace caboodle

module :private; // names invisible, declarations unreachable!

#ifdef _WIN32
#	define APICALL __declspec(dllimport) __stdcall
static constexpr bool Windows = true;
#else
#	define APICALL
static constexpr bool Windows = false;
#endif

namespace winapi {

extern "C" {
int APICALL WideCharToMultiByte(unsigned, unsigned long, const wchar_t *, int, char *,
                                int, const char *, int *);
}
static constexpr auto UTF8 = 65001;

static inline auto estimateNarrowSize(std::wstring_view U16) noexcept -> std::size_t {
	return WideCharToMultiByte(UTF8, 0, U16.data(), static_cast<int>(U16.size()), nullptr,
	                           0, nullptr, nullptr);
}
static inline auto convertFromWide(std::wstring_view U16) noexcept {
	return [&](char * Buffer, std::size_t Size) -> std::size_t {
		WideCharToMultiByte(UTF8, 0, U16.data(), static_cast<int>(U16.size()), Buffer,
		                    static_cast<int>(Size), nullptr, nullptr);
		return Size;
	};
}

template <typename String>
concept canResizeAndOverwrite =
    requires(String Str, std::size_t Size, std::size_t (*Callable)(char *, std::size_t)) {
	    { Str.resize_and_overwrite(Size, Callable) };
    };

template <canResizeAndOverwrite String = std::string>
decltype(auto) toUTF8(std::wstring_view Utf16, String && Utf8 = {}) {
	Utf8.resize_and_overwrite(estimateNarrowSize(Utf16), convertFromWide(Utf16));
	return static_cast<String &&>(Utf8);
}
} // namespace winapi

namespace caboodle {

auto utf8Path(const std::filesystem::path & Path) -> std::string {
	if constexpr (Windows)
		return winapi::toUTF8(Path.wstring());
	else
		return Path.string();
}

namespace po = boost::program_options;
using namespace po::ext;

auto parseOptions() -> po::variables_map {
	po::options_description OptionsDescription("Options available");
	// clang-format off
	OptionsDescription.add_options()
		("help", "produce help message")
		("media", po::value<std::string>()->default_value("media"), "media directory")
		("server", po::value<std::string>()->default_value(""), "server name or ip")
		;
	// clang-format on
	po::positional_options_description PositionalOptions;
	PositionalOptions.add("media", 1).add("server", 2);

	po::variables_map Option;
	bool needHelp = false;
	try {
		Option = parseCommandline(OptionsDescription, PositionalOptions);
	} catch (...) {
		needHelp = true;
	}
	if (Option.count("help") || Option["media"].as<std::string>().contains('?'))
		needHelp = true;

	if (needHelp) {
		std::println("{}", getHelpText(OptionsDescription));
		exit(-1);
	} else {
		return Option;
	}
}

} // namespace caboodle
