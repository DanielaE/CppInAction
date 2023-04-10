module the.whole.caboodle;
import std;

namespace caboodle {

// filenames have no guaranteed character encoding in POSIX.
// convert from platform specific encoding to UTF-8 (done by the standard library)
auto utf8Path(const std::filesystem::path & Path) -> std::string {
	const auto u8Filename = Path.u8string();
	std::string Filename;
	Filename.resize_and_overwrite(u8Filename.size(),
	                              [&](char * Str, std::size_t Length) -> std::size_t {
		                              if (Length != u8Filename.size())
			                              return 0;
		                              for (const auto Char : u8Filename)
			                              *(Str++) = static_cast<char>(Char);
		                              return Length;
	                              });
	return Filename;
}

} // namespace caboodle
