#include <stdexcept>
#include <iostream>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/XKBlib.h>

#include "debug.hpp"
#include "winmgr.hpp"
#include "utils.hpp"

bool _hadError = false;

int xerrorhandler(Display *dsp, XErrorEvent *error)
{
	char errorstring[128];
	XGetErrorText(dsp, error->error_code, errorstring, 128);
 
	cerr_line << "Xka umrely: " << errorstring << std::endl;
	std::cout << "DED" << std::endl;
	_hadError = true;

	return False;
}

// Vrati true pokud nastala X chyba a nastavi flag chyby na false
bool hadError()
{
	bool ret = _hadError;
	_hadError = false;
	return ret;
}

GawmWindowManager::GawmWindowManager()
{
	display = XOpenDisplay(nullptr);
	if (!display)
	{
		throw std::runtime_error("Nepodarilo se otevrit display!");
	}

	XSynchronize(display, True); // Synchronizace s xserverem pro debugování
	XSetErrorHandler(xerrorhandler);

	screen = DefaultScreen(display);
	rootWindow = DefaultRootWindow(display);
	overlayWindow = XCompositeGetOverlayWindow(display, rootWindow);
	
	XCompositeRedirectSubwindows(display, rootWindow, CompositeRedirectManual);
	setupEvents();
	initFbConfig();
	initWindow();
	initKnownWindows();
	initGL();
    allowInputPassthrough(overlayWindow);
	allowInputPassthrough(outputWindow);
	
	dbg_out << "GawmWindowManager: Screen: " << screen << ", rootWindow: " << rootWindow << ", overlayWindow: " << overlayWindow
			<< ", GL window: " << outputWindow << std::endl;
}

GawmWindowManager::~GawmWindowManager()
{
	destroyGL();
	destroyWindow();
	XCompositeReleaseOverlayWindow(display, rootWindow);
	XCloseDisplay(display);
}

void GawmWindowManager::render()
{
	// pozadi plochy
	glClearColor(0.25, 0.25, 0.25, 1.0);
	glClear(GL_COLOR_BUFFER_BIT); // FIXME: Způsobuje leaky!
	glEnable(GL_TEXTURE_2D);
	
	std::vector<Window> vadnaOkna;
	
	for (auto it=sortedWindows.rbegin(); it!=sortedWindows.rend(); ++it)
	{
		(*it)->render(zoom);
		if (hadError()) vadnaOkna.push_back((*it)->window);
	}
	
	for (auto vadneOkno : vadnaOkna)
	{
		eraseWindow(vadneOkno);
	}
	
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR)
	{
		cerr_line << "GL error: " << err << std::endl;
	}
	
	glXSwapBuffers(display, outputWindow);
}

