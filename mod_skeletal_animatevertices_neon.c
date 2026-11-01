#include "mod_skeletal_animatevertices_neon.h"

#ifdef NEON_POSSIBLE

#include <arm_neon.h>
#include "model_alias.h"
#include "r_shadow.h"

static inline void neon_transpose4_f32(float32x4_t *q0, float32x4_t *q1, float32x4_t *q2, float32x4_t *q3)
{
	float32x4x2_t t0 = vtrnq_f32(*q0, *q1);
	float32x4x2_t t1 = vtrnq_f32(*q2, *q3);
	*q0 = vcombine_f32(vget_low_f32(t0.val[0]), vget_low_f32(t1.val[0]));
	*q1 = vcombine_f32(vget_low_f32(t0.val[1]), vget_low_f32(t1.val[1]));
	*q2 = vcombine_f32(vget_high_f32(t0.val[0]), vget_high_f32(t1.val[0]));
	*q3 = vcombine_f32(vget_high_f32(t0.val[1]), vget_high_f32(t1.val[1]));
}

void Mod_Skeletal_AnimateVertices_NEON(const dp_model_t * RESTRICT model, const frameblend_t * RESTRICT frameblend, const skeleton_t *skeleton, float * RESTRICT vertex3f, float * RESTRICT normal3f, float * RESTRICT svector3f, float * RESTRICT tvector3f)
{
	int i, k;
	int blends;
	matrix4x4_t *bonepose;
	matrix4x4_t *boneposerelative;
	const blendweights_t * RESTRICT weights;
	int num_vertices_minus_one;

	num_vertices_minus_one = model->surfmesh.num_vertices - 1;

	bonepose = (matrix4x4_t *) Mod_Skeletal_AnimateVertices_AllocBuffers(sizeof(matrix4x4_t) * (model->num_bones*2 + model->surfmesh.num_blends));
	boneposerelative = bonepose + model->num_bones;

	if (skeleton && !skeleton->relativetransforms)
		skeleton = NULL;

	// interpolate matrices
	if (skeleton)
	{
		for (i = 0; i < model->num_bones; i++)
		{
			const float * RESTRICT n = model->data_baseboneposeinverse + i * 12;
			matrix4x4_t * RESTRICT s = &skeleton->relativetransforms[i];
			matrix4x4_t * RESTRICT b = &bonepose[i];
			matrix4x4_t * RESTRICT r = &boneposerelative[i];
			float32x4_t b0, b1, b2, b3, r0, r1, r2, r3, nr;
			if (model->data_bones[i].parent >= 0)
			{
				const matrix4x4_t * RESTRICT p = &bonepose[model->data_bones[i].parent];
				float32x4_t s0 = vld1q_f32(s->m[0]), s1 = vld1q_f32(s->m[1]), s2 = vld1q_f32(s->m[2]);
#define SKELETON_MATRIX(r, c) vdupq_laneq_f32(s##r, c)
				float32x4_t pr = vld1q_f32(p->m[0]);
				b0 = vmulq_f32(pr, SKELETON_MATRIX(0, 0));
				b1 = vmulq_f32(pr, SKELETON_MATRIX(0, 1));
				b2 = vmulq_f32(pr, SKELETON_MATRIX(0, 2));
				b3 = vmulq_f32(pr, SKELETON_MATRIX(0, 3));
				pr = vld1q_f32(p->m[1]);
				b0 = vfmaq_f32(b0, pr, SKELETON_MATRIX(1, 0));
				b1 = vfmaq_f32(b1, pr, SKELETON_MATRIX(1, 1));
				b2 = vfmaq_f32(b2, pr, SKELETON_MATRIX(1, 2));
				b3 = vfmaq_f32(b3, pr, SKELETON_MATRIX(1, 3));
				pr = vld1q_f32(p->m[2]);
				b0 = vfmaq_f32(b0, pr, SKELETON_MATRIX(2, 0));
				b1 = vfmaq_f32(b1, pr, SKELETON_MATRIX(2, 1));
				b2 = vfmaq_f32(b2, pr, SKELETON_MATRIX(2, 2));
				b3 = vfmaq_f32(b3, pr, SKELETON_MATRIX(2, 3));
				b3 = vaddq_f32(b3, vld1q_f32(p->m[3]));
#undef SKELETON_MATRIX
			}
			else
			{
				b0 = vld1q_f32(s->m[0]);
				b1 = vld1q_f32(s->m[1]);
				b2 = vld1q_f32(s->m[2]);
				b3 = vld1q_f32(s->m[3]);
#ifndef OPENGLORIENTATION
				neon_transpose4_f32(&b0, &b1, &b2, &b3);
#endif
			}
			vst1q_f32(b->m[0], b0);
			vst1q_f32(b->m[1], b1);
			vst1q_f32(b->m[2], b2);
			vst1q_f32(b->m[3], b3);
			nr = vld1q_f32(n);
			r0 = vmulq_laneq_f32(b0, nr, 0);
			r1 = vmulq_laneq_f32(b0, nr, 1);
			r2 = vmulq_laneq_f32(b0, nr, 2);
			r3 = vmulq_laneq_f32(b0, nr, 3);
			nr = vld1q_f32(n+4);
			r0 = vfmaq_laneq_f32(r0, b1, nr, 0);
			r1 = vfmaq_laneq_f32(r1, b1, nr, 1);
			r2 = vfmaq_laneq_f32(r2, b1, nr, 2);
			r3 = vfmaq_laneq_f32(r3, b1, nr, 3);
			nr = vld1q_f32(n+8);
			r0 = vfmaq_laneq_f32(r0, b2, nr, 0);
			r1 = vfmaq_laneq_f32(r1, b2, nr, 1);
			r2 = vfmaq_laneq_f32(r2, b2, nr, 2);
			r3 = vfmaq_laneq_f32(r3, b2, nr, 3);
			r3 = vaddq_f32(r3, b3);
			vst1q_f32(r->m[0], r0);
			vst1q_f32(r->m[1], r1);
			vst1q_f32(r->m[2], r2);
			vst1q_f32(r->m[3], r3);
		}
	}
	else
	{
		for (i = 0; i < model->num_bones; i++)
		{
			float m[12];
			const short * RESTRICT firstpose7s = model->data_poses7s + 7 * (frameblend[0].subframe * model->num_bones + i);
			float firstlerp = frameblend[0].lerp,
				firsttx = firstpose7s[0], firstty = firstpose7s[1], firsttz = firstpose7s[2],
				rx = firstpose7s[3] * firstlerp,
				ry = firstpose7s[4] * firstlerp,
				rz = firstpose7s[5] * firstlerp,
				rw = firstpose7s[6] * firstlerp,
				dx = firsttx*rw + firstty*rz - firsttz*ry,
				dy = -firsttx*rz + firstty*rw + firsttz*rx,
				dz = firsttx*ry - firstty*rx + firsttz*rw,
				dw = -firsttx*rx - firstty*ry - firsttz*rz,
				scale, sx, sy, sz, sw;
			for (blends = 1; blends < MAX_FRAMEBLENDS && frameblend[blends].lerp > 0; blends++)
			{
				const short * RESTRICT blendpose7s = model->data_poses7s + 7 * (frameblend[blends].subframe * model->num_bones + i);
				float blendlerp = frameblend[blends].lerp,
					blendtx = blendpose7s[0], blendty = blendpose7s[1], blendtz = blendpose7s[2],
					qx = blendpose7s[3], qy = blendpose7s[4], qz = blendpose7s[5], qw = blendpose7s[6];
				if (rx*qx + ry*qy + rz*qz + rw*qw < 0) blendlerp = -blendlerp;
				qx *= blendlerp;
				qy *= blendlerp;
				qz *= blendlerp;
				qw *= blendlerp;
				rx += qx;
				ry += qy;
				rz += qz;
				rw += qw;
				dx += blendtx*qw + blendty*qz - blendtz*qy;
				dy += -blendtx*qz + blendty*qw + blendtz*qx;
				dz += blendtx*qy - blendty*qx + blendtz*qw;
				dw += -blendtx*qx - blendty*qy - blendtz*qz;
			}
			scale = 1.0f / (rx*rx + ry*ry + rz*rz + rw*rw);
			sx = rx * scale;
			sy = ry * scale;
			sz = rz * scale;
			sw = rw * scale;
			m[0] = sw*rw + sx*rx - sy*ry - sz*rz;
			m[1] = 2*(sx*ry - sw*rz);
			m[2] = 2*(sx*rz + sw*ry);
			m[3] = model->num_posescale*(dx*sw - dy*sz + dz*sy - dw*sx);
			m[4] = 2*(sx*ry + sw*rz);
			m[5] = sw*rw + sy*ry - sx*rx - sz*rz;
			m[6] = 2*(sy*rz - sw*rx);
			m[7] = model->num_posescale*(dx*sz + dy*sw - dz*sx - dw*sy);
			m[8] = 2*(sx*rz - sw*ry);
			m[9] = 2*(sy*rz + sw*rx);
			m[10] = sw*rw + sz*rz - sx*rx - sy*ry;
			m[11] = model->num_posescale*(dy*sx + dz*sw - dx*sy - dw*sz);
			if (i == r_skeletal_debugbone.integer)
				m[r_skeletal_debugbonecomponent.integer % 12] += r_skeletal_debugbonevalue.value;
			m[3] *= r_skeletal_debugtranslatex.value;
			m[7] *= r_skeletal_debugtranslatey.value;
			m[11] *= r_skeletal_debugtranslatez.value;
			{
				const float * RESTRICT n = model->data_baseboneposeinverse + i * 12;
				matrix4x4_t * RESTRICT b = &bonepose[i];
				matrix4x4_t * RESTRICT r = &boneposerelative[i];
				float32x4_t b0, b1, b2, b3, r0, r1, r2, r3, nr;
				if (model->data_bones[i].parent >= 0)
				{
					const matrix4x4_t * RESTRICT p = &bonepose[model->data_bones[i].parent];
					float32x4_t pr = vld1q_f32(p->m[0]);
					b0 = vmulq_n_f32(pr, m[0]);
					b1 = vmulq_n_f32(pr, m[1]);
					b2 = vmulq_n_f32(pr, m[2]);
					b3 = vmulq_n_f32(pr, m[3]);
					pr = vld1q_f32(p->m[1]);
					b0 = vfmaq_n_f32(b0, pr, m[4]);
					b1 = vfmaq_n_f32(b1, pr, m[5]);
					b2 = vfmaq_n_f32(b2, pr, m[6]);
					b3 = vfmaq_n_f32(b3, pr, m[7]);
					pr = vld1q_f32(p->m[2]);
					b0 = vfmaq_n_f32(b0, pr, m[8]);
					b1 = vfmaq_n_f32(b1, pr, m[9]);
					b2 = vfmaq_n_f32(b2, pr, m[10]);
					b3 = vfmaq_n_f32(b3, pr, m[11]);
					b3 = vaddq_f32(b3, vld1q_f32(p->m[3]));
				}
				else
				{
					float temp0[4] = {m[0], m[4], m[8], 0.0f};
					float temp1[4] = {m[1], m[5], m[9], 0.0f};
					float temp2[4] = {m[2], m[6], m[10], 0.0f};
					float temp3[4] = {m[3], m[7], m[11], 1.0f};
					b0 = vld1q_f32(temp0);
					b1 = vld1q_f32(temp1);
					b2 = vld1q_f32(temp2);
					b3 = vld1q_f32(temp3);
				}
				vst1q_f32(b->m[0], b0);
				vst1q_f32(b->m[1], b1);
				vst1q_f32(b->m[2], b2);
				vst1q_f32(b->m[3], b3);
				nr = vld1q_f32(n);
				r0 = vmulq_laneq_f32(b0, nr, 0);
				r1 = vmulq_laneq_f32(b0, nr, 1);
				r2 = vmulq_laneq_f32(b0, nr, 2);
				r3 = vmulq_laneq_f32(b0, nr, 3);
				nr = vld1q_f32(n+4);
				r0 = vfmaq_laneq_f32(r0, b1, nr, 0);
				r1 = vfmaq_laneq_f32(r1, b1, nr, 1);
				r2 = vfmaq_laneq_f32(r2, b1, nr, 2);
				r3 = vfmaq_laneq_f32(r3, b1, nr, 3);
				nr = vld1q_f32(n+8);
				r0 = vfmaq_laneq_f32(r0, b2, nr, 0);
				r1 = vfmaq_laneq_f32(r1, b2, nr, 1);
				r2 = vfmaq_laneq_f32(r2, b2, nr, 2);
				r3 = vfmaq_laneq_f32(r3, b2, nr, 3);
				r3 = vaddq_f32(r3, b3);
				vst1q_f32(r->m[0], r0);
				vst1q_f32(r->m[1], r1);
				vst1q_f32(r->m[2], r2);
				vst1q_f32(r->m[3], r3);
			}
		}
	}

	// generate matrices for all blend combinations
	weights = model->surfmesh.data_blendweights;
	for (i = 0; i < model->surfmesh.num_blends; i++, weights++)
	{
		float * RESTRICT b = &boneposerelative[model->num_bones + i].m[0][0];
		const float * RESTRICT m = &boneposerelative[weights->index[0]].m[0][0];
		float f = weights->influence[0] * (1.0f / 255.0f);
		float32x4_t fv = vdupq_n_f32(f);
		float32x4_t b0 = vmulq_f32(vld1q_f32(m), fv);
		float32x4_t b1 = vmulq_f32(vld1q_f32(m+4), fv);
		float32x4_t b2 = vmulq_f32(vld1q_f32(m+8), fv);
		float32x4_t b3 = vmulq_f32(vld1q_f32(m+12), fv);
		for (k = 1; k < 4 && weights->influence[k]; k++)
		{
			m = &boneposerelative[weights->index[k]].m[0][0];
			f = weights->influence[k] * (1.0f / 255.0f);
			fv = vdupq_n_f32(f);
			b0 = vfmaq_f32(b0, vld1q_f32(m), fv);
			b1 = vfmaq_f32(b1, vld1q_f32(m+4), fv);
			b2 = vfmaq_f32(b2, vld1q_f32(m+8), fv);
			b3 = vfmaq_f32(b3, vld1q_f32(m+12), fv);
		}
		vst1q_f32(b, b0);
		vst1q_f32(b+4, b1);
		vst1q_f32(b+8, b2);
		vst1q_f32(b+12, b3);
	}

#define LOAD_MATRIX_SCALAR() const float * RESTRICT m = &boneposerelative[*b].m[0][0]

#define LOAD_MATRIX3() \
	const float * RESTRICT m = &boneposerelative[*b].m[0][0]; \
	float32x4_t m1 = vld1q_f32((m)); \
	float32x4_t m2 = vld1q_f32((m)+4); \
	float32x4_t m3 = vld1q_f32((m)+8);

#define LOAD_MATRIX4() \
	const float * RESTRICT m = &boneposerelative[*b].m[0][0]; \
	float32x4_t m1 = vld1q_f32((m)); \
	float32x4_t m2 = vld1q_f32((m)+4); \
	float32x4_t m3 = vld1q_f32((m)+8); \
	float32x4_t m4 = vld1q_f32((m)+12)

#define TRANSFORM_POSITION_SCALAR(in, out) \
	(out)[0] = ((in)[0] * m[0] + (in)[1] * m[4] + (in)[2] * m[ 8] + m[12]); \
	(out)[1] = ((in)[0] * m[1] + (in)[1] * m[5] + (in)[2] * m[ 9] + m[13]); \
	(out)[2] = ((in)[0] * m[2] + (in)[1] * m[6] + (in)[2] * m[10] + m[14]);
#define TRANSFORM_VECTOR_SCALAR(in, out) \
	(out)[0] = ((in)[0] * m[0] + (in)[1] * m[4] + (in)[2] * m[ 8]); \
	(out)[1] = ((in)[0] * m[1] + (in)[1] * m[5] + (in)[2] * m[ 9]); \
	(out)[2] = ((in)[0] * m[2] + (in)[1] * m[6] + (in)[2] * m[10]);

#define TRANSFORM_POSITION(in, out) { \
		float32x4_t pin = vld1q_f32(in); \
		float32x4_t x = vdupq_laneq_f32(pin, 0); \
		float32x4_t y = vdupq_laneq_f32(pin, 1); \
		float32x4_t z = vdupq_laneq_f32(pin, 2); \
		float32x4_t pout = vfmaq_f32(m4, x, m1); \
		pout = vfmaq_f32(pout, y, m2); \
		pout = vfmaq_f32(pout, z, m3); \
		vst1q_f32((out), pout); \
	}

