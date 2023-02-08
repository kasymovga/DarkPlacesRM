
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
#define EPSILON_VERTEX 0.001
#define EPSILON_NORMAL 1
#define EPSILON_TEXCOORD 0.001
#else
#define EPSILON_VERTEX 0
#define EPSILON_NORMAL 0
#define EPSILON_TEXCOORD 0
#endif

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

typedef struct ctx_s {
	const char *scriptbytes;
	float scene_fps;
	int scene_looping;
	double scene_suborigin[3];
	double scene_subrotate;
	mempool_t *mempool;
	const char *workdir_name;
	const char *model_path;
	char model_name[MAX_FILEPATH];
	char scene_name[MAX_FILEPATH];
	char model_name_uppercase[MAX_FILEPATH];
	char scene_name_uppercase[MAX_FILEPATH];
	char model_name_lowercase[MAX_FILEPATH];
	char scene_name_lowercase[MAX_FILEPATH];
	unsigned char outputbuffer[MAX_FILESIZE];
	unsigned char *output;
	unsigned int output_overflow;
	qfile_t *headerfile;
	qfile_t *qcheaderfile;
	qfile_t *framegroupsfile_dpm;
	qfile_t *framegroupsfile_md3;
	double modelorigin[3], modelrotate, modelscale;
	char shaders[MAX_SHADERS][32];
	int numshaders;
	int numattachments;
	attachment attachments[MAX_ATTACHMENTS];
	int numframes;
	frame_t frames[MAX_FRAMES];
	int numbones;
	bone_t bones[MAX_BONES]; // master bone list
	int numtriangles;
	triangle triangles[MAX_TRIS];
	int numverts;
	tripoint vertices[MAX_VERTS];
	//these are used while processing things
	bonepose_t bonematrix[MAX_BONES];
	char *modelfile;
	int vertremap[MAX_VERTS];
	const char *token;
	char tokenbuffer[1024];

} ctx_t;

ctx_t *ctx;

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

static int parsenodes(int *bones_map)
{
	int i, num, parent, num_exists;
	char line[1024], name[1024];
	char com_token[MAX_INPUTLINE];

	while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)))
	{
		num_exists = -1;
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
		if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] < ' ')
		{
			Con_Printf("error in nodes, expecting bone name in line:%s\n", line);
			return 0;
		}
		cleancopyname(name, com_token, MAX_NAME);//Con_Printf( "bone name: %s\n", name );

		//get parent number
		if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
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
		if (parent >= 0 && !ctx->bones[parent].defined)
		{
			Con_Printf("bone's parent bone has not been defined\n");
			return 0;
		}
		for (i = 0; i < ctx->numbones; i++)
		{
			if (!strcmp(ctx->bones[i].name, name))
			{
				num_exists = i;
				break;
			}
		}
		if (num_exists < 0) num_exists = ctx->numbones;
		if (num_exists < MAX_BONES)
		{
			bones_map[num] = num_exists;
			memcpy(ctx->bones[num_exists].name, name, MAX_NAME);
			ctx->bones[num_exists].defined = 1;
			ctx->bones[num_exists].parent = (parent >= 0 ? bones_map[parent] : parent);
			if (num_exists >= ctx->numbones)
				ctx->numbones = num_exists + 1;
		}
		// skip any trailing parameters (might be a later version of smd)
		while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
	}
	// skip any trailing parameters (might be a later version of smd)
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
	return 1;
}

