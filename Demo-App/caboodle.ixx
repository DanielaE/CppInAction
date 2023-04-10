export module the.whole.caboodle;
import std;

namespace caboodle {

export auto utf8Path(const std::filesystem::path & Path) -> std::string;

struct tOptions {
	std::string Media;
	std::string Server;
};

export auto getOptions(int argc, char * argv[]) -> tOptions;

} // namespace caboodle
