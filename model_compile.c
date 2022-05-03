
// converter for .smd files (and a .txt script) to .dpm
// written by Forest 'LordHavoc' Hale but placed into public domain
//
// disclaimer: Forest Hale is not not responsible if this code blinds you with
// its horrible design, sets your house on fire, makes you cry,
// or anything else - use at your own risk.
//
// Yes, this is perhaps my second worst code ever (next to zmodel).

// Thanks to Jalisk0 for the HalfLife2 .SMD bone weighting support

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "quakedef.h"
#include "common.h"
#include "fs.h"
#include "mathlib.h"
#include "model_compile.h"

#ifndef M_PI
#define M_PI 3.1415926535
#endif

#if _MSC_VER
#pragma warning (disable : 4244)
#endif

#define MAX_FILEPATH 1024
#define MAX_NAME 32
#define MAX_FRAMES 65536
#define MAX_TRIS 65536
#define MAX_VERTS (MAX_TRIS * 3)
#define MAX_BONES 256
#define MAX_SHADERS 256
#define MAX_FILESIZE (64*1024*1024)
#define MAX_INFLUENCES 16
#define MAX_ATTACHMENTS MAX_BONES
// model format related flags
#define DPMBONEFLAG_ATTACH 1

#if 1
#define EPSILON_VERTEX 0.1
#define EPSILON_NORMAL 0.001
#define EPSILON_TEXCOORD 0.0001
#else
#define EPSILON_VERTEX 0
#define EPSILON_NORMAL 0
#define EPSILON_TEXCOORD 0
#endif

static int scene_fps;
static int scene_looping;
static mempool_t *model_compile_memory_pool;
static char *texturedir_name;
static char *outputdir_name;
static char *model_name;
static char *scene_name;
static char *model_name_uppercase;
static char *scene_name_uppercase;
static char *model_name_lowercase;
static char *scene_name_lowercase;

static qfile_t *headerfile = NULL;
static qfile_t *qcheaderfile = NULL;
static qfile_t *framegroupsfile = NULL;

static double modelorigin[3] = {0, 0, 0}, modelrotate = 0, modelscale = 1;

// this makes it keep all bones, not removing unused ones (as they might be used for attachments)
static int keepallbones = 1;

static void stringtouppercase(char *in, char *out)
{
	// cleanup name
	while (*in)
	{
		*out = *in++;
		// force lowercase
		if (*out >= 'a' && *out <= 'z')
			*out += 'A' - 'a';
		out++;
	}
	*out++ = 0;
}

static void stringtolowercase(char *in, char *out)
{
	// cleanup name
	while (*in)
	{
		*out = *in++;
		// force lowercase
		if (*out >= 'A' && *out <= 'Z')
			*out += 'a' - 'A';
		out++;
	}
	*out++ = 0;
}


static void cleancopyname(char *out, char *in, int size)
{
	char *end = out + size - 1;
	// cleanup name
	while (out < end)
	{
		*out = *in++;
		if (!*out)
			break;
		// force lowercase
		if (*out >= 'A' && *out <= 'Z')
			*out += 'a' - 'A';
		// convert backslash to slash
		if (*out == '\\')
			*out = '/';
		out++;
	}
	end++;
	while (out < end)
		*out++ = 0; // pad with nulls
}

static void chopextension(char *text)
{
	char *temp;
	if (!*text)
		return;
	temp = text;
	while (*temp)
	{
		if (*temp == '\\')
			*temp = '/';
		temp++;
	}
	temp = text + strlen(text) - 1;
	while (temp >= text)
	{
		if (*temp == '.') // found an extension
		{
			// clear extension
			*temp++ = 0;
			while (*temp)
				*temp++ = 0;
			break;
		}
		if (*temp == '/') // no extension but hit path
			break;
		temp--;
	}
}

static double VectorDistance2D(const double *v1, const double *v2)
{
	return sqrt((v2[0]-v1[0])*(v2[0]-v1[0])+(v2[1]-v1[1])*(v2[1]-v1[1]));
}

char *scriptbytes, *scriptend;
fs_offset_t scriptsize;

/*
int scriptfiles = 0;
char *scriptfile[256];

struct
{
	double framerate;
	int noloop;
	int skip;
}
scriptfileinfo[256];

int parsefilenames(void)
{
	char *in, *out, *name;
	scriptfiles = 0;
	in = scriptbytes;
	out = scriptfilebuffer;
	while (*in && *in <= ' ')
		in++;
	while (*in &&
	while (scriptfiles < 256)
	{
		scriptfile[scriptfiles++] = name;
		while (*in && *in != '\n' && *in != '\r')
			in++;
		if (!*in)
			break;

		while (*b > ' ')
			b++;
		while (*b && *b <= ' ')
			*b++ = 0;
		if (*b == 0)
			break;
	}
	return scriptfiles;
}
*/

const char *tokenpos;

typedef struct bonepose_s
{
	double m[3][4];
}
bonepose_t;

typedef struct bone_s
{
	char name[MAX_NAME];
	int parent; // parent of this bone
	int flags;
	int users; // used to determine if a bone is used to avoid saving out unnecessary bones
	int defined;
}
bone_t;

typedef struct frame_s
{
	int defined;
	char name[MAX_NAME];
	double mins[3], maxs[3], yawradius, allradius; // clipping
	int numbones;
	bonepose_t *bones;
}
frame_t;

typedef struct tripoint_s
{
	int shadernum;
	double texcoord[2];

	int		numinfluences;
	double	influenceorigin[MAX_INFLUENCES][3];
	double	influencenormal[MAX_INFLUENCES][3];
	int		influencebone[MAX_INFLUENCES];
	float	influenceweight[MAX_INFLUENCES];

	// these are used for comparing against other tripoints (which are relative to other bones)
	double originalorigin[3];
	double originalnormal[3];
}
tripoint;

typedef struct triangle_s
{
	int shadernum;
	int v[3];
}
triangle;

typedef struct attachment_s
{
	char name[MAX_NAME];
	char parentname[MAX_NAME];
	bonepose_t matrix;
}
attachment;

static int numattachments = 0;
static attachment *attachments;

static int numframes = 0;
static frame_t *frames;
static int numbones = 0;
static bone_t *bones; // master bone list
static int numshaders = 0;
static char shaders[MAX_SHADERS][32];
static int numtriangles = 0;
static triangle *triangles;
static int numverts = 0;
static tripoint *vertices;

//these are used while processing things
static bonepose_t *bonematrix;
static char *modelfile;
static int *vertremap;

/*
static double wrapangles(double f)
{
	while (f < M_PI)
		f += M_PI * 2;
	while (f >= M_PI)
		f -= M_PI * 2;
	return f;
}
*/

static bonepose_t computebonematrix(double x, double y, double z, double a, double b, double c)
{
	bonepose_t out;
	double sr, sp, sy, cr, cp, cy;

	sy = sin(c);
	cy = cos(c);
	sp = sin(b);
	cp = cos(b);
	sr = sin(a);
	cr = cos(a);

	out.m[0][0] = cp*cy;
	out.m[1][0] = cp*sy;
	out.m[2][0] = -sp;
	out.m[0][1] = sr*sp*cy+cr*-sy;
	out.m[1][1] = sr*sp*sy+cr*cy;
	out.m[2][1] = sr*cp;
	out.m[0][2] = (cr*sp*cy+-sr*-sy);
	out.m[1][2] = (cr*sp*sy+-sr*cy);
	out.m[2][2] = cr*cp;
	out.m[0][3] = x;
	out.m[1][3] = y;
	out.m[2][3] = z;
	return out;
}

static bonepose_t concattransform(bonepose_t in1, bonepose_t in2)
{
	bonepose_t out;
	out.m[0][0] = in1.m[0][0] * in2.m[0][0] + in1.m[0][1] * in2.m[1][0] + in1.m[0][2] * in2.m[2][0];
	out.m[0][1] = in1.m[0][0] * in2.m[0][1] + in1.m[0][1] * in2.m[1][1] + in1.m[0][2] * in2.m[2][1];
	out.m[0][2] = in1.m[0][0] * in2.m[0][2] + in1.m[0][1] * in2.m[1][2] + in1.m[0][2] * in2.m[2][2];
	out.m[0][3] = in1.m[0][0] * in2.m[0][3] + in1.m[0][1] * in2.m[1][3] + in1.m[0][2] * in2.m[2][3] + in1.m[0][3];
	out.m[1][0] = in1.m[1][0] * in2.m[0][0] + in1.m[1][1] * in2.m[1][0] + in1.m[1][2] * in2.m[2][0];
	out.m[1][1] = in1.m[1][0] * in2.m[0][1] + in1.m[1][1] * in2.m[1][1] + in1.m[1][2] * in2.m[2][1];
	out.m[1][2] = in1.m[1][0] * in2.m[0][2] + in1.m[1][1] * in2.m[1][2] + in1.m[1][2] * in2.m[2][2];
	out.m[1][3] = in1.m[1][0] * in2.m[0][3] + in1.m[1][1] * in2.m[1][3] + in1.m[1][2] * in2.m[2][3] + in1.m[1][3];
	out.m[2][0] = in1.m[2][0] * in2.m[0][0] + in1.m[2][1] * in2.m[1][0] + in1.m[2][2] * in2.m[2][0];
	out.m[2][1] = in1.m[2][0] * in2.m[0][1] + in1.m[2][1] * in2.m[1][1] + in1.m[2][2] * in2.m[2][1];
	out.m[2][2] = in1.m[2][0] * in2.m[0][2] + in1.m[2][1] * in2.m[1][2] + in1.m[2][2] * in2.m[2][2];
	out.m[2][3] = in1.m[2][0] * in2.m[0][3] + in1.m[2][1] * in2.m[1][3] + in1.m[2][2] * in2.m[2][3] + in1.m[2][3];
	return out;
}

