/*
Copyright (C) 2003  T. Joseph Carter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#undef WIN32_LEAN_AND_MEAN  //hush a warning, SDL.h redefines this
#include <SDL.h>
#include <stdio.h>

#include "quakedef.h"
#include "image.h"
#include "utf8lib.h"

#ifndef __IPHONEOS__
#ifdef MACOSX
#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/hidsystem/event_status_driver.h>
static cvar_t apple_mouse_noaccel = {CVAR_SAVE, "apple_mouse_noaccel", "1", "disables mouse acceleration while DarkPlaces is active"};
static qboolean vid_usingnoaccel;
static double originalMouseSpeed = -1.0;
static io_connect_t IN_GetIOHandle(void)
{
	io_connect_t iohandle = MACH_PORT_NULL;
	kern_return_t status;
	io_service_t iohidsystem = MACH_PORT_NULL;
	mach_port_t masterport;

	status = IOMasterPort(MACH_PORT_NULL, &masterport);
	if(status != KERN_SUCCESS)
		return 0;

	iohidsystem = IORegistryEntryFromPath(masterport, kIOServicePlane ":/IOResources/IOHIDSystem");
	if(!iohidsystem)
		return 0;

	status = IOServiceOpen(iohidsystem, mach_task_self(), kIOHIDParamConnectType, &iohandle);
	IOObjectRelease(iohidsystem);

	return iohandle;
}
#endif
#endif

#ifdef WIN32
#define SDL_R_RESTART
#endif

#define SDL_MOUSE_RELATIVE_DOES_NOT_SUCK

// Tell startup code that we have a client
int cl_available = true;

qboolean vid_supportrefreshrate = false;

static qboolean vid_usingmouse = false;
/* 
 * SDL2 workaround for unimplemented RelativeMouse mode
 * defined with SDL_MOUSE_RELATIVE_DOES_NOT_SUCK at a later point
 */
static qboolean vid_usingmouse_relativeworks = false;
static qboolean vid_usinghidecursor = false;
static qboolean vid_hasfocus = false;
static qboolean vid_isfullscreen;
static qboolean vid_usingvsync = false;
static SDL_Joystick *vid_sdljoystick = NULL;

static int win_half_width = 50;
static int win_half_height = 50;
static int video_bpp;
static SDL_GLContext context;
static SDL_Window *window;
static int window_flags;
static cvar_t vid_sdl_use_scancodes = {CVAR_SAVE, "vid_sdl_use_scancodes", "1", "use SDL scancodes instead of keycodes"};
static cvar_t vid_touchscreen_sensitivity = {CVAR_SAVE, "vid_touchscreen_sensitivity", "0.25", "sensitivity of virtual touchpad"};
static SDL_Surface *vid_softsurface;
static vid_mode_t desktop_mode;

/////////////////////////
// Input handling
////
//TODO: Add error checking

#ifndef SDLK_PERCENT
#define SDLK_PERCENT '%'
#endif

static int MapKey( unsigned int sdlkey )
{
	switch(sdlkey)
	{
	default: return 0;
//	case SDLK_UNKNOWN:            return K_UNKNOWN;
	case SDLK_RETURN:             return K_ENTER;
	case SDLK_ESCAPE:             return K_ESCAPE;
	case SDLK_BACKSPACE:          return K_BACKSPACE;
	case SDLK_TAB:                return K_TAB;
	case SDLK_SPACE:              return K_SPACE;
	case SDLK_EXCLAIM:            return '!';
	case SDLK_QUOTEDBL:           return '"';
	case SDLK_HASH:               return '#';
	case SDLK_PERCENT:            return '%';
	case SDLK_DOLLAR:             return '$';
	case SDLK_AMPERSAND:          return '&';
	case SDLK_QUOTE:              return '\'';
	case SDLK_LEFTPAREN:          return '(';
	case SDLK_RIGHTPAREN:         return ')';
	case SDLK_ASTERISK:           return '*';
	case SDLK_PLUS:               return '+';
	case SDLK_COMMA:              return ',';
	case SDLK_MINUS:              return '-';
	case SDLK_PERIOD:             return '.';
	case SDLK_SLASH:              return '/';
	case SDLK_0:                  return '0';
	case SDLK_1:                  return '1';
	case SDLK_2:                  return '2';
	case SDLK_3:                  return '3';
	case SDLK_4:                  return '4';
	case SDLK_5:                  return '5';
	case SDLK_6:                  return '6';
	case SDLK_7:                  return '7';
	case SDLK_8:                  return '8';
	case SDLK_9:                  return '9';
	case SDLK_COLON:              return ':';
	case SDLK_SEMICOLON:          return ';';
	case SDLK_LESS:               return '<';
	case SDLK_EQUALS:             return '=';
	case SDLK_GREATER:            return '>';
	case SDLK_QUESTION:           return '?';
	case SDLK_AT:                 return '@';
	case SDLK_LEFTBRACKET:        return '[';
	case SDLK_BACKSLASH:          return '\\';
	case SDLK_RIGHTBRACKET:       return ']';
	case SDLK_CARET:              return '^';
	case SDLK_UNDERSCORE:         return '_';
	case SDLK_BACKQUOTE:          return '`';
	case SDLK_a:                  return 'a';
	case SDLK_b:                  return 'b';
	case SDLK_c:                  return 'c';
	case SDLK_d:                  return 'd';
	case SDLK_e:                  return 'e';
	case SDLK_f:                  return 'f';
	case SDLK_g:                  return 'g';
	case SDLK_h:                  return 'h';
	case SDLK_i:                  return 'i';
	case SDLK_j:                  return 'j';
	case SDLK_k:                  return 'k';
	case SDLK_l:                  return 'l';
	case SDLK_m:                  return 'm';
	case SDLK_n:                  return 'n';
	case SDLK_o:                  return 'o';
	case SDLK_p:                  return 'p';
	case SDLK_q:                  return 'q';
	case SDLK_r:                  return 'r';
	case SDLK_s:                  return 's';
	case SDLK_t:                  return 't';
	case SDLK_u:                  return 'u';
	case SDLK_v:                  return 'v';
	case SDLK_w:                  return 'w';
	case SDLK_x:                  return 'x';
	case SDLK_y:                  return 'y';
	case SDLK_z:                  return 'z';
	case SDLK_CAPSLOCK:           return K_CAPSLOCK;
	case SDLK_F1:                 return K_F1;
	case SDLK_F2:                 return K_F2;
	case SDLK_F3:                 return K_F3;
	case SDLK_F4:                 return K_F4;
	case SDLK_F5:                 return K_F5;
	case SDLK_F6:                 return K_F6;
	case SDLK_F7:                 return K_F7;
	case SDLK_F8:                 return K_F8;
#ifdef __EMSCRIPTEN__
	case SDLK_F9:                 return K_ESCAPE;
#else
	case SDLK_F9:                 return K_F9;
#endif
	case SDLK_F10:                return K_F10;
	case SDLK_F11:                return K_F11;
	case SDLK_F12:                return K_F12;
	case SDLK_PRINTSCREEN:        return K_PRINTSCREEN;
	case SDLK_SCROLLLOCK:         return K_SCROLLOCK;
	case SDLK_PAUSE:              return K_PAUSE;
	case SDLK_INSERT:             return K_INS;
	case SDLK_HOME:               return K_HOME;
	case SDLK_PAGEUP:             return K_PGUP;
#ifdef __IPHONEOS__
	case SDLK_DELETE:             return K_BACKSPACE;
#else
	case SDLK_DELETE:             return K_DEL;
#endif
	case SDLK_END:                return K_END;
	case SDLK_PAGEDOWN:           return K_PGDN;
	case SDLK_RIGHT:              return K_RIGHTARROW;
	case SDLK_LEFT:               return K_LEFTARROW;
	case SDLK_DOWN:               return K_DOWNARROW;
	case SDLK_UP:                 return K_UPARROW;
	case SDLK_NUMLOCKCLEAR:       return K_NUMLOCK;
	case SDLK_KP_DIVIDE:          return K_KP_DIVIDE;
	case SDLK_KP_MULTIPLY:        return K_KP_MULTIPLY;
	case SDLK_KP_MINUS:           return K_KP_MINUS;
	case SDLK_KP_PLUS:            return K_KP_PLUS;
	case SDLK_KP_ENTER:           return K_KP_ENTER;
	case SDLK_KP_1:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_1 : K_END);
	case SDLK_KP_2:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_2 : K_DOWNARROW);
	case SDLK_KP_3:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_3 : K_PGDN);
	case SDLK_KP_4:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_4 : K_LEFTARROW);
	case SDLK_KP_5:               return K_KP_5;
	case SDLK_KP_6:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_6 : K_RIGHTARROW);
	case SDLK_KP_7:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_7 : K_HOME);
	case SDLK_KP_8:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_8 : K_UPARROW);
	case SDLK_KP_9:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_9 : K_PGUP);
	case SDLK_KP_0:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_0 : K_INS);
	case SDLK_KP_PERIOD:          return ((SDL_GetModState() & KMOD_NUM) ? K_KP_PERIOD : K_DEL);
//	case SDLK_APPLICATION:        return K_APPLICATION;
//	case SDLK_POWER:              return K_POWER;
	case SDLK_KP_EQUALS:          return K_KP_EQUALS;
//	case SDLK_F13:                return K_F13;
//	case SDLK_F14:                return K_F14;
//	case SDLK_F15:                return K_F15;
//	case SDLK_F16:                return K_F16;
//	case SDLK_F17:                return K_F17;
//	case SDLK_F18:                return K_F18;
//	case SDLK_F19:                return K_F19;
//	case SDLK_F20:                return K_F20;
//	case SDLK_F21:                return K_F21;
//	case SDLK_F22:                return K_F22;
//	case SDLK_F23:                return K_F23;
//	case SDLK_F24:                return K_F24;
//	case SDLK_EXECUTE:            return K_EXECUTE;
//	case SDLK_HELP:               return K_HELP;
//	case SDLK_MENU:               return K_MENU;
//	case SDLK_SELECT:             return K_SELECT;
//	case SDLK_STOP:               return K_STOP;
//	case SDLK_AGAIN:              return K_AGAIN;
//	case SDLK_UNDO:               return K_UNDO;
//	case SDLK_CUT:                return K_CUT;
//	case SDLK_COPY:               return K_COPY;
//	case SDLK_PASTE:              return K_PASTE;
//	case SDLK_FIND:               return K_FIND;
//	case SDLK_MUTE:               return K_MUTE;
//	case SDLK_VOLUMEUP:           return K_VOLUMEUP;
//	case SDLK_VOLUMEDOWN:         return K_VOLUMEDOWN;
//	case SDLK_KP_COMMA:           return K_KP_COMMA;
//	case SDLK_KP_EQUALSAS400:     return K_KP_EQUALSAS400;
//	case SDLK_ALTERASE:           return K_ALTERASE;
//	case SDLK_SYSREQ:             return K_SYSREQ;
//	case SDLK_CANCEL:             return K_CANCEL;
//	case SDLK_CLEAR:              return K_CLEAR;
//	case SDLK_PRIOR:              return K_PRIOR;
//	case SDLK_RETURN2:            return K_RETURN2;
//	case SDLK_SEPARATOR:          return K_SEPARATOR;
//	case SDLK_OUT:                return K_OUT;
//	case SDLK_OPER:               return K_OPER;
//	case SDLK_CLEARAGAIN:         return K_CLEARAGAIN;
//	case SDLK_CRSEL:              return K_CRSEL;
//	case SDLK_EXSEL:              return K_EXSEL;
//	case SDLK_KP_00:              return K_KP_00;
//	case SDLK_KP_000:             return K_KP_000;
//	case SDLK_THOUSANDSSEPARATOR: return K_THOUSANDSSEPARATOR;
//	case SDLK_DECIMALSEPARATOR:   return K_DECIMALSEPARATOR;
//	case SDLK_CURRENCYUNIT:       return K_CURRENCYUNIT;
//	case SDLK_CURRENCYSUBUNIT:    return K_CURRENCYSUBUNIT;
//	case SDLK_KP_LEFTPAREN:       return K_KP_LEFTPAREN;
//	case SDLK_KP_RIGHTPAREN:      return K_KP_RIGHTPAREN;
//	case SDLK_KP_LEFTBRACE:       return K_KP_LEFTBRACE;
//	case SDLK_KP_RIGHTBRACE:      return K_KP_RIGHTBRACE;
//	case SDLK_KP_TAB:             return K_KP_TAB;
//	case SDLK_KP_BACKSPACE:       return K_KP_BACKSPACE;
//	case SDLK_KP_A:               return K_KP_A;
//	case SDLK_KP_B:               return K_KP_B;
//	case SDLK_KP_C:               return K_KP_C;
//	case SDLK_KP_D:               return K_KP_D;
//	case SDLK_KP_E:               return K_KP_E;
//	case SDLK_KP_F:               return K_KP_F;
//	case SDLK_KP_XOR:             return K_KP_XOR;
//	case SDLK_KP_POWER:           return K_KP_POWER;
//	case SDLK_KP_PERCENT:         return K_KP_PERCENT;
//	case SDLK_KP_LESS:            return K_KP_LESS;
//	case SDLK_KP_GREATER:         return K_KP_GREATER;
//	case SDLK_KP_AMPERSAND:       return K_KP_AMPERSAND;
//	case SDLK_KP_DBLAMPERSAND:    return K_KP_DBLAMPERSAND;
//	case SDLK_KP_VERTICALBAR:     return K_KP_VERTICALBAR;
//	case SDLK_KP_DBLVERTICALBAR:  return K_KP_DBLVERTICALBAR;
//	case SDLK_KP_COLON:           return K_KP_COLON;
//	case SDLK_KP_HASH:            return K_KP_HASH;
//	case SDLK_KP_SPACE:           return K_KP_SPACE;
//	case SDLK_KP_AT:              return K_KP_AT;
//	case SDLK_KP_EXCLAM:          return K_KP_EXCLAM;
//	case SDLK_KP_MEMSTORE:        return K_KP_MEMSTORE;
//	case SDLK_KP_MEMRECALL:       return K_KP_MEMRECALL;
//	case SDLK_KP_MEMCLEAR:        return K_KP_MEMCLEAR;
//	case SDLK_KP_MEMADD:          return K_KP_MEMADD;
//	case SDLK_KP_MEMSUBTRACT:     return K_KP_MEMSUBTRACT;
//	case SDLK_KP_MEMMULTIPLY:     return K_KP_MEMMULTIPLY;
//	case SDLK_KP_MEMDIVIDE:       return K_KP_MEMDIVIDE;
//	case SDLK_KP_PLUSMINUS:       return K_KP_PLUSMINUS;
//	case SDLK_KP_CLEAR:           return K_KP_CLEAR;
//	case SDLK_KP_CLEARENTRY:      return K_KP_CLEARENTRY;
//	case SDLK_KP_BINARY:          return K_KP_BINARY;
//	case SDLK_KP_OCTAL:           return K_KP_OCTAL;
//	case SDLK_KP_DECIMAL:         return K_KP_DECIMAL;
//	case SDLK_KP_HEXADECIMAL:     return K_KP_HEXADECIMAL;
	case SDLK_LCTRL:              return K_CTRL;
	case SDLK_LSHIFT:             return K_SHIFT;
	case SDLK_LALT:               return K_ALT;
//	case SDLK_LGUI:               return K_LGUI;
	case SDLK_RCTRL:              return K_CTRL;
	case SDLK_RSHIFT:             return K_SHIFT;
	case SDLK_RALT:               return K_ALT;
//	case SDLK_RGUI:               return K_RGUI;
//	case SDLK_MODE:               return K_MODE;
//	case SDLK_AUDIONEXT:          return K_AUDIONEXT;
//	case SDLK_AUDIOPREV:          return K_AUDIOPREV;
//	case SDLK_AUDIOSTOP:          return K_AUDIOSTOP;
//	case SDLK_AUDIOPLAY:          return K_AUDIOPLAY;
//	case SDLK_AUDIOMUTE:          return K_AUDIOMUTE;
//	case SDLK_MEDIASELECT:        return K_MEDIASELECT;
//	case SDLK_WWW:                return K_WWW;
//	case SDLK_MAIL:               return K_MAIL;
//	case SDLK_CALCULATOR:         return K_CALCULATOR;
//	case SDLK_COMPUTER:           return K_COMPUTER;
//	case SDLK_AC_SEARCH:          return K_AC_SEARCH; // Android button
//	case SDLK_AC_HOME:            return K_AC_HOME; // Android button
	case SDLK_AC_BACK:            return K_ESCAPE; // Android button