static int parseskeleton(int *bones_map)
{
	char line[1024], temp[2048];
	int i, frame, num;
	double x, y, z, a, b, c;
	int baseframe;
	char com_token[MAX_INPUTLINE];

	baseframe = ctx->numframes;
	frame = baseframe;

	while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)))
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
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
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
			if (ctx->frames[frame].defined)
			{
				Con_Printf("warning: duplicate frame\n");
			}
			sprintf(temp, "%s_%i", ctx->scene_name, i);
			if (strlen(temp) > 31)
			{
				sprintf(temp, "frame%i", i);
			}
			cleancopyname(ctx->frames[frame].name, temp, MAX_NAME);

			ctx->frames[frame].numbones = ctx->numbones;
			ctx->frames[frame].bones = Mem_Alloc(ctx->mempool, MAX_BONES * sizeof(bonepose_t));
			memset(ctx->frames[frame].bones, 0, MAX_BONES * sizeof(bonepose_t));
			ctx->frames[frame].bones[ctx->frames[frame].numbones - 1].m[0][1] = 1;
			ctx->frames[frame].defined = 1;
			if (ctx->numframes < frame + 1)
				ctx->numframes = frame + 1;
		}
		else
		{
			//the token was bone number
			num = atoi(com_token);
			//get x, y, z tokens
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'x' value in line:%s\n", line);
				return 0;
			}
			x = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'y' value in line:%s\n", line);
				return 0;
			}
			y = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'z' value in line:%s\n", line);
				return 0;
			}
			z = atof( com_token );

			//get a, b, c tokens
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'a' value in line:%s\n", line);
				return 0;
			}
			a = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'b' value in line:%s\n", line);
				return 0;
			}
			b = atof( com_token );

			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parseskeleton, expecting 'c' value in line:%s\n", line);
				return 0;
			}
			c = atof( com_token );

			if (num < 0 || num >= ctx->numbones)
			{
				Con_Printf("error: invalid bone number: %i\n", num);
				return 0;
			}
			num = bones_map[num];
			if (num < 0 || num >= ctx->numbones)
			{
				Con_Printf("error: invalid bone number: %i\n", num);
				return 0;
			}
			if (!ctx->bones[num].defined)
			{
				Con_Printf("error: bone %i not defined\n", num);
				return 0;
			}
			if (ctx->bones[num].parent < 0)
			{
				x += ctx->scene_suborigin[0] / ctx->modelscale;
				y += ctx->scene_suborigin[1] / ctx->modelscale;
				z -= ctx->scene_suborigin[2] / ctx->modelscale;
				c += (ctx->scene_subrotate / 180) * M_PI;
			}
			// LordHavoc: compute matrix
			ctx->frames[frame].bones[num] = computebonematrix(x * ctx->modelscale, y * ctx->modelscale, z * ctx->modelscale, a, b, c);
		}
		// skip any trailing parameters (might be a later version of smd)
		while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
	}
	// skip any trailing parameters (might be a later version of smd)
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');

	if (frame >= baseframe && ctx->qcheaderfile)
		FS_Printf(ctx->qcheaderfile, "$frame");
	for (frame = 0;frame < ctx->numframes;frame++)
	{
		if (!ctx->frames[frame].defined)
		{
			if (frame < 1)
			{
				Con_Printf("error: no first frame\n");
				return 0;
			}
			if (!ctx->frames[frame - 1].defined)
			{
				Con_Printf("error: no previous frame to duplicate\n");
				return 0;
			}
			sprintf(temp, "%s_%i", ctx->scene_name, frame - baseframe);
			if (strlen(temp) > 31)
			{
				sprintf(temp, "frame%i", frame - baseframe);
			}
			Con_Printf("frame %s missing, duplicating previous frame %s with new name %s\n", temp, ctx->frames[frame - 1].name, temp);
			ctx->frames[frame].defined = 1;
			cleancopyname(ctx->frames[frame].name, temp, MAX_NAME);
			ctx->frames[frame].numbones = ctx->numbones;
			ctx->frames[frame].bones = Mem_Alloc(ctx->mempool, MAX_BONES * sizeof(bonepose_t));
			memcpy(ctx->frames[frame].bones, ctx->frames[frame - 1].bones, ctx->frames[frame].numbones * sizeof(bonepose_t));
			ctx->frames[frame].bones[ctx->frames[frame].numbones - 1].m[0][1] = 1;
			Con_Printf("duplicate frame named %s\n", ctx->frames[frame].name);
		}
		if (frame >= baseframe && ctx->headerfile)
			FS_Printf(ctx->headerfile, "#define MODEL_%s_%s_%i %i\n", ctx->model_name_uppercase, ctx->scene_name_uppercase, frame - baseframe, frame);
		if (frame >= baseframe && ctx->qcheaderfile)
			FS_Printf(ctx->qcheaderfile, " %s_%i", ctx->scene_name_lowercase, frame - baseframe + 1);
	}
	if (ctx->headerfile)
	{
		FS_Printf(ctx->headerfile, "#define MODEL_%s_%s_START %i\n", ctx->model_name_uppercase, ctx->scene_name_uppercase, baseframe);
		FS_Printf(ctx->headerfile, "#define MODEL_%s_%s_END %i\n", ctx->model_name_uppercase, ctx->scene_name_uppercase, ctx->numframes);
		FS_Printf(ctx->headerfile, "#define MODEL_%s_%s_LENGTH %i\n", ctx->model_name_uppercase, ctx->scene_name_uppercase, ctx->numframes - baseframe);
		FS_Printf(ctx->headerfile, "\n");
	}
	if (baseframe > 0)
	{
		if (!ctx->framegroupsfile_dpm)
		{
			dpsnprintf(temp, sizeof(temp), "%s.dpm.framegroups", ctx->model_path);
			ctx->framegroupsfile_dpm = FS_OpenRealFile(temp, "w", false);
		}
		if (!ctx->framegroupsfile_md3)
		{
			dpsnprintf(temp, sizeof(temp), "%s.md3.framegroups", ctx->model_path);
			ctx->framegroupsfile_md3 = FS_OpenRealFile(temp, "w", false);
		}
		if (ctx->framegroupsfile_md3)
			FS_Printf(ctx->framegroupsfile_md3, "%i %i %.6f %i // %s\n", baseframe, ctx->numframes - baseframe, ctx->scene_fps, ctx->scene_looping, ctx->scene_name_lowercase);
		if (ctx->framegroupsfile_dpm)
			FS_Printf(ctx->framegroupsfile_dpm, "%i %i %.6f %i // %s\n", baseframe, ctx->numframes - baseframe, ctx->scene_fps, ctx->scene_looping, ctx->scene_name_lowercase);
	}
	if (ctx->qcheaderfile)
		FS_Printf(ctx->qcheaderfile, "\n");
	return 1;
}

/*
int sentinelcheckframes(char *filename, int fileline)
{
	int i;
	for (i = 0;i < ctx->numframes;i++)
	{
		if (ctx->frames[i].defined && ctx->frames[i].bones)
		{
			if (ctx->frames[i].bones[ctx->frames[i].numbones - 1].m[0][1] != 1)
			{
				Con_Printf("sentinelcheckframes: error on frame %s detected at %s:%i\n", ctx->frames[i].name, filename, fileline);
				exit(1);
			}
		}
	}
	return 1;
}
*/