static void transform(double in[3], bonepose_t matrix, double out[3])
{
	out[0] = in[0] * matrix.m[0][0] + in[1] * matrix.m[0][1] + in[2] * matrix.m[0][2] + matrix.m[0][3];
	out[1] = in[0] * matrix.m[1][0] + in[1] * matrix.m[1][1] + in[2] * matrix.m[1][2] + matrix.m[1][3];
	out[2] = in[0] * matrix.m[2][0] + in[1] * matrix.m[2][1] + in[2] * matrix.m[2][2] + matrix.m[2][3];
}

static void transformnormal(double in[3], bonepose_t matrix, double out[3])
{
	out[0] = in[0] * matrix.m[0][0] + in[1] * matrix.m[0][1] + in[2] * matrix.m[0][2];
	out[1] = in[0] * matrix.m[1][0] + in[1] * matrix.m[1][1] + in[2] * matrix.m[1][2];
	out[2] = in[0] * matrix.m[2][0] + in[1] * matrix.m[2][1] + in[2] * matrix.m[2][2];
}

static void inversetransform(double in[3], bonepose_t matrix, double out[3])
{
	double temp[3];
	temp[0] = in[0] - matrix.m[0][3];
	temp[1] = in[1] - matrix.m[1][3];
	temp[2] = in[2] - matrix.m[2][3];
	out[0] = temp[0] * matrix.m[0][0] + temp[1] * matrix.m[1][0] + temp[2] * matrix.m[2][0];
	out[1] = temp[0] * matrix.m[0][1] + temp[1] * matrix.m[1][1] + temp[2] * matrix.m[2][1];
	out[2] = temp[0] * matrix.m[0][2] + temp[1] * matrix.m[1][2] + temp[2] * matrix.m[2][2];
}

/*
void rotate(double in[3], bonepose_t matrix, double out[3])
{
	out[0] = in[0] * matrix.m[0][0] + in[1] * matrix.m[0][1] + in[2] * matrix.m[0][2];
	out[1] = in[0] * matrix.m[1][0] + in[1] * matrix.m[1][1] + in[2] * matrix.m[1][2];
	out[2] = in[0] * matrix.m[2][0] + in[1] * matrix.m[2][1] + in[2] * matrix.m[2][2];
}
*/

static void inverserotate(double in[3], bonepose_t matrix, double out[3])
{
	out[0] = in[0] * matrix.m[0][0] + in[1] * matrix.m[1][0] + in[2] * matrix.m[2][0];
	out[1] = in[0] * matrix.m[0][1] + in[1] * matrix.m[1][1] + in[2] * matrix.m[2][1];
	out[2] = in[0] * matrix.m[0][2] + in[1] * matrix.m[1][2] + in[2] * matrix.m[2][2];
}

static int parsenodes(void)
{
	int num, parent;
	char line[1024], name[1024];

	memset(bones, 0, sizeof(bone_t) * MAX_BONES);
	numbones = 0;

	while (COM_ParseToken_VM_Tokenize(&tokenpos, true))
	{
		// if this is the end keyword, we're done with this section of the file
		if (!strcmp(com_token, "end"))
			break;

		//parse this line read by tokens

		//get bone number
		//we already read the first token, so use it
		if (com_token[0] <= ' ')
		{
			Con_Printf("error in nodes, expecting bone number in line:%s\n", line);
			return 0;
		}
		num = atoi( com_token );

		//get bone name
		if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] < ' ')
		{
			Con_Printf("error in nodes, expecting bone name in line:%s\n", line);
			return 0;
		}
		cleancopyname(name, com_token, MAX_NAME);//Con_Printf( "bone name: %s\n", name );

		//get parent number
		if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
		{
			Con_Printf("error in nodes, expecting parent number in line:%s\n", line);
			return 0;
		}
		parent = atoi( com_token );

		if (num < 0 || num >= MAX_BONES)
		{
			Con_Printf("invalid bone number %i\n", num);
			return 0;
		}
		if (parent >= num)
		{
			Con_Printf("bone's parent >= bone's number\n");
			return 0;
		}
		if (parent < -1)
		{
			Con_Printf("bone's parent < -1\n");
			return 0;
		}
		if (parent >= 0 && !bones[parent].defined)
		{
			Con_Printf("bone's parent bone has not been defined\n");
			return 0;
		}
		memcpy(bones[num].name, name, MAX_NAME);
		bones[num].defined = 1;
		bones[num].parent = parent;
		if (num >= numbones)
			numbones = num + 1;
		// skip any trailing parameters (might be a later version of smd)
		while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
	}
	// skip any trailing parameters (might be a later version of smd)
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
	return 1;
}

static int parseskeleton(void)
{
	char line[1024], temp[2048];
	int i, frame, num;
	double x, y, z, a, b, c;
	int baseframe;

	baseframe = numframes;
	frame = baseframe;

	while (COM_ParseToken_VM_Tokenize(&tokenpos, true))
	{
		// if this is the end keyword, we're done with this section of the file
		if (!strcmp(com_token, "end"))
			break;

		//parse this line read by tokens

		//get opening line token
		//we already read the first token, so use it
		if (com_token[0] <= ' ')
		{
			Con_Printf("error in parseskeleton, script line:%s\n", line);
			return 0;
		}

		if (!strcmp(com_token, "time"))
		{
			//get the time value
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting time value in line:%s\n", line);
				return 0;
			}
			i = atoi( com_token );
			if (i < 0)
			{
				Con_Printf("invalid time %i\n", i);
				return 0;
			}

			frame = baseframe + i;
			if (frame >= MAX_FRAMES)
			{
				Con_Printf("only %i frames supported currently\n", MAX_FRAMES);
				return 0;
			}
			if (frames[frame].defined)
			{
				Con_Printf("warning: duplicate frame\n");
				Z_Free(frames[frame].bones);
			}
			sprintf(temp, "%s_%i", scene_name, i);
			if (strlen(temp) > 31)
			{
				Con_Printf("error: frame name \"%s\" is longer than 31 characters\n", temp);
				return 0;
			}
			cleancopyname(frames[frame].name, temp, MAX_NAME);

			frames[frame].numbones = numbones + numattachments + 1;
			frames[frame].bones = Z_Malloc(frames[frame].numbones * sizeof(bonepose_t));
			memset(frames[frame].bones, 0, frames[frame].numbones * sizeof(bonepose_t));
			frames[frame].bones[frames[frame].numbones - 1].m[0][1] = 35324;
			frames[frame].defined = 1;
			if (numframes < frame + 1)
				numframes = frame + 1;
		}
		else
		{
			//the token was bone number
			num = atoi( com_token );

			//get x, y, z tokens
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'x' value in line:%s\n", line);
				return 0;
			}
			x = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'y' value in line:%s\n", line);
				return 0;
			}
			y = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'z' value in line:%s\n", line);
				return 0;
			}
			z = atof( com_token );

			//get a, b, c tokens
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'a' value in line:%s\n", line);
				return 0;
			}
			a = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'b' value in line:%s\n", line);
				return 0;
			}
			b = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'c' value in line:%s\n", line);
				return 0;
			}
			c = atof( com_token );

			if (num < 0 || num >= numbones)
			{
				Con_Printf("error: invalid bone number: %i\n", num);
				return 0;
			}
			if (!bones[num].defined)
			{
				Con_Printf("error: bone %i not defined\n", num);
				return 0;
			}
			// LordHavoc: compute matrix
			frames[frame].bones[num] = computebonematrix(x * modelscale, y * modelscale, z * modelscale, a, b, c);
		}
		// skip any trailing parameters (might be a later version of smd)
		while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
	}
	// skip any trailing parameters (might be a later version of smd)
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');

	if (frame >= baseframe && qcheaderfile)
		FS_Printf(qcheaderfile, "$frame");
	for (frame = 0;frame < numframes;frame++)
	{
		if (!frames[frame].defined)
		{
			if (frame < 1)
			{
				Con_Printf("error: no first frame\n");
				return 0;
			}
			if (!frames[frame - 1].defined)
			{
				Con_Printf("error: no previous frame to duplicate\n");
				return 0;
			}
			sprintf(temp, "%s_%i", scene_name, frame - baseframe);
			if (strlen(temp) > 31)
			{
				Con_Printf("error: frame name \"%s\" is longer than 31 characters\n", temp);
				return 0;
			}
			Con_Printf("frame %s missing, duplicating previous frame %s with new name %s\n", temp, frames[frame - 1].name, temp);
			frames[frame].defined = 1;
			cleancopyname(frames[frame].name, temp, MAX_NAME);
			frames[frame].numbones = numbones + numattachments + 1;
			frames[frame].bones = Z_Malloc(frames[frame].numbones * sizeof(bonepose_t));
			memcpy(frames[frame].bones, frames[frame - 1].bones, frames[frame].numbones * sizeof(bonepose_t));
			frames[frame].bones[frames[frame].numbones - 1].m[0][1] = 35324;
			Con_Printf("duplicate frame named %s\n", frames[frame].name);
		}
		if (frame >= baseframe && headerfile)
			FS_Printf(headerfile, "#define MODEL_%s_%s_%i %i\n", model_name_uppercase, scene_name_uppercase, frame - baseframe, frame);
		if (frame >= baseframe && qcheaderfile)
			FS_Printf(qcheaderfile, " %s_%i", scene_name_lowercase, frame - baseframe + 1);
	}
	if (headerfile)
	{
		FS_Printf(headerfile, "#define MODEL_%s_%s_START %i\n", model_name_uppercase, scene_name_uppercase, baseframe);
		FS_Printf(headerfile, "#define MODEL_%s_%s_END %i\n", model_name_uppercase, scene_name_uppercase, numframes);
		FS_Printf(headerfile, "#define MODEL_%s_%s_LENGTH %i\n", model_name_uppercase, scene_name_uppercase, numframes - baseframe);
		FS_Printf(headerfile, "\n");
	}
	if (framegroupsfile)
	{
		FS_Printf(framegroupsfile, "%i %i %i %i // %s\n", baseframe, numframes - baseframe, scene_fps, scene_looping, scene_name_lowercase);
	}
	if (qcheaderfile)
		FS_Printf(qcheaderfile, "\n");
	return 1;
}

