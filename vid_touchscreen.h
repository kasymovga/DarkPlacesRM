#ifndef _VID_TOUCHSCREEN_H_
#define _VID_TOUCHSCREEN_H_
// multitouch[10][] represents the mouse pointer
struct finger {
	int state;
	int area_id;
	float x, y;
	float start_x, start_y;
};
#define MAXFINGERS 11
#define TOUCHSCREEN_AREAS_MAXCOUNT 128
extern struct finger multitouch[MAXFINGERS];
void VID_TouchscreenDraw(void);
void VID_TouchscreenInit(void);
qboolean VID_TouchscreenInMove(int x, int y, int st);
extern cvar_t vid_touchscreen;
extern cvar_t vid_touchscreen_showkeyboard;
extern cvar_t vid_touchscreen_active;
extern cvar_t vid_touchscreen_mouse;
#endif
