module gui;

import std;

#ifdef __clang__
// Clang bug: missing/bad implementation of [module.import]/7:
// the names from non-reexported modules must be visible
import sdl;
import video;
#endif

namespace gui {

static const auto initializedSDL    = SDL_Init(SDL_INIT_VIDEO);
static constexpr auto TextureFormat = SDL_PIXELFORMAT_ARGB8888;

static constexpr bool successful(int Code) {
	return Code == 0;
}

static auto centeredBox(tDimensions Dimensions,
                        int Monitor = SDL_GetNumVideoDisplays()) noexcept {
	struct {
		int x = SDL_WINDOWPOS_CENTERED;
		int y = SDL_WINDOWPOS_CENTERED;
		int Width;
		int Height;
	} Box{ .Width = Dimensions.Width, .Height = Dimensions.Height };

	if (SDL_Rect Display;
	    Monitor > 0 and successful(SDL_GetDisplayBounds(Monitor - 1, &Display))) {
		Box.Width  = std::min(Display.w, Box.Width);
		Box.Height = std::min(Display.h, Box.Height);
		Box.x      = Display.x + (Display.w - Box.Width) / 2;
		Box.y      = Display.y + (Display.h - Box.Height) / 2;
	}
	return Box;
}

FancyWindow::FancyWindow(tDimensions Dimensions) noexcept {
	const auto Viewport = centeredBox(Dimensions);

	Window_   = { "Look at me!", // clang-format off
		          Viewport.x, Viewport.y, Viewport.Width, Viewport.Height, // clang-format on
		          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN };
	Renderer_ = { Window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC };

	SDL_SetWindowMinimumSize(Window_, Viewport.Width, Viewport.Height);
	SDL_RenderSetLogicalSize(Renderer_, Viewport.Width, Viewport.Height);
	SDL_RenderSetIntegerScale(Renderer_, SDL_TRUE);
	SDL_SetRenderDrawColor(Renderer_, 240, 240, 240, 240);
}

void FancyWindow::updateFrom(const video::FrameHeader & Header) noexcept {
	if (not Header.isFirstFrame())
		return;

	if (Header.hasNoPixels()) {
		SDL_HideWindow(Window_);
		Texture_ = {};
	} else {
		Width_        = Header.Width_;
		Height_       = Header.Height_;
		PixelsPitch_  = Header.LinePitch_;
		SourceFormat_ = Header.Format_ == std::to_underlying(video::PixelFormat::RGBA)
		                    ? SDL_PIXELFORMAT_ABGR8888
		                    : SDL_PIXELFORMAT_ARGB8888;
		Texture_ = sdl::Texture(Renderer_, TextureFormat, SDL_TEXTUREACCESS_STREAMING,
		                        Width_, Height_);
		SDL_SetWindowMinimumSize(Window_, Width_, Height_);
		SDL_RenderSetLogicalSize(Renderer_, Width_, Height_);
		SDL_ShowWindow(Window_);
	}
}

void FancyWindow::present(video::tPixels Pixels) noexcept {
	void * TextureData;
	int TexturePitch;

	SDL_RenderClear(Renderer_);
	if (successful(SDL_LockTexture(Texture_, nullptr, &TextureData, &TexturePitch))) {
		SDL_ConvertPixels(Width_, Height_, SourceFormat_, Pixels.data(), PixelsPitch_,
		                  TextureFormat, TextureData, TexturePitch);
		SDL_UnlockTexture(Texture_);
		SDL_RenderCopy(Renderer_, Texture_, nullptr, nullptr);
	}
	SDL_RenderPresent(Renderer_);
}

bool isAlive() noexcept {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT)
			return false;
	}
	return true;
}

} // namespace gui