/*
int sentinelcheckframes(char *filename, int fileline)
{
	int i;
	for (i = 0;i < numframes;i++)
	{
		if (frames[i].defined && frames[i].bones)
		{
			if (frames[i].bones[frames[i].numbones - 1].m[0][1] != 35324)
			{
				Con_Printf("sentinelcheckframes: error on frame %s detected at %s:%i\n", frames[i].name, filename, fileline);
				exit(1);
			}
		}
	}
	return 1;
}
*/

static int freeframes(void)
{
	int i;
	//sentinelcheckframes(__FILE__, __LINE__);
	//Con_Printf("no errors were detected\n");
	for (i = 0;i < numframes;i++)
	{
		if (frames[i].defined && frames[i].bones)
		{
			//fprintf(stdout, "freeing %s\n", frames[i].name);
			//fflush(stdout);
			//if (frames[i].bones[frames[i].numbones - 1].m[0][1] != 35324)
			//	Con_Printf("freeframes: error on frame %s\n", frames[i].name);
			//else
				Z_Free(frames[i].bones);
		}
	}
	numframes = 0;
	return 1;
}

static int initframes(void)
{
	memset(frames, 0, sizeof(frame_t) * MAX_FRAMES);
	return 1;
}

static int parsetriangles(void)
{
	char line[1024], cleanline[MAX_NAME];
	int i, j, corner, found = 0;
	double org[3], normal[3];
	double d;
	int vbonenum;
	double vtexcoord[2];
	int		numinfluences;
	int		temp_numbone[MAX_INFLUENCES];
	double	temp_influence[MAX_INFLUENCES];

	numtriangles = 0;
	numshaders = 0;
	numverts = 0;

	for (i = 0;i < numbones;i++)
	{
		if (bones[i].parent >= 0)
			bonematrix[i] = concattransform(bonematrix[bones[i].parent], frames[0].bones[i]);
		else
			bonematrix[i] = frames[0].bones[i];
	}
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true))
	{
		// if this is the end keyword, we're done with this section of the file
		if (!strcmp(com_token, "end"))
			break;

		// get the shader name (already parsed)
		if (com_token[0] != '\n')
			cleancopyname (cleanline, com_token, MAX_NAME);
		else
			cleancopyname (cleanline, "notexture", MAX_NAME);
		found = 0;
		for (i = 0;i < numshaders;i++)
		{
			if (!strcmp(shaders[i], cleanline))
			{
				found = 1;
				break;
			}
		}
		triangles[numtriangles].shadernum = i;
		if (!found)
		{
			if (i == MAX_SHADERS)
			{
				Con_Printf("MAX_SHADERS reached\n");
				return 0;
			}
			cleancopyname(shaders[i], cleanline, MAX_NAME);
			numshaders++;
		}
		if (com_token[0] != '\n')
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
		}
		for (corner = 0;corner < 3;corner++)
		{
			//parse this line read by tokens
			org[0] = 0;org[1] = 0;org[2] = 0;
			normal[0] = 0;normal[1] = 0;normal[2] = 0;
			vtexcoord[0] = 0;vtexcoord[1] = 0;

			//get bonenum token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'bonenum', script line:%s\n", line);
				return 0;
			}
			vbonenum = atoi( com_token );

			//get org[0] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'org[0]', script line:%s\n", line);
				return 0;
			}
			org[0] = atof( com_token ) * modelscale;

			//get org[1] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'org[1]', script line:%s\n", line);
				return 0;
			}
			org[1] = atof( com_token ) * modelscale;

			//get org[2] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'org[2]', script line:%s\n", line);
				return 0;
			}
			org[2] = atof( com_token ) * modelscale;

			//get normal[0] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'normal[0]', script line:%s\n", line);
				return 0;
			}
			normal[0] = atof( com_token );

			//get normal[1] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'normal[1]', script line:%s\n", line);
				return 0;
			}
			normal[1] = atof( com_token );

			//get normal[2] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'normal[2]', script line:%s\n", line);
				return 0;
			}
			normal[2] = atof( com_token );

			//get vtexcoord[0] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'vtexcoord[0]', script line:%s\n", line);
				return 0;
			}
			vtexcoord[0] = atof( com_token );

			//get vtexcoord[1] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'vtexcoord[1]', script line:%s\n", line);
				return 0;
			}
			vtexcoord[1] = atof( com_token );

			// are there more words (HalfLife2) or not (HalfLife1)?
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
			{
				// one influence (HalfLife1)
				numinfluences = 1;
				temp_numbone[0] = vbonenum;
				temp_influence[0] = 1.0f;
			}
			else
			{
				// multiple influences found (HalfLife2)
				int c;

				numinfluences = atoi( com_token );
				if( !numinfluences )
				{
					Con_Printf("error in parsetriangles, expecting 'numinfluences', script line:%s\n", line);
					return 0;
				}
				if (numinfluences >= MAX_INFLUENCES)
				{
					Con_Printf("numinfluences is too big\n");
					return 0;
				}

				//read by pairs, bone number and influence
				for( c = 0; c < numinfluences; c++ )
				{
					//get bone number
					if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
					{
						Con_Printf("invalid vertex influence \"%s\"\n", line);
						return 0;
					}
					temp_numbone[c] = atoi(com_token);
					if(temp_numbone[c] < 0 || temp_numbone[c] >= numbones )
					{
						Con_Printf("invalid vertex influence (invalid bone number) \"%s\"\n", line);
						return 0;
					}
					//get influence weight
					if (!COM_ParseToken_VM_Tokenize(&tokenpos, true) || com_token[0] <= ' ')
					{
						Con_Printf("invalid vertex influence \"%s\"\n", line);
						return 0;
					}
					temp_influence[c] = atof(com_token);
					if( temp_influence[c] < 0.0f )
					{
						Con_Printf("invalid vertex influence weight, ignored \"%s\"\n", line);
						return 0;
					}
					else if( temp_influence[c] > 1.0f )
						temp_influence[c] = 1.0f;
				}
			}

			// validate linked bones
			if( numinfluences < 1)
			{
				Con_Printf("vertex with no influence found in triangle data\n");
				return 0;
			}
			for( i=0; i<numinfluences; i++ )
			{
				if (temp_numbone[i] < 0 || temp_numbone[i] >= MAX_BONES )
				{
					Con_Printf("invalid bone number %i in triangle data\n", temp_numbone[i]);
					return 0;
				}
				if (!bones[temp_numbone[i]].defined)
				{
					Con_Printf("bone %i in triangle data is not defined\n", temp_numbone[i]);
					return 0;
				}
			}

			// add vertex to list if unique
			for (i = 0;i < numverts;i++)
			{
				if (vertices[i].shadernum != triangles[numtriangles].shadernum
					|| vertices[i].numinfluences != numinfluences
					|| VectorDistance(vertices[i].originalorigin, org) > EPSILON_VERTEX
					|| VectorDistance(vertices[i].originalnormal, normal) > EPSILON_NORMAL
					|| VectorDistance2D(vertices[i].texcoord, vtexcoord) > EPSILON_TEXCOORD)
					continue;
				for (j = 0;j < numinfluences;j++)
					if (vertices[i].influencebone[j] != temp_numbone[j] || vertices[i].influenceweight[j] != temp_influence[j])
						break;
				if (j == numinfluences)
					break;
			}
			triangles[numtriangles].v[corner] = i;

			if (i >= numverts)
			{
				numverts++;
				if (numverts >= MAX_VERTS)
				{
					Con_Printf("numverts is too big\n");
					return 0;
				}
				vertices[i].shadernum = triangles[numtriangles].shadernum;
				vertices[i].texcoord[0] = vtexcoord[0];
				vertices[i].texcoord[1] = vtexcoord[1];
				vertices[i].originalorigin[0] = org[0];vertices[i].originalorigin[1] = org[1];vertices[i].originalorigin[2] = org[2];
				vertices[i].originalnormal[0] = normal[0];vertices[i].originalnormal[1] = normal[1];vertices[i].originalnormal[2] = normal[2];
				vertices[i].numinfluences = numinfluences;
				for( j=0; j < vertices[i].numinfluences; j++ )
				{
					// untransform the origin and normal
					inversetransform(org, bonematrix[temp_numbone[j]], vertices[i].influenceorigin[j]);
					inverserotate(normal, bonematrix[temp_numbone[j]], vertices[i].influencenormal[j]);

					d = 1 / sqrt(vertices[i].influencenormal[j][0] * vertices[i].influencenormal[j][0] + vertices[i].influencenormal[j][1] * vertices[i].influencenormal[j][1] + vertices[i].influencenormal[j][2] * vertices[i].influencenormal[j][2]);
					vertices[i].influencenormal[j][0] *= d;
					vertices[i].influencenormal[j][1] *= d;
					vertices[i].influencenormal[j][2] *= d;

					// round off minor errors in the normal
					if (fabs(vertices[i].influencenormal[j][0]) < 0.001)
						vertices[i].influencenormal[j][0] = 0;
					if (fabs(vertices[i].influencenormal[j][1]) < 0.001)
						vertices[i].influencenormal[j][1] = 0;
					if (fabs(vertices[i].influencenormal[j][2]) < 0.001)
						vertices[i].influencenormal[j][2] = 0;

					d = 1 / sqrt(vertices[i].influencenormal[j][0] * vertices[i].influencenormal[j][0] + vertices[i].influencenormal[j][1] * vertices[i].influencenormal[j][1] + vertices[i].influencenormal[j][2] * vertices[i].influencenormal[j][2]);
					vertices[i].influencenormal[j][0] *= d;
					vertices[i].influencenormal[j][1] *= d;
					vertices[i].influencenormal[j][2] *= d;
					vertices[i].influencebone[j] = temp_numbone[j];
					vertices[i].influenceweight[j] = temp_influence[j];
				}
			}
			// skip any trailing parameters (might be a later version of smd)
			while (com_token[0] != '\n' && COM_ParseToken_VM_Tokenize(&tokenpos, true));
		}
		numtriangles++;
		if (numtriangles >= MAX_TRIS)
		{
			Con_Printf("numtriangles is too big\n");
			return 0;
		}
	}
	// skip any trailing parameters (might be a later version of smd)
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');

	Con_Printf("parsetriangles: done\n");
	return 1;
}

