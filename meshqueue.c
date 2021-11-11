
#include "quakedef.h"
#include "meshqueue.h"

typedef struct meshqueue_s
{
	struct meshqueue_s *next;
	void (*callback)(const entity_render_t *ent, const rtlight_t *rtlight, int numsurfaces, int *surfaceindices);
	const entity_render_t *ent;
	int surfacenumber;
	const rtlight_t *rtlight;
	dptransparentsortcategory_t category;
}
meshqueue_t;

int trans_sortarraysize;
meshqueue_t **trans_hash = NULL;
meshqueue_t ***trans_hashpointer = NULL;

float mqt_viewplanedist;
meshqueue_t *mqt_array;
int mqt_count;
int mqt_total;

void R_MeshQueue_BeginScene(void)
{
	mqt_count = 0;
	mqt_viewplanedist = DotProduct(r_refdef.view.origin, r_refdef.view.forward);
}

static void bad_meshqueue_t_callback (const entity_render_t *ent, const rtlight_t *rtlight, int numsurfaces, int *surfacelist) {
	if (developer.integer) {
		Con_DPrintf("^1bad_meshqueue_t_callback: Would be call back to NULL!\n");
	} else {
		Sys_Error("^1bad_meshqueue_t_callback:  Would be call back to NULL!\n");
	}
}

void R_MeshQueue_AddTransparent(dptransparentsortcategory_t category, void (*callback)(const entity_render_t *ent, const rtlight_t *rtlight, int numsurfaces, int *surfacelist), const entity_render_t *ent, int surfacenumber, const rtlight_t *rtlight)
{
	meshqueue_t *mq;
	if (mqt_count >= mqt_total || !mqt_array)
	{
		int newtotal = max(1024, mqt_total * 2);
		meshqueue_t *newarray = (meshqueue_t *)Mem_Alloc(cls.permanentmempool, newtotal * sizeof(meshqueue_t));
		if (mqt_array)
		{
			memcpy(newarray, mqt_array, mqt_total * sizeof(meshqueue_t));
			Mem_Free(mqt_array);
		}
		mqt_array = newarray;
		mqt_total = newtotal;
	}
	mq = &mqt_array[mqt_count++];
	mq->callback = callback;
	mq->ent = ent;
	mq->surfacenumber = surfacenumber;
	mq->rtlight = rtlight;
	mq->category = category;
	mq->next = NULL;
}

void R_MeshQueue_RenderTransparent(void)
{
	int i, batchnumsurfaces;
	const entity_render_t *ent;
	const rtlight_t *rtlight;
	void (*callback)(const entity_render_t *ent, const rtlight_t *rtlight, int numsurfaces, int *surfaceindices);
	int batchsurfaceindex[MESHQUEUE_TRANSPARENT_BATCHSIZE];
	meshqueue_t *mqt;

	if (!mqt_count)
		return;

	callback = bad_meshqueue_t_callback;
	ent = NULL;
	rtlight = NULL;
	batchnumsurfaces = 0;

	for (i = 0, mqt = mqt_array; i < mqt_count; i++, mqt++)
	{
		if (ent != mqt->ent || rtlight != mqt->rtlight || callback != mqt->callback || batchnumsurfaces >= MESHQUEUE_TRANSPARENT_BATCHSIZE)
		{
			if (batchnumsurfaces)
				callback(ent, rtlight, batchnumsurfaces, batchsurfaceindex);
			batchnumsurfaces = 0;
			ent = mqt->ent;
			rtlight = mqt->rtlight;
			callback = mqt->callback;
		}
		batchsurfaceindex[batchnumsurfaces++] = mqt->surfacenumber;
	}
	if (batchnumsurfaces)
		callback(ent, rtlight, batchnumsurfaces, batchsurfaceindex);
	mqt_count = 0;

	return;
}
