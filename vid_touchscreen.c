#include "quakedef.h"
#include "vid_touchscreen.h"

cvar_t vid_touchscreen_sensitivity = {CVAR_SAVE, "vid_touchscreen_sensitivity", "0.25", "sensitivity of virtual touchpad"};
cvar_t vid_touchscreen = {0, "vid_touchscreen", "0", "Use touchscreen-style input (no mouse grab, track mouse motion only while button is down, screen areas for mimicing joystick axes and buttons"};
cvar_t vid_touchscreen_showkeyboard = {0, "vid_touchscreen_showkeyboard", "0", "shows the platform's screen keyboard for text entry, can be set by csqc or menu qc if it wants to receive text input, does nothing if the platform has no screen keyboard"};
cvar_t vid_touchscreen_active = {0, "vid_touchscreen_active", "1", "activate/deactivate touchscreen controls" };
cvar_t vid_touchscreen_scale = {CVAR_SAVE, "vid_touchscreen_scale", "1", "scale of touchscreen items" };
cvar_t vid_touchscreen_mirror = {CVAR_SAVE, "vid_touchscreen_mirror", "0", "mirroring position of touchscreen in-game items" };
static cvar_t vid_touchscreen_outlinealpha = {0, "vid_touchscreen_outlinealpha", "0", "opacity of touchscreen area outlines"};
static cvar_t vid_touchscreen_overlayalpha = {0, "vid_touchscreen_overlayalpha", "0.25", "opacity of touchscreen area icons"};
struct finger multitouch[MAXFINGERS];

#define TOUCHSCREEN_AREAS_MAXCOUNT 128

struct touchscreen_area {
	int dest, corner, x, y, width, height;
	char image[64], cmd[32];
};
static struct touchscreen_area touchscreen_areas[TOUCHSCREEN_AREAS_MAXCOUNT - 2];
static int touchscreen_areas_count;
static int scr_numtouchscreenareas;
static scr_touchscreenarea_t scr_touchscreenareas[TOUCHSCREEN_AREAS_MAXCOUNT];

void VID_TouchscreenDraw(void)
{
	int i;
	scr_touchscreenarea_t *a;
	cachepic_t *pic;
	if (!vid_touchscreen.integer) return;
	for (i = 0, a = scr_touchscreenareas;i < scr_numtouchscreenareas;i++, a++)
	{
		if (vid_touchscreen_outlinealpha.value > 0 && a->rect[0] >= 0 && a->rect[1] >= 0 && a->rect[2] >= 4 && a->rect[3] >= 4)
		{
			DrawQ_Fill(a->rect[0] +              2, a->rect[1]                 , a->rect[2] - 4,          1    , 1, 1, 1, vid_touchscreen_outlinealpha.value * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] +              1, a->rect[1] +              1, a->rect[2] - 2,          1    , 1, 1, 1, vid_touchscreen_outlinealpha.value * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0]                 , a->rect[1] +              2,          2    , a->rect[3] - 2, 1, 1, 1, vid_touchscreen_outlinealpha.value * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] + a->rect[2] - 2, a->rect[1] +              2,          2    , a->rect[3] - 2, 1, 1, 1, vid_touchscreen_outlinealpha.value * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] +              1, a->rect[1] + a->rect[3] - 2, a->rect[2] - 2,          1    , 1, 1, 1, vid_touchscreen_outlinealpha.value * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] +              2, a->rect[1] + a->rect[3] - 1, a->rect[2] - 4,          1    , 1, 1, 1, vid_touchscreen_outlinealpha.value * (0.5f + 0.5f * a->active), 0);
		}
		pic = a->pic ? Draw_CachePic(a->pic) : NULL;
		if (pic && pic->tex != r_texture_notexture)
			DrawQ_Pic(a->rect[0], a->rect[1], Draw_CachePic(a->pic), a->rect[2], a->rect[3], 1, 1, 1, vid_touchscreen_overlayalpha.value * (0.5f + 0.5f * a->active), 0);
	}
}