//	case SDLK_AC_FORWARD:         return K_AC_FORWARD; // Android button
//	case SDLK_AC_STOP:            return K_AC_STOP; // Android button
//	case SDLK_AC_REFRESH:         return K_AC_REFRESH; // Android button
//	case SDLK_AC_BOOKMARKS:       return K_AC_BOOKMARKS; // Android button
//	case SDLK_BRIGHTNESSDOWN:     return K_BRIGHTNESSDOWN;
//	case SDLK_BRIGHTNESSUP:       return K_BRIGHTNESSUP;
//	case SDLK_DISPLAYSWITCH:      return K_DISPLAYSWITCH;
//	case SDLK_KBDILLUMTOGGLE:     return K_KBDILLUMTOGGLE;
//	case SDLK_KBDILLUMDOWN:       return K_KBDILLUMDOWN;
//	case SDLK_KBDILLUMUP:         return K_KBDILLUMUP;
//	case SDLK_EJECT:              return K_EJECT;
//	case SDLK_SLEEP:              return K_SLEEP;
	}
}

static int MapScancode( unsigned int sdlscancode )
{
	switch(sdlscancode)
	{
	default: return 0;
	case SDL_SCANCODE_RETURN:             return K_ENTER;
	case SDL_SCANCODE_ESCAPE:             return K_ESCAPE;
	case SDL_SCANCODE_BACKSPACE:          return K_BACKSPACE;
	case SDL_SCANCODE_TAB:                return K_TAB;
	case SDL_SCANCODE_SPACE:              return K_SPACE;
	case SDL_SCANCODE_APOSTROPHE:         return '\'';
	case SDL_SCANCODE_COMMA:              return ',';
	case SDL_SCANCODE_MINUS:              return '-';
	case SDL_SCANCODE_PERIOD:             return '.';
	case SDL_SCANCODE_SLASH:              return '/';
	case SDL_SCANCODE_0:                  return '0';
	case SDL_SCANCODE_1:                  return '1';
	case SDL_SCANCODE_2:                  return '2';
	case SDL_SCANCODE_3:                  return '3';
	case SDL_SCANCODE_4:                  return '4';
	case SDL_SCANCODE_5:                  return '5';
	case SDL_SCANCODE_6:                  return '6';
	case SDL_SCANCODE_7:                  return '7';
	case SDL_SCANCODE_8:                  return '8';
	case SDL_SCANCODE_9:                  return '9';
	case SDL_SCANCODE_EQUALS:             return '=';
	case SDL_SCANCODE_BACKSLASH:          return '\\';
	case SDL_SCANCODE_GRAVE:              return '`';
	case SDL_SCANCODE_A:                  return 'a';
	case SDL_SCANCODE_B:                  return 'b';
	case SDL_SCANCODE_C:                  return 'c';
	case SDL_SCANCODE_D:                  return 'd';
	case SDL_SCANCODE_E:                  return 'e';
	case SDL_SCANCODE_F:                  return 'f';
	case SDL_SCANCODE_G:                  return 'g';
	case SDL_SCANCODE_H:                  return 'h';
	case SDL_SCANCODE_I:                  return 'i';
	case SDL_SCANCODE_J:                  return 'j';
	case SDL_SCANCODE_K:                  return 'k';
	case SDL_SCANCODE_L:                  return 'l';
	case SDL_SCANCODE_M:                  return 'm';
	case SDL_SCANCODE_N:                  return 'n';
	case SDL_SCANCODE_O:                  return 'o';
	case SDL_SCANCODE_P:                  return 'p';
	case SDL_SCANCODE_Q:                  return 'q';
	case SDL_SCANCODE_R:                  return 'r';
	case SDL_SCANCODE_S:                  return 's';
	case SDL_SCANCODE_T:                  return 't';
	case SDL_SCANCODE_U:                  return 'u';
	case SDL_SCANCODE_V:                  return 'v';
	case SDL_SCANCODE_W:                  return 'w';
	case SDL_SCANCODE_X:                  return 'x';
	case SDL_SCANCODE_Y:                  return 'y';
	case SDL_SCANCODE_Z:                  return 'z';
	case SDL_SCANCODE_CAPSLOCK:           return K_CAPSLOCK;
	case SDL_SCANCODE_F1:                 return K_F1;
	case SDL_SCANCODE_F2:                 return K_F2;
	case SDL_SCANCODE_F3:                 return K_F3;
	case SDL_SCANCODE_F4:                 return K_F4;
	case SDL_SCANCODE_F5:                 return K_F5;
	case SDL_SCANCODE_F6:                 return K_F6;
	case SDL_SCANCODE_F7:                 return K_F7;
	case SDL_SCANCODE_F8:                 return K_F8;
#ifdef __EMSCRIPTEN__
	case SDL_SCANCODE_F9:                 return K_ESCAPE;
#else
	case SDL_SCANCODE_F9:                 return K_F9;
#endif
	case SDL_SCANCODE_F10:                return K_F10;
	case SDL_SCANCODE_F11:                return K_F11;
	case SDL_SCANCODE_F12:                return K_F12;
	case SDL_SCANCODE_PRINTSCREEN:        return K_PRINTSCREEN;
	case SDL_SCANCODE_SCROLLLOCK:         return K_SCROLLOCK;
	case SDL_SCANCODE_PAUSE:              return K_PAUSE;
	case SDL_SCANCODE_INSERT:             return K_INS;
	case SDL_SCANCODE_HOME:               return K_HOME;
	case SDL_SCANCODE_PAGEUP:             return K_PGUP;
#ifdef __IPHONEOS__
	case SDL_SCANCODE_DELETE:             return K_BACKSPACE;
#else
	case SDL_SCANCODE_DELETE:             return K_DEL;
#endif
	case SDL_SCANCODE_END:                return K_END;
	case SDL_SCANCODE_PAGEDOWN:           return K_PGDN;
	case SDL_SCANCODE_RIGHT:              return K_RIGHTARROW;
	case SDL_SCANCODE_LEFT:               return K_LEFTARROW;
	case SDL_SCANCODE_DOWN:               return K_DOWNARROW;
	case SDL_SCANCODE_UP:                 return K_UPARROW;
	case SDL_SCANCODE_NUMLOCKCLEAR:       return K_NUMLOCK;
	case SDL_SCANCODE_KP_DIVIDE:          return K_KP_DIVIDE;
	case SDL_SCANCODE_KP_MULTIPLY:        return K_KP_MULTIPLY;
	case SDL_SCANCODE_KP_MINUS:           return K_KP_MINUS;
	case SDL_SCANCODE_KP_PLUS:            return K_KP_PLUS;
	case SDL_SCANCODE_KP_ENTER:           return K_KP_ENTER;
	case SDL_SCANCODE_KP_1:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_1 : K_END);
	case SDL_SCANCODE_KP_2:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_2 : K_DOWNARROW);
	case SDL_SCANCODE_KP_3:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_3 : K_PGDN);
	case SDL_SCANCODE_KP_4:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_4 : K_LEFTARROW);
	case SDL_SCANCODE_KP_5:               return K_KP_5;
	case SDL_SCANCODE_KP_6:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_6 : K_RIGHTARROW);
	case SDL_SCANCODE_KP_7:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_7 : K_HOME);
	case SDL_SCANCODE_KP_8:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_8 : K_UPARROW);
	case SDL_SCANCODE_KP_9:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_9 : K_PGUP);
	case SDL_SCANCODE_KP_0:               return ((SDL_GetModState() & KMOD_NUM) ? K_KP_0 : K_INS);
	case SDL_SCANCODE_KP_PERIOD:          return ((SDL_GetModState() & KMOD_NUM) ? K_KP_PERIOD : K_DEL);
//	case SDL_SCANCODE_APPLICATION:        return K_APPLICATION;
//	case SDL_SCANCODE_POWER:              return K_POWER;
	case SDL_SCANCODE_KP_EQUALS:          return K_KP_EQUALS;
//	case SDL_SCANCODE_F13:                return K_F13;
//	case SDL_SCANCODE_F14:                return K_F14;
//	case SDL_SCANCODE_F15:                return K_F15;
//	case SDL_SCANCODE_F16:                return K_F16;
//	case SDL_SCANCODE_F17:                return K_F17;
//	case SDL_SCANCODE_F18:                return K_F18;
//	case SDL_SCANCODE_F19:                return K_F19;
//	case SDL_SCANCODE_F20:                return K_F20;
//	case SDL_SCANCODE_F21:                return K_F21;
//	case SDL_SCANCODE_F22:                return K_F22;
//	case SDL_SCANCODE_F23:                return K_F23;
//	case SDL_SCANCODE_F24:                return K_F24;
//	case SDL_SCANCODE_EXECUTE:            return K_EXECUTE;
//	case SDL_SCANCODE_HELP:               return K_HELP;
//	case SDL_SCANCODE_MENU:               return K_MENU;
//	case SDL_SCANCODE_SELECT:             return K_SELECT;
//	case SDL_SCANCODE_STOP:               return K_STOP;
//	case SDL_SCANCODE_AGAIN:              return K_AGAIN;
//	case SDL_SCANCODE_UNDO:               return K_UNDO;
//	case SDL_SCANCODE_CUT:                return K_CUT;
//	case SDL_SCANCODE_COPY:               return K_COPY;
//	case SDL_SCANCODE_PASTE:              return K_PASTE;
//	case SDL_SCANCODE_FIND:               return K_FIND;
//	case SDL_SCANCODE_MUTE:               return K_MUTE;
//	case SDL_SCANCODE_VOLUMEUP:           return K_VOLUMEUP;
//	case SDL_SCANCODE_VOLUMEDOWN:         return K_VOLUMEDOWN;
//	case SDL_SCANCODE_KP_COMMA:           return K_KP_COMMA;
//	case SDL_SCANCODE_KP_EQUALSAS400:     return K_KP_EQUALSAS400;
//	case SDL_SCANCODE_ALTERASE:           return K_ALTERASE;
//	case SDL_SCANCODE_SYSREQ:             return K_SYSREQ;
//	case SDL_SCANCODE_CANCEL:             return K_CANCEL;
//	case SDL_SCANCODE_CLEAR:              return K_CLEAR;
//	case SDL_SCANCODE_PRIOR:              return K_PRIOR;
//	case SDL_SCANCODE_RETURN2:            return K_RETURN2;
//	case SDL_SCANCODE_SEPARATOR:          return K_SEPARATOR;
//	case SDL_SCANCODE_OUT:                return K_OUT;
//	case SDL_SCANCODE_OPER:               return K_OPER;
//	case SDL_SCANCODE_CLEARAGAIN:         return K_CLEARAGAIN;
//	case SDL_SCANCODE_CRSEL:              return K_CRSEL;
//	case SDL_SCANCODE_EXSEL:              return K_EXSEL;
//	case SDL_SCANCODE_KP_00:              return K_KP_00;
//	case SDL_SCANCODE_KP_000:             return K_KP_000;
//	case SDL_SCANCODE_THOUSANDSSEPARATOR: return K_THOUSANDSSEPARATOR;
//	case SDL_SCANCODE_DECIMALSEPARATOR:   return K_DECIMALSEPARATOR;
//	case SDL_SCANCODE_CURRENCYUNIT:       return K_CURRENCYUNIT;
//	case SDL_SCANCODE_CURRENCYSUBUNIT:    return K_CURRENCYSUBUNIT;
//	case SDL_SCANCODE_KP_LEFTPAREN:       return K_KP_LEFTPAREN;
//	case SDL_SCANCODE_KP_RIGHTPAREN:      return K_KP_RIGHTPAREN;
//	case SDL_SCANCODE_KP_LEFTBRACE:       return K_KP_LEFTBRACE;
//	case SDL_SCANCODE_KP_RIGHTBRACE:      return K_KP_RIGHTBRACE;
//	case SDL_SCANCODE_KP_TAB:             return K_KP_TAB;
//	case SDL_SCANCODE_KP_BACKSPACE:       return K_KP_BACKSPACE;
//	case SDL_SCANCODE_KP_A:               return K_KP_A;
//	case SDL_SCANCODE_KP_B:               return K_KP_B;
//	case SDL_SCANCODE_KP_C:               return K_KP_C;
//	case SDL_SCANCODE_KP_D:               return K_KP_D;
//	case SDL_SCANCODE_KP_E:               return K_KP_E;
//	case SDL_SCANCODE_KP_F:               return K_KP_F;
//	case SDL_SCANCODE_KP_XOR:             return K_KP_XOR;
//	case SDL_SCANCODE_KP_POWER:           return K_KP_POWER;
//	case SDL_SCANCODE_KP_PERCENT:         return K_KP_PERCENT;
//	case SDL_SCANCODE_KP_LESS:            return K_KP_LESS;
//	case SDL_SCANCODE_KP_GREATER:         return K_KP_GREATER;
//	case SDL_SCANCODE_KP_AMPERSAND:       return K_KP_AMPERSAND;
//	case SDL_SCANCODE_KP_DBLAMPERSAND:    return K_KP_DBLAMPERSAND;
//	case SDL_SCANCODE_KP_VERTICALBAR:     return K_KP_VERTICALBAR;
//	case SDL_SCANCODE_KP_DBLVERTICALBAR:  return K_KP_DBLVERTICALBAR;
//	case SDL_SCANCODE_KP_COLON:           return K_KP_COLON;
//	case SDL_SCANCODE_KP_HASH:            return K_KP_HASH;
//	case SDL_SCANCODE_KP_SPACE:           return K_KP_SPACE;
//	case SDL_SCANCODE_KP_AT:              return K_KP_AT;
//	case SDL_SCANCODE_KP_EXCLAM:          return K_KP_EXCLAM;
//	case SDL_SCANCODE_KP_MEMSTORE:        return K_KP_MEMSTORE;
//	case SDL_SCANCODE_KP_MEMRECALL:       return K_KP_MEMRECALL;
//	case SDL_SCANCODE_KP_MEMCLEAR:        return K_KP_MEMCLEAR;
//	case SDL_SCANCODE_KP_MEMADD:          return K_KP_MEMADD;
//	case SDL_SCANCODE_KP_MEMSUBTRACT:     return K_KP_MEMSUBTRACT;
//	case SDL_SCANCODE_KP_MEMMULTIPLY:     return K_KP_MEMMULTIPLY;
//	case SDL_SCANCODE_KP_MEMDIVIDE:       return K_KP_MEMDIVIDE;
//	case SDL_SCANCODE_KP_PLUSMINUS:       return K_KP_PLUSMINUS;
//	case SDL_SCANCODE_KP_CLEAR:           return K_KP_CLEAR;
//	case SDL_SCANCODE_KP_CLEARENTRY:      return K_KP_CLEARENTRY;
//	case SDL_SCANCODE_KP_BINARY:          return K_KP_BINARY;
//	case SDL_SCANCODE_KP_OCTAL:           return K_KP_OCTAL;
//	case SDL_SCANCODE_KP_DECIMAL:         return K_KP_DECIMAL;
//	case SDL_SCANCODE_KP_HEXADECIMAL:     return K_KP_HEXADECIMAL;
	case SDL_SCANCODE_LCTRL:              return K_CTRL;
	case SDL_SCANCODE_LSHIFT:             return K_SHIFT;
	case SDL_SCANCODE_LALT:               return K_ALT;
//	case SDL_SCANCODE_LGUI:               return K_LGUI;
	case SDL_SCANCODE_RCTRL:              return K_CTRL;
	case SDL_SCANCODE_RSHIFT:             return K_SHIFT;
	case SDL_SCANCODE_RALT:               return K_ALT;
//	case SDL_SCANCODE_RGUI:               return K_RGUI;
//	case SDL_SCANCODE_MODE:               return K_MODE;
//	case SDL_SCANCODE_AUDIONEXT:          return K_AUDIONEXT;
//	case SDL_SCANCODE_AUDIOPREV:          return K_AUDIOPREV;
//	case SDL_SCANCODE_AUDIOSTOP:          return K_AUDIOSTOP;
//	case SDL_SCANCODE_AUDIOPLAY:          return K_AUDIOPLAY;
//	case SDL_SCANCODE_AUDIOMUTE:          return K_AUDIOMUTE;
//	case SDL_SCANCODE_MEDIASELECT:        return K_MEDIASELECT;
//	case SDL_SCANCODE_WWW:                return K_WWW;
//	case SDL_SCANCODE_MAIL:               return K_MAIL;
//	case SDL_SCANCODE_CALCULATOR:         return K_CALCULATOR;
//	case SDL_SCANCODE_COMPUTER:           return K_COMPUTER;
//	case SDL_SCANCODE_AC_SEARCH:          return K_AC_SEARCH; // Android button
//	case SDL_SCANCODE_AC_HOME:            return K_AC_HOME; // Android button
	case SDL_SCANCODE_AC_BACK:            return K_ESCAPE; // Android button