void GawmWindowManager::setupEvents()
{
	int modlist[] = {ShiftMask, LockMask, ControlMask, Mod1Mask, Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask};
	int scrolllock = 0;
	int numlock = 0;
	auto modmap = XGetModifierMapping(display);
	UTILS_SCOPE_EXIT([&]{XFreeModifiermap(modmap);});

	// Najdeme modifikatory scrolllock a numlock 
	// Prakticky zkopirovano z Fluxboxu
	for (int i=0, realkey=0; i<8; ++i)
	{
		for (int key=0; key < modmap->max_keypermod; ++key, ++realkey)
		{
			if (modmap->modifiermap[realkey] == 0)
			{
				continue;
			}    

			KeySym ks = XkbKeycodeToKeysym(display, modmap->modifiermap[realkey], 0, 0);

			switch (ks) 
			{
				case XK_Scroll_Lock:
					scrolllock = modlist[i];
					break;
				case XK_Num_Lock:
					numlock = modlist[i];
					break;
			}
		}
	}
	
	// Odchytavani klaves pro Window manager
	escapeKey = XKeysymToKeycode(display, XStringToKeysym("Escape"));
	XGrabKey(display, escapeKey, AnyModifier, rootWindow, True, GrabModeAsync, GrabModeAsync); 
	
	std::cout << "numlock: " << numlock << ", scrolllock: " << scrolllock << std::endl;
	
	int buttony[] = {Button1, Mod4Mask, Button4, Mod4Mask, Button5, Mod4Mask, Button1, Mod1Mask};
	for (unsigned i = 0; i < sizeof(buttony)/sizeof(int)/2; i++)
	{
		int button = buttony[i*2];
		int mask = buttony[i*2 + 1];
		for (int j = 0; j < 8; j++)
		{
			XGrabButton(display, button, 
			mask | ((j & 1) ? LockMask : 0) | ((j & 2) ? numlock : 0) | ((j & 4) ? scrolllock : 0), 
			rootWindow, True, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
		}
	}
	XSelectInput(display, rootWindow, SubstructureNotifyMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask);
}

void GawmWindowManager::initFbConfig()
{
	int glDisplayAttribs[] =
	{
		GLX_DRAWABLE_TYPE,	GLX_WINDOW_BIT,
		GLX_RENDER_TYPE,	GLX_RGBA_BIT,
		GLX_RED_SIZE,		8,
		GLX_GREEN_SIZE, 	8,
		GLX_BLUE_SIZE, 		8,
		GLX_ALPHA_SIZE, 	8,
		GLX_DEPTH_SIZE, 	24,
		GLX_STENCIL_SIZE, 	8,
		GLX_DOUBLEBUFFER, 	True,
		None
	};

	int configCount;

	auto fbConfigs = glXChooseFBConfig(display, screen, glDisplayAttribs, &configCount);
	if (!fbConfigs)
	{
		throw std::runtime_error("Nenalezena zadna konfigurace framebufferu odpovidajici atributum glDisplayAttribs! (Nepodporovana graficka karta?)");
	}
	fbConfig = fbConfigs[0]; // proste berem prvni konfiguraci
	XFree(fbConfigs);

}

void GawmWindowManager::initWindow()
{
	// Podle konfigurace vytvorime okno
	auto visualInfo = glXGetVisualFromFBConfig(display, fbConfig);
	XSetWindowAttributes windowAttribs;
	windowAttribs.background_pixmap = None;
	windowAttribs.border_pixel = 0;
	windowAttribs.colormap = XCreateColormap(display, overlayWindow, visualInfo->visual, AllocNone);

	XGetWindowAttributes(display, overlayWindow, &overlayWindowAttribs);

	maxX = overlayWindowAttribs.width - 1;
	maxY = overlayWindowAttribs.height - 1;

	outputWindow = XCreateWindow(
		display, overlayWindow,
		0, 0,
		overlayWindowAttribs.width, overlayWindowAttribs.height,
		0, visualInfo->depth, InputOutput, visualInfo->visual,
		CWBorderPixel | CWColormap, &windowAttribs);
	if (!outputWindow)
	{
		throw std::runtime_error("Nelze vytvorit okno. Bug?");
	}

	XFree(visualInfo);

	XStoreName(display, outputWindow, "OH GOD GAWM");
	XMapWindow(display, outputWindow);
}

void GawmWindowManager::destroyWindow()
{
	XFreeColormap(display, windowAttribs.colormap);
	XDestroyWindow(display, outputWindow);
}

void GawmWindowManager::initGL()
{
	// Vytvoreni OpenGL kontextu
	auto ctx = glXCreateNewContext(display, fbConfig, GLX_RGBA_TYPE, nullptr, True);
	XSync(display, False);
	if (!ctx)
	{
		throw std::runtime_error("Nepodarilo se vytvorit OpenGL kontext!");
	}
	glXMakeCurrent(display, outputWindow, ctx);
	initGlFunctions();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslated(-1.0, 1.0, 0.0);
	glScaled( 2.0 / overlayWindowAttribs.width, -2.0 / overlayWindowAttribs.height, 1.0);
}

void GawmWindowManager::destroyGL()
{
	glXDestroyContext(display, ctx);
    displayGlErrors();
}

void GawmWindowManager::allowInputPassthrough(Window window)
{
	XserverRegion region = XFixesCreateRegion(display, NULL, 0);
	XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, 0);
	XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region); // FIXME: Pokud je toto odkomentováno, přestanou se odchytávat klávesy.
	XFixesDestroyRegion(display, region);
    
	// experiment2
	//XGrabPointer(display, overlayWindow, True /*owner_events - proverit*/, 0/*event_mask*/, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	//XGrabButton(display, AnyButton, AnyModifier, overlayWindow, True /*owner_events - proverit*/, 0/*event_mask*/, GrabModeAsync, GrabModeAsync, None, None);
	
}

bool GawmWindowManager::isKnownWindow(Window window)
{
	TKnownWindowsMap::iterator it = knownWindows.find(window);
	if ( it != knownWindows.end() ) {
		return true;
	}
	else {
		return false;
	}
}

GawmWindow* GawmWindowManager::getHighestWindow()
{
	if(!sortedWindows.empty()){
		return sortedWindows.front();
	}else{
		return NULL;
	}
}

GawmWindow* GawmWindowManager::getHighestWindowAtLocation(int lX, int lY)
{
	for (auto sortedWindow : sortedWindows)
	{
		if(sortedWindow->containsPoint(lX, lY))
		{
			return sortedWindow;
		}
	}
	return NULL;
}