static int parsemodelfile(void)
{
	tokenpos = modelfile;
	while (COM_ParseToken_VM_Tokenize(&tokenpos, false))
	{
		if (!strcmp(com_token, "version"))
		{
			COM_ParseToken_VM_Tokenize(&tokenpos, true);
			if (atoi(com_token) != 1)
			{
				Con_Printf("file is version %s, only version 1 is supported\n", com_token);
				return 0;
			}
		}
		else if (!strcmp(com_token, "nodes"))
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
			if (!parsenodes())
				return 0;
		}
		else if (!strcmp(com_token, "skeleton"))
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
			if (!parseskeleton())
				return 0;
		}
		else if (!strcmp(com_token, "triangles"))
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true) && com_token[0] != '\n');
			if (!parsetriangles())
				return 0;
		}
		else
		{
			Con_Printf("unknown command \"%s\"\n", com_token);
			return 0;
		}
	}
	return 1;
}

static int addattachments(void)
{
	int i, j;
	//sentinelcheckframes(__FILE__, __LINE__);
	for (i = 0;i < numattachments;i++)
	{
		bones[numbones].defined = 1;
		bones[numbones].parent = -1;
		bones[numbones].flags = DPMBONEFLAG_ATTACH;
		for (j = 0;j < numbones;j++)
			if (!strcmp(bones[j].name, attachments[i].parentname))
				bones[numbones].parent = j;
		if (bones[numbones].parent < 0)
			Con_Printf("warning: unable to find bone \"%s\" for attachment \"%s\", using root instead\n", attachments[i].parentname, attachments[i].name);
		cleancopyname(bones[numbones].name, attachments[i].name, MAX_NAME);
		// we have to duplicate the attachment in every frame
		//sentinelcheckframes(__FILE__, __LINE__);
		for (j = 0;j < numframes;j++)
			frames[j].bones[numbones] = attachments[i].matrix;
		//sentinelcheckframes(__FILE__, __LINE__);
		numbones++;
	}
	numattachments = 0;
	//sentinelcheckframes(__FILE__, __LINE__);
	return 1;
}

static int cleanupbones(void)
{
	int i, j;
	int oldnumbones;
	int remap[MAX_BONES];
	//sentinelcheckframes(__FILE__, __LINE__);

	// figure out which bones are used
	for (i = 0;i < numbones;i++)
	{
		if (bones[i].defined)
		{
			// keep all bones as they may be unmentioned attachment points, and this allows the same animations to be used on multiple meshes that might have different bone usage but the same original skeleton
			bones[i].users = 1;
			//bones[i].users = 0;
			if (bones[i].flags & DPMBONEFLAG_ATTACH)
				bones[i].users++;
		}
	}
	for (i = 0;i < numverts;i++)
		for (j = 0;j < vertices[i].numinfluences;j++)
			bones[vertices[i].influencebone[j]].users++;
	for (i = 0;i < numbones;i++)
		if (bones[i].defined && bones[i].users && bones[i].parent >= 0)
			bones[bones[i].parent].users++;

	// now calculate the remapping table for whichever ones should remain
	oldnumbones = numbones;
	numbones = 0;
	for (i = 0;i < oldnumbones;i++)
	{
		if (bones[i].defined && bones[i].users)
			remap[i] = numbones++;
		else
		{
			remap[i] = -1;
			//for (j = 0;j < numframes;j++)
			//	memset(&frames[j].bones[i], 0, sizeof(bonepose_t));
		}
	}

	//sentinelcheckframes(__FILE__, __LINE__);
	// shuffle bone data around to eliminate gaps
	for (i = 0;i < oldnumbones;i++)
		if (bones[i].parent >= 0)
			bones[i].parent = remap[bones[i].parent];
	for (i = 0;i < oldnumbones;i++)
		if (remap[i] >= 0 && remap[i] != i)
			bones[remap[i]] = bones[i];
	//sentinelcheckframes(__FILE__, __LINE__);
	for (i = 0;i < numframes;i++)
	{
		if (frames[i].defined)
		{
			//sentinelcheckframes(__FILE__, __LINE__);
			for (j = 0;j < oldnumbones;j++)
			{
				if (remap[j] >= 0 && remap[j] != j)
				{
					//Con_Printf("copying bone %i to %i\n", j, remap[j]);
					//sentinelcheckframes(__FILE__, __LINE__);
					frames[i].bones[remap[j]] = frames[i].bones[j];
					//sentinelcheckframes(__FILE__, __LINE__);
				}
			}
			//sentinelcheckframes(__FILE__, __LINE__);
		}
	}
	//sentinelcheckframes(__FILE__, __LINE__);

	// remap vertex references
	for (i = 0;i < numverts;i++)
		if (vertices[i].numinfluences)
			for(j = 0;j < vertices[i].numinfluences;j++)
				vertices[i].influencebone[j] = remap[vertices[i].influencebone[j]];

	//sentinelcheckframes(__FILE__, __LINE__);
	return 1;
}