//	case SDL_SCANCODE_AC_FORWARD:         return K_AC_FORWARD; // Android button
//	case SDL_SCANCODE_AC_STOP:            return K_AC_STOP; // Android button
//	case SDL_SCANCODE_AC_REFRESH:         return K_AC_REFRESH; // Android button
//	case SDL_SCANCODE_AC_BOOKMARKS:       return K_AC_BOOKMARKS; // Android button
//	case SDL_SCANCODE_BRIGHTNESSDOWN:     return K_BRIGHTNESSDOWN;
//	case SDL_SCANCODE_BRIGHTNESSUP:       return K_BRIGHTNESSUP;
//	case SDL_SCANCODE_DISPLAYSWITCH:      return K_DISPLAYSWITCH;
//	case SDL_SCANCODE_KBDILLUMTOGGLE:     return K_KBDILLUMTOGGLE;
//	case SDL_SCANCODE_KBDILLUMDOWN:       return K_KBDILLUMDOWN;
//	case SDL_SCANCODE_KBDILLUMUP:         return K_KBDILLUMUP;
//	case SDL_SCANCODE_EJECT:              return K_EJECT;
//	case SDL_SCANCODE_SLEEP:              return K_SLEEP;
	case SDL_SCANCODE_LEFTBRACKET:        return '[';
	case SDL_SCANCODE_RIGHTBRACKET:       return ']';
	}
}

qboolean VID_HasScreenKeyboardSupport(void)
{
	return SDL_HasScreenKeyboardSupport() != SDL_FALSE;
}

void VID_ShowKeyboard(qboolean show)
{
	if (!SDL_HasScreenKeyboardSupport())
		return;

	if (show)
	{
		if (!SDL_IsTextInputActive())
			SDL_StartTextInput();
	}
	else
	{
		if (SDL_IsTextInputActive())
			SDL_StopTextInput();
	}
}

qboolean VID_ShowingKeyboard(void)
{
	return SDL_IsTextInputActive() != 0;
}

void VID_SetMouse(qboolean fullscreengrab, qboolean relative, qboolean hidecursor)
{
#ifdef MACOSX
	if(relative)
		if(vid_usingmouse && (vid_usingnoaccel != !!apple_mouse_noaccel.integer))
			VID_SetMouse(false, false, false); // ungrab first!
#endif
	if (vid_usingmouse != relative)
	{
		vid_usingmouse = relative;
		cl_ignoremousemoves = 2;
#ifdef SDL_MOUSE_RELATIVE_DOES_NOT_SUCK
		vid_usingmouse_relativeworks = SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE) == 0;
        Con_DPrintf("VID_SetMouse(%i, %i, %i) relativeworks = %i\n", (int)fullscreengrab, (int)relative, (int)hidecursor, (int)vid_usingmouse_relativeworks);
#else
		vid_usingmouse_relativeworks = SDL_FALSE;
#endif
#ifdef MACOSX
		if(relative)
		{
			// Save the status of mouse acceleration
			originalMouseSpeed = -1.0; // in case of error
			if(apple_mouse_noaccel.integer)
			{
				io_connect_t mouseDev = IN_GetIOHandle();
				if(mouseDev != 0)
				{
					if(IOHIDGetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), &originalMouseSpeed) == kIOReturnSuccess)
					{
						Con_DPrintf("previous mouse acceleration: %f\n", originalMouseSpeed);
						if(IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), -1.0) != kIOReturnSuccess)
						{
							Con_Print("Could not disable mouse acceleration (failed at IOHIDSetAccelerationWithKey).\n");
							Cvar_SetValueQuick(&apple_mouse_noaccel, 0);
						}
					}
					else
					{
						Con_Print("Could not disable mouse acceleration (failed at IOHIDGetAccelerationWithKey).\n");
						Cvar_SetValueQuick(&apple_mouse_noaccel, 0);
					}
					IOServiceClose(mouseDev);
				}
				else
				{
					Con_Print("Could not disable mouse acceleration (failed at IO_GetIOHandle).\n");
					Cvar_SetValueQuick(&apple_mouse_noaccel, 0);
				}
			}

			vid_usingnoaccel = !!apple_mouse_noaccel.integer;
		}
		else
		{
			if(originalMouseSpeed != -1.0)
			{
				io_connect_t mouseDev = IN_GetIOHandle();
				if(mouseDev != 0)
				{
					Con_DPrintf("restoring mouse acceleration to: %f\n", originalMouseSpeed);
					if(IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), originalMouseSpeed) != kIOReturnSuccess)
						Con_Print("Could not re-enable mouse acceleration (failed at IOHIDSetAccelerationWithKey).\n");
					IOServiceClose(mouseDev);
				}
				else
					Con_Print("Could not re-enable mouse acceleration (failed at IO_GetIOHandle).\n");
			}
		}
#endif
	}
	if (vid_usinghidecursor != hidecursor)
	{
		vid_usinghidecursor = hidecursor;
		SDL_ShowCursor( hidecursor ? SDL_DISABLE : SDL_ENABLE);
	}
}

// multitouch[10][] represents the mouse pointer
struct finger {
	int state;
	int area_id;
	float x, y;
	float start_x, start_y;
};
#define MAXFINGERS 11
#define TOUCHSCREEN_AREAS_MAXCOUNT 128
static struct finger multitouch[MAXFINGERS];
struct touchscreen_area {
	int dest, corner, x, y, width, height;
	char image[64], cmd[32];
};
static struct touchscreen_area touchscreen_areas[TOUCHSCREEN_AREAS_MAXCOUNT - 2];
int touchscreen_areas_count;

static void VID_TouchScreenInit(void) {
	const char *cfg_path = "dptouchscreen.cfg";
	char *cfg = (char*)FS_LoadFile(cfg_path, tempmempool, false, NULL);
	char *line;
	char *nl;
	const char *tok;
	int line_num = 0;
	touchscreen_areas_count = 0;
	while (cfg && touchscreen_areas_count < TOUCHSCREEN_AREAS_MAXCOUNT - 2) {
		line_num++;
		nl = strchr(cfg, '\n');
		if (nl) {
			line = cfg;
			nl[0] = '\0';
			cfg = &nl[1];
		} else {
			line = cfg;
			cfg = NULL;
		}
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		if (tok[0] == '/') continue; //Commentary
		touchscreen_areas[touchscreen_areas_count].dest = atoi(tok);
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		touchscreen_areas[touchscreen_areas_count].corner = atoi(tok);
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		touchscreen_areas[touchscreen_areas_count].x = atoi(tok);
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		touchscreen_areas[touchscreen_areas_count].y = atoi(tok);
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		touchscreen_areas[touchscreen_areas_count].width = atoi(tok);
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		touchscreen_areas[touchscreen_areas_count].height = atoi(tok);
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		strlcpy(touchscreen_areas[touchscreen_areas_count].image, tok, sizeof(touchscreen_areas[touchscreen_areas_count].image));
		if (!(tok = strtok_r(line, " \t", &line))) { Con_Printf("Touch screen info parse error: %s:%i: not enough parameters!\n", cfg_path, line_num); continue; }
		strlcpy(touchscreen_areas[touchscreen_areas_count].cmd, tok, sizeof(touchscreen_areas[touchscreen_areas_count].cmd));
		touchscreen_areas_count++;
	}
}

// modified heavily by ELUAN
static qboolean VID_TouchscreenArea(int dest, int corner, float px, float py, float pwidth, float pheight, const char *icon, const char *command, qboolean *resultbutton, int id)
{
	int finger;
	float fx, fy, fwidth, fheight;
	float rel[3];
	float sqsum;
	qboolean button = false;
	qboolean check_dest = false;
	if (vid_touchscreen_active.integer) {
		if ((dest & 32)) {
			*resultbutton = false;
			return false;
		}
	} else {
		if (!(dest & 32)) {
			*resultbutton = false;
			return false;
		}
	}
	VectorClear(rel);
	if (key_consoleactive & KEY_CONSOLEACTIVE_USER) {
		check_dest = dest & 1;
	} else {
		if (key_dest == key_console)
			check_dest = (dest & 1);
		else if (key_dest == key_game)
			check_dest = (dest & 2);
		else
			check_dest = (dest & 4);
	}
	if (check_dest)
	{
		if (corner & 1) px += vid_conwidth.value;
		if (corner & 2) py += vid_conheight.value;
		if (corner & 4) px += vid_conwidth.value * 0.5f;
		if (corner & 8) py += vid_conheight.value * 0.5f;
		if (corner & 16) {px *= vid_conwidth.value * (1.0f / 640.0f);py *= vid_conheight.value * (1.0f / 480.0f);pwidth *= vid_conwidth.value * (1.0f / 640.0f);pheight *= vid_conheight.value * (1.0f / 480.0f);}
		fx = px / vid_conwidth.value;
		fy = py / vid_conheight.value;
		fwidth = pwidth / vid_conwidth.value;
		fheight = pheight / vid_conheight.value;
		for (finger = 0;finger < MAXFINGERS;finger++)
		{
			if (multitouch[finger].state && ((multitouch[finger].start_x >= fx && multitouch[finger].start_y >= fy && multitouch[finger].start_x < fx + fwidth && multitouch[finger].start_y < fy + fheight && multitouch[finger].area_id < 0) || multitouch[finger].area_id == id))
			{
				multitouch[finger].area_id = id;
				rel[0] = bound(-1, (multitouch[finger].x - multitouch[finger].start_x) * (4.0f / fwidth), 1);
				rel[1] = bound(-1, (multitouch[finger].y - multitouch[finger].start_y) * (4.0f / fheight), 1);
				rel[2] = 0;

				sqsum = rel[0]*rel[0] + rel[1]*rel[1];
				if (sqsum > 1)
				{
					// ignore the third component
					Vector2Normalize2(rel, rel);
				}
				button = true;
				break;
			}
		}
		if (scr_numtouchscreenareas < TOUCHSCREEN_AREAS_MAXCOUNT)
		{
			scr_touchscreenareas[scr_numtouchscreenareas].pic = icon;
			scr_touchscreenareas[scr_numtouchscreenareas].rect[0] = px;
			scr_touchscreenareas[scr_numtouchscreenareas].rect[1] = py;
			scr_touchscreenareas[scr_numtouchscreenareas].rect[2] = pwidth;
			scr_touchscreenareas[scr_numtouchscreenareas].rect[3] = pheight;
			scr_touchscreenareas[scr_numtouchscreenareas].active = button;
			// the pics may have alpha too.
			scr_touchscreenareas[scr_numtouchscreenareas].activealpha = 1.f;
			scr_touchscreenareas[scr_numtouchscreenareas].inactivealpha = 0.95f;
			scr_numtouchscreenareas++;
		}
	}
	if (command[0] == '*') {
		if (!strcmp(command, "*move")) {
			if (button) {
				cl.cmd.forwardmove -= rel[1] * cl_forwardspeed.value;
				cl.cmd.sidemove += rel[0] * cl_sidespeed.value;
			}
		} else if (!strcmp(command, "*aim")) {
			if (button) {
				cl.viewangles[0] += rel[1] * cl_pitchspeed.value * vid_touchscreen_sensitivity.value;
				cl.viewangles[1] -= rel[0] * cl_yawspeed.value * vid_touchscreen_sensitivity.value;
				multitouch[finger].start_x = multitouch[finger].x;
				multitouch[finger].start_y = multitouch[finger].y;
			}
		} else if (!strcmp(command, "*click")) {
			if (*resultbutton != button) {
				Key_Event(K_MOUSE1, 0, button, false);
			}
		} else if (!strcmp(command, "*menu")) {
			if (*resultbutton != button) {
				Key_Event(K_ESCAPE, 0, button, false);
			}
		} else if (!strcmp(command, "*touchtoggle")) {
			if (!button && *resultbutton) {
				Cvar_SetValueQuick(&vid_touchscreen_active, !vid_touchscreen_active.integer);
			}
		} else if (!strcmp(command, "*keyboard")) {
			if (button && *resultbutton != button)
				VID_ShowKeyboard(!VID_ShowingKeyboard());
		}
	} else {
		if (*resultbutton != button)
		{
			if (command) {
				if (command[0] == '+' && !button) {
					char minus_command[64];
					strlcpy(minus_command, command, 64);
					minus_command[0] = '-';
					Cbuf_AddText(minus_command);
				} else if (button) {
					Cbuf_AddText(command);
				}
				Cbuf_AddText("\n");
			}
		}
	}
	*resultbutton = button;
	return button;
}

void VID_BuildJoyState(vid_joystate_t *joystate)
{
	VID_Shared_BuildJoyState_Begin(joystate);

	if (vid_sdljoystick)
	{
		SDL_Joystick *joy = vid_sdljoystick;
		int j;
		int numaxes;
		int numbuttons;
		numaxes = SDL_JoystickNumAxes(joy);
		for (j = 0;j < numaxes;j++)
			joystate->axis[j] = SDL_JoystickGetAxis(joy, j) * (1.0f / 32767.0f);
		numbuttons = SDL_JoystickNumButtons(joy);
		for (j = 0;j < numbuttons;j++)
			joystate->button[j] = SDL_JoystickGetButton(joy, j);
	}

	VID_Shared_BuildJoyState_Finish(joystate);
}

/////////////////////
// Movement handling
////
static qboolean IN_Move_TouchScreen_Quake(void)
{
	int x, y, n = 0, p = 0, st;
	static qboolean oldbuttons[TOUCHSCREEN_AREAS_MAXCOUNT];
	static qboolean buttons[TOUCHSCREEN_AREAS_MAXCOUNT];
	keydest_t keydest = (key_consoleactive & KEY_CONSOLEACTIVE_USER) ? key_console : key_dest;
	memcpy(oldbuttons, buttons, sizeof(oldbuttons));

	// simple quake controls
	st = SDL_GetMouseState(&x, &y);
	multitouch[MAXFINGERS-1].x = ((float)x) / vid.width;
	multitouch[MAXFINGERS-1].y = ((float)y) / vid.height;
	if (!multitouch[MAXFINGERS-1].state) {
		multitouch[MAXFINGERS-1].area_id = -1;
		multitouch[MAXFINGERS-1].start_x = multitouch[MAXFINGERS-1].x;
		multitouch[MAXFINGERS-1].start_y = multitouch[MAXFINGERS-1].y;
	}
	multitouch[MAXFINGERS-1].state = st;

	// top of screen is toggleconsole and K_ESCAPE
	in_windowmouse_x = x;
	in_windowmouse_y = y;
	for (n = 0; n < touchscreen_areas_count; n++) {
		p += VID_TouchscreenArea(touchscreen_areas[n].dest, touchscreen_areas[n].corner, touchscreen_areas[n].x, touchscreen_areas[n].y,
				touchscreen_areas[n].width, touchscreen_areas[n].height, touchscreen_areas[n].image, touchscreen_areas[n].cmd, &buttons[n], n);
	}
	if (!p) {
		n++;
		p += VID_TouchscreenArea(7, 0,   0,   0,  64,  64, NULL                         , "toggleconsole", &buttons[n], n);
		n++;
		p += VID_TouchscreenArea(6, 0,   0,   0, vid_conwidth.integer, vid_conheight.integer, NULL, "*click", &buttons[n], n);
	}
	if (keydest == key_console && !VID_ShowingKeyboard())
	{
		// user entered a command, close the console now
		Con_ToggleConsole_f();
	}
	return p;
}

