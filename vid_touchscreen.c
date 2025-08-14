#include "quakedef.h"
#include "vid_touchscreen.h"

#define DPTOUCH_CFG_PATH "dptouchscreen.cfg"
#define DPTOUCH_CUSTOM_CFG_PATH "dptouchscreen_custom.cfg"

cvar_t vid_touchscreen_sensitivity = {CVAR_SAVE, "vid_touchscreen_sensitivity", "0.25", "sensitivity of virtual touchpad"};
cvar_t vid_touchscreen = {0, "vid_touchscreen", "0", "Use touchscreen-style input (no mouse grab, track mouse motion only while button is down, screen areas for mimicing joystick axes and buttons"};
cvar_t vid_touchscreen_showkeyboard = {0, "vid_touchscreen_showkeyboard", "0", "shows the platform's screen keyboard for text entry, can be set by csqc or menu qc if it wants to receive text input, does nothing if the platform has no screen keyboard"};
cvar_t vid_touchscreen_active = {0, "vid_touchscreen_active", "1", "activate/deactivate touchscreen controls" };
cvar_t vid_touchscreen_scale = {CVAR_SAVE, "vid_touchscreen_scale", "1", "scale of touchscreen items" };
cvar_t vid_touchscreen_mirror = {CVAR_SAVE, "vid_touchscreen_mirror", "0", "mirroring position of touchscreen in-game items" };
cvar_t vid_touchscreen_mouse = {CVAR_SAVE, "vid_touchscreen_mouse", "0", "use mouse input as touchscreen events" };
cvar_t vid_touchscreen_editor = {0, "vid_touchscreen_editor", "0", "enable touchscreen element editing" };
static cvar_t vid_touchscreen_outlinealpha = {0, "vid_touchscreen_outlinealpha", "0", "opacity of touchscreen area outlines"};
static cvar_t vid_touchscreen_overlayalpha = {0, "vid_touchscreen_overlayalpha", "0.25", "opacity of touchscreen area icons"};
struct finger multitouch[MAXFINGERS];
qboolean vid_touchscreen_visible = 1;

#define TOUCHSCREEN_AREAS_MAXCOUNT 128

struct touchscreen_area {
	int dest, corner, x, y, width, height;
	char image[64], cmd[32];
};
static struct touchscreen_area touchscreen_areas[TOUCHSCREEN_AREAS_MAXCOUNT - 2];
static struct touchscreen_area touchscreen_areas_backup[TOUCHSCREEN_AREAS_MAXCOUNT - 2];
static int touchscreen_areas_count;
static int touchscreen_editor_selected;
static int touchscreen_editor_selected_screen;
static int scr_numtouchscreenareas;
static scr_touchscreenarea_t scr_touchscreenareas[TOUCHSCREEN_AREAS_MAXCOUNT];

static void VID_TouchscreenLoad(qboolean custom);
static void VID_TouchscreenSaveCustom(void);