static int initframes(void)
{
	memset(ctx->frames, 0, sizeof(frame_t) * MAX_FRAMES);
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
	char com_token[MAX_INPUTLINE];

	ctx->numtriangles = 0;
	ctx->numshaders = 0;
	ctx->numverts = 0;

	for (i = 0;i < ctx->numbones;i++)
	{
		if (ctx->bones[i].parent >= 0)
			ctx->bonematrix[i] = concattransform(ctx->bonematrix[ctx->bones[i].parent], ctx->frames[0].bones[i]);
		else
			ctx->bonematrix[i] = ctx->frames[0].bones[i];
	}
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)))
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
		for (i = 0;i < ctx->numshaders;i++)
		{
			if (!strcmp(ctx->shaders[i], cleanline))
			{
				found = 1;
				break;
			}
		}
		ctx->triangles[ctx->numtriangles].shadernum = i;
		if (!found)
		{
			if (i == MAX_SHADERS)
			{
				Con_Printf("MAX_SHADERS reached\n");
				return 0;
			}
			cleancopyname(ctx->shaders[i], cleanline, MAX_NAME);
			ctx->numshaders++;
		}
		if (com_token[0] != '\n')
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
		}
		for (corner = 0;corner < 3;corner++)
		{
			//parse this line read by tokens
			org[0] = 0;org[1] = 0;org[2] = 0;
			normal[0] = 0;normal[1] = 0;normal[2] = 0;
			vtexcoord[0] = 0;vtexcoord[1] = 0;

			//get bonenum token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'bonenum', script line:%s\n", line);
				return 0;
			}
			vbonenum = atoi( com_token );

			//get org[0] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'org[0]', script line:%s\n", line);
				return 0;
			}
			org[0] = atof( com_token ) * ctx->modelscale;

			//get org[1] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'org[1]', script line:%s\n", line);
				return 0;
			}
			org[1] = atof( com_token ) * ctx->modelscale;

			//get org[2] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'org[2]', script line:%s\n", line);
				return 0;
			}
			org[2] = atof( com_token ) * ctx->modelscale;

			//get normal[0] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'normal[0]', script line:%s\n", line);
				return 0;
			}
			normal[0] = atof( com_token );

			//get normal[1] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'normal[1]', script line:%s\n", line);
				return 0;
			}
			normal[1] = atof( com_token );

			//get normal[2] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'normal[2]', script line:%s\n", line);
				return 0;
			}
			normal[2] = atof( com_token );

			//get vtexcoord[0] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'vtexcoord[0]', script line:%s\n", line);
				return 0;
			}
			vtexcoord[0] = atof( com_token );

			//get vtexcoord[1] token
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
			{
				Con_Printf("error in parsetriangles, expecting 'vtexcoord[1]', script line:%s\n", line);
				return 0;
			}
			vtexcoord[1] = atof( com_token );

			// are there more words (HalfLife2) or not (HalfLife1)?
			if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
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
					if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
					{
						Con_Printf("invalid vertex influence \"%s\"\n", line);
						return 0;
					}
					temp_numbone[c] = atoi(com_token);
					if(temp_numbone[c] < 0 || temp_numbone[c] >= ctx->numbones )
					{
						Con_Printf("invalid vertex influence (invalid bone number) \"%s\"\n", line);
						return 0;
					}
					//get influence weight
					if (!COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) || com_token[0] <= ' ')
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
				if (!ctx->bones[temp_numbone[i]].defined)
				{
					Con_Printf("bone %i in triangle data is not defined\n", temp_numbone[i]);
					return 0;
				}
			}

			// add vertex to list if unique
			for (i = 0;i < ctx->numverts;i++)
			{
				if (ctx->vertices[i].shadernum != ctx->triangles[ctx->numtriangles].shadernum
					|| VectorDistance(ctx->vertices[i].originalorigin, org) > EPSILON_VERTEX
					|| VectorDistance(ctx->vertices[i].originalnormal, normal) > EPSILON_NORMAL
					|| VectorDistance2D(ctx->vertices[i].texcoord, vtexcoord) > EPSILON_TEXCOORD)
					continue;
				for (j = 0;j < numinfluences;j++)
					if (ctx->vertices[i].influencebone[j] != temp_numbone[j] || ctx->vertices[i].influenceweight[j] != temp_influence[j])
						break;
				if (j == numinfluences)
					break;
			}
			ctx->triangles[ctx->numtriangles].v[corner] = i;

			if (i >= ctx->numverts)
			{
				ctx->numverts++;
				if (ctx->numverts >= MAX_VERTS)
				{
					Con_Printf("numverts is too big\n");
					return 0;
				}
				ctx->vertices[i].shadernum = ctx->triangles[ctx->numtriangles].shadernum;
				ctx->vertices[i].texcoord[0] = vtexcoord[0];
				ctx->vertices[i].texcoord[1] = vtexcoord[1];
				ctx->vertices[i].originalorigin[0] = org[0];ctx->vertices[i].originalorigin[1] = org[1];ctx->vertices[i].originalorigin[2] = org[2];
				ctx->vertices[i].originalnormal[0] = normal[0];ctx->vertices[i].originalnormal[1] = normal[1];ctx->vertices[i].originalnormal[2] = normal[2];
				ctx->vertices[i].numinfluences = numinfluences;
				for( j=0; j < ctx->vertices[i].numinfluences; j++ )
				{
					// untransform the origin and normal
					inversetransform(org, ctx->bonematrix[temp_numbone[j]], ctx->vertices[i].influenceorigin[j]);
					inverserotate(normal, ctx->bonematrix[temp_numbone[j]], ctx->vertices[i].influencenormal[j]);

					d = 1 / sqrt(ctx->vertices[i].influencenormal[j][0] * ctx->vertices[i].influencenormal[j][0] + ctx->vertices[i].influencenormal[j][1] * ctx->vertices[i].influencenormal[j][1] + ctx->vertices[i].influencenormal[j][2] * ctx->vertices[i].influencenormal[j][2]);
					ctx->vertices[i].influencenormal[j][0] *= d;
					ctx->vertices[i].influencenormal[j][1] *= d;
					ctx->vertices[i].influencenormal[j][2] *= d;

					// round off minor errors in the normal
					if (fabs(ctx->vertices[i].influencenormal[j][0]) < 0.001)
						ctx->vertices[i].influencenormal[j][0] = 0;
					if (fabs(ctx->vertices[i].influencenormal[j][1]) < 0.001)
						ctx->vertices[i].influencenormal[j][1] = 0;
					if (fabs(ctx->vertices[i].influencenormal[j][2]) < 0.001)
						ctx->vertices[i].influencenormal[j][2] = 0;

					d = 1 / sqrt(ctx->vertices[i].influencenormal[j][0] * ctx->vertices[i].influencenormal[j][0] + ctx->vertices[i].influencenormal[j][1] * ctx->vertices[i].influencenormal[j][1] + ctx->vertices[i].influencenormal[j][2] * ctx->vertices[i].influencenormal[j][2]);
					ctx->vertices[i].influencenormal[j][0] *= d;
					ctx->vertices[i].influencenormal[j][1] *= d;
					ctx->vertices[i].influencenormal[j][2] *= d;
					ctx->vertices[i].influencebone[j] = temp_numbone[j];
					ctx->vertices[i].influenceweight[j] = temp_influence[j];
				}
			}
			// skip any trailing parameters (might be a later version of smd)
			while (com_token[0] != '\n' && COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)));
		}
		ctx->numtriangles++;
		if (ctx->numtriangles >= MAX_TRIS)
		{
			Con_Printf("numtriangles is too big\n");
			return 0;
		}
	}
	// skip any trailing parameters (might be a later version of smd)
	while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');

	Con_Printf("parsetriangles: done\n");
	return 1;
}