void IN_Move( void )
{
	static int old_x = 0, old_y = 0;
	static int stuck = 0;
	static keydest_t oldkeydest;
	static qboolean oldshowkeyboard;
	int x, y;
	vid_joystate_t joystate;
	keydest_t keydest = (key_consoleactive & KEY_CONSOLEACTIVE_USER) ? key_console : key_dest;

	scr_numtouchscreenareas = 0;

	// Only apply the new keyboard state if the input changes.
	if (keydest != oldkeydest || !!vid_touchscreen_showkeyboard.integer != oldshowkeyboard)
	{
		switch(keydest)
		{
			case key_console: VID_ShowKeyboard(true);break;
			case key_message: VID_ShowKeyboard(true);break;
			default: VID_ShowKeyboard(!!vid_touchscreen_showkeyboard.integer); break;
		}
	}
	oldkeydest = keydest;
	oldshowkeyboard = !!vid_touchscreen_showkeyboard.integer;

	if (!vid_touchscreen.integer || !IN_Move_TouchScreen_Quake())
	{
		if (vid_usingmouse && vid_activewindow)
		{
			if (vid_stick_mouse.integer || !vid_usingmouse_relativeworks)
			{
				// have the mouse stuck in the middle, example use: prevent expose effect of beryl during the game when not using
				// window grabbing. --blub
	
				// we need 2 frames to initialize the center position
				if(!stuck)
				{
					SDL_WarpMouseInWindow(window, win_half_width, win_half_height);
					SDL_GetMouseState(&x, &y);
					SDL_GetRelativeMouseState(&x, &y);
					++stuck;
				} else {
					SDL_GetRelativeMouseState(&x, &y);
					in_mouse_x = x + old_x;
					in_mouse_y = y + old_y;
					SDL_GetMouseState(&x, &y);
					old_x = x - win_half_width;
					old_y = y - win_half_height;
					SDL_WarpMouseInWindow(window, win_half_width, win_half_height);
				}
			} else {
				SDL_GetRelativeMouseState( &x, &y );
				in_mouse_x = x;
				in_mouse_y = y;
			}
		}

		SDL_GetMouseState(&x, &y);
		in_windowmouse_x = x;
		in_windowmouse_y = y;
	}

	VID_BuildJoyState(&joystate);
	VID_ApplyJoyState(&joystate);
}

/////////////////////
// Message Handling
////

#ifdef SDL_R_RESTART
static qboolean sdl_needs_restart;
static void sdl_start(void)
{
}
static void sdl_shutdown(void)
{
	sdl_needs_restart = false;
}
static void sdl_newmap(void)
{
}
#endif

static keynum_t buttonremap[] =
{
	K_MOUSE1,
	K_MOUSE3,
	K_MOUSE2,

/*
 * Mouse wheels are BUTTONS in SDL 1, they are NOT buttons in SDL2!
 * Mapping them here will cause MOUSE4 and MOUSE5 to register as MWHEELUP and MWHEELDOWN respectively,
 * making them effectively useless.
 */

	K_MOUSE4,
	K_MOUSE5,
	K_MOUSE6,
	K_MOUSE7,
	K_MOUSE8,
	K_MOUSE9,
	K_MOUSE10,
	K_MOUSE11,
	K_MOUSE12,
	K_MOUSE13,
	K_MOUSE14,
	K_MOUSE15,
	K_MOUSE16,
};

//#define DEBUGSDLEVENTS