static int cleanupframes(void)
{
	int i, j/*, best*/, k;
	double org[3], dist, mins[3], maxs[3], yawradius, allradius;
	for (i = 0;i < numframes;i++)
	{
		//sentinelcheckframes(__FILE__, __LINE__);
		for (j = 0;j < numbones;j++)
		{
			if (bones[j].defined)
			{
				if (bones[j].parent >= 0)
					bonematrix[j] = concattransform(bonematrix[bones[j].parent], frames[i].bones[j]);
				else
					bonematrix[j] = frames[i].bones[j];
			}
		}
		mins[0] = mins[1] = mins[2] = 0;
		maxs[0] = maxs[1] = maxs[2] = 0;
		yawradius = 0;
		allradius = 0;
		//best = 0;
		for (j = 0;j < numverts;j++)
		{
			for (k = 0;k < vertices[i].numinfluences;k++)
			{
				transform(vertices[j].influenceorigin[k], bonematrix[vertices[j].influencebone[k]], org);

				if (mins[0] > org[0]) mins[0] = org[0];
				if (mins[1] > org[1]) mins[1] = org[1];
				if (mins[2] > org[2]) mins[2] = org[2];
				if (maxs[0] < org[0]) maxs[0] = org[0];
				if (maxs[1] < org[1]) maxs[1] = org[1];
				if (maxs[2] < org[2]) maxs[2] = org[2];

				dist = org[0]*org[0]+org[1]*org[1];

				if (yawradius < dist)
					yawradius = dist;

				dist += org[2]*org[2];

				if (allradius < dist)
				{
					//		best = j;
					allradius = dist;
				}
			}
		}
		/*
		j = best;
		transform(vertices[j].origin, bonematrix[vertices[j].bonenum], org);
		Con_Printf("furthest vert of frame %s is on bone %s (%i), matrix is:\n%f %f %f %f\n%f %f %f %f\n%f %f %f %f\nvertex origin %f %f %f - %f %f %f\nbbox %f %f %f - %f %f %f - %f %f\n", frames[i].name, bones[vertices[j].bonenum].name, vertices[j].bonenum
		, bonematrix[vertices[j].bonenum].m[0][0], bonematrix[vertices[j].bonenum].m[0][1], bonematrix[vertices[j].bonenum].m[0][2], bonematrix[vertices[j].bonenum].m[0][3]
		, bonematrix[vertices[j].bonenum].m[1][0], bonematrix[vertices[j].bonenum].m[1][1], bonematrix[vertices[j].bonenum].m[1][2], bonematrix[vertices[j].bonenum].m[1][3]
		, bonematrix[vertices[j].bonenum].m[2][0], bonematrix[vertices[j].bonenum].m[2][1], bonematrix[vertices[j].bonenum].m[2][2], bonematrix[vertices[j].bonenum].m[2][3]
		, vertices[j].origin[0], vertices[j].origin[1], vertices[j].origin[2], org[0], org[1], org[2]
		, mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2], sqrt(yawradius), sqrt(allradius));
		*/
		frames[i].mins[0] = mins[0];
		frames[i].mins[1] = mins[1];
		frames[i].mins[2] = mins[2];
		frames[i].maxs[0] = maxs[0];
		frames[i].maxs[1] = maxs[1];
		frames[i].maxs[2] = maxs[2];
		frames[i].yawradius = sqrt(yawradius);
		frames[i].allradius = sqrt(allradius);
		//sentinelcheckframes(__FILE__, __LINE__);
	}
	return 1;
}

static int cleanupshadernames(void)
{
	int i;
	char temp[1024+MAX_NAME];
	for (i = 0;i < numshaders;i++)
	{
		chopextension(shaders[i]);
		sprintf(temp, "%s%s", texturedir_name, shaders[i]);
		if (strlen(temp) >= MAX_NAME)
			Con_Printf("warning: shader name too long %s\n", temp);
		cleancopyname(shaders[i], temp, MAX_NAME);
	}
	return 1;
}

static void fixrootbones(void)
{
	int i, j;
	float cy, sy;
	bonepose_t rootpose;
	cy = cos(modelrotate * M_PI / 180.0);
	sy = sin(modelrotate * M_PI / 180.0);
	rootpose.m[0][0] = cy;
	rootpose.m[1][0] = sy;
	rootpose.m[2][0] = 0;
	rootpose.m[0][1] = -sy;
	rootpose.m[1][1] = cy;
	rootpose.m[2][1] = 0;
	rootpose.m[0][2] = 0;
	rootpose.m[1][2] = 0;
	rootpose.m[2][2] = 1;
	rootpose.m[0][3] = -modelorigin[0] * rootpose.m[0][0] + -modelorigin[1] * rootpose.m[1][0] + -modelorigin[2] * rootpose.m[2][0];
	rootpose.m[1][3] = -modelorigin[0] * rootpose.m[0][1] + -modelorigin[1] * rootpose.m[1][1] + -modelorigin[2] * rootpose.m[2][1];
	rootpose.m[2][3] = -modelorigin[0] * rootpose.m[0][2] + -modelorigin[1] * rootpose.m[1][2] + -modelorigin[2] * rootpose.m[2][2];
	for (j = 0;j < numbones;j++)
	{
		if (bones[j].parent < 0)
		{
			// a root bone
			for (i = 0;i < numframes;i++)
				frames[i].bones[j] = concattransform(rootpose, frames[i].bones[j]);
		}
	}
}

static char *token;

static void inittokens(char *script)
{
	token = script;
}

static char tokenbuffer[1024];

static char *gettoken(void)
{
	char *out;
	out = tokenbuffer;
	while (*token && *token <= ' ' && *token != '\n')
		token++;
	if (!*token)
		return NULL;
	switch (*token)
	{
	case '\"':
		token++;
		while (*token && *token != '\r' && *token != '\n' && *token != '\"')
			*out++ = *token++;
		*out++ = 0;
		if (*token == '\"')
			token++;
		else
			Con_Printf("warning: unterminated quoted string\n");
		return tokenbuffer;
	case '(':
	case ')':
	case '{':
	case '}':
	case '[':
	case ']':
	case '\n':
		tokenbuffer[0] = *token++;
		tokenbuffer[1] = 0;
		return tokenbuffer;
	default:
		while (*token && *token > ' ' && *token != '(' && *token != ')' && *token != '{' && *token != '}' && *token != '[' && *token != ']' && *token != '\"')
			*out++ = *token++;
		*out++ = 0;
		return tokenbuffer;
	}
}

typedef struct sccommand_s
{
	char *name;
	int (*code)(void);
}
sccommand;

static int isdouble(char *c)
{
	while (*c)
	{
		switch (*c)
		{
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '.':
		case 'e':
		case 'E':
		case '-':
		case '+':
			break;
		default:
			return 0;
		}
		c++;
	}
	return 1;
}

static int isfilename(char *c)
{
	while (*c)
	{
		if (*c < ' ')
			return 0;
		c++;
	}
	return 1;
}

static int sc_attachment(void)
{
	int i;
	char *c;
	double origin[3], angles[3];
	if (numattachments >= MAX_ATTACHMENTS)
	{
		Con_Printf("ran out of attachment slots\n");
		return 0;
	}
	c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	cleancopyname(attachments[numattachments].name, c, MAX_NAME);

	c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	cleancopyname(attachments[numattachments].parentname, c, MAX_NAME);

	for (i = 0;i < 6;i++)
	{
		c = gettoken();
		if (!c)
			return 0;
		if (!isdouble(c))
			return 0;
		if (i < 3)
			origin[i] = atof(c);
		else
			angles[i - 3] = atof(c) * (M_PI / 180.0);
	}
	attachments[numattachments].matrix = computebonematrix(origin[0], origin[1], origin[2], angles[0], angles[1], angles[2]);

	numattachments++;
	return 1;
}

static int sc_outputdir(void)
{
	char *c;
	if (outputdir_name[0]) {
		while (gettoken()[0] != '\n');
		return 1;
	}
	c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	strlcpy(outputdir_name, c, sizeof(outputdir_name));
	chopextension(outputdir_name);
	if (strlen(outputdir_name) && outputdir_name[strlen(outputdir_name) - 1] != '/')
		strlcat(outputdir_name, "/", sizeof(outputdir_name));
	return 1;
}

static int sc_model(void)
{
	char *c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	strlcpy(model_name, c, MAX_FILEPATH);
	chopextension(model_name);
	stringtouppercase(model_name, model_name_uppercase);
	stringtolowercase(model_name, model_name_lowercase);

	return 1;
}

static int sc_texturedir(void)
{
	char *c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	strlcpy(texturedir_name, c, MAX_FILEPATH);
	if (texturedir_name[0] == '/')
	{
		Con_Printf("texturedir not allowed to begin with \"/\" (could access parent directories)\n");
		Con_Printf("normally a texturedir should be the same name as the model it is for (example: models/biggun/ for models/biggun.dpm)\n");
		return 0;
	}
	if (strstr(texturedir_name, ":"))
	{
		Con_Printf("\":\" not allowed in texturedir (could access parent directories)\n");
		Con_Printf("normally a texturedir should be the same name as the model it is for (example: models/biggun/ for models/biggun.dpm)\n");
		return 0;
	}
	if (strstr(texturedir_name, "."))
	{
		Con_Printf("\".\" not allowed in texturedir (could access parent directories)\n");
		Con_Printf("normally a texturedir should be the same name as the model it is for (example: models/biggun/ for models/biggun.dpm)\n");
		return 0;
	}
	if (strstr(texturedir_name, "\\"))
	{
		Con_Printf("\"\\\" not allowed in texturedir (use / instead)\n");
		Con_Printf("normally a texturedir should be the same name as the model it is for (example: models/biggun/ for models/biggun.dpm)\n");
		return 0;
	}
	return 1;
}

static int sc_origin(void)
{
	int i;
	char *c;
	for (i = 0;i < 3;i++)
	{
		c = gettoken();
		if (!c)
			return 0;
		if (!isdouble(c))
			return 0;
		modelorigin[i] = atof(c);
	}
	return 1;
}

static int sc_rotate(void)
{
	char *c = gettoken();
	if (!c)
		return 0;
	if (!isdouble(c))
		return 0;
	modelrotate = atof(c);
	return 1;
}

static int sc_scale(void)
{
	char *c = gettoken();
	if (!c)
		return 0;
	if (!isdouble(c))
		return 0;
	modelscale = atof(c);
	return 1;
}

