module the.whole.caboodle;
import std;

import argparse;

namespace caboodle {

auto getOptions(int argc, char * argv[]) -> tOptions {
	argparse::ArgumentParser Options("Demo application", "",
	                                 argparse::default_arguments::help, false);
	Options.add_argument("media", "-m", "--media")
	    .help("media directory")
	    .default_value("media");
	Options.add_argument("server", "-s", "--server")
	    .help("server name or ip")
	    .default_value("");

	bool needHelp = true;
	try {
		Options.parse_args(argc, argv);
		needHelp = Options.get<bool>("--help");
	} catch (...) { /* print help */
	}
	if (Options.get("media").contains('?'))
		needHelp = true;

	if (needHelp) {
		std::println("{}", Options.help().str());
		exit(-1);
	}
	return { .Media  = std::move(Options).get("media"),
		     .Server = std::move(Options).get("server") };
}

} // namespace caboodle
