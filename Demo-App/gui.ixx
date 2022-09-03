export module gui;

import "c_resource.hpp";
import sdl; // precompiled module, taken from BMI cache
import video;

// wrap the SDL (Simple Directmedia Layer https://www.libsdl.org/) C API types
// and their assorted functions

namespace sdl {
using Window   = stdex::c_resource<SDL_Window, SDL_CreateWindow, SDL_DestroyWindow>;
using Renderer = stdex::c_resource<SDL_Renderer, SDL_CreateRenderer, SDL_DestroyRenderer>;
using Texture  = stdex::c_resource<SDL_Texture, SDL_CreateTexture, SDL_DestroyTexture>;
} // namespace sdl

export namespace gui {
enum width : int {};
enum height : int {};

// the most minimal GUI
// capable of showing a frame with some decor for user interaction
// renders the video frames
struct FancyWindow {
	FancyWindow(width Width, height Height);

	void updateFrom(const video::FrameHeader & Header);
	void present(video::tPixels Pixels);

private:
	sdl::Window Window_;
	sdl::Renderer Renderer_;
	sdl::Texture Texture_;
	int Width_;
	int Height_;
	int PixelsPitch_;
	int SourceFormat_;
};

bool isAlive();

} // namespace gui