void GawmWindowManager::insertWindow(Window window, int x, int y, int width, int height)
{
	GawmWindow *w = new GawmWindow(display, screen, window, x, y, width, height);
	knownWindows.insert(window, w);
	sortedWindows.push_front(w);
	
	if (hadError()) eraseWindow(window);
}

void GawmWindowManager::eraseWindow(Window window)
{
	GawmWindow *w = &knownWindows.at(window);
	knownWindows.erase(window);
	sortedWindows.remove(w);
	
	hadError();
}

void GawmWindowManager::configureWindow(Window window, int newX, int newY, int newWidth, int newHeight)
{
	knownWindows.at(window).configure(newX, newY, newWidth, newHeight);
	
	if (hadError()) eraseWindow(window);
}

void GawmWindowManager::setVisibilityOfWindow(Window window, bool visible)
{
	knownWindows.at(window).setVisible(visible);
	
	if (hadError()) eraseWindow(window);
}

void GawmWindowManager::raiseWindow(Window window)
{
	XRaiseWindow(display, window);
	GawmWindow *gw = &knownWindows.at(window);
	sortedWindows.remove(gw);
	sortedWindows.push_front(gw);
	
	if (hadError()) eraseWindow(window);
}

void GawmWindowManager::moveResizeWindow(GawmWindow *window, int newX, int newY, int newWidth, int newHeight)
{
	XMoveResizeWindow(display, window->window, newX, newY, newWidth, newHeight);
	
	if (hadError()) eraseWindow(window->window);
}

void GawmWindowManager::moveDesktop(int xdiff, int ydiff)
{
	dbg_e_motion << "presun plochy o " << xdiff << "," << ydiff << std::endl;
	for (GawmWindow* draggedWindow : sortedWindows)
	{
		XWindowAttributes attr;
		XGetWindowAttributes(display, draggedWindow->window, &attr);
		XMoveResizeWindow(display, draggedWindow->window, attr.x+xdiff, attr.y+ydiff, draggedWindow->width, draggedWindow->height);
	}
}

void GawmWindowManager::initKnownWindows()
{
	Window root;
	Window parent;
	Window *children;
	Status status;
	unsigned nNumChildren;

	status = XQueryTree(display, rootWindow, &root, &parent, &children, &nNumChildren);
	if (status == 0)
	{
		// Nemohu získat strom oken, přerušuji.
		return;
	}

	if (nNumChildren == 0)
	{
		// Kořeň nemá žádné děcka.
		return;
	}

	for (unsigned i = 0; i < nNumChildren; i++)
	{
		if (children[i] == overlayWindow)
		{
			cerr_line << "Jedno z deti roota je overlay, to je asi spatne" << std::endl;
		}
		
		XWindowAttributes w_attr;

		status = XGetWindowAttributes(display, children[i], &w_attr);
		if (status == 0)
		{
			// Nemohu získat geometrii okna, pokračuji dalším.
			cerr_line << "Okno je " << children[i] << " a nemá geometrii" << std::endl;
			continue;
		}

		// Přidáme potomka Xek do mapy známých oken...
		knownWindows.insert(children[i], new GawmWindow(display, screen, children[i],
														w_attr.x, w_attr.y,
														w_attr.width+2*w_attr.border_width, w_attr.height+2*w_attr.border_width));
		// ... a zviditeníme ho, pokud je IsViewable
		if (w_attr.map_state == IsViewable)
		{
			knownWindows.at(children[i]).setVisible(true);
		}
	}

	XFree(children);
}

void GawmWindowManager::zoomIn(int x, int y)
{
	zoomTo(zoomLevel + 1, x, y);
}

void GawmWindowManager::zoomOut(int x, int y)
{
	zoomTo(zoomLevel - 1, x, y);
}

void GawmWindowManager::zoomTo(int level, int x, int y)
{
	static double zoomLevels[] = {0.125, 0.25, 0.5, 1.0, 2.0};
	static int maxZoomLevel = 4;
	static int minZoomLevel = 0;
	
	zoomLevel = level;
	auto prevZoom = zoom;
	if (zoomLevel > maxZoomLevel)
	{
		zoomLevel = maxZoomLevel;
	}
	else if (zoomLevel < minZoomLevel)
	{
		zoomLevel = minZoomLevel;
	}
	
	zoom = zoomLevels[zoomLevel];
	if (zoom == prevZoom) return;
	auto zoomMultiplier = zoom / prevZoom; 
	
	// celá plocha se posune o rozdíl staré a nové pozice kurzoru myši
	moveDesktop( x / zoomMultiplier - x, y / zoomMultiplier - y);
	
	dbg_e_buttonPress << "zoom = " << zoom << std::endl;
}
