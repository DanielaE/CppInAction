module gui;

namespace gui {

static constexpr auto TextureFormat = SDL_PIXELFORMAT_ARGB8888;

FancyWindow::FancyWindow(width Width, height Height) {
	SDL_Init(SDL_INIT_VIDEO);
	Window_   = { "Look at me!",
		          SDL_WINDOWPOS_CENTERED,
		          SDL_WINDOWPOS_CENTERED,
		          Width,
		          Height,
		          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN };
	Renderer_ = { Window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC };

	SDL_SetWindowMinimumSize(Window_, Width, Height);
	SDL_RenderSetLogicalSize(Renderer_, Width, Height);
	SDL_RenderSetIntegerScale(Renderer_, SDL_TRUE);
	SDL_SetRenderDrawColor(Renderer_, 240, 240, 240, 240);
}

void FancyWindow::updateFrom(const video::FrameHeader & Header) {
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

void FancyWindow::present(video::tPixels Pixels) {
	void * TextureData;
	int TexturePitch;

	SDL_RenderClear(Renderer_);
	if (0 == SDL_LockTexture(Texture_, nullptr, &TextureData, &TexturePitch)) {
		SDL_ConvertPixels(Width_, Height_, SourceFormat_, Pixels.data(), PixelsPitch_,
		                  TextureFormat, TextureData, TexturePitch);
		SDL_UnlockTexture(Texture_);
		SDL_RenderCopy(Renderer_, Texture_, nullptr, nullptr);
	}
	SDL_RenderPresent(Renderer_);
}

bool processEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT)
			return false;
	}
	return true;
}

} // namespace gui