static int sc_scene(void)
{
	char *c;
	char filename[MAX_FILEPATH * 3];
	c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	dpsnprintf(filename, sizeof(filename), "%s%s", outputdir_name, c);
	modelfile = (char*)FS_LoadFile(filename, model_compile_memory_pool, 0, NULL);
	if (!modelfile)
	{
		Con_Printf("sc_scene: %s load failed\n", filename);
		return 0;
	}
	cleancopyname(scene_name, c, MAX_NAME);
	chopextension(scene_name);
	stringtouppercase(scene_name, scene_name_uppercase);
	stringtolowercase(scene_name, scene_name_lowercase);
	Con_Printf("parsing scene %s\n", scene_name);
	if (!headerfile)
	{
		sprintf(filename, "%s%s.h", outputdir_name, model_name);
		headerfile = FS_OpenRealFile(filename, "w", false);
		if (headerfile)
		{
			FS_Printf(headerfile, "/*\n");
			FS_Printf(headerfile, "Generated header file for %s\n", model_name);
			FS_Printf(headerfile, "This file contains frame number definitions for use in code referencing the model, to make code more readable and maintainable.\n");
			FS_Printf(headerfile, "*/\n");
			FS_Printf(headerfile, "\n");
			FS_Printf(headerfile, "#ifndef MODEL_%s_H\n", model_name_uppercase);
			FS_Printf(headerfile, "#define MODEL_%s_H\n", model_name_uppercase);
			FS_Printf(headerfile, "\n");
		}
	}
	if (!qcheaderfile)
	{
		sprintf(filename, "%s%s.qc", outputdir_name, model_name);
		qcheaderfile = FS_OpenRealFile(filename, "w", false);
		if (qcheaderfile)
		{
			FS_Printf(qcheaderfile, "/*\n");
			FS_Printf(qcheaderfile, "Generated header file for %s\n", model_name);
			FS_Printf(qcheaderfile, "This file contains frame number definitions for use in code referencing the model, simply copy and paste into your qc file.\n");
			FS_Printf(qcheaderfile, "*/\n");
			FS_Printf(qcheaderfile, "\n");
		}
	}
	if (!framegroupsfile)
	{
		sprintf(filename, "%s%s.framegroups", outputdir_name, model_name);
		framegroupsfile = FS_OpenRealFile(filename, "w", false);
	}
	scene_fps = 1;
	scene_looping = 1;
	while ((c = gettoken())[0] != '\n')
	{
		if (!strcmp(c, "fps"))
		{
			c = gettoken();
			if (c[0] == '\n') break;
			scene_fps = atoi(c);
		}
		if (!strcmp(c, "noloop"))
			scene_looping = 0;
	}
	if (!parsemodelfile())
	{
		Con_Printf("sc_scene: parsemodelfile() failed\n");
		return 0;
	}
	return 1;
}

static int sc_comment(void)
{
	while (gettoken()[0] != '\n');
	return 1;
}

static int sc_nothing(void)
{
	return 1;
}

static sccommand sc_commands[] =
{
	{"attachment", sc_attachment},
	{"outputdir", sc_outputdir},
	{"model", sc_model},
	{"texturedir", sc_texturedir},
	{"origin", sc_origin},
	{"rotate", sc_rotate},
	{"scale", sc_scale},
	{"scene", sc_scene},
	{"#", sc_comment},
	{"\n", sc_nothing},
	{"", NULL}
};

static int processcommand(char *command)
{
	int r;
	sccommand *c;
	c = sc_commands;
	while (c->name[0])
	{
		if (!strcmp(c->name, command))
		{
			Con_Printf("executing command %s\n", command);
			r = c->code();
			if (!r)
				Con_Printf("error processing script\n");
			return r;
		}
		c++;
	}
	Con_Printf("command %s not recognized\n", command);
	return 0;
}

static int writemodel_dpm(void);
static int writemodel_md3(void);
static void processscript(void)
{
	int i;
	char *c;
	inittokens(scriptbytes);
	numframes = 0;
	numbones = 0;
	numshaders = 0;
	numtriangles = 0;
	initframes();
	while ((c = gettoken()))
		if (c[0] > ' ')
			if (!processcommand(c))
				return;
	if (!addattachments())
	{
		freeframes();
		return;
	}
	if (!keepallbones && !cleanupbones())
	{
		freeframes();
		return;
	}
	if (!cleanupframes())
	{
		freeframes();
		return;
	}
	if (!cleanupshadernames())
	{
		freeframes();
		return;
	}
	fixrootbones();
	// print model stats
	Con_Printf("model stats:\n");
	Con_Printf("%i vertices %i triangles %i bones %i shaders %i frames\n", numverts, numtriangles, numbones, numshaders, numframes);
	Con_Printf("meshes:\n");
	for (i = 0;i < numshaders;i++)
	{
		int j;
		int nverts, ntris;
		nverts = 0;
		for (j = 0;j < numverts;j++)
			if (vertices[j].shadernum == i)
				nverts++;
		ntris = 0;
		for (j = 0;j < numtriangles;j++)
			if (triangles[j].shadernum == i)
				ntris++;
		Con_Printf("%5i tris%6i verts : %s\n", ntris, nverts, shaders[i]);
	}
	// write the model formats
	writemodel_dpm();
	writemodel_md3();
	freeframes();
}

int Mod_Compile_DPM_MD3(const char *script, const char *dir)
{
	texturedir_name = Z_Malloc(MAX_FILEPATH);
	outputdir_name = Z_Malloc(MAX_FILEPATH);
	model_name = Z_Malloc(MAX_FILEPATH);
	scene_name = Z_Malloc(MAX_FILEPATH);
	model_name_uppercase = Z_Malloc(MAX_FILEPATH);
	scene_name_uppercase = Z_Malloc(MAX_FILEPATH);
	model_name_lowercase = Z_Malloc(MAX_FILEPATH);
	scene_name_lowercase = Z_Malloc(MAX_FILEPATH);
	attachments = Z_Malloc(sizeof(attachment) * MAX_ATTACHMENTS);
	frames = Z_Malloc(sizeof(frame_t) * MAX_FRAMES);
	bones = Z_Malloc(sizeof(bone_t) * MAX_BONES); // master bone list
	//shaders[MAX_SHADERS][32];
	triangles = Z_Malloc(sizeof(triangle) * MAX_TRIS);
	vertices = Z_Malloc(sizeof(tripoint) * MAX_VERTS);
	bonematrix = Z_Malloc(sizeof(bonepose_t) * MAX_BONES);
	vertremap = Z_Malloc(sizeof(int) * MAX_VERTS);
	model_compile_memory_pool = Mem_AllocPool("smd_compile", 0, NULL);
	scriptbytes = (char*)FS_LoadFile(script, model_compile_memory_pool, 0, &scriptsize);
	if (!scriptbytes)
	{
		Con_Printf("unable to read script file\n");
		Mem_FreePool(&model_compile_memory_pool);
		return 0;
	}
	scriptend = scriptbytes + scriptsize;
	if (*dir) dpsnprintf(outputdir_name, MAX_FILEPATH, "%s", dir);
	processscript();
	if (headerfile)
	{
		FS_Printf(headerfile, "#endif /*MODEL_%s_H*/\n", model_name_uppercase);
		FS_Close(headerfile);
		headerfile = NULL;
	}
	if (qcheaderfile)
	{
		FS_Printf(qcheaderfile, "\n// end of frame definitions for %s\n\n\n", model_name);
		FS_Close(qcheaderfile);
		qcheaderfile = NULL;
	}
	if (framegroupsfile)
	{
		FS_Close(framegroupsfile);
		framegroupsfile = NULL;
	}
#if (_MSC_VER && _DEBUG)
	Con_Printf("destroy any key\n");
	getchar();
#endif
	Mem_FreePool(&model_compile_memory_pool);
	Z_Free(texturedir_name);
	Z_Free(outputdir_name);
	Z_Free(model_name);
	Z_Free(scene_name);
	Z_Free(model_name_uppercase);
	Z_Free(scene_name_uppercase);
	Z_Free(model_name_lowercase);
	Z_Free(scene_name_lowercase);
	Z_Free(attachments);
	Z_Free(frames);
	Z_Free(bones);
	Z_Free(triangles);
	Z_Free(vertices);
	Z_Free(bonematrix);
	Z_Free(vertremap);
	return 0;
}

/*
bone_t tbone[MAX_BONES];
int boneusage[MAX_BONES], boneremap[MAX_BONES], boneunmap[MAX_BONES];

int numtbones;

typedef struct tvert_s
{
	int bonenum;
	double texcoord[2];
	double origin[3];
	double normal[3];
}
tvert_t;

tvert_t tvert[MAX_VERTS];
int tvertusage[MAX_VERTS];
int tvertremap[MAX_VERTS];
int tvertunmap[MAX_VERTS];

int numtverts;

int ttris[MAX_TRIS][4]; // 0, 1, 2 are vertex indices, 3 is shader number

char shaders[MAX_SHADERS][MAX_NAME];

int numshaders;

int shaderusage[MAX_SHADERS];
int shadertrisstart[MAX_SHADERS];
int shadertriscurrent[MAX_SHADERS];
int shadertris[MAX_TRIS];
*/

static unsigned char *output;
static unsigned char outputbuffer[MAX_FILESIZE];

static void putstring(char *in, int length)
{
	while (*in && length)
	{
		*output++ = *in++;
		length--;
	}
	// pad with nulls
	while (length--)
		*output++ = 0;
}

