module;

#ifndef _WIN32
#	error this is not Windows!
#endif

module the.whole.caboodle;
import std;

namespace winapi {

#define APICALL __declspec(dllimport) __stdcall

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

// fs::path::string() has unspecified encoding on Windows.
// convert from UTF16 to UTF8 with guaranteed semantics.
auto utf8Path(const std::filesystem::path & Path) -> std::string {
	return winapi::toUTF8(Path.wstring());
}

} // namespace caboodle
