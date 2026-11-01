#ifndef MOD_SKELETAL_ANIMATEVERTICES_NEON_H
#define MOD_SKELETAL_ANIMATEVERTICES_NEON_H

#include "quakedef.h"

#ifdef NEON_POSSIBLE
void Mod_Skeletal_AnimateVertices_NEON(const dp_model_t * RESTRICT model, const frameblend_t * RESTRICT frameblend, const skeleton_t *skeleton, float * RESTRICT vertex3f, float * RESTRICT normal3f, float * RESTRICT svector3f, float * RESTRICT tvector3f);
#endif

#endif