static int parsemodelfile(void)
{
	char com_token[MAX_INPUTLINE];
	int bones_map[MAX_BONES];
	memset(bones_map, 0, sizeof(bones_map));
	tokenpos = ctx->modelfile;
	while (COM_ParseToken_VM_Tokenize(&tokenpos, false, com_token, sizeof(com_token)))
	{
		if (!strcmp(com_token, "version"))
		{
			COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token));
			if (atoi(com_token) != 1)
			{
				Con_Printf("file is version %s, only version 1 is supported\n", com_token);
				return 0;
			}
		}
		else if (!strcmp(com_token, "nodes"))
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
			if (!parsenodes(bones_map))
				return 0;
		}
		else if (!strcmp(com_token, "skeleton"))
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
			if (!parseskeleton(bones_map))
				return 0;
		}
		else if (!strcmp(com_token, "triangles"))
		{
			// skip any trailing parameters (might be a later version of smd)
			while (COM_ParseToken_VM_Tokenize(&tokenpos, true, com_token, sizeof(com_token)) && com_token[0] != '\n');
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
	for (i = 0;i < ctx->numattachments;i++)
	{
		if (ctx->numbones >= MAX_BONES)
		{
			Con_Printf("addattachments: too much bones\n");
			return 0;
		}
		ctx->bones[ctx->numbones].defined = 1;
		ctx->bones[ctx->numbones].parent = -1;
		ctx->bones[ctx->numbones].flags = DPMBONEFLAG_ATTACH;
		for (j = 0;j < ctx->numbones;j++)
			if (!strcmp(ctx->bones[j].name, ctx->attachments[i].parentname))
				ctx->bones[ctx->numbones].parent = j;
		if (ctx->bones[ctx->numbones].parent < 0)
			Con_Printf("warning: unable to find bone \"%s\" for attachment \"%s\", using root instead\n", ctx->attachments[i].parentname, ctx->attachments[i].name);
		cleancopyname(ctx->bones[ctx->numbones].name, ctx->attachments[i].name, MAX_NAME);
		// we have to duplicate the attachment in every frame
		//sentinelcheckframes(__FILE__, __LINE__);
		for (j = 0;j < ctx->numframes;j++)
		{
			ctx->frames[j].bones[ctx->numbones] = ctx->attachments[i].matrix;
			ctx->frames[j].numbones = ctx->numbones + 1;
		}
		//sentinelcheckframes(__FILE__, __LINE__);
		ctx->numbones++;
	}
	ctx->numattachments = 0;
	//sentinelcheckframes(__FILE__, __LINE__);
	return 1;
}