void VID_TouchscreenDraw(void)
{
	int i;
	scr_touchscreenarea_t *a;
	cachepic_t *pic;
	float outlinealpha;
	float overlayalpha;
	static float vid_touchscreen_alpha;
	if (!vid_touchscreen.integer) return;
	if (vid_touchscreen_visible)
	{
		if (vid_touchscreen_alpha != 1)
		{
			vid_touchscreen_alpha += cl.realframetime;
			if (vid_touchscreen_alpha > 1) vid_touchscreen_alpha = 1;
		}
	}
	else
	{
		if (vid_touchscreen_alpha != 0)
		{
			vid_touchscreen_alpha -= cl.realframetime;
			if (vid_touchscreen_alpha < 0) vid_touchscreen_alpha = 0;
		}
		else
			return;
	}
	outlinealpha = vid_touchscreen_outlinealpha.value * vid_touchscreen_alpha;
	overlayalpha = vid_touchscreen_overlayalpha.value * vid_touchscreen_alpha;
	if (vid_touchscreen_editor.integer)
	{
		overlayalpha = 1;
		DrawQ_Fill(0, 0, vid_conwidth.value, vid_conheight.value * 0.2, 0, 0, 0, 1, 0);
		DrawQ_Fill(0, vid_conheight.value * 0.2, vid_conwidth.value * 0.1, vid_conheight.value, 0, 0, 0, 1, 0);
		DrawQ_Fill(vid_conwidth.value * 0.9, vid_conheight.value * 0.2, vid_conwidth.value * 0.1, vid_conheight.value, 0, 0, 0, 1, 0);
		if (touchscreen_editor_selected >= 0 && touchscreen_editor_selected < touchscreen_areas_count)
		{
			int n = strlen(touchscreen_areas[touchscreen_editor_selected].cmd);
			DrawQ_String_Scale(vid_conwidth.value / 2 - (n / 2) * 16, 16, touchscreen_areas[touchscreen_editor_selected].cmd, 40, 16, 16, 1, 1, 1, 1, 1, 1, 0, NULL, true, FONT_DEFAULT);
		}
	}
	for (i = scr_numtouchscreenareas - 1, a = &scr_touchscreenareas[i]; i >= 0; i--, a--)
	{
		if (outlinealpha > 0 && a->rect[0] >= 0 && a->rect[1] >= 0 && a->rect[2] >= 4 && a->rect[3] >= 4)
		{
			DrawQ_Fill(a->rect[0] +              2, a->rect[1]                 , a->rect[2] - 4,          1    , 1, 1, 1, outlinealpha * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] +              1, a->rect[1] +              1, a->rect[2] - 2,          1    , 1, 1, 1, outlinealpha * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0]                 , a->rect[1] +              2,          2    , a->rect[3] - 2, 1, 1, 1, outlinealpha * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] + a->rect[2] - 2, a->rect[1] +              2,          2    , a->rect[3] - 2, 1, 1, 1, outlinealpha * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] +              1, a->rect[1] + a->rect[3] - 2, a->rect[2] - 2,          1    , 1, 1, 1, outlinealpha * (0.5f + 0.5f * a->active), 0);
			DrawQ_Fill(a->rect[0] +              2, a->rect[1] + a->rect[3] - 1, a->rect[2] - 4,          1    , 1, 1, 1, outlinealpha * (0.5f + 0.5f * a->active), 0);
		}
		if (i == touchscreen_editor_selected_screen)
		{
			DrawQ_Fill(a->rect[0] +              2, a->rect[1]                 , a->rect[2] - 4,          1    , 1, 1, 1, 0.5f, 0);
			DrawQ_Fill(a->rect[0] +              1, a->rect[1] +              1, a->rect[2] - 2,          1    , 1, 1, 1, 0.5f, 0);
			DrawQ_Fill(a->rect[0]                 , a->rect[1] +              2,          2    , a->rect[3] - 2, 1, 1, 1, 0.5f, 0);
			DrawQ_Fill(a->rect[0] + a->rect[2] - 2, a->rect[1] +              2,          2    , a->rect[3] - 2, 1, 1, 1, 0.5f, 0);
			DrawQ_Fill(a->rect[0] +              1, a->rect[1] + a->rect[3] - 2, a->rect[2] - 2,          1    , 1, 1, 1, 0.5f, 0);
			DrawQ_Fill(a->rect[0] +              2, a->rect[1] + a->rect[3] - 1, a->rect[2] - 4,          1    , 1, 1, 1, 0.5f, 0);
		}
		pic = a->pic ? Draw_CachePic(a->pic) : NULL;
		if (pic && pic->tex != r_texture_notexture)
			DrawQ_Pic(a->rect[0], a->rect[1], Draw_CachePic(a->pic), a->rect[2], a->rect[3], 1, 1, 1, overlayalpha * (0.5f + 0.5f * a->active), 0);
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
	if (vid_touchscreen_scale.value > 0 && vid_touchscreen_scale.value != 1 && strncmp(command, "*editor_", 8))
	{
		px *= vid_touchscreen_scale.value;
		py *= vid_touchscreen_scale.value;
		pwidth *= vid_touchscreen_scale.value;
		pheight *= vid_touchscreen_scale.value;
	}
	if (vid_touchscreen_mirror.integer && strncmp(command, "*editor_", 8))
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
	if (vid_touchscreen_editor.integer) {
		check_dest = (dest & 2) && !(dest & 5) && strcmp(command, "*toucheditortoggle");
	} else if (key_consoleactive) {
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
		if (vid_touchscreen_editor.integer) {
			px *= 0.8;
			py *= 0.8;
			pwidth *= 0.8;
			pheight *= 0.8;
			px += vid_conwidth.value * 0.1;
			py += vid_conheight.value * 0.2;
		}
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
				if (vid_touchscreen_editor.integer && id < touchscreen_areas_count && !*resultbutton)
				{
					touchscreen_editor_selected = id;
					touchscreen_editor_selected_screen = scr_numtouchscreenareas;
				}
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
		if (vid_touchscreen_editor.integer) {
			if (!strcmp(command_part, "*editor_accept")) {
				if (button && !*resultbutton) {
					if (touchscreen_editor_selected >= 0) {
						touchscreen_editor_selected = -1;
						touchscreen_editor_selected_screen = -1;
					} else {
						memcpy(touchscreen_areas_backup, touchscreen_areas, sizeof(touchscreen_areas));
						VID_TouchscreenSaveCustom();
						Cvar_SetValueQuick(&vid_touchscreen_editor, 0);
					}
				}
			} else if (!strcmp(command_part, "*editor_reject")) {
				if (button && !*resultbutton) {
					touchscreen_editor_selected = -1;
					touchscreen_editor_selected_screen = -1;
					memcpy(touchscreen_areas, touchscreen_areas_backup, sizeof(touchscreen_areas));
					Cvar_SetValueQuick(&vid_touchscreen_editor, 0);
				}
			} else if (!strcmp(command_part, "*editor_reset")) {
				if (button && !*resultbutton) {
					touchscreen_editor_selected = -1;
					touchscreen_editor_selected_screen = -1;
					VID_TouchscreenLoad(false);
				}
			} else if (!strcmp(command_part, "*editor_mirror")) {
				if (button && !*resultbutton) {
					Cvar_SetValueQuick(&vid_touchscreen_mirror, !vid_touchscreen_mirror.integer);
				}
			} else if (!strcmp(command_part, "*editor_scaleplus")) {
				if (button && !*resultbutton) {
					if (touchscreen_editor_selected >= 0) {
						if (touchscreen_areas[touchscreen_editor_selected].width > 8 && touchscreen_areas[touchscreen_editor_selected].height > 8)
						{
							touchscreen_areas[touchscreen_editor_selected].width += 4;
							touchscreen_areas[touchscreen_editor_selected].height += 4;
						}
					} else {
						Cvar_SetValueQuick(&vid_touchscreen_scale, bound(0.1, vid_touchscreen_scale.value + 0.1, 10));
					}
				}
			} else if (!strcmp(command_part, "*editor_scaleminus")) {
				if (button && !*resultbutton) {
					if (touchscreen_editor_selected >= 0) {
						if (touchscreen_areas[touchscreen_editor_selected].width < vid_conwidth.integer / 2 && touchscreen_areas[touchscreen_editor_selected].height < vid_conheight.integer / 2)
						{
							touchscreen_areas[touchscreen_editor_selected].width -= 4;
							touchscreen_areas[touchscreen_editor_selected].height -= 4;
						}
					} else {
						Cvar_SetValueQuick(&vid_touchscreen_scale, bound(0.1, vid_touchscreen_scale.value - 0.1, 10));
					}
				}
			} else {
				if (button) { //dragging
					struct finger *touch = &multitouch[finger];
					if (touch->area_id < touchscreen_areas_count) {
						if (touch->x != touch->start_x || touch->y != touch->start_y) {
							int aid = touch->area_id;
							float invscale = ((vid_touchscreen_scale.value > 0) ? (1 / vid_touchscreen_scale.value) : 1);
							touchscreen_areas[aid].x =
									(touch->x - 0.1) * 1.25 * vid_conwidth.value -
									touchscreen_areas[aid].width * 0.5 / invscale;
							touchscreen_areas[aid].y =
									(touch->y - 0.2) * 1.25 * vid_conheight.value -
									touchscreen_areas[aid].height * 0.5 / invscale;
							touch->start_x = touch->x;
							touch->start_y = touch->y;
							if (touchscreen_areas[aid].corner & 4) {
								touchscreen_areas[aid].x -= vid_conwidth.integer / 2;
							}
							if (touchscreen_areas[aid].corner & 8) {
								touchscreen_areas[aid].y -= vid_conheight.integer / 2;
							}
							if (touchscreen_areas[aid].corner & 1) {
								touchscreen_areas[aid].x -= vid_conwidth.integer;
							}
							if (touchscreen_areas[aid].corner & 2) {
								touchscreen_areas[aid].y -= vid_conheight.integer;
							}
							if (vid_touchscreen_mirror.integer) {
								if (touchscreen_areas[aid].corner & 1)
									touchscreen_areas[aid].x = -vid_conwidth.integer - touchscreen_areas[aid].x - touchscreen_areas[aid].width;
								else {
									touchscreen_areas[aid].x = vid_conwidth.integer - touchscreen_areas[aid].x - touchscreen_areas[aid].width;
								}
							}
							touchscreen_areas[aid].x = (int)(touchscreen_areas[aid].x * invscale) / 2 * 2;
							touchscreen_areas[aid].y = (int)(touchscreen_areas[aid].y * invscale) / 2 * 2;
						}
					}
				}
			}
		} else if (command_part[0] == '*' && (button || *resultbutton)) {
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
				if (button) {
					in_windowmouse_x = multitouch[finger].x * vid.width;
					in_windowmouse_y = multitouch[finger].y * vid.height;
				}
				if (*resultbutton != button) {
					Key_Event(K_MOUSE1, 0, button, false);
				}
			} else if (!strcmp(command_part, "*menu")) {
				if (*resultbutton != button) {
					if (VID_ShowingKeyboard() && !key_consoleactive && key_dest != key_console) {
						if (button) {
							VID_ShowKeyboard(false);
						}
					}
					Key_Event(K_ESCAPE, 0, button, false);
				}
			} else if (!strcmp(command_part, "*touchtoggle")) {
				if (!button && *resultbutton) {
					Cvar_SetValueQuick(&vid_touchscreen_active, !vid_touchscreen_active.integer);
				}
			} else if (!strcmp(command_part, "*keyboard")) {
				if (button && *resultbutton != button)
					VID_ShowKeyboard(!VID_ShowingKeyboard());
			} else if (!strcmp(command_part, "*toucheditortoggle")) {
				if (button && *resultbutton != button)
					Cvar_SetValueQuick(&vid_touchscreen_editor, !vid_touchscreen_editor.integer);
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

static void VID_TouchscreenLoad(qboolean custom)
{
	const char *cfg_path = (custom ? DPTOUCH_CUSTOM_CFG_PATH : DPTOUCH_CFG_PATH);
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

static void VID_TouchscreenSaveCustom(void)
{
	int i;
	qfile_t *file = FS_OpenRealFile(DPTOUCH_CUSTOM_CFG_PATH, "w", false);
	if (!file) {
		//error message?
		return;
	}
	FS_Printf(file, "// <mode> <corner> <x> <y> <width> <height> <icon> <command>\n");
	FS_Printf(file, "// mode is a bit field: 1 menu/console mode, 2 game mode, 4 text mode, 32 touchscreen disable mode\n");
	FS_Printf(file, "// corner is a bit field: 1 from right, 2 from down, 4 from horizontal center, 8 from vertical center\n");
	for (i = 0; i < touchscreen_areas_count; i++) {
		struct touchscreen_area *ta = &touchscreen_areas[i];
		FS_Printf(file, "%i %i %i %i %i %i %s %s\n",
				ta->dest, ta->corner, ta->x, ta->y, ta->width, ta->height, ta->image, ta->cmd);
	}
	FS_Close(file);
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
	if (vid_touchscreen_mouse.integer) {
		multitouch[MAXFINGERS-1].x = ((float)x) / vid.width;
		multitouch[MAXFINGERS-1].y = ((float)y) / vid.height;
		if (!multitouch[MAXFINGERS-1].state) {
			multitouch[MAXFINGERS-1].area_id = -1;
			multitouch[MAXFINGERS-1].start_x = multitouch[MAXFINGERS-1].x;
			multitouch[MAXFINGERS-1].start_y = multitouch[MAXFINGERS-1].y;
		}
		multitouch[MAXFINGERS-1].state = st;
		in_windowmouse_x = x;
		in_windowmouse_y = y;
	} else
		multitouch[MAXFINGERS-1].state = 0;

	scr_numtouchscreenareas = 0;
	for (n = 0; n < touchscreen_areas_count; n++) {
		p += VID_TouchscreenArea(touchscreen_areas[n].dest, touchscreen_areas[n].corner, touchscreen_areas[n].x, touchscreen_areas[n].y,
				touchscreen_areas[n].width, touchscreen_areas[n].height, touchscreen_areas[n].image, touchscreen_areas[n].cmd, &buttons[n], n);
	}
	// top of screen is toggleconsole and K_ESCAPE
	if (!p) {
		n++;
		p += VID_TouchscreenArea(7, 0,   0,   0,  64,  64, NULL                         , "toggleconsole", &buttons[n], n);
		n++;
		p += VID_TouchscreenArea(6, 0,   0,   0, vid_conwidth.value / scale, vid_conheight.value / scale, NULL, "*click", &buttons[n], n);
	} else {
		n += 2;
	}
	if (vid_touchscreen_editor.integer) {
		//touch screen menu
		float eh = vid_conheight.value * 0.15;
		float ew = vid_conwidth.value * 0.15;
		if (ew > eh) ew = eh;
		if (eh > ew) eh = ew;
		if (touchscreen_editor_selected >= 0 && touchscreen_editor_selected < touchscreen_areas_count) {
			n++;
			p += VID_TouchscreenArea(2, 4, -ew * 1.5, -eh, ew, eh, "gfx/dptouch_scaleplus.tga", "*editor_scaleplus", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 4, -ew * 0.5, -eh, ew, eh, "gfx/dptouch_scaleminus.tga", "*editor_scaleminus", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 4, ew * 0.5, -eh, ew, eh, "gfx/dptouch_accept.tga", "*editor_accept", &buttons[n], n);
			n += 6;
		} else {
			n += 3;
			n++;
			p += VID_TouchscreenArea(2, 1, -ew * 3, -eh, ew, eh, "gfx/dptouch_accept.tga", "*editor_accept", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 1, -ew * 2 , -eh, ew, eh, "gfx/dptouch_reset.tga", "*editor_reset", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 1, -ew, -eh, ew, eh, "gfx/dptouch_reject.tga", "*editor_reject", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 0, 0, -eh, ew, eh, "gfx/dptouch_mirror.tga", "*editor_mirror", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 0, ew , -eh, ew, eh, "gfx/dptouch_scaleplus.tga", "*editor_scaleplus", &buttons[n], n);
			n++;
			p += VID_TouchscreenArea(2, 0, ew * 2, -eh, ew, eh, "gfx/dptouch_scaleminus.tga", "*editor_scaleminus", &buttons[n], n);
		}
	} else
		n += 9;

	if (keydest == key_console && !VID_ShowingKeyboard())
	{
		// user entered a command, close the console now
		Con_ToggleConsole_f();
	}
	return p;
}

void VID_TouchscreenInit(void) {
	VID_TouchscreenLoad(true); //attempt load custom first
	if (!touchscreen_areas_count) VID_TouchscreenLoad(false);
	memcpy(touchscreen_areas_backup, touchscreen_areas, sizeof(touchscreen_areas));
	Cvar_RegisterVariable(&vid_touchscreen);
	Cvar_RegisterVariable(&vid_touchscreen_showkeyboard);
	Cvar_RegisterVariable(&vid_touchscreen_active);
	Cvar_RegisterVariable(&vid_touchscreen_sensitivity);
	Cvar_RegisterVariable(&vid_touchscreen_outlinealpha);
	Cvar_RegisterVariable(&vid_touchscreen_overlayalpha);
	Cvar_RegisterVariable(&vid_touchscreen_scale);
	Cvar_RegisterVariable(&vid_touchscreen_mirror);
	Cvar_RegisterVariable(&vid_touchscreen_mouse);
	Cvar_RegisterVariable(&vid_touchscreen_editor);
	touchscreen_editor_selected = -1;
	touchscreen_editor_selected_screen = -1;
}
