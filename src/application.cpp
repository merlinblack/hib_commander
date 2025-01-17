#include "application.h"
#include "lb_application.h"
#include "sdl.h"
#include "timer.h"
#include "rectangle.h"
#include "LuaRef.h"
#include <stdio.h>
#include <memory>
#include <sstream>
#include <unistd.h>

using ManualBind::LuaRef;

Application::Application() : 
	onRaspberry(false),
	onMacMini(false),
	isPictureFrame(false),
	shouldStop(false),
	width(800),
	height(600),
	sdlInitialised(false)
{
	sdl = std::make_shared<SDL>();
	timer = std::make_shared<Timer>();
	scripts = std::make_shared<ScriptManager>();
	telnet = std::make_shared<TelnetConsole>(scripts->getMainLuaState());
	renderList = std::make_shared<RenderList>();
	overlayRenderList = std::make_shared<RenderList>();
}

Application::~Application()
{
}

bool Application::initSDL(bool fullscreen)
{
	isFullscreen = fullscreen;

	bool failed;
	if (isFullscreen)
		sdl->withFullscreenDesktop();

	failed = sdl
		->withVideo()
		->withTimer()
		->withJPG()
		->withPNG()
		->init();

	if (failed)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not init SDL: %s", sdl->getLastErrorMessage().c_str());
		return true;
	}

	failed = sdl->createWindow(
		"Disorganiser",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		width,
		height
		);

	if (failed)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", sdl->getLastErrorMessage().c_str());
		return true;
	}

	SDL_GetRendererOutputSize(const_cast<SDL_Renderer *>(sdl->getRenderer()), &width, &height);

	sdlInitialised = true;

	sdl->clear();
	sdl->present();

	timer->withInterval(10)->start();

	return false;
}

void Application::initLuaAppPtr(ApplicationPtr app)
{
	lua_State* L = scripts->getMainLuaState();
	ApplicationBinding::push(L, app);
	lua_setglobal(L, "app");
}

void Application::loadConfig(std::string configPath)
{
	std::stringstream ss;

	if (configPath.empty()) {
		ss << getenv("HOME");
		ss << "/.config/disorganiser/config.lua";
	}
	else {
		ss << configPath;
	}

	if (scripts->loadFromFile(ss.str()))
	{
		shouldStop = true;
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load configuration.");
		return;
	}

	// Run config file
	scripts->resume();
}

void Application::initSystem()
{
	if (scripts->loadFromFile("scripts/init.lua"))
	{
		shouldStop = true;
	}
}

void Application::shutdown()
{
	telnet->shutdown();
	scripts->shutdown();
	timer->stop();
	sdl->shutdown();
}

void Application::setTextInputMode(bool enable)
{
	if (enable)
	{
		SDL_StartTextInput();
	}
	else
	{
		SDL_StopTextInput();
	}
}

void Application::setShowCursor(bool enable)
{
	SDL_ShowCursor( enable ? SDL_ENABLE : SDL_DISABLE );
}

void Application::handleTextInput(const SDL_Event& event)
{
	LuaRef textInput = scripts->getGlobal("handleTextInput");
	if (textInput.isFunction())
	{
		textInput(event.text.text);
	}
}

void Application::handleKeyUp(const SDL_Event& event)
{
	LuaRef keyUp = scripts->getGlobal("handleKeyUp");
	if (keyUp.isFunction())
	{
		keyUp((int)event.key.keysym.scancode, (char)event.key.keysym.sym);
	}
}

void Application::handleKeyDown(const SDL_Event& event)
{
	LuaRef keyDown = scripts->getGlobal("handleKeyDown");
	if (keyDown.isFunction())
	{
		keyDown((int)event.key.keysym.scancode, (char)event.key.keysym.sym);
	}
}

void Application::handleMouse(const SDL_Event& event)
{
	if (event.button.which == SDL_TOUCH_MOUSEID)
		return;

	LuaRef mouse = scripts->getGlobal("handleMouse");
	if (mouse.isFunction())
	{
		switch (event.type) {
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEMOTION:
				mouse(
					event.type,
					event.button.x,
					event.button.y,
					event.button.button,
					event.button.state,
					event.button.clicks,
					event.button.which
				);
				break;
		}
	}
}

void Application::handleTouch(const SDL_Event& event)
{
	LuaRef touch = scripts->getGlobal("handleTouch");
	if (touch.isFunction())
	{
		touch(
			event.type,
			event.tfinger.x,
			event.tfinger.y,
			event.tfinger.dx,
			event.tfinger.dy
		);
	}
}

void Application::handleTimer(const SDL_Event& event)
{
	/**
	 * Run next task coroutine
	 */
	scripts->resume();

	/**
	 * Process telnet connections
	*/
	telnet->process();
}

void Application::render()
{
	if (renderList->shouldRender()||overlayRenderList->shouldRender())
	{
		sdl->clear();
		renderList->render(sdl->getRenderer());
		overlayRenderList->render(sdl->getRenderer());
		sdl->present();
	}
}

void Application::dispatchEvent(const SDL_Event& event)
{
	switch (event.type)
	{
	case SDL_QUIT:
		shouldStop = true;
		break;
	/*
	TODO: figure out what window event we should trigger rendering on, after being minimized.
	case SDL_WINDOWEVENT_SHOWN:
		shouldRender = true;
		break;
	*/
	case SDL_TEXTINPUT:
		handleTextInput(event);
		break;
	case SDL_KEYUP:
		handleKeyUp(event);
		break;
	case SDL_KEYDOWN:
		handleKeyDown(event);
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEMOTION:
	case SDL_MOUSEWHEEL:
		handleMouse(event);
		break;
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
	case SDL_FINGERMOTION:
		handleTouch(event);
		break;
	case SDL_USEREVENT:
		handleTimer(event);
		break;
	default:
		//PrintEvent(&event);
		break;
	}
}

void Application::eventLoop()
{
	SDL_Event event;

	while (!shouldStop)
	{
		if (SDL_WaitEvent(&event))
		{
			do 
			{
				try 
				{
					dispatchEvent(event);
				}
				catch (ManualBind::LuaException& gotMooned)
				{
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", gotMooned.what());
				}
			} 
			while (SDL_PollEvent(&event));

			render();
		}
		else
		{
			SDL_Log( "There was a problem waiting for events: %s", SDL_GetError());
		}
	}
}

// Get a placeholder texture that will soon be replaced.
TexturePtr Application::getEmptyTexture()
{
	if (!emptyTexture)
	{
		emptyTexture = std::make_shared<Texture>();
		emptyTexture->createEmpty(sdl->getRenderer());
	}

	return emptyTexture;
}