/*
static void putnulls(int num)
{
	while (num--)
		*output++ = 0;
}
*/

static void putbyte(int num)
{
	*output++ = num;
}

/*
static void putbeshort(int num)
{
	*output++ = ((num >>  8) & 0xFF);
	*output++ = ((num >>  0) & 0xFF);
}
*/

static void putbelong(int num)
{
	*output++ = ((num >> 24) & 0xFF);
	*output++ = ((num >> 16) & 0xFF);
	*output++ = ((num >>  8) & 0xFF);
	*output++ = ((num >>  0) & 0xFF);
}

static void putbefloat(double num)
{
	union
	{
		float f;
		int i;
	}
	n;

	n.f = num;
	// this matchs for both positive and negative 0, thus setting it to positive 0
	if (n.f == 0)
		n.f = 0;
	putbelong(n.i);
}

static void putleshort(int num)
{
	*output++ = ((num >>  0) & 0xFF);
	*output++ = ((num >>  8) & 0xFF);
}

static void putlelong(int num)
{
	*output++ = ((num >>  0) & 0xFF);
	*output++ = ((num >>  8) & 0xFF);
	*output++ = ((num >> 16) & 0xFF);
	*output++ = ((num >> 24) & 0xFF);
}

static void putlefloat(double num)
{
	union
	{
		float f;
		int i;
	}
	n;

	n.f = num;
	// this matchs for both positive and negative 0, thus setting it to positive 0
	if (n.f == 0)
		n.f = 0;
	putlelong(n.i);
}

/*
static void putinit(void)
{
	output = outputbuffer;
}
*/

static int putgetposition(void)
{
	return (int) ((unsigned char *) output - (unsigned char *) outputbuffer);
}

static void putsetposition(int n)
{
	output = (unsigned char *) outputbuffer + n;
}

//double posemins[MAX_FRAMES][3], posemaxs[MAX_FRAMES][3], poseradius[MAX_FRAMES];

static int writemodel_dpm(void)
{
	int i, j, k, l, nverts, ntris;
	float mins[3], maxs[3], yawradius, allradius;
	int pos_filesize, pos_lumps, pos_frames, pos_bones, pos_meshs, pos_verts, pos_texcoords, pos_index, pos_groupids, pos_framebones;
	int filesize, restoreposition;
	char filename[MAX_FILEPATH * 3];

	//sentinelcheckframes(__FILE__, __LINE__);

	putsetposition(0);

	// ID string
	putstring("DARKPLACESMODEL", 16);

	// type 2 model, hierarchical skeletal pose
	putbelong(2);

	// filesize
	pos_filesize = putgetposition();
	putbelong(0);

	// bounding box, cylinder, and sphere
	mins[0] = frames[0].mins[0];
	mins[1] = frames[0].mins[1];
	mins[2] = frames[0].mins[2];
	maxs[0] = frames[0].maxs[0];
	maxs[1] = frames[0].maxs[1];
	maxs[2] = frames[0].maxs[2];
	yawradius = frames[0].yawradius;
	allradius = frames[0].allradius;
	for (i = 0;i < numframes;i++)
	{
		if (mins[0] > frames[i].mins[0]) mins[0] = frames[i].mins[0];
		if (mins[1] > frames[i].mins[1]) mins[1] = frames[i].mins[1];
		if (mins[2] > frames[i].mins[2]) mins[2] = frames[i].mins[2];
		if (maxs[0] < frames[i].maxs[0]) maxs[0] = frames[i].maxs[0];
		if (maxs[1] < frames[i].maxs[1]) maxs[1] = frames[i].maxs[1];
		if (maxs[2] < frames[i].maxs[2]) maxs[2] = frames[i].maxs[2];
		if (yawradius < frames[i].yawradius) yawradius = frames[i].yawradius;
		if (allradius < frames[i].allradius) allradius = frames[i].allradius;
	}
	putbefloat(mins[0]);
	putbefloat(mins[1]);
	putbefloat(mins[2]);
	putbefloat(maxs[0]);
	putbefloat(maxs[1]);
	putbefloat(maxs[2]);
	putbefloat(yawradius);
	putbefloat(allradius);

	// numbers of things
	putbelong(numbones);
	putbelong(numshaders);
	putbelong(numframes);

	// offsets to things
	pos_lumps = putgetposition();
	putbelong(0);
	putbelong(0);
	putbelong(0);

	// store the bones
	pos_bones = putgetposition();
	for (i = 0;i < numbones;i++)
	{
		putstring(bones[i].name, MAX_NAME);
		putbelong(bones[i].parent);
		putbelong(bones[i].flags);
	}

	// store the meshs
	pos_meshs = putgetposition();
	// skip over the mesh structs, they will be filled in later
	putsetposition(pos_meshs + numshaders * (MAX_NAME + 24));

	// store the frames
	pos_frames = putgetposition();
	// skip over the frame structs, they will be filled in later
	putsetposition(pos_frames + numframes * (MAX_NAME + 36));

	// store the data referenced by meshs
	for (i = 0;i < numshaders;i++)
	{
		pos_verts = putgetposition();
		nverts = 0;
		for (j = 0;j < numverts;j++)
		{
			if (vertices[j].shadernum == i)
			{
				vertremap[j] = nverts++;
				putbelong(vertices[j].numinfluences); // how many bones for this vertex (always 1 for smd)
				for (k = 0;k < vertices[j].numinfluences;k++)
				{
					putbefloat(vertices[j].influenceorigin[k][0] * vertices[j].influenceweight[k]);
					putbefloat(vertices[j].influenceorigin[k][1] * vertices[j].influenceweight[k]);
					putbefloat(vertices[j].influenceorigin[k][2] * vertices[j].influenceweight[k]);
					putbefloat(vertices[j].influenceweight[k]); // influence of the bone on the vertex
					putbefloat(vertices[j].influencenormal[k][0] * vertices[j].influenceweight[k]);
					putbefloat(vertices[j].influencenormal[k][1] * vertices[j].influenceweight[k]);
					putbefloat(vertices[j].influencenormal[k][2] * vertices[j].influenceweight[k]);
					putbelong(vertices[j].influencebone[k]); // number of the bone
				}
			}
			else
				vertremap[j] = -1;
		}
		pos_texcoords = putgetposition();
		for (j = 0;j < numverts;j++)
		{
			if (vertices[j].shadernum == i)
			{
				// OpenGL wants bottom to top texcoords
				putbefloat(vertices[j].texcoord[0]);
				putbefloat(1.0f - vertices[j].texcoord[1]);
			}
		}
		pos_index = putgetposition();
		ntris = 0;
		for (j = 0;j < numtriangles;j++)
		{
			if (triangles[j].shadernum == i)
			{
				putbelong(vertremap[triangles[j].v[0]]);
				putbelong(vertremap[triangles[j].v[1]]);
				putbelong(vertremap[triangles[j].v[2]]);
				ntris++;
			}
		}
		pos_groupids = putgetposition();
		for (j = 0;j < numtriangles;j++)
			if (triangles[j].shadernum == i)
				putbelong(0);

		// now we actually write the mesh header
		restoreposition = putgetposition();
		putsetposition(pos_meshs + i * (MAX_NAME + 24));
		putstring(shaders[i], MAX_NAME);
		putbelong(nverts);
		putbelong(ntris);
		putbelong(pos_verts);
		putbelong(pos_texcoords);
		putbelong(pos_index);
		putbelong(pos_groupids);
		putsetposition(restoreposition);
	}

	// store the data referenced by frames
	for (i = 0;i < numframes;i++)
	{
		pos_framebones = putgetposition();
		for (j = 0;j < numbones;j++)
			for (k = 0;k < 3;k++)
				for (l = 0;l < 4;l++)
					putbefloat(frames[i].bones[j].m[k][l]);

		// now we actually write the frame header
		restoreposition = putgetposition();
		putsetposition(pos_frames + i * (MAX_NAME + 36));
		putstring(frames[i].name, MAX_NAME);
		putbefloat(frames[i].mins[0]);
		putbefloat(frames[i].mins[1]);
		putbefloat(frames[i].mins[2]);
		putbefloat(frames[i].maxs[0]);
		putbefloat(frames[i].maxs[1]);
		putbefloat(frames[i].maxs[2]);
		putbefloat(frames[i].yawradius);
		putbefloat(frames[i].allradius);
		putbelong(pos_framebones);
		putsetposition(restoreposition);
	}

	filesize = putgetposition();
	putsetposition(pos_lumps);
	putbelong(pos_bones);
	putbelong(pos_meshs);
	putbelong(pos_frames);
	putsetposition(pos_filesize);
	putbelong(filesize);
	putsetposition(filesize);

	sprintf(filename, "%s%s.dpm", outputdir_name, model_name);
	FS_WriteFile(filename, outputbuffer, filesize);
	Con_Printf("wrote file %s (size %5ik)\n", filename, (filesize + 1023) >> 10);

	return 1;
}


