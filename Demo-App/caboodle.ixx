module;
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <tuple>

export module the.whole.caboodle;
import boost.program_options; // precompiled module, taken from BMI cache

namespace caboodle {

// fs::path::string() has unspecified encoding on Windows
// convert from UTF16 to UTF8 with guaranteed semantics
export std::string utf8Path(const std::filesystem::path & Path);

//------------------------------------------------------------------------------

boost::program_options::variables_map parseOptions();

export auto getOptions() {
	const auto Option = parseOptions();
	return std::tuple{ Option["media"].as<std::string>(),
		               Option["server"].as<std::string>() };
}

} // namespace caboodle

module : private; // names invisible, declarations unreachable!

using namespace std; // bad practice!

#ifdef _WIN32
#	define APICALL __declspec(dllimport) __stdcall
#else
#	define APICALL
#endif

namespace winapi {

extern "C" {
int APICALL WideCharToMultiByte(unsigned, unsigned long, const wchar_t *, int,
                                char *, int, const char *, int *);
}
static constexpr auto UTF8 = 65001;

static inline size_t estimateNarrow(wstring_view U16) noexcept {
	return WideCharToMultiByte(UTF8, 0, U16.data(),
	                           static_cast<int>(U16.size()), nullptr, 0,
	                           nullptr, nullptr);
}
static inline auto convertFromWide(wstring_view U16) noexcept {
	return [&](char * Buffer, size_t Size) -> size_t {
		WideCharToMultiByte(UTF8, 0, U16.data(), static_cast<int>(U16.size()),
		                    Buffer, static_cast<int>(Size), nullptr, nullptr);
		return Size;
	};
}

template <typename Str = string>
	requires(requires(Str s, size_t r, size_t (*f)(char *, size_t)) {
		{ s.resize_and_overwrite(r, f) };
	})
decltype(auto) toUTF8(wstring_view Utf16, Str && Utf8 = {}) {
	Utf8.resize_and_overwrite(estimateNarrow(Utf16), convertFromWide(Utf16));
	return static_cast<Str &&>(Utf8);
}
} // namespace winapi

namespace caboodle {

string utf8Path(const filesystem::path & Path) {
	if constexpr (_WIN32 + 0)
		return winapi::toUTF8(Path.wstring());
	else
		return Path.string();
}

namespace po = boost::program_options;
using namespace po::ext;

po::variables_map parseOptions() {
	po::options_description OptionsDescription("Options available");
	// clang-format off
	OptionsDescription.add_options()
		("help", "produce help message")
		("media", po::value<string>()->default_value("media"), "media directory")
		("server", po::value<string>()->default_value(""), "server name or ip")
		;
	// clang-format on
	po::positional_options_description PositionalOptions;
	PositionalOptions.add("media", 1).add("server", 2);

	po::variables_map Option;
	bool needHelp = false;
	try {
		Option = parseCommandline(OptionsDescription, PositionalOptions);
	} catch (...) { needHelp = true; }
	if (Option.count("help") || Option["media"].as<string>().contains('?'))
		needHelp = true;

	if (needHelp) {
		println("{}", getHelpText(OptionsDescription));
		exit(-1);
	} else {
		return Option;
	}
}

} // namespace caboodle