// SDL2
void Sys_SendKeyEvents( void )
{
	static qboolean sound_active = true;
	int keycode;
	int i;
	qboolean isdown;
	Uchar unicode;
	SDL_Event event;
	qboolean skipbinds = false;

	VID_EnableJoystick(true);

	while( SDL_PollEvent( &event ) )
		loop_start:
		switch( event.type ) {
			case SDL_QUIT:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_Event: SDL_QUIT\n");
#endif
				Sys_Quit(0);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				skipbinds = event.key.repeat;
#ifdef DEBUGSDLEVENTS
				if (event.type == SDL_KEYDOWN)
					Con_DPrintf("SDL_Event: SDL_KEYDOWN %i\n", event.key.keysym.sym);
				else
					Con_DPrintf("SDL_Event: SDL_KEYUP %i\n", event.key.keysym.sym);
#endif
				if (vid_sdl_use_scancodes.integer)
					keycode = MapScancode(event.key.keysym.scancode);
				else
					keycode = MapKey(event.key.keysym.sym);

				isdown = (event.key.state == SDL_PRESSED);
				unicode = 0;
				if(isdown)
				{
					if(SDL_PollEvent(&event))
					{
						if(event.type == SDL_TEXTINPUT)
						{
							// combine key code from SDL_KEYDOWN event and character
							// from SDL_TEXTINPUT event in a single Key_Event call
#ifdef DEBUGSDLEVENTS
							Con_DPrintf("SDL_Event: SDL_TEXTINPUT - text: %s\n", event.text.text);
#endif
							unicode = u8_getchar_utf8_enabled(event.text.text + (int)u8_bytelen(event.text.text, 0), NULL);
						}
						else
						{
							if (!VID_JoyBlockEmulatedKeys(keycode))
								Key_Event(keycode, 0, isdown, skipbinds);

							goto loop_start;
						}
					}
				}
				if (!VID_JoyBlockEmulatedKeys(keycode))
					Key_Event(keycode, unicode, isdown, skipbinds);
				break;
			case SDL_MOUSEBUTTONDOWN:
#ifdef __EMSCRIPTEN__
				{
					int flags = SDL_GetWindowFlags(window);
					if (!(flags & SDL_WINDOW_FULLSCREEN)) {
						Con_Printf("Restore fullscreen...\n");
						SDL_SetWindowFullscreen(window, flags | SDL_WINDOW_FULLSCREEN);
					}
				}
#endif
			case SDL_MOUSEBUTTONUP:
#ifdef DEBUGSDLEVENTS
				if (event.type == SDL_MOUSEBUTTONDOWN)
					Con_DPrintf("SDL_Event: SDL_MOUSEBUTTONDOWN\n");
				else
					Con_DPrintf("SDL_Event: SDL_MOUSEBUTTONUP\n");
#endif
				if ((!vid_touchscreen.integer || !vid_touchscreen_active.integer) && event.button.button > 0 && event.button.button <= ARRAY_SIZE(buttonremap))
					Key_Event( buttonremap[event.button.button - 1], 0, event.button.state == SDL_PRESSED, false );
				break;
			case SDL_MOUSEWHEEL:
				// TODO support wheel x direction.
				i = event.wheel.y;
				while (i > 0) {
					--i;
					Key_Event( K_MWHEELUP, 0, true, false );
					Key_Event( K_MWHEELUP, 0, false, false );
				}
				while (i < 0) {
					++i;
					Key_Event( K_MWHEELDOWN, 0, true, false );
					Key_Event( K_MWHEELDOWN, 0, false, false );
				}
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
			case SDL_JOYAXISMOTION:
			case SDL_JOYBALLMOTION:
			case SDL_JOYHATMOTION:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_Event: SDL_JOY*\n");
#endif
				break;
			case SDL_WINDOWEVENT:
				Con_DPrintf("SDL_Event: SDL_WINDOWEVENT %i\n", (int)event.window.event);
				if (window && event.window.windowID == SDL_GetWindowID(window)) // how to compare?
				{
					switch(event.window.event)
					{
					case SDL_WINDOWEVENT_SHOWN:
						vid_hidden = false;
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_SHOWN\n");
						break;
					case  SDL_WINDOWEVENT_HIDDEN:
						vid_hidden = true;
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_HIDDEN\n");
						break;
					case SDL_WINDOWEVENT_EXPOSED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_EXPOSED\n");
						break;
					case SDL_WINDOWEVENT_MOVED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_MOVED\n");
						break;
					case SDL_WINDOWEVENT_RESIZED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_RESIZED\n");
						if(vid_resizable.integer < 2)
						{
							vid.width = event.window.data1;
							vid.height = event.window.data2;
							if (vid_softsurface)
							{
								SDL_FreeSurface(vid_softsurface);
								vid_softsurface = SDL_CreateRGBSurface(SDL_SWSURFACE, vid.width, vid.height, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
								SDL_SetSurfaceBlendMode(vid_softsurface, SDL_BLENDMODE_NONE);
								vid.softpixels = (unsigned int *)vid_softsurface->pixels;
								if (vid.softdepthpixels)
									free(vid.softdepthpixels);
								vid.softdepthpixels = (unsigned int*)calloc(1, vid.width * vid.height * 4);
							}
#ifdef SDL_R_RESTART
							// better not call R_Modules_Restart from here directly, as this may wreak havoc...
							// so, let's better queue it for next frame
							if(!sdl_needs_restart)
							{
								Cbuf_AddText("\nr_restart\n");
								sdl_needs_restart = true;
							}
#endif
						}
						break;
					case SDL_WINDOWEVENT_MINIMIZED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_MINIMIZED\n");
						break;
					case SDL_WINDOWEVENT_MAXIMIZED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_MAXIMIZED\n");
						break;
					case SDL_WINDOWEVENT_RESTORED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_RESTORED\n");
						break;
					case SDL_WINDOWEVENT_ENTER:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_ENTER\n");
						break;
					case SDL_WINDOWEVENT_LEAVE:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_LEAVE\n");
						break;
					case SDL_WINDOWEVENT_FOCUS_GAINED:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_GAINED\n");
						vid_hasfocus = true;
						vid_hidden = false; //ugly workaround, SDL_WINDOWEVENT_SHOWN missed on Android. SDL2 bug?
						break;
					case SDL_WINDOWEVENT_FOCUS_LOST:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_FOCUS_LOST\n");
						vid_hasfocus = false;
						break;
					case SDL_WINDOWEVENT_CLOSE:
						Con_DPrintf("SDL_Event: SDL_WINDOWEVENT_CLOSE\n");
						Sys_Quit(0);
						break;
					}
				}
				break;
			case SDL_TEXTEDITING:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_Event: SDL_TEXTEDITING - composition = %s, cursor = %d, selection lenght = %d\n", event.edit.text, event.edit.start, event.edit.length);
#endif
				// FIXME!  this is where composition gets supported
				break;
			case SDL_TEXTINPUT:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_Event: SDL_TEXTINPUT - text: %s\n", event.text.text);
#endif
				// convert utf8 string to char
				// NOTE: this code is supposed to run even if utf8enable is 0
				unicode = u8_getchar_utf8_enabled(event.text.text + (int)u8_bytelen(event.text.text, 0), NULL);
				Key_Event(K_TEXT, unicode, true, false);
				Key_Event(K_TEXT, unicode, false, false);
				break;
			case SDL_MOUSEMOTION:
				break;
			case SDL_FINGERDOWN:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_FINGERDOWN for finger %i\n", (int)event.tfinger.fingerId);
#endif
				for (i = 0;i < MAXFINGERS-1;i++)
				{
					if (!multitouch[i].state)
					{
						multitouch[i].area_id = -1;
						multitouch[i].state = event.tfinger.fingerId + 1;
						multitouch[i].start_x = multitouch[i].x = event.tfinger.x;
						multitouch[i].start_y = multitouch[i].y = event.tfinger.y;
						// TODO: use event.tfinger.pressure?
						break;
					}
				}
				if (i == MAXFINGERS-1)
					Con_DPrintf("Too many fingers at once!\n");
				break;
			case SDL_FINGERUP:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_FINGERUP for finger %i\n", (int)event.tfinger.fingerId);
#endif
				for (i = 0;i < MAXFINGERS-1;i++)
				{
					if (multitouch[i].state == event.tfinger.fingerId + 1)
					{
						multitouch[i].area_id = -1;
						multitouch[i].state = 0;
						break;
					}
				}
				if (i == MAXFINGERS-1)
					Con_DPrintf("No SDL_FINGERDOWN event matches this SDL_FINGERMOTION event\n");
				break;
			case SDL_FINGERMOTION:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("SDL_FINGERMOTION for finger %i\n", (int)event.tfinger.fingerId);
#endif
				for (i = 0;i < MAXFINGERS-1;i++)
				{
					if (multitouch[i].state == event.tfinger.fingerId + 1)
					{
						multitouch[i].x = event.tfinger.x;
						multitouch[i].y = event.tfinger.y;
						break;
					}
				}
				if (i == MAXFINGERS-1)
					Con_DPrintf("No SDL_FINGERDOWN event matches this SDL_FINGERMOTION event\n");
				break;
			default:
#ifdef DEBUGSDLEVENTS
				Con_DPrintf("Received unrecognized SDL_Event type 0x%x\n", event.type);
#endif
				break;
		}

	// enable/disable sound on focus gain/loss
	if ((!vid_hidden && vid_activewindow) || !snd_mutewhenidle.integer)
	{
		if (!sound_active)
		{
			S_UnblockSound ();
			sound_active = true;
		}
	}
	else
	{
		if (sound_active)
		{
			S_BlockSound ();
			sound_active = false;
		}
	}
}

/////////////////
// Video system
////

#ifdef USE_GLES2
#ifndef qglClear
#ifdef __IPHONEOS__
#include <OpenGLES/ES2/gl.h>
#else
#include <SDL_opengles.h>
#endif

//#define PRECALL //Con_Printf("GLCALL %s:%i\n", __FILE__, __LINE__)
#define PRECALL
#define POSTCALL
GLboolean wrapglIsBuffer(GLuint buffer) {PRECALL;return glIsBuffer(buffer);POSTCALL;}
GLboolean wrapglIsEnabled(GLenum cap) {PRECALL;return glIsEnabled(cap);POSTCALL;}
GLboolean wrapglIsFramebuffer(GLuint framebuffer) {PRECALL;return glIsFramebuffer(framebuffer);POSTCALL;}
//GLboolean wrapglIsQuery(GLuint qid) {PRECALL;return glIsQuery(qid);POSTCALL;}
GLboolean wrapglIsRenderbuffer(GLuint renderbuffer) {PRECALL;return glIsRenderbuffer(renderbuffer);POSTCALL;}
//GLboolean wrapglUnmapBuffer(GLenum target) {PRECALL;return glUnmapBuffer(target);POSTCALL;}
GLenum wrapglCheckFramebufferStatus(GLenum target) {PRECALL;return glCheckFramebufferStatus(target);POSTCALL;}
GLenum wrapglGetError(void) {PRECALL;return glGetError();POSTCALL;}
GLuint wrapglCreateProgram(void) {PRECALL;return glCreateProgram();POSTCALL;}
GLuint wrapglCreateShader(GLenum shaderType) {PRECALL;return glCreateShader(shaderType);POSTCALL;}
//GLuint wrapglGetHandle(GLenum pname) {PRECALL;return glGetHandle(pname);POSTCALL;}
GLint wrapglGetAttribLocation(GLuint programObj, const GLchar *name) {PRECALL;return glGetAttribLocation(programObj, name);POSTCALL;}
GLint wrapglGetUniformLocation(GLuint programObj, const GLchar *name) {PRECALL;return glGetUniformLocation(programObj, name);POSTCALL;}
//GLvoid* wrapglMapBuffer(GLenum target, GLenum access) {PRECALL;return glMapBuffer(target, access);POSTCALL;}
const GLubyte* wrapglGetString(GLenum name) {PRECALL;return (const GLubyte*)glGetString(name);POSTCALL;}
void wrapglActiveStencilFace(GLenum e) {PRECALL;Con_Printf("glActiveStencilFace(e)\n");POSTCALL;}
void wrapglActiveTexture(GLenum e) {PRECALL;glActiveTexture(e);POSTCALL;}
void wrapglAlphaFunc(GLenum func, GLclampf ref) {PRECALL;Con_Printf("glAlphaFunc(func, ref)\n");POSTCALL;}
void wrapglArrayElement(GLint i) {PRECALL;Con_Printf("glArrayElement(i)\n");POSTCALL;}
void wrapglAttachShader(GLuint containerObj, GLuint obj) {PRECALL;glAttachShader(containerObj, obj);POSTCALL;}
//void wrapglBegin(GLenum mode) {PRECALL;Con_Printf("glBegin(mode)\n");POSTCALL;}
//void wrapglBeginQuery(GLenum target, GLuint qid) {PRECALL;glBeginQuery(target, qid);POSTCALL;}
void wrapglBindAttribLocation(GLuint programObj, GLuint index, const GLchar *name) {PRECALL;glBindAttribLocation(programObj, index, name);POSTCALL;}
//void wrapglBindFragDataLocation(GLuint programObj, GLuint index, const GLchar *name) {PRECALL;glBindFragDataLocation(programObj, index, name);POSTCALL;}
void wrapglBindBuffer(GLenum target, GLuint buffer) {PRECALL;glBindBuffer(target, buffer);POSTCALL;}
void wrapglBindFramebuffer(GLenum target, GLuint framebuffer) {PRECALL;glBindFramebuffer(target, framebuffer);POSTCALL;}
void wrapglBindRenderbuffer(GLenum target, GLuint renderbuffer) {PRECALL;glBindRenderbuffer(target, renderbuffer);POSTCALL;}
void wrapglBindTexture(GLenum target, GLuint texture) {PRECALL;glBindTexture(target, texture);POSTCALL;}
void wrapglBlendEquation(GLenum e) {PRECALL;glBlendEquation(e);POSTCALL;}
void wrapglBlendFunc(GLenum sfactor, GLenum dfactor) {PRECALL;glBlendFunc(sfactor, dfactor);POSTCALL;}
void wrapglBufferData(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage) {PRECALL;glBufferData(target, size, data, usage);POSTCALL;}
void wrapglBufferSubData(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data) {PRECALL;glBufferSubData(target, offset, size, data);POSTCALL;}
void wrapglClear(GLbitfield mask) {PRECALL;glClear(mask);POSTCALL;}
void wrapglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {PRECALL;glClearColor(red, green, blue, alpha);POSTCALL;}
void wrapglClearDepth(GLclampd depth) {PRECALL;/*Con_Printf("glClearDepth(%f)\n", depth);glClearDepthf((float)depth);*/POSTCALL;}
void wrapglClearStencil(GLint s) {PRECALL;glClearStencil(s);POSTCALL;}
void wrapglClientActiveTexture(GLenum target) {PRECALL;Con_Printf("glClientActiveTexture(target)\n");POSTCALL;}
void wrapglColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {PRECALL;Con_Printf("glColor4f(red, green, blue, alpha)\n");POSTCALL;}
void wrapglColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha) {PRECALL;Con_Printf("glColor4ub(red, green, blue, alpha)\n");POSTCALL;}
void wrapglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {PRECALL;glColorMask(red, green, blue, alpha);POSTCALL;}
void wrapglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr) {PRECALL;Con_Printf("glColorPointer(size, type, stride, ptr)\n");POSTCALL;}
void wrapglCompileShader(GLuint shaderObj) {PRECALL;glCompileShader(shaderObj);POSTCALL;}
void wrapglCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border,  GLsizei imageSize, const void *data) {PRECALL;glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);POSTCALL;}
void wrapglCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) {PRECALL;Con_Printf("glCompressedTexImage3D(target, level, internalformat, width, height, depth, border, imageSize, data)\n");POSTCALL;}
void wrapglCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {PRECALL;glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);POSTCALL;}
void wrapglCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) {PRECALL;Con_Printf("glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data)\n");POSTCALL;}
void wrapglCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {PRECALL;glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);POSTCALL;}
void wrapglCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {PRECALL;glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);POSTCALL;}
void wrapglCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) {PRECALL;Con_Printf("glCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y, width, height)\n");POSTCALL;}
void wrapglCullFace(GLenum mode) {PRECALL;glCullFace(mode);POSTCALL;}
void wrapglDeleteBuffers(GLsizei n, const GLuint *buffers) {PRECALL;glDeleteBuffers(n, buffers);POSTCALL;}
void wrapglDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {PRECALL;glDeleteFramebuffers(n, framebuffers);POSTCALL;}
void wrapglDeleteShader(GLuint obj) {PRECALL;glDeleteShader(obj);POSTCALL;}
void wrapglDeleteProgram(GLuint obj) {PRECALL;glDeleteProgram(obj);POSTCALL;}
//void wrapglDeleteQueries(GLsizei n, const GLuint *ids) {PRECALL;glDeleteQueries(n, ids);POSTCALL;}
void wrapglDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {PRECALL;glDeleteRenderbuffers(n, renderbuffers);POSTCALL;}
void wrapglDeleteTextures(GLsizei n, const GLuint *textures) {PRECALL;glDeleteTextures(n, textures);POSTCALL;}
void wrapglDepthFunc(GLenum func) {PRECALL;glDepthFunc(func);POSTCALL;}
void wrapglDepthMask(GLboolean flag) {PRECALL;glDepthMask(flag);POSTCALL;}
//void wrapglDepthRange(GLclampd near_val, GLclampd far_val) {PRECALL;glDepthRangef((float)near_val, (float)far_val);POSTCALL;}
void wrapglDepthRangef(GLclampf near_val, GLclampf far_val) {PRECALL;glDepthRangef(near_val, far_val);POSTCALL;}
void wrapglDetachShader(GLuint containerObj, GLuint attachedObj) {PRECALL;glDetachShader(containerObj, attachedObj);POSTCALL;}
void wrapglDisable(GLenum cap) {PRECALL;glDisable(cap);POSTCALL;}
void wrapglDisableClientState(GLenum cap) {PRECALL;Con_Printf("glDisableClientState(cap)\n");POSTCALL;}
void wrapglDisableVertexAttribArray(GLuint index) {PRECALL;glDisableVertexAttribArray(index);POSTCALL;}
void wrapglDrawArrays(GLenum mode, GLint first, GLsizei count) {PRECALL;glDrawArrays(mode, first, count);POSTCALL;}
void wrapglDrawBuffer(GLenum mode) {PRECALL;Con_Printf("glDrawBuffer(mode)\n");POSTCALL;}
void wrapglDrawBuffers(GLsizei n, const GLenum *bufs) {PRECALL;Con_Printf("glDrawBuffers(n, bufs)\n");POSTCALL;}
void wrapglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {PRECALL;glDrawElements(mode, count, type, indices);POSTCALL;}
//void wrapglDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices) {PRECALL;glDrawRangeElements(mode, start, end, count, type, indices);POSTCALL;}
//void wrapglDrawRangeElementsEXT(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices) {PRECALL;glDrawRangeElements(mode, start, end, count, type, indices);POSTCALL;}
void wrapglEnable(GLenum cap) {PRECALL;glEnable(cap);POSTCALL;}
void wrapglEnableClientState(GLenum cap) {PRECALL;Con_Printf("glEnableClientState(cap)\n");POSTCALL;}
void wrapglEnableVertexAttribArray(GLuint index) {PRECALL;glEnableVertexAttribArray(index);POSTCALL;}
//void wrapglEnd(void) {PRECALL;Con_Printf("glEnd()\n");POSTCALL;}
//void wrapglEndQuery(GLenum target) {PRECALL;glEndQuery(target);POSTCALL;}
void wrapglFinish(void) {PRECALL;glFinish();POSTCALL;}
void wrapglFlush(void) {PRECALL;glFlush();POSTCALL;}
void wrapglFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {PRECALL;glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);POSTCALL;}
void wrapglFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {PRECALL;glFramebufferTexture2D(target, attachment, textarget, texture, level);POSTCALL;}
void wrapglFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset) {PRECALL;Con_Printf("glFramebufferTexture3D()\n");POSTCALL;}
void wrapglGenBuffers(GLsizei n, GLuint *buffers) {PRECALL;glGenBuffers(n, buffers);POSTCALL;}
void wrapglGenFramebuffers(GLsizei n, GLuint *framebuffers) {PRECALL;glGenFramebuffers(n, framebuffers);POSTCALL;}
//void wrapglGenQueries(GLsizei n, GLuint *ids) {PRECALL;glGenQueries(n, ids);POSTCALL;}
void wrapglGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {PRECALL;glGenRenderbuffers(n, renderbuffers);POSTCALL;}
void wrapglGenTextures(GLsizei n, GLuint *textures) {PRECALL;glGenTextures(n, textures);POSTCALL;}
void wrapglGenerateMipmap(GLenum target) {PRECALL;glGenerateMipmap(target);POSTCALL;}
void wrapglGetActiveAttrib(GLuint programObj, GLuint index, GLsizei maxLength, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {PRECALL;glGetActiveAttrib(programObj, index, maxLength, length, size, type, name);POSTCALL;}
void wrapglGetActiveUniform(GLuint programObj, GLuint index, GLsizei maxLength, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {PRECALL;glGetActiveUniform(programObj, index, maxLength, length, size, type, name);POSTCALL;}
void wrapglGetAttachedShaders(GLuint containerObj, GLsizei maxCount, GLsizei *count, GLuint *obj) {PRECALL;glGetAttachedShaders(containerObj, maxCount, count, obj);POSTCALL;}
void wrapglGetBooleanv(GLenum pname, GLboolean *params) {PRECALL;glGetBooleanv(pname, params);POSTCALL;}
void wrapglGetCompressedTexImage(GLenum target, GLint lod, void *img) {PRECALL;Con_Printf("glGetCompressedTexImage(target, lod, img)\n");POSTCALL;}
void wrapglGetDoublev(GLenum pname, GLdouble *params) {PRECALL;Con_Printf("glGetDoublev(pname, params)\n");POSTCALL;}
void wrapglGetFloatv(GLenum pname, GLfloat *params) {PRECALL;glGetFloatv(pname, params);POSTCALL;}
void wrapglGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {PRECALL;glGetFramebufferAttachmentParameteriv(target, attachment, pname, params);POSTCALL;}
void wrapglGetShaderInfoLog(GLuint obj, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {PRECALL;glGetShaderInfoLog(obj, maxLength, length, infoLog);POSTCALL;}
void wrapglGetProgramInfoLog(GLuint obj, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {PRECALL;glGetProgramInfoLog(obj, maxLength, length, infoLog);POSTCALL;}
void wrapglGetIntegerv(GLenum pname, GLint *params) {PRECALL;glGetIntegerv(pname, params);POSTCALL;}
void wrapglGetShaderiv(GLuint obj, GLenum pname, GLint *params) {PRECALL;glGetShaderiv(obj, pname, params);POSTCALL;}
void wrapglGetProgramiv(GLuint obj, GLenum pname, GLint *params) {PRECALL;glGetProgramiv(obj, pname, params);POSTCALL;}
//void wrapglGetQueryObjectiv(GLuint qid, GLenum pname, GLint *params) {PRECALL;glGetQueryObjectiv(qid, pname, params);POSTCALL;}
//void wrapglGetQueryObjectuiv(GLuint qid, GLenum pname, GLuint *params) {PRECALL;glGetQueryObjectuiv(qid, pname, params);POSTCALL;}
//void wrapglGetQueryiv(GLenum target, GLenum pname, GLint *params) {PRECALL;glGetQueryiv(target, pname, params);POSTCALL;}
void wrapglGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {PRECALL;glGetRenderbufferParameteriv(target, pname, params);POSTCALL;}
void wrapglGetShaderSource(GLuint obj, GLsizei maxLength, GLsizei *length, GLchar *source) {PRECALL;glGetShaderSource(obj, maxLength, length, source);POSTCALL;}
void wrapglGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels) {PRECALL;Con_Printf("glGetTexImage(target, level, format, type, pixels)\n");POSTCALL;}
void wrapglGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {PRECALL;Con_Printf("glGetTexLevelParameterfv(target, level, pname, params)\n");POSTCALL;}
void wrapglGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {PRECALL;Con_Printf("glGetTexLevelParameteriv(target, level, pname, params)\n");POSTCALL;}
void wrapglGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {PRECALL;glGetTexParameterfv(target, pname, params);POSTCALL;}
void wrapglGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {PRECALL;glGetTexParameteriv(target, pname, params);POSTCALL;}
void wrapglGetUniformfv(GLuint programObj, GLint location, GLfloat *params) {PRECALL;glGetUniformfv(programObj, location, params);POSTCALL;}
void wrapglGetUniformiv(GLuint programObj, GLint location, GLint *params) {PRECALL;glGetUniformiv(programObj, location, params);POSTCALL;}
void wrapglHint(GLenum target, GLenum mode) {PRECALL;glHint(target, mode);POSTCALL;}
void wrapglLineWidth(GLfloat width) {PRECALL;glLineWidth(width);POSTCALL;}
void wrapglLinkProgram(GLuint programObj) {PRECALL;glLinkProgram(programObj);POSTCALL;}
void wrapglLoadIdentity(void) {PRECALL;Con_Printf("glLoadIdentity()\n");POSTCALL;}
void wrapglLoadMatrixf(const GLfloat *m) {PRECALL;Con_Printf("glLoadMatrixf(m)\n");POSTCALL;}
void wrapglMatrixMode(GLenum mode) {PRECALL;Con_Printf("glMatrixMode(mode)\n");POSTCALL;}
void wrapglMultiTexCoord1f(GLenum target, GLfloat s) {PRECALL;Con_Printf("glMultiTexCoord1f(target, s)\n");POSTCALL;}
void wrapglMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t) {PRECALL;Con_Printf("glMultiTexCoord2f(target, s, t)\n");POSTCALL;}
void wrapglMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r) {PRECALL;Con_Printf("glMultiTexCoord3f(target, s, t, r)\n");POSTCALL;}
void wrapglMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {PRECALL;Con_Printf("glMultiTexCoord4f(target, s, t, r, q)\n");POSTCALL;}
void wrapglNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr) {PRECALL;Con_Printf("glNormalPointer(type, stride, ptr)\n");POSTCALL;}
void wrapglPixelStorei(GLenum pname, GLint param) {PRECALL;glPixelStorei(pname, param);POSTCALL;}
void wrapglPointSize(GLfloat size) {PRECALL;Con_Printf("glPointSize(size)\n");POSTCALL;}
//void wrapglPolygonMode(GLenum face, GLenum mode) {PRECALL;Con_Printf("glPolygonMode(face, mode)\n");POSTCALL;}
void wrapglPolygonOffset(GLfloat factor, GLfloat units) {PRECALL;glPolygonOffset(factor, units);POSTCALL;}
void wrapglPolygonStipple(const GLubyte *mask) {PRECALL;Con_Printf("glPolygonStipple(mask)\n");POSTCALL;}
void wrapglReadBuffer(GLenum mode) {PRECALL;Con_Printf("glReadBuffer(mode)\n");POSTCALL;}
void wrapglReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {PRECALL;glReadPixels(x, y, width, height, format, type, pixels);POSTCALL;}
void wrapglRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {PRECALL;glRenderbufferStorage(target, internalformat, width, height);POSTCALL;}
void wrapglScissor(GLint x, GLint y, GLsizei width, GLsizei height) {PRECALL;glScissor(x, y, width, height);POSTCALL;}
void wrapglShaderSource(GLuint shaderObj, GLsizei count, const GLchar **string, const GLint *length) {PRECALL;glShaderSource(shaderObj, count, string, length);POSTCALL;}
void wrapglStencilFunc(GLenum func, GLint ref, GLuint mask) {PRECALL;glStencilFunc(func, ref, mask);POSTCALL;}
void wrapglStencilFuncSeparate(GLenum func1, GLenum func2, GLint ref, GLuint mask) {PRECALL;Con_Printf("glStencilFuncSeparate(func1, func2, ref, mask)\n");POSTCALL;}
void wrapglStencilMask(GLuint mask) {PRECALL;glStencilMask(mask);POSTCALL;}
void wrapglStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {PRECALL;glStencilOp(fail, zfail, zpass);POSTCALL;}
void wrapglStencilOpSeparate(GLenum e1, GLenum e2, GLenum e3, GLenum e4) {PRECALL;Con_Printf("glStencilOpSeparate(e1, e2, e3, e4)\n");POSTCALL;}
void wrapglTexCoord1f(GLfloat s) {PRECALL;Con_Printf("glTexCoord1f(s)\n");POSTCALL;}
void wrapglTexCoord2f(GLfloat s, GLfloat t) {PRECALL;Con_Printf("glTexCoord2f(s, t)\n");POSTCALL;}
void wrapglTexCoord3f(GLfloat s, GLfloat t, GLfloat r) {PRECALL;Con_Printf("glTexCoord3f(s, t, r)\n");POSTCALL;}
void wrapglTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {PRECALL;Con_Printf("glTexCoord4f(s, t, r, q)\n");POSTCALL;}
void wrapglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr) {PRECALL;Con_Printf("glTexCoordPointer(size, type, stride, ptr)\n");POSTCALL;}
void wrapglTexEnvf(GLenum target, GLenum pname, GLfloat param) {PRECALL;Con_Printf("glTexEnvf(target, pname, param)\n");POSTCALL;}
void wrapglTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {PRECALL;Con_Printf("glTexEnvfv(target, pname, params)\n");POSTCALL;}
void wrapglTexEnvi(GLenum target, GLenum pname, GLint param) {PRECALL;Con_Printf("glTexEnvi(target, pname, param)\n");POSTCALL;}
void wrapglTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {PRECALL;glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);POSTCALL;}
void wrapglTexImage3D(GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {PRECALL;Con_Printf("glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels)\n");POSTCALL;}
void wrapglTexParameterf(GLenum target, GLenum pname, GLfloat param) {PRECALL;glTexParameterf(target, pname, param);POSTCALL;}
void wrapglTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {PRECALL;glTexParameterfv(target, pname, params);POSTCALL;}
void wrapglTexParameteri(GLenum target, GLenum pname, GLint param) {PRECALL;glTexParameteri(target, pname, param);POSTCALL;}
void wrapglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {PRECALL;glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);POSTCALL;}
void wrapglTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels) {PRECALL;Con_Printf("glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels)\n");POSTCALL;}
void wrapglUniform1f(GLint location, GLfloat v0) {PRECALL;glUniform1f(location, v0);POSTCALL;}
void wrapglUniform1fv(GLint location, GLsizei count, const GLfloat *value) {PRECALL;glUniform1fv(location, count, value);POSTCALL;}
void wrapglUniform1i(GLint location, GLint v0) {PRECALL;glUniform1i(location, v0);POSTCALL;}
void wrapglUniform1iv(GLint location, GLsizei count, const GLint *value) {PRECALL;glUniform1iv(location, count, value);POSTCALL;}
void wrapglUniform2f(GLint location, GLfloat v0, GLfloat v1) {PRECALL;glUniform2f(location, v0, v1);POSTCALL;}
void wrapglUniform2fv(GLint location, GLsizei count, const GLfloat *value) {PRECALL;glUniform2fv(location, count, value);POSTCALL;}
void wrapglUniform2i(GLint location, GLint v0, GLint v1) {PRECALL;glUniform2i(location, v0, v1);POSTCALL;}
void wrapglUniform2iv(GLint location, GLsizei count, const GLint *value) {PRECALL;glUniform2iv(location, count, value);POSTCALL;}
void wrapglUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {PRECALL;glUniform3f(location, v0, v1, v2);POSTCALL;}
void wrapglUniform3fv(GLint location, GLsizei count, const GLfloat *value) {PRECALL;glUniform3fv(location, count, value);POSTCALL;}
void wrapglUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {PRECALL;glUniform3i(location, v0, v1, v2);POSTCALL;}
void wrapglUniform3iv(GLint location, GLsizei count, const GLint *value) {PRECALL;glUniform3iv(location, count, value);POSTCALL;}
void wrapglUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {PRECALL;glUniform4f(location, v0, v1, v2, v3);POSTCALL;}
void wrapglUniform4fv(GLint location, GLsizei count, const GLfloat *value) {PRECALL;glUniform4fv(location, count, value);POSTCALL;}
void wrapglUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {PRECALL;glUniform4i(location, v0, v1, v2, v3);POSTCALL;}
void wrapglUniform4iv(GLint location, GLsizei count, const GLint *value) {PRECALL;glUniform4iv(location, count, value);POSTCALL;}
void wrapglUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {PRECALL;glUniformMatrix2fv(location, count, transpose, value);POSTCALL;}
void wrapglUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {PRECALL;glUniformMatrix3fv(location, count, transpose, value);POSTCALL;}
void wrapglUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {PRECALL;glUniformMatrix4fv(location, count, transpose, value);POSTCALL;}
void wrapglUseProgram(GLuint programObj) {PRECALL;glUseProgram(programObj);POSTCALL;}
void wrapglValidateProgram(GLuint programObj) {PRECALL;glValidateProgram(programObj);POSTCALL;}
void wrapglVertex2f(GLfloat x, GLfloat y) {PRECALL;Con_Printf("glVertex2f(x, y)\n");POSTCALL;}
void wrapglVertex3f(GLfloat x, GLfloat y, GLfloat z) {PRECALL;Con_Printf("glVertex3f(x, y, z)\n");POSTCALL;}
void wrapglVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {PRECALL;Con_Printf("glVertex4f(x, y, z, w)\n");POSTCALL;}
void wrapglVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer) {PRECALL;glVertexAttribPointer(index, size, type, normalized, stride, pointer);POSTCALL;}
void wrapglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr) {PRECALL;Con_Printf("glVertexPointer(size, type, stride, ptr)\n");POSTCALL;}
void wrapglViewport(GLint x, GLint y, GLsizei width, GLsizei height) {PRECALL;glViewport(x, y, width, height);POSTCALL;}
void wrapglVertexAttrib1f(GLuint index, GLfloat v0) {PRECALL;glVertexAttrib1f(index, v0);POSTCALL;}
//void wrapglVertexAttrib1s(GLuint index, GLshort v0) {PRECALL;glVertexAttrib1s(index, v0);POSTCALL;}
//void wrapglVertexAttrib1d(GLuint index, GLdouble v0) {PRECALL;glVertexAttrib1d(index, v0);POSTCALL;}
void wrapglVertexAttrib2f(GLuint index, GLfloat v0, GLfloat v1) {PRECALL;glVertexAttrib2f(index, v0, v1);POSTCALL;}
//void wrapglVertexAttrib2s(GLuint index, GLshort v0, GLshort v1) {PRECALL;glVertexAttrib2s(index, v0, v1);POSTCALL;}
//void wrapglVertexAttrib2d(GLuint index, GLdouble v0, GLdouble v1) {PRECALL;glVertexAttrib2d(index, v0, v1);POSTCALL;}
void wrapglVertexAttrib3f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2) {PRECALL;glVertexAttrib3f(index, v0, v1, v2);POSTCALL;}
//void wrapglVertexAttrib3s(GLuint index, GLshort v0, GLshort v1, GLshort v2) {PRECALL;glVertexAttrib3s(index, v0, v1, v2);POSTCALL;}
//void wrapglVertexAttrib3d(GLuint index, GLdouble v0, GLdouble v1, GLdouble v2) {PRECALL;glVertexAttrib3d(index, v0, v1, v2);POSTCALL;}
void wrapglVertexAttrib4f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {PRECALL;glVertexAttrib4f(index, v0, v1, v2, v3);POSTCALL;}
//void wrapglVertexAttrib4s(GLuint index, GLshort v0, GLshort v1, GLshort v2, GLshort v3) {PRECALL;glVertexAttrib4s(index, v0, v1, v2, v3);POSTCALL;}
//void wrapglVertexAttrib4d(GLuint index, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3) {PRECALL;glVertexAttrib4d(index, v0, v1, v2, v3);POSTCALL;}
//void wrapglVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) {PRECALL;glVertexAttrib4Nub(index, x, y, z, w);POSTCALL;}
void wrapglVertexAttrib1fv(GLuint index, const GLfloat *v) {PRECALL;glVertexAttrib1fv(index, v);POSTCALL;}
//void wrapglVertexAttrib1sv(GLuint index, const GLshort *v) {PRECALL;glVertexAttrib1sv(index, v);POSTCALL;}
//void wrapglVertexAttrib1dv(GLuint index, const GLdouble *v) {PRECALL;glVertexAttrib1dv(index, v);POSTCALL;}
void wrapglVertexAttrib2fv(GLuint index, const GLfloat *v) {PRECALL;glVertexAttrib2fv(index, v);POSTCALL;}
//void wrapglVertexAttrib2sv(GLuint index, const GLshort *v) {PRECALL;glVertexAttrib2sv(index, v);POSTCALL;}
//void wrapglVertexAttrib2dv(GLuint index, const GLdouble *v) {PRECALL;glVertexAttrib2dv(index, v);POSTCALL;}
void wrapglVertexAttrib3fv(GLuint index, const GLfloat *v) {PRECALL;glVertexAttrib3fv(index, v);POSTCALL;}
//void wrapglVertexAttrib3sv(GLuint index, const GLshort *v) {PRECALL;glVertexAttrib3sv(index, v);POSTCALL;}
//void wrapglVertexAttrib3dv(GLuint index, const GLdouble *v) {PRECALL;glVertexAttrib3dv(index, v);POSTCALL;}
void wrapglVertexAttrib4fv(GLuint index, const GLfloat *v) {PRECALL;glVertexAttrib4fv(index, v);POSTCALL;}
//void wrapglVertexAttrib4sv(GLuint index, const GLshort *v) {PRECALL;glVertexAttrib4sv(index, v);POSTCALL;}
//void wrapglVertexAttrib4dv(GLuint index, const GLdouble *v) {PRECALL;glVertexAttrib4dv(index, v);POSTCALL;}
//void wrapglVertexAttrib4iv(GLuint index, const GLint *v) {PRECALL;glVertexAttrib4iv(index, v);POSTCALL;}
//void wrapglVertexAttrib4bv(GLuint index, const GLbyte *v) {PRECALL;glVertexAttrib4bv(index, v);POSTCALL;}
//void wrapglVertexAttrib4ubv(GLuint index, const GLubyte *v) {PRECALL;glVertexAttrib4ubv(index, v);POSTCALL;}
//void wrapglVertexAttrib4usv(GLuint index, const GLushort *v) {PRECALL;glVertexAttrib4usv(index, GLushort v);POSTCALL;}
//void wrapglVertexAttrib4uiv(GLuint index, const GLuint *v) {PRECALL;glVertexAttrib4uiv(index, v);POSTCALL;}
//void wrapglVertexAttrib4Nbv(GLuint index, const GLbyte *v) {PRECALL;glVertexAttrib4Nbv(index, v);POSTCALL;}
//void wrapglVertexAttrib4Nsv(GLuint index, const GLshort *v) {PRECALL;glVertexAttrib4Nsv(index, v);POSTCALL;}
//void wrapglVertexAttrib4Niv(GLuint index, const GLint *v) {PRECALL;glVertexAttrib4Niv(index, v);POSTCALL;}
//void wrapglVertexAttrib4Nubv(GLuint index, const GLubyte *v) {PRECALL;glVertexAttrib4Nubv(index, v);POSTCALL;}
//void wrapglVertexAttrib4Nusv(GLuint index, const GLushort *v) {PRECALL;glVertexAttrib4Nusv(index, GLushort v);POSTCALL;}
//void wrapglVertexAttrib4Nuiv(GLuint index, const GLuint *v) {PRECALL;glVertexAttrib4Nuiv(index, v);POSTCALL;}
//void wrapglGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params) {PRECALL;glGetVertexAttribdv(index, pname, params);POSTCALL;}
void wrapglGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {PRECALL;glGetVertexAttribfv(index, pname, params);POSTCALL;}
void wrapglGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {PRECALL;glGetVertexAttribiv(index, pname, params);POSTCALL;}
void wrapglGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer) {PRECALL;glGetVertexAttribPointerv(index, pname, pointer);POSTCALL;}
#endif