static int writemodel_md3(void)
{
	int i, j, k, l, nverts, ntris, numtags;
	int pos_lumps, pos_frameinfo, pos_tags, pos_firstmesh, pos_meshstart, pos_meshlumps, pos_meshshaders, pos_meshelements, pos_meshtexcoords, pos_meshframevertices, pos_meshend, pos_end;
	int filesize, restoreposition;
	char filename[MAX_FILEPATH * 3];

	// FIXME: very bad known bug: this does not obey Quake3's limit of 1000 vertices and 2000 triangles per mesh

	//sentinelcheckframes(__FILE__, __LINE__);

	putsetposition(0);

	// write model header
	putstring("IDP3", 4); // identifier
	putlelong(15); // version
	putstring(model_name, 64);
	putlelong(0);// flags (FIXME)
	putlelong(numframes); // frames
	numtags = 0;
	for (i = 0;i < numbones;i++)
		if (!strncmp(bones[i].name, "TAG_", 4))
			numtags++;
	putlelong(numtags); // number of tags per frame
	putlelong(numshaders); // number of meshes
	putlelong(1); // number of shader names per mesh (they are stacked things like quad shell I think)

	// these are filled in later
	pos_lumps = putgetposition();
	putlelong(0); // frameinfo
	putlelong(0); // tags
	putlelong(0); // first mesh
	putlelong(0); // end

	// store frameinfo
	pos_frameinfo = putgetposition();
	for (i = 0;i < numframes;i++)
	{
		putlefloat(frames[i].mins[0]);
		putlefloat(frames[i].mins[1]);
		putlefloat(frames[i].mins[2]);
		putlefloat(frames[i].maxs[0]);
		putlefloat(frames[i].maxs[1]);
		putlefloat(frames[i].maxs[2]);
		putlefloat((frames[i].mins[0] + frames[i].maxs[0]) * 0.5f);
		putlefloat((frames[i].mins[1] + frames[i].maxs[1]) * 0.5f);
		putlefloat((frames[i].mins[2] + frames[i].maxs[2]) * 0.5f);
		putlefloat(frames[i].allradius);
		putstring(frames[i].name, 64);
	}

	// store tags
	pos_tags = putgetposition();
	if (numtags)
	{
		for (i = 0;i < numframes;i++)
		{
			for (k = 0;k < numbones;k++)
			{
				if (bones[k].defined)
				{
					if (bones[k].parent >= 0)
						bonematrix[k] = concattransform(bonematrix[bones[k].parent], frames[i].bones[k]);
					else
						bonematrix[k] = frames[i].bones[k];
				}
			}
			for (j = 0;j < numbones;j++)
			{
				if (strncmp(bones[j].name, "TAG_", 4))
					continue;
				putstring(bones[j].name, 64);
				// output the origin and then 9 rotation floats
				// these are in a transposed order compared to our matrices,
				// so this indexing looks a little odd.
				// origin
				putlefloat(bonematrix[j].m[0][3]);
				putlefloat(bonematrix[j].m[1][3]);
				putlefloat(bonematrix[j].m[2][3]);
				// x axis
				putlefloat(bonematrix[j].m[0][0]);
				putlefloat(bonematrix[j].m[1][0]);
				putlefloat(bonematrix[j].m[2][0]);
				// y axis
				putlefloat(bonematrix[j].m[0][1]);
				putlefloat(bonematrix[j].m[1][1]);
				putlefloat(bonematrix[j].m[2][1]);
				// z axis
				putlefloat(bonematrix[j].m[0][2]);
				putlefloat(bonematrix[j].m[1][2]);
				putlefloat(bonematrix[j].m[2][2]);
			}
		}
	}

	// store the meshes
	pos_firstmesh = putgetposition();
	for (i = 0;i < numshaders;i++)
	{
		nverts = 0;
		for (j = 0;j < numverts;j++)
		{
			if (vertices[j].shadernum == i)
				vertremap[j] = nverts++;
			else
				vertremap[j] = -1;
		}
		ntris = 0;
		for (j = 0;j < numtriangles;j++)
			if (triangles[j].shadernum == i)
				ntris++;

		// write mesh header
		pos_meshstart = putgetposition();
		putstring("IDP3", 4); // identifier
		putstring(shaders[i], 64); // mesh name
		putlelong(0); // flags (what is this for?)
		putlelong(numframes); // number of frames
		putlelong(1); // how many shaders to apply to this mesh (quad shell and such?)
		putlelong(nverts);
		putlelong(ntris);
		// filled in later
		pos_meshlumps = putgetposition();
		putlelong(0); // elements
		putlelong(0); // shaders
		putlelong(0); // texcoords
		putlelong(0); // framevertices
		putlelong(0); // end
		// write names of shaders to use on this mesh (only one supported in this writer)
		pos_meshshaders = putgetposition();
		putstring(shaders[i], 64); // shader name
		putlelong(0); // shader number (used internally by Quake3 after load?)
		// write triangles
		pos_meshelements = putgetposition();
		for (j = 0;j < numtriangles;j++)
		{
			if (triangles[j].shadernum == i)
			{
				// swap the triangles because Quake3 uses clockwise triangles
				putlelong(vertremap[triangles[j].v[0]]);
				putlelong(vertremap[triangles[j].v[2]]);
				putlelong(vertremap[triangles[j].v[1]]);
			}
		}
		// write texcoords
		pos_meshtexcoords = putgetposition();
		for (j = 0;j < numverts;j++)
		{
			if (vertices[j].shadernum == i)
			{
				// OpenGL wants bottom to top texcoords
				putlefloat(vertices[j].texcoord[0]);
				putlefloat(1.0f - vertices[j].texcoord[1]);
			}
		}
		pos_meshframevertices = putgetposition();
		for (j = 0;j < numframes;j++)
		{
			for (k = 0;k < numbones;k++)
			{
				if (bones[k].defined)
				{
					if (bones[k].parent >= 0)
						bonematrix[k] = concattransform(bonematrix[bones[k].parent], frames[j].bones[k]);
					else
						bonematrix[k] = frames[j].bones[k];
				}
			}
			for (k = 0;k < numverts;k++)
			{
				if (vertices[k].shadernum == i)
				{
					double vertex[3], normal[3], v[3], longitude, latitude;
					vertex[0] = vertex[1] = vertex[2] = normal[0] = normal[1] = normal[2] = 0;
					for (l = 0;l < vertices[k].numinfluences;l++)
					{
						transform(vertices[k].influenceorigin[l], bonematrix[vertices[k].influencebone[l]], v);
						vertex[0] += v[0] * vertices[k].influenceweight[l];
						vertex[1] += v[1] * vertices[k].influenceweight[l];
						vertex[2] += v[2] * vertices[k].influenceweight[l];
						transformnormal(vertices[k].influencenormal[l], bonematrix[vertices[k].influencebone[l]], v);
						normal[0] += v[0] * vertices[k].influenceweight[l];
						normal[1] += v[1] * vertices[k].influenceweight[l];
						normal[2] += v[2] * vertices[k].influenceweight[l];
					}
					// write the vertex position in Quake3's 10.6 fixed point format
					for (l = 0;l < 3;l++)
					{
						double f = vertex[l] * 64.0;
						if (f < -32768.0)
							f = -32768.0;
						if (f >  32767.0)
							f =  32767.0;
						putleshort(f);
					}
					// write the surface normal as 8bit quantized lat/long
					// the latitude is a yaw angle, but the longitude is not
					// equivalent to pitch - it is biased 90 degrees
					// (0 = up, 90 = horizontal, 180 = down)
					// this is a common source of bugs in md3 exporters
					latitude = atan2(normal[1], normal[0]);
					longitude = acos(normal[2]);
#if 0
					double normal2[3];
					normal2[0] = cos(latitude) * sin(longitude);
					normal2[1] = sin(latitude) * sin(longitude);
					normal2[2] =                 cos(longitude);
					Con_Printf("%f %f %f : %f %f %f (%f %f = %f %f %f)\n", vertex[0], vertex[1], vertex[2], normal[0], normal[1], normal[2], longitude * 180.0/M_PI, latitude * 180.0/M_PI, normal2[0], normal2[1], normal2[2]);
#endif
					putbyte((int)(longitude * 128 / M_PI + 256.5) & 255);
					putbyte((int)(latitude * 128 / M_PI + 256.5) & 255);
				}
			}
		}

		// now we actually write the mesh lumps
		pos_meshend = putgetposition();
		restoreposition = putgetposition();
		putsetposition(pos_meshlumps);
		putlelong(pos_meshelements - pos_meshstart);
		putlelong(pos_meshshaders - pos_meshstart);
		putlelong(pos_meshtexcoords - pos_meshstart);
		putlelong(pos_meshframevertices - pos_meshstart);
		putlelong(pos_meshend - pos_meshstart);
		putsetposition(restoreposition);
	}

	pos_end = putgetposition();

	putsetposition(pos_lumps);
	putlelong(pos_frameinfo); // frameinfo
	putlelong(pos_tags); // tags
	putlelong(pos_firstmesh); // first mesh
	putlelong(pos_end); // end
	putsetposition(pos_end);

	filesize = pos_end;

	sprintf(filename, "%s%s.md3", outputdir_name, model_name);
	FS_WriteFile(filename, outputbuffer, filesize);
	Con_Printf("wrote file %s (size %5ik)\n", filename, (filesize + 1023) >> 10);

	return 1;
}