static int cleanupframes(void)
{
	int i, j/*, best*/, k;
	double org[3], dist, mins[3], maxs[3], yawradius, allradius;
	for (i = 0;i < ctx->numframes;i++)
	{
		//sentinelcheckframes(__FILE__, __LINE__);
		for (j = 0;j < ctx->numbones;j++)
		{
			if (ctx->bones[j].defined)
			{
				if (ctx->bones[j].parent >= 0)
					ctx->bonematrix[j] = concattransform(ctx->bonematrix[ctx->bones[j].parent], ctx->frames[i].bones[j]);
				else
					ctx->bonematrix[j] = ctx->frames[i].bones[j];
			}
		}
		mins[0] = mins[1] = mins[2] = 0;
		maxs[0] = maxs[1] = maxs[2] = 0;
		yawradius = 0;
		allradius = 0;
		//best = 0;
		for (j = 0;j < ctx->numverts;j++)
		{
			for (k = 0;k < ctx->vertices[i].numinfluences;k++)
			{
				transform(ctx->vertices[j].influenceorigin[k], ctx->bonematrix[ctx->vertices[j].influencebone[k]], org);

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
		transform(ctx->vertices[j].origin, ctx->bonematrix[ctx->vertices[j].bonenum], org);
		Con_Printf("furthest vert of frame %s is on bone %s (%i), matrix is:\n%f %f %f %f\n%f %f %f %f\n%f %f %f %f\nvertex origin %f %f %f - %f %f %f\nbbox %f %f %f - %f %f %f - %f %f\n", ctx->frames[i].name, ctx->bones[ctx->vertices[j].bonenum].name, ctx->vertices[j].bonenum
		, ctx->bonematrix[ctx->vertices[j].bonenum].m[0][0], ctx->bonematrix[ctx->vertices[j].bonenum].m[0][1], ctx->bonematrix[ctx->vertices[j].bonenum].m[0][2], ctx->bonematrix[ctx->vertices[j].bonenum].m[0][3]
		, ctx->bonematrix[ctx->vertices[j].bonenum].m[1][0], ctx->bonematrix[ctx->vertices[j].bonenum].m[1][1], ctx->bonematrix[ctx->vertices[j].bonenum].m[1][2], ctx->bonematrix[ctx->vertices[j].bonenum].m[1][3]
		, ctx->bonematrix[ctx->vertices[j].bonenum].m[2][0], ctx->bonematrix[ctx->vertices[j].bonenum].m[2][1], ctx->bonematrix[ctx->vertices[j].bonenum].m[2][2], ctx->bonematrix[ctx->vertices[j].bonenum].m[2][3]
		, vertices[j].origin[0], vertices[j].origin[1], vertices[j].origin[2], org[0], org[1], org[2]
		, mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2], sqrt(yawradius), sqrt(allradius));
		*/
		ctx->frames[i].mins[0] = mins[0];
		ctx->frames[i].mins[1] = mins[1];
		ctx->frames[i].mins[2] = mins[2];
		ctx->frames[i].maxs[0] = maxs[0];
		ctx->frames[i].maxs[1] = maxs[1];
		ctx->frames[i].maxs[2] = maxs[2];
		ctx->frames[i].yawradius = sqrt(yawradius);
		ctx->frames[i].allradius = sqrt(allradius);
		//sentinelcheckframes(__FILE__, __LINE__);
	}
	return 1;
}

static int cleanupshadernames(void)
{
	int i;
	char temp[1024+MAX_NAME];
	for (i = 0;i < ctx->numshaders;i++)
	{
		chopextension(ctx->shaders[i]);
		sprintf(temp, "%s", ctx->shaders[i]);
		if (strlen(temp) >= MAX_NAME)
			Con_Printf("warning: shader name too long %s\n", temp);
		cleancopyname(ctx->shaders[i], temp, MAX_NAME);
	}
	return 1;
}

static void fixrootbones(void)
{
	int i, j;
	float cy, sy;
	bonepose_t rootpose;
	cy = cos(ctx->modelrotate * M_PI / 180.0);
	sy = sin(ctx->modelrotate * M_PI / 180.0);
	rootpose.m[0][0] = cy;
	rootpose.m[1][0] = sy;
	rootpose.m[2][0] = 0;
	rootpose.m[0][1] = -sy;
	rootpose.m[1][1] = cy;
	rootpose.m[2][1] = 0;
	rootpose.m[0][2] = 0;
	rootpose.m[1][2] = 0;
	rootpose.m[2][2] = 1;
	rootpose.m[0][3] = -ctx->modelorigin[0] * rootpose.m[0][0] + -ctx->modelorigin[1] * rootpose.m[1][0] + -ctx->modelorigin[2] * rootpose.m[2][0];
	rootpose.m[1][3] = -ctx->modelorigin[0] * rootpose.m[0][1] + -ctx->modelorigin[1] * rootpose.m[1][1] + -ctx->modelorigin[2] * rootpose.m[2][1];
	rootpose.m[2][3] = -ctx->modelorigin[0] * rootpose.m[0][2] + -ctx->modelorigin[1] * rootpose.m[1][2] + -ctx->modelorigin[2] * rootpose.m[2][2];
	for (j = 0;j < ctx->numbones;j++)
	{
		if (ctx->bones[j].parent < 0)
		{
			// a root bone
			for (i = 0;i < ctx->numframes;i++)
				ctx->frames[i].bones[j] = concattransform(rootpose, ctx->frames[i].bones[j]);
		}
	}
}

static void inittokens(const char *script)
{
	ctx->token = script;
}

static char *gettoken(void)
{
	char *out;
	out = ctx->tokenbuffer;
	while (*ctx->token && *ctx->token <= ' ' && *ctx->token != '\n')
		ctx->token++;
	if (!*ctx->token)
		return NULL;
	switch (*ctx->token)
	{
	case '\"':
		ctx->token++;
		while (*ctx->token && *ctx->token != '\r' && *ctx->token != '\n' && *ctx->token != '\"')
			*out++ = *ctx->token++;
		*out++ = 0;
		if (*ctx->token == '\"')
			ctx->token++;
		else
			Con_Printf("warning: unterminated quoted string\n");
		return ctx->tokenbuffer;
	case '(':
	case ')':
	case '{':
	case '}':
	case '[':
	case ']':
	case '\n':
		ctx->tokenbuffer[0] = *ctx->token++;
		ctx->tokenbuffer[1] = 0;
		return ctx->tokenbuffer;
	default:
		while (*ctx->token && *ctx->token > ' ' && *ctx->token != '(' && *ctx->token != ')' && *ctx->token != '{' && *ctx->token != '}' && *ctx->token != '[' && *ctx->token != ']' && *ctx->token != '\"')
			*out++ = *ctx->token++;
		*out++ = 0;
		return ctx->tokenbuffer;
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
	if (ctx->numattachments >= MAX_ATTACHMENTS)
	{
		Con_Printf("ran out of attachment slots\n");
		return 0;
	}
	c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	cleancopyname(ctx->attachments[ctx->numattachments].name, c, MAX_NAME);

	c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	cleancopyname(ctx->attachments[ctx->numattachments].parentname, c, MAX_NAME);

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
	ctx->attachments[ctx->numattachments].matrix = computebonematrix(origin[0], origin[1], origin[2], angles[0], angles[1], angles[2]);

	ctx->numattachments++;
	return 1;
}

static int sc_model(void)
{
	char *c = gettoken();
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	strlcpy(ctx->model_name, c, MAX_FILEPATH);
	chopextension(ctx->model_name);
	stringtouppercase(ctx->model_name, ctx->model_name_uppercase);
	stringtolowercase(ctx->model_name, ctx->model_name_lowercase);

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
		ctx->modelorigin[i] = atof(c);
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
	ctx->modelrotate = atof(c);
	return 1;
}

static int sc_scale(void)
{
	char *c = gettoken();
	if (!c)
		return 0;
	if (!isdouble(c))
		return 0;
	ctx->modelscale = atof(c);
	return 1;
}

static int sc_scene(void)
{
	char *c;
	char filename[MAX_FILEPATH * 3];
	c = gettoken();
	ctx->scene_suborigin[0] = 0;
	ctx->scene_suborigin[1] = 0;
	ctx->scene_suborigin[2] = 0;
	ctx->scene_subrotate = 0;
	if (!c)
		return 0;
	if (!isfilename(c))
		return 0;
	dpsnprintf(filename, sizeof(filename), "%s%s", ctx->workdir_name, c);
	ctx->modelfile = (char*)FS_LoadFile(filename, ctx->mempool, 0, NULL);
	if (!ctx->modelfile)
	{
		Con_Printf("sc_scene: %s load failed\n", filename);
		return 0;
	}
	cleancopyname(ctx->scene_name, c, MAX_NAME);
	chopextension(ctx->scene_name);
	stringtouppercase(ctx->scene_name, ctx->scene_name_uppercase);
	stringtolowercase(ctx->scene_name, ctx->scene_name_lowercase);
	Con_Printf("parsing scene %s\n", ctx->scene_name);
	if (!ctx->headerfile)
	{
		sprintf(filename, "%s.h", ctx->model_path);
		ctx->headerfile = FS_OpenRealFile(filename, "w", false);
		if (ctx->headerfile)
		{
			FS_Printf(ctx->headerfile, "/*\n");
			FS_Printf(ctx->headerfile, "Generated header file for %s\n", ctx->model_name);
			FS_Printf(ctx->headerfile, "This file contains frame number definitions for use in code referencing the model, to make code more readable and maintainable.\n");
			FS_Printf(ctx->headerfile, "*/\n");
			FS_Printf(ctx->headerfile, "\n");
			FS_Printf(ctx->headerfile, "#ifndef MODEL_%s_H\n", ctx->model_name_uppercase);
			FS_Printf(ctx->headerfile, "#define MODEL_%s_H\n", ctx->model_name_uppercase);
			FS_Printf(ctx->headerfile, "\n");
		}
	}
	if (!ctx->qcheaderfile)
	{
		sprintf(filename, "%s.qc", ctx->model_path);
		ctx->qcheaderfile = FS_OpenRealFile(filename, "w", false);
		if (ctx->qcheaderfile)
		{
			FS_Printf(ctx->qcheaderfile, "/*\n");
			FS_Printf(ctx->qcheaderfile, "Generated header file for %s\n", ctx->model_name);
			FS_Printf(ctx->qcheaderfile, "This file contains frame number definitions for use in code referencing the model, simply copy and paste into your qc file.\n");
			FS_Printf(ctx->qcheaderfile, "*/\n");
			FS_Printf(ctx->qcheaderfile, "\n");
		}
	}
	ctx->scene_fps = 1;
	ctx->scene_looping = 1;
	while ((c = gettoken())[0] != '\n')
	{
		if (!strcmp(c, "fps"))
		{
			c = gettoken();
			if (c[0] == '\n') break;
			ctx->scene_fps = atof(c);
		}
		if (!strcmp(c, "noloop"))
			ctx->scene_looping = 0;
		if (!strcmp(c, "suborigin"))
		{
			c = gettoken();
			if (c[0] == '\n') break;
			ctx->scene_suborigin[0] = atof(c);
			c = gettoken();
			if (c[0] == '\n') break;
			ctx->scene_suborigin[1] = atof(c);
			c = gettoken();
			if (c[0] == '\n') break;
			ctx->scene_suborigin[2] = atof(c);
		}
		if (!strcmp(c, "subrotate"))
		{
			c = gettoken();
			if (c[0] == '\n') break;
			ctx->scene_subrotate = atof(c);
		}
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
	{"outputdir", sc_comment},
	{"model", sc_model},
	{"texturedir", sc_comment},
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
	inittokens(ctx->scriptbytes);
	ctx->numframes = 0;
	ctx->numbones = 0;
	ctx->numshaders = 0;
	ctx->numtriangles = 0;
	initframes();
	while ((c = gettoken()))
		if (c[0] > ' ')
			if (!processcommand(c))
				return;
	if (!addattachments())
	{
		return;
	}
	if (!cleanupframes())
	{
		return;
	}
	if (!cleanupshadernames())
	{
		return;
	}
	fixrootbones();
	// print model stats
	Con_Printf("model stats:\n");
	Con_Printf("%i vertices %i triangles %i bones %i shaders %i frames\n", ctx->numverts, ctx->numtriangles, ctx->numbones, ctx->numshaders, ctx->numframes);
	Con_Printf("meshes:\n");
	for (i = 0;i < ctx->numshaders;i++)
	{
		int j;
		int nverts, ntris;
		nverts = 0;
		for (j = 0;j < ctx->numverts;j++)
			if (ctx->vertices[j].shadernum == i)
				nverts++;
		ntris = 0;
		for (j = 0;j < ctx->numtriangles;j++)
			if (ctx->triangles[j].shadernum == i)
				ntris++;
		Con_Printf("%5i tris%6i verts : %s\n", ntris, nverts, ctx->shaders[i]);
	}
	// write the model formats
	writemodel_dpm();
	writemodel_md3();
}

int Mod_Compile_DPM_MD3(const char *script, const char *workdir, const char *outpath)
{
	mempool_t *m;
	m = Mem_AllocPool("model_compile", 0, NULL);
	ctx = Mem_Alloc(m, sizeof(ctx_t));
	memset(ctx, 0, sizeof(*ctx));
	ctx->output = ctx->outputbuffer;
	ctx->mempool = m;
	ctx->modelscale = 1;
	ctx->scriptbytes = script;
	ctx->workdir_name = workdir;
	ctx->model_path = outpath;
	processscript();
	if (ctx->headerfile)
	{
		FS_Printf(ctx->headerfile, "#endif /*MODEL_%s_H*/\n", ctx->model_name_uppercase);
		FS_Close(ctx->headerfile);
		ctx->headerfile = NULL;
	}
	if (ctx->qcheaderfile)
	{
		FS_Printf(ctx->qcheaderfile, "\n// end of frame definitions for %s\n\n\n", ctx->model_name);
		FS_Close(ctx->qcheaderfile);
		ctx->qcheaderfile = NULL;
	}
	if (ctx->framegroupsfile_md3)
	{
		FS_Close(ctx->framegroupsfile_md3);
		ctx->framegroupsfile_md3 = NULL;
	}
	if (ctx->framegroupsfile_dpm)
	{
		FS_Close(ctx->framegroupsfile_dpm);
		ctx->framegroupsfile_dpm = NULL;
	}
#if (_MSC_VER && _DEBUG)
	Con_Printf("destroy any key\n");
	getchar();
#endif
	Mem_FreePool(&m);
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

int shaderusage[MAX_SHADERS];
int shadertrisstart[MAX_SHADERS];
int shadertriscurrent[MAX_SHADERS];
int shadertris[MAX_TRIS];
*/

static void putstring(char *in, int length)
{
	if (ctx->output + length >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += length;
		return;
	}
	while (*in && length)
	{
		*(ctx->output++) = *in++;
		length--;
	}
	// pad with nulls
	while (length--)
		*(ctx->output++) = 0;
}

/*
static void putnulls(int num)
{
	if (ctx->output + num >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += num;
		return;
	}
	while (num--)
		*ctx->output++ = 0;
}
*/

static void putbyte(int num)
{
	if (ctx->output + 1 >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += 1;
		return;
	}
	*ctx->output++ = num;
}

/*
static void putbeshort(int num)
{
	if (ctx->output + 2 >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += 2;
		return;
	}
	*ctx->output++ = ((num >>  8) & 0xFF);
	*ctx->output++ = ((num >>  0) & 0xFF);
}
*/

static void putbelong(int num)
{
	if (ctx->output + 4 >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += 4;
		return;
	}
	*ctx->output++ = ((num >> 24) & 0xFF);
	*ctx->output++ = ((num >> 16) & 0xFF);
	*ctx->output++ = ((num >>  8) & 0xFF);
	*ctx->output++ = ((num >>  0) & 0xFF);
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
	if (ctx->output + 2 >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += 2;
		return;
	}
	*ctx->output++ = ((num >>  0) & 0xFF);
	*ctx->output++ = ((num >>  8) & 0xFF);
}

static void putlelong(int num)
{
	if (ctx->output + 4 >= ctx->outputbuffer + MAX_FILESIZE)
	{
		ctx->output_overflow = 1;
		ctx->output += 4;
		return;
	}
	*ctx->output++ = ((num >>  0) & 0xFF);
	*ctx->output++ = ((num >>  8) & 0xFF);
	*ctx->output++ = ((num >> 16) & 0xFF);
	*ctx->output++ = ((num >> 24) & 0xFF);
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
	ctx->output = ctx->outputbuffer;
}
*/

static int putgetposition(void)
{
	return (int) ((unsigned char *) ctx->output - (unsigned char *) ctx->outputbuffer);
}

static void putsetposition(int n)
{
	ctx->output = (unsigned char *) ctx->outputbuffer + n;
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
	mins[0] = ctx->frames[0].mins[0];
	mins[1] = ctx->frames[0].mins[1];
	mins[2] = ctx->frames[0].mins[2];
	maxs[0] = ctx->frames[0].maxs[0];
	maxs[1] = ctx->frames[0].maxs[1];
	maxs[2] = ctx->frames[0].maxs[2];
	yawradius = ctx->frames[0].yawradius;
	allradius = ctx->frames[0].allradius;
	for (i = 0;i < ctx->numframes;i++)
	{
		if (mins[0] > ctx->frames[i].mins[0]) mins[0] = ctx->frames[i].mins[0];
		if (mins[1] > ctx->frames[i].mins[1]) mins[1] = ctx->frames[i].mins[1];
		if (mins[2] > ctx->frames[i].mins[2]) mins[2] = ctx->frames[i].mins[2];
		if (maxs[0] < ctx->frames[i].maxs[0]) maxs[0] = ctx->frames[i].maxs[0];
		if (maxs[1] < ctx->frames[i].maxs[1]) maxs[1] = ctx->frames[i].maxs[1];
		if (maxs[2] < ctx->frames[i].maxs[2]) maxs[2] = ctx->frames[i].maxs[2];
		if (yawradius < ctx->frames[i].yawradius) yawradius = ctx->frames[i].yawradius;
		if (allradius < ctx->frames[i].allradius) allradius = ctx->frames[i].allradius;
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
	putbelong(ctx->numbones);
	putbelong(ctx->numshaders);
	putbelong(ctx->numframes);

	// offsets to things
	pos_lumps = putgetposition();
	putbelong(0);
	putbelong(0);
	putbelong(0);

	// store the bones
	pos_bones = putgetposition();
	for (i = 0;i < ctx->numbones;i++)
	{
		putstring(ctx->bones[i].name, MAX_NAME);
		putbelong(ctx->bones[i].parent);
		putbelong(ctx->bones[i].flags);
	}

	// store the meshs
	pos_meshs = putgetposition();
	// skip over the mesh structs, they will be filled in later
	putsetposition(pos_meshs + ctx->numshaders * (MAX_NAME + 24));

	// store the frames
	pos_frames = putgetposition();
	// skip over the frame structs, they will be filled in later
	putsetposition(pos_frames + ctx->numframes * (MAX_NAME + 36));

	// store the data referenced by meshs
	for (i = 0;i < ctx->numshaders;i++)
	{
		pos_verts = putgetposition();
		nverts = 0;
		for (j = 0;j < ctx->numverts;j++)
		{
			if (ctx->vertices[j].shadernum == i)
			{
				ctx->vertremap[j] = nverts++;
				putbelong(ctx->vertices[j].numinfluences); // how many bones for this vertex (always 1 for smd)
				for (k = 0;k < ctx->vertices[j].numinfluences;k++)
				{
					putbefloat(ctx->vertices[j].influenceorigin[k][0] * ctx->vertices[j].influenceweight[k]);
					putbefloat(ctx->vertices[j].influenceorigin[k][1] * ctx->vertices[j].influenceweight[k]);
					putbefloat(ctx->vertices[j].influenceorigin[k][2] * ctx->vertices[j].influenceweight[k]);
					putbefloat(ctx->vertices[j].influenceweight[k]); // influence of the bone on the vertex
					putbefloat(ctx->vertices[j].influencenormal[k][0] * ctx->vertices[j].influenceweight[k]);
					putbefloat(ctx->vertices[j].influencenormal[k][1] * ctx->vertices[j].influenceweight[k]);
					putbefloat(ctx->vertices[j].influencenormal[k][2] * ctx->vertices[j].influenceweight[k]);
					putbelong(ctx->vertices[j].influencebone[k]); // number of the bone
				}
			}
			else
				ctx->vertremap[j] = -1;
		}
		pos_texcoords = putgetposition();
		for (j = 0;j < ctx->numverts;j++)
		{
			if (ctx->vertices[j].shadernum == i)
			{
				// OpenGL wants bottom to top texcoords
				putbefloat(ctx->vertices[j].texcoord[0]);
				putbefloat(1.0f - ctx->vertices[j].texcoord[1]);
			}
		}
		pos_index = putgetposition();
		ntris = 0;
		for (j = 0;j < ctx->numtriangles;j++)
		{
			if (ctx->triangles[j].shadernum == i)
			{
				putbelong(ctx->vertremap[ctx->triangles[j].v[0]]);
				putbelong(ctx->vertremap[ctx->triangles[j].v[1]]);
				putbelong(ctx->vertremap[ctx->triangles[j].v[2]]);
				ntris++;
			}
		}
		pos_groupids = putgetposition();
		for (j = 0;j < ctx->numtriangles;j++)
			if (ctx->triangles[j].shadernum == i)
				putbelong(0);

		// now we actually write the mesh header
		restoreposition = putgetposition();
		putsetposition(pos_meshs + i * (MAX_NAME + 24));
		putstring(ctx->shaders[i], MAX_NAME);
		putbelong(nverts);
		putbelong(ntris);
		putbelong(pos_verts);
		putbelong(pos_texcoords);
		putbelong(pos_index);
		putbelong(pos_groupids);
		putsetposition(restoreposition);
	}

	// store the data referenced by frames
	for (i = 0;i < ctx->numframes;i++)
	{
		pos_framebones = putgetposition();
		for (j = 0;j < ctx->numbones;j++)
			for (k = 0;k < 3;k++)
				for (l = 0;l < 4;l++)
					putbefloat(ctx->frames[i].bones[j].m[k][l]);

		// now we actually write the frame header
		restoreposition = putgetposition();
		putsetposition(pos_frames + i * (MAX_NAME + 36));
		putstring(ctx->frames[i].name, MAX_NAME);
		putbefloat(ctx->frames[i].mins[0]);
		putbefloat(ctx->frames[i].mins[1]);
		putbefloat(ctx->frames[i].mins[2]);
		putbefloat(ctx->frames[i].maxs[0]);
		putbefloat(ctx->frames[i].maxs[1]);
		putbefloat(ctx->frames[i].maxs[2]);
		putbefloat(ctx->frames[i].yawradius);
		putbefloat(ctx->frames[i].allradius);
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

	sprintf(filename, "%s.dpm", ctx->model_path);
	if (!ctx->output_overflow)
	{
		FS_WriteFile(filename, ctx->outputbuffer, filesize);
		Con_Printf("wrote file %s (size %5ik)\n", filename, (filesize + 1023) >> 10);
	}
	else
		Con_Printf("wrote file %s (size %5ik) - failed, too big\n", filename, (filesize + 1023) >> 10);

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
	putstring(ctx->model_name, 64);
	putlelong(0);// flags (FIXME)
	putlelong(ctx->numframes); // frames
	numtags = 0;
	for (i = 0;i < ctx->numbones;i++)
		if (!strncmp(ctx->bones[i].name, "TAG_", 4))
			numtags++;
	putlelong(numtags); // number of tags per frame
	putlelong(ctx->numshaders); // number of meshes
	putlelong(1); // number of shader names per mesh (they are stacked things like quad shell I think)

	// these are filled in later
	pos_lumps = putgetposition();
	putlelong(0); // frameinfo
	putlelong(0); // tags
	putlelong(0); // first mesh
	putlelong(0); // end

	// store frameinfo
	pos_frameinfo = putgetposition();
	for (i = 0;i < ctx->numframes;i++)
	{
		putlefloat(ctx->frames[i].mins[0]);
		putlefloat(ctx->frames[i].mins[1]);
		putlefloat(ctx->frames[i].mins[2]);
		putlefloat(ctx->frames[i].maxs[0]);
		putlefloat(ctx->frames[i].maxs[1]);
		putlefloat(ctx->frames[i].maxs[2]);
		putlefloat((ctx->frames[i].mins[0] + ctx->frames[i].maxs[0]) * 0.5f);
		putlefloat((ctx->frames[i].mins[1] + ctx->frames[i].maxs[1]) * 0.5f);
		putlefloat((ctx->frames[i].mins[2] + ctx->frames[i].maxs[2]) * 0.5f);
		putlefloat(ctx->frames[i].allradius);
		putstring(ctx->frames[i].name, 64);
	}

	// store tags
	pos_tags = putgetposition();
	if (numtags)
	{
		for (i = 0;i < ctx->numframes;i++)
		{
			for (k = 0;k < ctx->numbones;k++)
			{
				if (ctx->bones[k].defined)
				{
					if (ctx->bones[k].parent >= 0)
						ctx->bonematrix[k] = concattransform(ctx->bonematrix[ctx->bones[k].parent], ctx->frames[i].bones[k]);
					else
						ctx->bonematrix[k] = ctx->frames[i].bones[k];
				}
			}
			for (j = 0;j < ctx->numbones;j++)
			{
				if (strncmp(ctx->bones[j].name, "TAG_", 4))
					continue;
				putstring(ctx->bones[j].name, 64);
				// output the origin and then 9 rotation floats
				// these are in a transposed order compared to our matrices,
				// so this indexing looks a little odd.
				// origin
				putlefloat(ctx->bonematrix[j].m[0][3]);
				putlefloat(ctx->bonematrix[j].m[1][3]);
				putlefloat(ctx->bonematrix[j].m[2][3]);
				// x axis
				putlefloat(ctx->bonematrix[j].m[0][0]);
				putlefloat(ctx->bonematrix[j].m[1][0]);
				putlefloat(ctx->bonematrix[j].m[2][0]);
				// y axis
				putlefloat(ctx->bonematrix[j].m[0][1]);
				putlefloat(ctx->bonematrix[j].m[1][1]);
				putlefloat(ctx->bonematrix[j].m[2][1]);
				// z axis
				putlefloat(ctx->bonematrix[j].m[0][2]);
				putlefloat(ctx->bonematrix[j].m[1][2]);
				putlefloat(ctx->bonematrix[j].m[2][2]);
			}
		}
	}

	// store the meshes
	pos_firstmesh = putgetposition();
	for (i = 0;i < ctx->numshaders;i++)
	{
		nverts = 0;
		for (j = 0;j < ctx->numverts;j++)
		{
			if (ctx->vertices[j].shadernum == i)
				ctx->vertremap[j] = nverts++;
			else
				ctx->vertremap[j] = -1;
		}
		ntris = 0;
		for (j = 0;j < ctx->numtriangles;j++)
			if (ctx->triangles[j].shadernum == i)
				ntris++;

		// write mesh header
		pos_meshstart = putgetposition();
		putstring("IDP3", 4); // identifier
		putstring(ctx->shaders[i], 64); // mesh name
		putlelong(0); // flags (what is this for?)
		putlelong(ctx->numframes); // number of frames
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
		putstring(ctx->shaders[i], 64); // shader name
		putlelong(0); // shader number (used internally by Quake3 after load?)
		// write triangles
		pos_meshelements = putgetposition();
		for (j = 0;j < ctx->numtriangles;j++)
		{
			if (ctx->triangles[j].shadernum == i)
			{
				// swap the triangles because Quake3 uses clockwise triangles
				putlelong(ctx->vertremap[ctx->triangles[j].v[0]]);
				putlelong(ctx->vertremap[ctx->triangles[j].v[2]]);
				putlelong(ctx->vertremap[ctx->triangles[j].v[1]]);
			}
		}
		// write texcoords
		pos_meshtexcoords = putgetposition();
		for (j = 0;j < ctx->numverts;j++)
		{
			if (ctx->vertices[j].shadernum == i)
			{
				// OpenGL wants bottom to top texcoords
				putlefloat(ctx->vertices[j].texcoord[0]);
				putlefloat(1.0f - ctx->vertices[j].texcoord[1]);
			}
		}
		pos_meshframevertices = putgetposition();
		for (j = 0;j < ctx->numframes;j++)
		{
			for (k = 0;k < ctx->numbones;k++)
			{
				if (ctx->bones[k].defined)
				{
					if (ctx->bones[k].parent >= 0)
						ctx->bonematrix[k] = concattransform(ctx->bonematrix[ctx->bones[k].parent], ctx->frames[j].bones[k]);
					else
						ctx->bonematrix[k] = ctx->frames[j].bones[k];
				}
			}
			for (k = 0;k < ctx->numverts;k++)
			{
				if (ctx->vertices[k].shadernum == i)
				{
					double vertex[3], normal[3], v[3], longitude, latitude;
					vertex[0] = vertex[1] = vertex[2] = normal[0] = normal[1] = normal[2] = 0;
					for (l = 0;l < ctx->vertices[k].numinfluences;l++)
					{
						transform(ctx->vertices[k].influenceorigin[l], ctx->bonematrix[ctx->vertices[k].influencebone[l]], v);
						vertex[0] += v[0] * ctx->vertices[k].influenceweight[l];
						vertex[1] += v[1] * ctx->vertices[k].influenceweight[l];
						vertex[2] += v[2] * ctx->vertices[k].influenceweight[l];
						transformnormal(ctx->vertices[k].influencenormal[l], ctx->bonematrix[ctx->vertices[k].influencebone[l]], v);
						normal[0] += v[0] * ctx->vertices[k].influenceweight[l];
						normal[1] += v[1] * ctx->vertices[k].influenceweight[l];
						normal[2] += v[2] * ctx->vertices[k].influenceweight[l];
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

	sprintf(filename, "%s.md3", ctx->model_path);
	if (!ctx->output_overflow)
	{
		FS_WriteFile(filename, ctx->outputbuffer, filesize);
		Con_Printf("wrote file %s (size %5ik)\n", filename, (filesize + 1023) >> 10);
	}
	else
		Con_Printf("wrote file %s (size %5ik) - failed, too big\n", filename, (filesize + 1023) >> 10);


	return 1;
}