static void GLES_Init(void)
{
#ifndef qglClear
	qglIsBufferARB = wrapglIsBuffer;
	qglIsEnabled = wrapglIsEnabled;
	qglIsFramebufferEXT = wrapglIsFramebuffer;
//	qglIsQueryARB = wrapglIsQuery;
	qglIsRenderbufferEXT = wrapglIsRenderbuffer;
//	qglUnmapBufferARB = wrapglUnmapBuffer;
	qglCheckFramebufferStatus = wrapglCheckFramebufferStatus;
	qglGetError = wrapglGetError;
	qglCreateProgram = wrapglCreateProgram;
	qglCreateShader = wrapglCreateShader;
//	qglGetHandleARB = wrapglGetHandle;
	qglGetAttribLocation = wrapglGetAttribLocation;
	qglGetUniformLocation = wrapglGetUniformLocation;
//	qglMapBufferARB = wrapglMapBuffer;
	qglGetString = wrapglGetString;
//	qglActiveStencilFaceEXT = wrapglActiveStencilFace;
	qglActiveTexture = wrapglActiveTexture;
	qglAlphaFunc = wrapglAlphaFunc;
	qglArrayElement = wrapglArrayElement;
	qglAttachShader = wrapglAttachShader;
//	qglBegin = wrapglBegin;
//	qglBeginQueryARB = wrapglBeginQuery;
	qglBindAttribLocation = wrapglBindAttribLocation;
//	qglBindFragDataLocation = wrapglBindFragDataLocation;
	qglBindBufferARB = wrapglBindBuffer;
	qglBindFramebuffer = wrapglBindFramebuffer;
	qglBindRenderbuffer = wrapglBindRenderbuffer;
	qglBindTexture = wrapglBindTexture;
	qglBlendEquationEXT = wrapglBlendEquation;
	qglBlendFunc = wrapglBlendFunc;
	qglBufferDataARB = wrapglBufferData;
	qglBufferSubDataARB = wrapglBufferSubData;
	qglClear = wrapglClear;
	qglClearColor = wrapglClearColor;
	qglClearDepth = wrapglClearDepth;
	qglClearStencil = wrapglClearStencil;
	qglClientActiveTexture = wrapglClientActiveTexture;
	qglColor4f = wrapglColor4f;
	qglColor4ub = wrapglColor4ub;
	qglColorMask = wrapglColorMask;
	qglColorPointer = wrapglColorPointer;
	qglCompileShader = wrapglCompileShader;
	qglCompressedTexImage2DARB = wrapglCompressedTexImage2D;
	qglCompressedTexImage3DARB = wrapglCompressedTexImage3D;
	qglCompressedTexSubImage2DARB = wrapglCompressedTexSubImage2D;
	qglCompressedTexSubImage3DARB = wrapglCompressedTexSubImage3D;
	qglCopyTexImage2D = wrapglCopyTexImage2D;
	qglCopyTexSubImage2D = wrapglCopyTexSubImage2D;
	qglCopyTexSubImage3D = wrapglCopyTexSubImage3D;
	qglCullFace = wrapglCullFace;
	qglDeleteBuffersARB = wrapglDeleteBuffers;
	qglDeleteFramebuffers = wrapglDeleteFramebuffers;
	qglDeleteProgram = wrapglDeleteProgram;
	qglDeleteShader = wrapglDeleteShader;
//	qglDeleteQueriesARB = wrapglDeleteQueries;
	qglDeleteRenderbuffers = wrapglDeleteRenderbuffers;
	qglDeleteTextures = wrapglDeleteTextures;
	qglDepthFunc = wrapglDepthFunc;
	qglDepthMask = wrapglDepthMask;
	qglDepthRangef = wrapglDepthRangef;
	qglDetachShader = wrapglDetachShader;
	qglDisable = wrapglDisable;
	qglDisableClientState = wrapglDisableClientState;
	qglDisableVertexAttribArray = wrapglDisableVertexAttribArray;
	qglDrawArrays = wrapglDrawArrays;
//	qglDrawBuffer = wrapglDrawBuffer;
//	qglDrawBuffersARB = wrapglDrawBuffers;
	qglDrawElements = wrapglDrawElements;
//	qglDrawRangeElements = wrapglDrawRangeElements;
	qglEnable = wrapglEnable;
	qglEnableClientState = wrapglEnableClientState;
	qglEnableVertexAttribArray = wrapglEnableVertexAttribArray;
//	qglEnd = wrapglEnd;
//	qglEndQueryARB = wrapglEndQuery;
	qglFinish = wrapglFinish;
	qglFlush = wrapglFlush;
	qglFramebufferRenderbufferEXT = wrapglFramebufferRenderbuffer;
	qglFramebufferTexture2DEXT = wrapglFramebufferTexture2D;
	qglFramebufferTexture3DEXT = wrapglFramebufferTexture3D;
	qglGenBuffersARB = wrapglGenBuffers;
	qglGenFramebuffers = wrapglGenFramebuffers;
//	qglGenQueriesARB = wrapglGenQueries;
	qglGenRenderbuffers = wrapglGenRenderbuffers;
	qglGenTextures = wrapglGenTextures;
	qglGenerateMipmapEXT = wrapglGenerateMipmap;
	qglGetActiveAttrib = wrapglGetActiveAttrib;
	qglGetActiveUniform = wrapglGetActiveUniform;
	qglGetAttachedShaders = wrapglGetAttachedShaders;
	qglGetBooleanv = wrapglGetBooleanv;
//	qglGetCompressedTexImageARB = wrapglGetCompressedTexImage;
	qglGetDoublev = wrapglGetDoublev;
	qglGetFloatv = wrapglGetFloatv;
	qglGetFramebufferAttachmentParameterivEXT = wrapglGetFramebufferAttachmentParameteriv;
	qglGetProgramInfoLog = wrapglGetProgramInfoLog;
	qglGetShaderInfoLog = wrapglGetShaderInfoLog;
	qglGetIntegerv = wrapglGetIntegerv;
	qglGetShaderiv = wrapglGetShaderiv;
	qglGetProgramiv = wrapglGetProgramiv;
//	qglGetQueryObjectivARB = wrapglGetQueryObjectiv;
//	qglGetQueryObjectuivARB = wrapglGetQueryObjectuiv;
//	qglGetQueryivARB = wrapglGetQueryiv;
	qglGetRenderbufferParameterivEXT = wrapglGetRenderbufferParameteriv;
	qglGetShaderSource = wrapglGetShaderSource;
	qglGetTexImage = wrapglGetTexImage;
	qglGetTexLevelParameterfv = wrapglGetTexLevelParameterfv;
	qglGetTexLevelParameteriv = wrapglGetTexLevelParameteriv;
	qglGetTexParameterfv = wrapglGetTexParameterfv;
	qglGetTexParameteriv = wrapglGetTexParameteriv;
	qglGetUniformfv = wrapglGetUniformfv;
	qglGetUniformiv = wrapglGetUniformiv;
	qglHint = wrapglHint;
	qglLineWidth = wrapglLineWidth;
	qglLinkProgram = wrapglLinkProgram;
	qglLoadIdentity = wrapglLoadIdentity;
	qglLoadMatrixf = wrapglLoadMatrixf;
	qglMatrixMode = wrapglMatrixMode;
	qglMultiTexCoord1f = wrapglMultiTexCoord1f;
	qglMultiTexCoord2f = wrapglMultiTexCoord2f;
	qglMultiTexCoord3f = wrapglMultiTexCoord3f;
	qglMultiTexCoord4f = wrapglMultiTexCoord4f;
	qglNormalPointer = wrapglNormalPointer;
	qglPixelStorei = wrapglPixelStorei;
	qglPointSize = wrapglPointSize;
//	qglPolygonMode = wrapglPolygonMode;
	qglPolygonOffset = wrapglPolygonOffset;
//	qglPolygonStipple = wrapglPolygonStipple;
	qglReadBuffer = wrapglReadBuffer;
	qglReadPixels = wrapglReadPixels;
	qglRenderbufferStorage = wrapglRenderbufferStorage;
	qglScissor = wrapglScissor;
	qglShaderSource = wrapglShaderSource;
	qglStencilFunc = wrapglStencilFunc;
	qglStencilFuncSeparate = wrapglStencilFuncSeparate;
	qglStencilMask = wrapglStencilMask;
	qglStencilOp = wrapglStencilOp;
	qglStencilOpSeparate = wrapglStencilOpSeparate;
	qglTexCoord1f = wrapglTexCoord1f;
	qglTexCoord2f = wrapglTexCoord2f;
	qglTexCoord3f = wrapglTexCoord3f;
	qglTexCoord4f = wrapglTexCoord4f;
	qglTexCoordPointer = wrapglTexCoordPointer;
	qglTexEnvf = wrapglTexEnvf;
	qglTexEnvfv = wrapglTexEnvfv;
	qglTexEnvi = wrapglTexEnvi;
	qglTexImage2D = wrapglTexImage2D;
	qglTexImage3D = wrapglTexImage3D;
	qglTexParameterf = wrapglTexParameterf;
	qglTexParameterfv = wrapglTexParameterfv;
	qglTexParameteri = wrapglTexParameteri;
	qglTexSubImage2D = wrapglTexSubImage2D;
	qglTexSubImage3D = wrapglTexSubImage3D;
	qglUniform1f = wrapglUniform1f;
	qglUniform1fv = wrapglUniform1fv;
	qglUniform1i = wrapglUniform1i;
	qglUniform1iv = wrapglUniform1iv;
	qglUniform2f = wrapglUniform2f;
	qglUniform2fv = wrapglUniform2fv;
	qglUniform2i = wrapglUniform2i;
	qglUniform2iv = wrapglUniform2iv;
	qglUniform3f = wrapglUniform3f;
	qglUniform3fv = wrapglUniform3fv;
	qglUniform3i = wrapglUniform3i;
	qglUniform3iv = wrapglUniform3iv;
	qglUniform4f = wrapglUniform4f;
	qglUniform4fv = wrapglUniform4fv;
	qglUniform4i = wrapglUniform4i;
	qglUniform4iv = wrapglUniform4iv;
	qglUniformMatrix2fv = wrapglUniformMatrix2fv;
	qglUniformMatrix3fv = wrapglUniformMatrix3fv;
	qglUniformMatrix4fv = wrapglUniformMatrix4fv;
	qglUseProgram = wrapglUseProgram;
	qglValidateProgram = wrapglValidateProgram;
	qglVertex2f = wrapglVertex2f;
	qglVertex3f = wrapglVertex3f;
	qglVertex4f = wrapglVertex4f;
	qglVertexAttribPointer = wrapglVertexAttribPointer;
	qglVertexPointer = wrapglVertexPointer;
	qglViewport = wrapglViewport;
	qglVertexAttrib1f = wrapglVertexAttrib1f;
//	qglVertexAttrib1s = wrapglVertexAttrib1s;
//	qglVertexAttrib1d = wrapglVertexAttrib1d;
	qglVertexAttrib2f = wrapglVertexAttrib2f;
//	qglVertexAttrib2s = wrapglVertexAttrib2s;
//	qglVertexAttrib2d = wrapglVertexAttrib2d;
	qglVertexAttrib3f = wrapglVertexAttrib3f;
//	qglVertexAttrib3s = wrapglVertexAttrib3s;
//	qglVertexAttrib3d = wrapglVertexAttrib3d;
	qglVertexAttrib4f = wrapglVertexAttrib4f;
//	qglVertexAttrib4s = wrapglVertexAttrib4s;
//	qglVertexAttrib4d = wrapglVertexAttrib4d;
//	qglVertexAttrib4Nub = wrapglVertexAttrib4Nub;
	qglVertexAttrib1fv = wrapglVertexAttrib1fv;
//	qglVertexAttrib1sv = wrapglVertexAttrib1sv;
//	qglVertexAttrib1dv = wrapglVertexAttrib1dv;
	qglVertexAttrib2fv = wrapglVertexAttrib2fv;
//	qglVertexAttrib2sv = wrapglVertexAttrib2sv;
//	qglVertexAttrib2dv = wrapglVertexAttrib2dv;
	qglVertexAttrib3fv = wrapglVertexAttrib3fv;
//	qglVertexAttrib3sv = wrapglVertexAttrib3sv;
//	qglVertexAttrib3dv = wrapglVertexAttrib3dv;
	qglVertexAttrib4fv = wrapglVertexAttrib4fv;
//	qglVertexAttrib4sv = wrapglVertexAttrib4sv;
//	qglVertexAttrib4dv = wrapglVertexAttrib4dv;
//	qglVertexAttrib4iv = wrapglVertexAttrib4iv;
//	qglVertexAttrib4bv = wrapglVertexAttrib4bv;
//	qglVertexAttrib4ubv = wrapglVertexAttrib4ubv;
//	qglVertexAttrib4usv = wrapglVertexAttrib4usv;
//	qglVertexAttrib4uiv = wrapglVertexAttrib4uiv;
//	qglVertexAttrib4Nbv = wrapglVertexAttrib4Nbv;
//	qglVertexAttrib4Nsv = wrapglVertexAttrib4Nsv;
//	qglVertexAttrib4Niv = wrapglVertexAttrib4Niv;
//	qglVertexAttrib4Nubv = wrapglVertexAttrib4Nubv;
//	qglVertexAttrib4Nusv = wrapglVertexAttrib4Nusv;
//	qglVertexAttrib4Nuiv = wrapglVertexAttrib4Nuiv;
//	qglGetVertexAttribdv = wrapglGetVertexAttribdv;
	qglGetVertexAttribfv = wrapglGetVertexAttribfv;
	qglGetVertexAttribiv = wrapglGetVertexAttribiv;
	qglGetVertexAttribPointerv = wrapglGetVertexAttribPointerv;
#endif

	gl_renderer = (const char *)qglGetString(GL_RENDERER);
	gl_vendor = (const char *)qglGetString(GL_VENDOR);
	gl_version = (const char *)qglGetString(GL_VERSION);
	gl_extensions = (const char *)qglGetString(GL_EXTENSIONS);
	
	if (!gl_extensions)
		gl_extensions = "";
	if (!gl_platformextensions)
		gl_platformextensions = "";
	
	Con_Printf("GL_VENDOR: %s\n", gl_vendor);
	Con_Printf("GL_RENDERER: %s\n", gl_renderer);
	Con_Printf("GL_VERSION: %s\n", gl_version);
	Con_DPrintf("GL_EXTENSIONS: %s\n", gl_extensions);
	Con_DPrintf("%s_EXTENSIONS: %s\n", gl_platform, gl_platformextensions);
	
	// LordHavoc: report supported extensions
	Con_DPrintf("\nQuakeC extensions for server and client: %s\nQuakeC extensions for menu: %s\n", vm_sv_extensions, vm_m_extensions );

	// GLES devices in general do not like GL_BGRA, so use GL_RGBA
	vid.forcetextype = TEXTYPE_RGBA;
	
	vid.support.gl20shaders = true;
	vid.support.amd_texture_texture4 = false;
	vid.support.arb_depth_texture = SDL_GL_ExtensionSupported("GL_OES_depth_texture") != 0; // renderbuffer used anyway on gles2?
	vid.support.arb_draw_buffers = false;
	vid.support.arb_multitexture = false;
	vid.support.arb_occlusion_query = false;
	vid.support.arb_query_buffer_object = false;
	vid.support.arb_shadow = false;
	vid.support.arb_texture_compression = false; // different (vendor-specific) formats than on desktop OpenGL...
	vid.support.arb_texture_cube_map = SDL_GL_ExtensionSupported("GL_OES_texture_cube_map") != 0;
	vid.support.arb_texture_env_combine = false;
	vid.support.arb_texture_gather = false;
	vid.support.arb_texture_non_power_of_two = strstr(gl_extensions, "GL_OES_texture_npot") != NULL;
	vid.support.arb_vertex_buffer_object = true; // GLES2 core
	vid.support.ati_separate_stencil = false;
	vid.support.ext_blend_minmax = false;
	vid.support.ext_blend_subtract = true; // GLES2 core
	vid.support.ext_blend_func_separate = true; // GLES2 core
	vid.support.ext_draw_range_elements = false;

	/*	ELUAN:
		Note: "In OS 2.1, the functions in GL_OES_framebuffer_object were not usable from the Java API.
		Calling them just threw an exception. Android developer relations confirmed that they forgot to implement these. (yeah...)
		It's apparently been fixed in 2.2, though I haven't tested."
	*/
	vid.support.ext_framebuffer_object = false;//true;

	vid.support.ext_packed_depth_stencil = false;
	vid.support.ext_stencil_two_side = false;
	vid.support.ext_texture_3d = SDL_GL_ExtensionSupported("GL_OES_texture_3D") != 0;
	vid.support.ext_texture_compression_s3tc = SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc") != 0;
	vid.support.ext_texture_edge_clamp = true; // GLES2 core
	vid.support.ext_texture_filter_anisotropic = false; // probably don't want to use it...
	vid.support.ext_texture_srgb = false;
	vid.support.arb_texture_float = SDL_GL_ExtensionSupported("GL_OES_texture_float") != 0;
	vid.support.arb_half_float_pixel = SDL_GL_ExtensionSupported("GL_OES_texture_half_float") != 0;
	vid.support.arb_half_float_vertex = SDL_GL_ExtensionSupported("GL_OES_vertex_half_float") != 0;

	// NOTE: On some devices, a value of 512 gives better FPS than the maximum.
	qglGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint*)&vid.maxtexturesize_2d);

