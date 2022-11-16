/* =============================================================================
The server

 - waits for clients to connect at anyone of a list of given endpoints
 - when a client connects, observes a given directory for all files in there,
   repeating this endlessly
 - filters all GIF files which contain a video
 - decodes each video file into individual video frames
 - sends each frame at the correct time to the client
 - sends filler frames if there happen to be no GIF files to process

The client

 - tries to connect to anyone of a list of given server endpoints
 - receives video frames from the network connection
 - presents the video frames in a reasonable manner in a GUI window

The application

 - watches all inputs that the user can interact with for the desire to end
   the application
 - handles timeouts and errors properly and performs a clean shutdown if needed
==============================================================================*/

import std; // precompiled module, taken from BMI cache

import asio; // precompiled module, taken from BMI cache
import executor;
import gui;
import net;
import the.whole.caboodle;

import client;
import events;
import server;

using namespace std::chrono_literals;

static constexpr auto ServerPort        = net::tPort{ 34567 };
static constexpr auto ResolveTimeBudget = 1s;

int main() {
	auto [MediaDirectory, ServerName] = caboodle::getOptions();
	if (MediaDirectory.empty())
		return -2;
	const auto ServerEndpoints =
	    net::resolveHostEndpoints(ServerName, ServerPort, ResolveTimeBudget);
	if (ServerEndpoints.empty())
		return -3;

	asio::io_context ExecutionContext; // we have executors at home
	std::stop_source Stop;             // the mother of all stops
	const auto schedule = executor::makeScheduler(ExecutionContext, Stop);

	const auto Listening =
	    schedule(server::serve, ServerEndpoints, std::move(MediaDirectory));
	if (not Listening)
		return -4;

	schedule(client::showVideos, gui::FancyWindow({ .Width = 1280, .Height = 1024 }),
	         ServerEndpoints);
	schedule(handleEvents::fromTerminal);
	schedule(handleEvents::fromGUI);

	ExecutionContext.run();
}