#define TRANSFORM_VECTOR(in, out) { \
		float32x4_t vin = vld1q_f32(in); \
		float32x4_t x = vdupq_laneq_f32(vin, 0); \
		float32x4_t y = vdupq_laneq_f32(vin, 1); \
		float32x4_t z = vdupq_laneq_f32(vin, 2); \
		float32x4_t vout = vmulq_f32(x, m1); \
		vout = vfmaq_f32(vout, y, m2); \
		vout = vfmaq_f32(vout, z, m3); \
		vst1q_f32((out), vout); \
	}

	// transform vertex attributes by blended matrices
	if (vertex3f)
	{
		const float * RESTRICT v = model->surfmesh.data_vertex3f;
		const unsigned short * RESTRICT b = model->surfmesh.blends;
		// special case common combinations of attributes to avoid repeated loading of matrices
		if (normal3f)
		{
			const float * RESTRICT n = model->surfmesh.data_normal3f;
			if (svector3f && tvector3f)
			{
				const float * RESTRICT svec = model->surfmesh.data_svector3f;
				const float * RESTRICT tvec = model->surfmesh.data_tvector3f;

				// Note that each SIMD iteration stores 4 floats, so we break one vertex short
				// and handle the final vertex with scalars to avoid out-of-bounds writes
				for (i = 0; i < num_vertices_minus_one; i++, v += 3, n += 3, svec += 3, tvec += 3, b++,
						vertex3f += 3, normal3f += 3, svector3f += 3, tvector3f += 3)
				{
					LOAD_MATRIX4();
					TRANSFORM_POSITION(v, vertex3f);
					TRANSFORM_VECTOR(n, normal3f);
					TRANSFORM_VECTOR(svec, svector3f);
					TRANSFORM_VECTOR(tvec, tvector3f);
				}

				{
					LOAD_MATRIX_SCALAR();
					TRANSFORM_POSITION_SCALAR(v, vertex3f);
					TRANSFORM_VECTOR_SCALAR(n, normal3f);
					TRANSFORM_VECTOR_SCALAR(svec, svector3f);
					TRANSFORM_VECTOR_SCALAR(tvec, tvector3f);
				}
				return;
			}

			for (i = 0; i < num_vertices_minus_one; i++, v += 3, n += 3, b++, vertex3f += 3, normal3f += 3)
			{
				LOAD_MATRIX4();
				TRANSFORM_POSITION(v, vertex3f);
				TRANSFORM_VECTOR(n, normal3f);
			}
			{
				LOAD_MATRIX_SCALAR();
				TRANSFORM_POSITION_SCALAR(v, vertex3f);
				TRANSFORM_VECTOR_SCALAR(n, normal3f);
			}
		}
		else
		{
			for (i = 0; i < num_vertices_minus_one; i++, v += 3, b++, vertex3f += 3)
			{
				LOAD_MATRIX4();
				TRANSFORM_POSITION(v, vertex3f);
			}
			{
				LOAD_MATRIX_SCALAR();
				TRANSFORM_POSITION_SCALAR(v, vertex3f);
			}
		}
	}

	else if (normal3f)
	{
		const float * RESTRICT n = model->surfmesh.data_normal3f;
		const unsigned short * RESTRICT b = model->surfmesh.blends;
		for (i = 0; i < num_vertices_minus_one; i++, n += 3, b++, normal3f += 3)
		{
			LOAD_MATRIX3();
			TRANSFORM_VECTOR(n, normal3f);
		}
		{
			LOAD_MATRIX_SCALAR();
			TRANSFORM_VECTOR_SCALAR(n, normal3f);
		}
	}

	if (svector3f)
	{
		const float * RESTRICT svec = model->surfmesh.data_svector3f;
		const unsigned short * RESTRICT b = model->surfmesh.blends;
		for (i = 0; i < num_vertices_minus_one; i++, svec += 3, b++, svector3f += 3)
		{
			LOAD_MATRIX3();
			TRANSFORM_VECTOR(svec, svector3f);
		}
		{
			LOAD_MATRIX_SCALAR();
			TRANSFORM_VECTOR_SCALAR(svec, svector3f);
		}
	}

	if (tvector3f)
	{
		const float * RESTRICT tvec = model->surfmesh.data_tvector3f;
		const unsigned short * RESTRICT b = model->surfmesh.blends;
		for (i = 0; i < num_vertices_minus_one; i++, tvec += 3, b++, tvector3f += 3)
		{
			LOAD_MATRIX3();
			TRANSFORM_VECTOR(tvec, tvector3f);
		}
		{
			LOAD_MATRIX_SCALAR();
			TRANSFORM_VECTOR_SCALAR(tvec, tvector3f);
		}
	}

#undef LOAD_MATRIX3
#undef LOAD_MATRIX4
#undef TRANSFORM_POSITION
#undef TRANSFORM_VECTOR
#undef LOAD_MATRIX_SCALAR
#undef TRANSFORM_POSITION_SCALAR
#undef TRANSFORM_VECTOR_SCALAR
}

#endif // NEON_POSSIBLE