#ifdef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
	if (vid.support.ext_texture_filter_anisotropic)
		qglGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, (GLint*)&vid.max_anisotropy);
#endif
	if (vid.support.arb_texture_cube_map)
		qglGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, (GLint*)&vid.maxtexturesize_cubemap);
#ifdef GL_MAX_3D_TEXTURE_SIZE
	if (vid.support.ext_texture_3d)
		qglGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, (GLint*)&vid.maxtexturesize_3d);
#endif
	Con_Printf("GL_MAX_CUBE_MAP_TEXTURE_SIZE = %i\n", vid.maxtexturesize_cubemap);
	Con_Printf("GL_MAX_3D_TEXTURE_SIZE = %i\n", vid.maxtexturesize_3d);
	{
#define GL_ALPHA_BITS                           0x0D55
#define GL_RED_BITS                             0x0D52
#define GL_GREEN_BITS                           0x0D53
#define GL_BLUE_BITS                            0x0D54
#define GL_DEPTH_BITS                           0x0D56
#define GL_STENCIL_BITS                         0x0D57
		int fb_r = -1, fb_g = -1, fb_b = -1, fb_a = -1, fb_d = -1, fb_s = -1;
		qglGetIntegerv(GL_RED_BITS    , &fb_r);
		qglGetIntegerv(GL_GREEN_BITS  , &fb_g);
		qglGetIntegerv(GL_BLUE_BITS   , &fb_b);
		qglGetIntegerv(GL_ALPHA_BITS  , &fb_a);
		qglGetIntegerv(GL_DEPTH_BITS  , &fb_d);
		qglGetIntegerv(GL_STENCIL_BITS, &fb_s);
		Con_Printf("Framebuffer depth is R%iG%iB%iA%iD%iS%i\n", fb_r, fb_g, fb_b, fb_a, fb_d, fb_s);
	}

	// verify that cubemap textures are really supported
	if (vid.support.arb_texture_cube_map && vid.maxtexturesize_cubemap < 256)
		vid.support.arb_texture_cube_map = false;
	
	// verify that 3d textures are really supported
	if (vid.support.ext_texture_3d && vid.maxtexturesize_3d < 32)
	{
		vid.support.ext_texture_3d = false;
		Con_Printf("GL_OES_texture_3d reported bogus GL_MAX_3D_TEXTURE_SIZE, disabled\n");
	}

	vid.texunits = 4;
	vid.teximageunits = 8;
	vid.texarrayunits = 5;
	//qglGetIntegerv(GL_MAX_TEXTURE_UNITS, (GLint*)&vid.texunits);
	qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, (GLint*)&vid.teximageunits);CHECKGLERROR
	//qglGetIntegerv(GL_MAX_TEXTURE_COORDS, (GLint*)&vid.texarrayunits);CHECKGLERROR
	vid.texunits = bound(1, vid.texunits, MAX_TEXTUREUNITS);
	vid.teximageunits = bound(1, vid.teximageunits, MAX_TEXTUREUNITS);
	vid.texarrayunits = bound(1, vid.texarrayunits, MAX_TEXTUREUNITS);
	Con_DPrintf("Using GLES2.0 rendering path - %i texture matrix, %i texture images, %i texcoords%s\n", vid.texunits, vid.teximageunits, vid.texarrayunits, vid.support.ext_framebuffer_object ? ", shadowmapping supported" : "");
	vid.renderpath = RENDERPATH_GLES2;
	vid.sRGBcapable2D = false;
	vid.sRGBcapable3D = false;
	// VorteX: set other info (maybe place them in VID_InitMode?)
	{
		extern cvar_t gl_info_vendor;
		extern cvar_t gl_info_renderer;
		extern cvar_t gl_info_version;
		extern cvar_t gl_info_platform;
		extern cvar_t gl_info_driver;
		Cvar_SetQuick(&gl_info_vendor, gl_vendor);
		Cvar_SetQuick(&gl_info_renderer, gl_renderer);
		Cvar_SetQuick(&gl_info_version, gl_version);
		Cvar_SetQuick(&gl_info_platform, gl_platform ? gl_platform : "");
		Cvar_SetQuick(&gl_info_driver, gl_driver);
	}
}
#endif

void *GL_GetProcAddress(const char *name)
{
	void *p = NULL;
	p = SDL_GL_GetProcAddress(name);
	return p;
}

static qboolean vid_sdl_initjoysticksystem = false;

void VID_Init (void)
{
#ifndef __IPHONEOS__
#ifdef MACOSX
	Cvar_RegisterVariable(&apple_mouse_noaccel);
#endif
#endif
#ifdef DP_MOBILETOUCH
	Cvar_SetValueQuick(&vid_touchscreen, 1);
#endif

#ifdef SDL_R_RESTART
	R_RegisterModule("SDL", sdl_start, sdl_shutdown, sdl_newmap, NULL, NULL);
#endif
	Cvar_RegisterVariable(&vid_sdl_use_scancodes);
	Cvar_RegisterVariable(&vid_touchscreen_sensitivity);
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		Sys_Error ("Failed to init SDL video subsystem: %s", SDL_GetError());
	vid_sdl_initjoysticksystem = SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0;
	if (vid_sdl_initjoysticksystem)
		Con_Printf("Failed to init SDL joystick subsystem: %s\n", SDL_GetError());
	vid_isfullscreen = false;
}