static qboolean VID_TouchscreenArea(int dest, int corner, float px, float py, float pwidth, float pheight, const char *icon, const char *command, qboolean *resultbutton, int id)
{
	int finger;
	float fx, fy, fwidth, fheight;
	float rel[3];
	qboolean button = false;
	qboolean check_dest = false;
	char command_part[32];
	if (vid_touchscreen_scale.value > 0)
	{
		px *= vid_touchscreen_scale.value;
		py *= vid_touchscreen_scale.value;
		pwidth *= vid_touchscreen_scale.value;
		pheight *= vid_touchscreen_scale.value;
	}
	if (vid_touchscreen_mirror.integer)
	{
		if (dest == 2) //only for in-game controls
		{
			if (corner & 1)
			{
				px = -px - pwidth;
				corner &= ~1;
			}
			else if (!(corner & 4))
			{
				px = -px - pwidth;
				corner |= 1;
			}
		}
	}
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
				rel[0] = (multitouch[finger].x - multitouch[finger].start_x) * 32;
				rel[1] = (multitouch[finger].y - multitouch[finger].start_y) * 32;
				rel[2] = 0;
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
	while (command && *command) {
		char *comma = strchr(command, ',');
		if (comma) {
			memcpy(command_part, command, comma - command);
			command_part[comma - command] = '\0';
			command = &comma[1];
		} else {
			strlcpy(command_part, command, sizeof(command_part));
			command = NULL;
		}
		if (command_part[0] == '*') {
			if (!strcmp(command_part, "*move")) {
				if (button) {
					cl.cmd.forwardmove -= rel[1] * cl_forwardspeed.value;
					cl.cmd.sidemove += rel[0] * cl_sidespeed.value;
				}
			} else if (!strcmp(command_part, "*aim")) {
				if (button) {
					cl.viewangles[0] += rel[1] * cl_pitchspeed.value * vid_touchscreen_sensitivity.value;
					cl.viewangles[1] -= rel[0] * cl_yawspeed.value * vid_touchscreen_sensitivity.value;
					multitouch[finger].start_x = multitouch[finger].x;
					multitouch[finger].start_y = multitouch[finger].y;
				}
			} else if (!strcmp(command_part, "*click")) {
				if (*resultbutton != button) {
					Key_Event(K_MOUSE1, 0, button, false);
				}
			} else if (!strcmp(command_part, "*menu")) {
				if (*resultbutton != button) {
					Key_Event(K_ESCAPE, 0, button, false);
				}
			} else if (!strcmp(command_part, "*touchtoggle")) {
				if (!button && *resultbutton) {
					Cvar_SetValueQuick(&vid_touchscreen_active, !vid_touchscreen_active.integer);
				}
			} else if (!strcmp(command_part, "*keyboard")) {
				if (button && *resultbutton != button)
					VID_ShowKeyboard(!VID_ShowingKeyboard());
			}
		} else {
			if (*resultbutton != button)
			{
				if (command_part[0]) {
					if (command_part[0] == '+' && !button) {
						char minus_command[64];
						strlcpy(minus_command, command_part, 64);
						minus_command[0] = '-';
						Cbuf_AddText(minus_command);
					} else if (button) {
						Cbuf_AddText(command_part);
					}
					Cbuf_AddText("\n");
				}
			}
		}
	}
	*resultbutton = button;
	return button;
}

static void VID_TouchscreenLoad(void)
{
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
		if (!(tok = strtok_r(line, " \t", &line))) { continue; }
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

qboolean VID_TouchscreenInMove(int x, int y, int st)
{
	int n = 0, p = 0;
	static qboolean oldbuttons[TOUCHSCREEN_AREAS_MAXCOUNT];
	static qboolean buttons[TOUCHSCREEN_AREAS_MAXCOUNT];
	float scale = vid_touchscreen_scale.value;
	keydest_t keydest = (key_consoleactive & KEY_CONSOLEACTIVE_USER) ? key_console : key_dest;
	if (scale <= 0)
		scale = 1;

	memcpy(oldbuttons, buttons, sizeof(oldbuttons));
	// simple quake controls
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
	scr_numtouchscreenareas = 0;
	for (n = 0; n < touchscreen_areas_count; n++) {
		p += VID_TouchscreenArea(touchscreen_areas[n].dest, touchscreen_areas[n].corner, touchscreen_areas[n].x, touchscreen_areas[n].y,
				touchscreen_areas[n].width, touchscreen_areas[n].height, touchscreen_areas[n].image, touchscreen_areas[n].cmd, &buttons[n], n);
	}
	if (!p) {
		n++;
		p += VID_TouchscreenArea(7, 0,   0,   0,  64,  64, NULL                         , "toggleconsole", &buttons[n], n);
		n++;
		p += VID_TouchscreenArea(6, 0,   0,   0, vid_conwidth.value / scale, vid_conheight.value / scale, NULL, "*click", &buttons[n], n);
	}
	if (keydest == key_console && !VID_ShowingKeyboard())
	{
		// user entered a command, close the console now
		Con_ToggleConsole_f();
	}
	return p;
}

void VID_TouchscreenInit(void) {
	VID_TouchscreenLoad();
	Cvar_RegisterVariable(&vid_touchscreen);
	Cvar_RegisterVariable(&vid_touchscreen_showkeyboard);
	Cvar_RegisterVariable(&vid_touchscreen_active);
	Cvar_RegisterVariable(&vid_touchscreen_sensitivity);
	Cvar_RegisterVariable(&vid_touchscreen_outlinealpha);
	Cvar_RegisterVariable(&vid_touchscreen_overlayalpha);
	Cvar_RegisterVariable(&vid_touchscreen_scale);
	Cvar_RegisterVariable(&vid_touchscreen_mirror);
}