static int vid_sdljoystickindex = -1;
void VID_EnableJoystick(qboolean enable)
{
	int index = joy_enable.integer > 0 ? joy_index.integer : -1;
	int numsdljoysticks;
	qboolean success = false;
	int sharedcount = 0;
	int sdlindex = -1;
	sharedcount = VID_Shared_SetJoystick(index);
	if (index >= 0 && index < sharedcount)
		success = true;
	sdlindex = index - sharedcount;

	numsdljoysticks = SDL_NumJoysticks();
	if (sdlindex < 0 || sdlindex >= numsdljoysticks)
		sdlindex = -1;

	// update cvar containing count of XInput joysticks + SDL joysticks
	if (joy_detected.integer != sharedcount + numsdljoysticks)
		Cvar_SetValueQuick(&joy_detected, sharedcount + numsdljoysticks);

	if (vid_sdljoystickindex != sdlindex)
	{
		vid_sdljoystickindex = sdlindex;
		// close SDL joystick if active
		if (vid_sdljoystick)
			SDL_JoystickClose(vid_sdljoystick);
		vid_sdljoystick = NULL;
		if (sdlindex >= 0)
		{
			vid_sdljoystick = SDL_JoystickOpen(sdlindex);
			if (vid_sdljoystick)
			{
				const char *joystickname = SDL_JoystickName(vid_sdljoystick);
				Con_Printf("Joystick %i opened (SDL_Joystick %i is \"%s\" with %i axes, %i buttons, %i balls)\n", index, sdlindex, joystickname, (int)SDL_JoystickNumAxes(vid_sdljoystick), (int)SDL_JoystickNumButtons(vid_sdljoystick), (int)SDL_JoystickNumBalls(vid_sdljoystick));
			}
			else
			{
				Con_Printf("Joystick %i failed (SDL_JoystickOpen(%i) returned: %s)\n", index, sdlindex, SDL_GetError());
				sdlindex = -1;
			}
		}
	}

	if (sdlindex >= 0)
		success = true;

	if (joy_active.integer != (success ? 1 : 0))
		Cvar_SetValueQuick(&joy_active, success ? 1 : 0);
}

#include "darkplaces.xpm"
#include "nexuiz.xpm"
#include "rexuiz.xpm"
static SDL_Surface *icon = NULL;
static void VID_SetIcon(void)
{
	/*
	 * Somewhat restricted XPM reader. Only supports XPMs saved by GIMP 2.4 at
	 * default settings with less than 91 colors and transparency.
	 */

	int width, height, colors, isize, i, j;
	int thenone = -1;
	static SDL_Color palette[256];
	unsigned short palenc[256]; // store color id by char
	char *xpm;
	char **idata, *data;
	icon = NULL;
	// only use non-XPM icon support in SDL v1.3 and higher
	// SDL v1.2 does not support "smooth" transparency, and thus is better
	// off the xpm way
	data = (char *) loadimagepixelsbgra("darkplaces-icon", false, false, false, NULL);
	if(data)
	{
		unsigned int red = 0x00FF0000;
		unsigned int green = 0x0000FF00;
		unsigned int blue = 0x000000FF;
		unsigned int alpha = 0xFF000000;
		width = image_width;
		height = image_height;

		// reallocate with malloc, as this is in tempmempool (do not want)
		xpm = data;
		data = (char *) malloc(width * height * 4);
		memcpy(data, xpm, width * height * 4);
		Mem_Free(xpm);
		xpm = NULL;

		icon = SDL_CreateRGBSurface(0, width, height, 32, LittleLong(red), LittleLong(green), LittleLong(blue), LittleLong(alpha));

		if (icon)
			icon->pixels = data;
		else
		{
			Con_Printf(	"Failed to create surface for the window Icon!\n"
					"%s\n", SDL_GetError());
			free(data);
		}
	}

	// we only get here if non-XPM icon was missing, or SDL version is not
	// sufficient for transparent non-XPM icons
	if(!icon)
	{
		xpm = (char *) FS_LoadFile("darkplaces-icon.xpm", tempmempool, false, NULL);
		idata = NULL;
		if(xpm)
			idata = XPM_DecodeString(xpm);
		if(!idata)
			idata = ENGINE_ICON;
		if(xpm)
			Mem_Free(xpm);

		data = idata[0];
		if(sscanf(data, "%i %i %i %i", &width, &height, &colors, &isize) == 4)
		{
			if(isize == 1)
			{
				for(i = 0; i < colors; ++i)
				{
					unsigned int r, g, b;
					char idx;

					if(sscanf(idata[i+1], "%c c #%02x%02x%02x", &idx, &r, &g, &b) != 4)
					{
						char foo[2];
						if(sscanf(idata[i+1], "%c c Non%1[e]", &idx, foo) != 2) // I take the DailyWTF credit for this. --div0
							break;
						else
						{
							palette[i].r = 255; // color key
							palette[i].g = 0;
							palette[i].b = 255;
							palette[i].a = 0;
							thenone = i; // weeeee
							palenc[(unsigned char) idx] = i;
						}
					}
					else
					{
						palette[i].r = r - (r == 255 && g == 0 && b == 255); // change 255/0/255 pink to 254/0/255 for color key
						palette[i].g = g;
						palette[i].b = b;
						palette[i].a = 255;
						palenc[(unsigned char) idx] = i;
					}
				}

				if (i == colors)
				{
					// allocate the image data
					data = (char*) malloc(width*height);

					for(j = 0; j < height; ++j)
					{
						for(i = 0; i < width; ++i)
						{
							// casting to the safest possible datatypes ^^
							data[j * width + i] = palenc[((unsigned char*)idata[colors+j+1])[i]];
						}
					}

					icon = SDL_CreateRGBSurface(0, width, height, 8, 0,0,0,0);// rmask, gmask, bmask, amask); no mask needed
					// 8 bit surfaces get an empty palette allocated according to the docs
					// so it's a palette image for sure :) no endian check necessary for the mask

					if(icon)
					{
						icon->pixels = data;
						SDL_SetPaletteColors(icon->format->palette, palette, 0, colors);
						SDL_SetColorKey(icon, SDL_TRUE, thenone);
					}
					else
					{
						Con_Printf(	"Failed to create surface for the window Icon!\n"
								"%s\n", SDL_GetError());
						free(data);
					}
				}
				else
				{
					Con_Printf("This XPM's palette looks odd. Can't continue.\n");
				}
			}
			else
			{
				// NOTE: Only 1-char colornames are supported
				Con_Printf("This XPM's palette is either huge or idiotically unoptimized. It's key size is %i\n", isize);
			}
		}
		else
		{
			// NOTE: Only 1-char colornames are supported
			Con_Printf("Sorry, but this does not even look similar to an XPM.\n");
		}
	}

	if (icon)
		SDL_SetWindowIcon(window, icon);
	else
		Con_Printf("Failed load icon!\n");
}

static void VID_OutputVersion(void)
{
	SDL_version version;
	SDL_GetVersion(&version);
	Con_Printf(	"Linked against SDL version %d.%d.%d\n"
					"Using SDL library version %d.%d.%d\n",
					SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
					version.major, version.minor, version.patch );
}

#ifdef WIN32
static void AdjustWindowBounds(viddef_mode_t *mode, RECT *rect)
{
    int workHeight, workWidth, titleBarPixels, screenHeight;
    RECT workArea;

	LONG width = mode->width; // vid_width
	LONG height = mode->height; // vid_height

	// adjust width and height for the space occupied by window decorators (title bar, borders)
	rect->top = 0;
	rect->left = 0;
	rect->right = width;
	rect->bottom = height;
	AdjustWindowRectEx(rect, WS_CAPTION|WS_THICKFRAME, false, 0);

	SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
	workWidth = workArea.right - workArea.left;
	workHeight = workArea.bottom - workArea.top;

	// SDL forces the window height to be <= screen height - 27px (on Win8.1 - probably intended for the title bar) 
	// If the task bar is docked to the the left screen border and we move the window to negative y,
	// there would be some part of the regular desktop visible on the bottom of the screen.
	titleBarPixels = 2;
	screenHeight = GetSystemMetrics(SM_CYSCREEN);
	if (screenHeight == workHeight)
		titleBarPixels = -rect->top;

	//Con_Printf("window mode: %dx%d, workArea: %d/%d-%d/%d (%dx%d), title: %d\n", width, height, workArea.left, workArea.top, workArea.right, workArea.bottom, workArea.right - workArea.left, workArea.bottom - workArea.top, titleBarPixels);

	// if height and width matches the physical or previously adjusted screen height and width, adjust it to available desktop area
	if ((width == GetSystemMetrics(SM_CXSCREEN) || width == workWidth) && (height == screenHeight || height == workHeight - titleBarPixels))
	{
		rect->left = workArea.left;
		mode->width = workWidth;
		rect->top = workArea.top + titleBarPixels;
		mode->height = workHeight - titleBarPixels;
	}
	else 
	{
		rect->left = workArea.left + max(0, (workWidth - width) / 2);
		rect->top = workArea.top + max(0, (workHeight - height) / 2);
	}
}
#endif

static qboolean VID_InitModeGL(viddef_mode_t *mode)
{
	int windowflags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
	int xPos = SDL_WINDOWPOS_UNDEFINED;
	int yPos = SDL_WINDOWPOS_UNDEFINED;
#ifndef USE_GLES2
	int i;
	const char *drivername;
#endif

	win_half_width = mode->width>>1;
	win_half_height = mode->height>>1;

	if(vid_resizable.integer)
		windowflags |= SDL_WINDOW_RESIZABLE;

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, vid_desktopfullscreen.integer? "0" : "1");
	VID_OutputVersion();

#ifndef USE_GLES2
	// SDL usually knows best
	drivername = NULL;

// COMMANDLINEOPTION: SDL GL: -gl_driver <drivername> selects a GL driver library, default is whatever SDL recommends, useful only for 3dfxogl.dll/3dfxvgl.dll or fxmesa or similar, if you don't know what this is for, you don't need it
	i = COM_CheckParm("-gl_driver");
	if (i && i < com_argc - 1)
		drivername = com_argv[i + 1];
	if (SDL_GL_LoadLibrary(drivername) < 0)
	{
		Con_Printf("Unable to load GL driver \"%s\": %s\n", drivername, SDL_GetError());
		return false;
	}
#endif

#ifdef DP_MOBILETOUCH
	// mobile platforms are always fullscreen, we'll get the resolution after opening the window
	mode->fullscreen = true;
	// hide the menu with SDL_WINDOW_BORDERLESS
	windowflags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
#endif
#ifndef USE_GLES2
	if ((qglGetString = (const GLubyte* (GLAPIENTRY *)(GLenum name))GL_GetProcAddress("glGetString")) == NULL)
	{
		VID_Shutdown();
		Con_Print("Required OpenGL function glGetString not found\n");
		return false;
	}
#endif

	// Knghtbrd: should do platform-specific extension string function here

	vid_isfullscreen = false;
	if (mode->fullscreen) {
		if (vid_desktopfullscreen.integer || sys_first_run.integer)
		{
			vid_mode_t *m = VID_GetDesktopMode();
			mode->width = m->width;
			mode->height = m->height;
			windowflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		else
			windowflags |= SDL_WINDOW_FULLSCREEN;
		vid_isfullscreen = true;
	}
	else {
#ifdef WIN32
		RECT rect;
		AdjustWindowBounds(mode, &rect);
		xPos = rect.left;
		yPos = rect.top;
#endif
	}
	//flags |= SDL_HWSURFACE;

	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
	if (mode->bitsperpixel >= 32)
	{
		SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);
		SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8);
	}
	else
	{
		SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 5);
		SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 5);
		SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 16);
	}
	if (mode->stereobuffer)
		SDL_GL_SetAttribute (SDL_GL_STEREO, 1);
	if (mode->samples > 1)
	{
		SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, mode->samples);
	}
#ifdef USE_GLES2
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute (SDL_GL_RETAINED_BACKING, 1);
#endif
	video_bpp = mode->bitsperpixel;
	window_flags = windowflags;
	window = SDL_CreateWindow(gamename, xPos, yPos, mode->width, mode->height, windowflags);
	if (window == NULL)
	{
		Con_Printf("Failed to set video mode to %ix%i: %s\n", mode->width, mode->height, SDL_GetError());
		VID_Shutdown();
		return false;
	}
	VID_SetIcon();
	SDL_GetWindowSize(window, &mode->width, &mode->height);
	context = SDL_GL_CreateContext(window);
	if (context == NULL)
	{
		Con_Printf("Failed to initialize OpenGL context: %s\n", SDL_GetError());
		VID_Shutdown();
		return false;
	}
	vid_softsurface = NULL;
	vid.softpixels = NULL;
	SDL_GL_SetSwapInterval(vid_vsync.integer != 0);
	vid_usingvsync = (vid_vsync.integer != 0);
	gl_platform = "SDL";
	gl_platformextensions = "";

#ifdef USE_GLES2
	GLES_Init();
#else
	GL_Init();
#endif

	vid_hidden = false;
	vid_activewindow = false;
	vid_hasfocus = true;
	vid_usingmouse = false;
	vid_usinghidecursor = false;
	return true;
}

extern cvar_t gl_info_extensions;
extern cvar_t gl_info_vendor;
extern cvar_t gl_info_renderer;
extern cvar_t gl_info_version;
extern cvar_t gl_info_platform;
extern cvar_t gl_info_driver;

qboolean VID_InitMode(viddef_mode_t *mode)
{
	if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
		Sys_Error ("Failed to init SDL video subsystem: %s", SDL_GetError());

	VID_TouchScreenInit();
	return VID_InitModeGL(mode);
}

void VID_Shutdown (void)
{
	VID_EnableJoystick(false);
	VID_SetMouse(false, false, false);
	VID_RestoreSystemGamma();
	if (vid_softsurface)
		SDL_FreeSurface(vid_softsurface);
	vid_softsurface = NULL;
	vid.softpixels = NULL;
	if (vid.softdepthpixels)
		free(vid.softdepthpixels);
	vid.softdepthpixels = NULL;

	SDL_DestroyWindow(window);
	window = NULL;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);

	gl_driver[0] = 0;
	gl_extensions = "";
	gl_platform = "";
	gl_platformextensions = "";
}

int VID_SetGamma (unsigned short *ramps, int rampsize)
{
	return !SDL_SetWindowGammaRamp (window, ramps, ramps + rampsize, ramps + rampsize*2);
}

int VID_GetGamma (unsigned short *ramps, int rampsize)
{
	return !SDL_GetWindowGammaRamp (window, ramps, ramps + rampsize, ramps + rampsize*2);
}

void VID_Finish (void)
{
	vid_activewindow = !vid_hidden && vid_hasfocus;

	VID_UpdateGamma(false, 256);

	if (!vid_hidden)
	{
		CHECKGLERROR
		if (r_speeds.integer == 2 || gl_finish.integer)
			GL_Finish();

		{
			qboolean vid_usevsync;
			vid_usevsync = (vid_vsync.integer && !cls.timedemo);
			if (vid_usingvsync != vid_usevsync)
			{
				vid_usingvsync = vid_usevsync;
				if (SDL_GL_SetSwapInterval(vid_usevsync != 0) >= 0)
					Con_DPrintf("Vsync %s\n", vid_usevsync ? "activated" : "deactivated");
				else
					Con_DPrintf("ERROR: can't %s vsync\n", vid_usevsync ? "activate" : "deactivate");
			}
		}
		SDL_GL_SwapWindow(window);
	}
}

vid_mode_t *VID_GetDesktopMode(void)
{
	SDL_DisplayMode mode;
	int bpp;
	Uint32 rmask, gmask, bmask, amask;
	SDL_GetDesktopDisplayMode(0, &mode);
	SDL_PixelFormatEnumToMasks(mode.format, &bpp, &rmask, &gmask, &bmask, &amask);
	desktop_mode.width = mode.w;
	desktop_mode.height = mode.h;
	desktop_mode.bpp = bpp;
	desktop_mode.refreshrate = mode.refresh_rate;
	desktop_mode.pixelheight_num = 1;
	desktop_mode.pixelheight_denom = 1; // SDL does not provide this
	// TODO check whether this actually works, or whether we do still need
	// a read-window-size-after-entering-desktop-fullscreen hack for
	// multiscreen setups.
	return &desktop_mode;
}

size_t VID_ListModes(vid_mode_t *modes, size_t maxcount)
{
	size_t k = 0;
	int modenum;
	int nummodes = SDL_GetNumDisplayModes(0);
	SDL_DisplayMode mode;
	for (modenum = 0;modenum < nummodes;modenum++)
	{
		if (k >= maxcount)
			break;
		if (SDL_GetDisplayMode(0, modenum, &mode))
			continue;
		modes[k].width = mode.w;
		modes[k].height = mode.h;
		// FIXME bpp?
		modes[k].refreshrate = mode.refresh_rate;
		modes[k].pixelheight_num = 1;
		modes[k].pixelheight_denom = 1; // SDL does not provide this
		k++;
	}
	return k;
}
