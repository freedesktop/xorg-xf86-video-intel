/*
 * Copyright © 2006,2008,2011 Intel Corporation
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Wang Zhenyu <zhenyu.z.wang@sna.com>
 *    Eric Anholt <eric@anholt.net>
 *    Carl Worth <cworth@redhat.com>
 *    Keith Packard <keithp@keithp.com>
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>

#include "sna.h"
#include "sna_reg.h"
#include "sna_render.h"
#include "sna_render_inline.h"
#include "sna_video.h"

#include "gen5_render.h"

#if DEBUG_RENDER
#undef DBG
#define DBG(x) ErrorF x
#endif

#define NO_COMPOSITE_SPANS 0

#define PREFER_BLT_FILL 1

#define DBG_NO_STATE_CACHE 0
#define DBG_NO_SURFACE_CACHE 0

#define MAX_3D_SIZE 8192

#define GEN5_GRF_BLOCKS(nreg)    ((nreg + 15) / 16 - 1)

/* Set up a default static partitioning of the URB, which is supposed to
 * allow anything we would want to do, at potentially lower performance.
 */
#define URB_CS_ENTRY_SIZE     1
#define URB_CS_ENTRIES	      0

#define URB_VS_ENTRY_SIZE     1
#define URB_VS_ENTRIES	      128 /* minimum of 8 */

#define URB_GS_ENTRY_SIZE     0
#define URB_GS_ENTRIES	      0

#define URB_CLIP_ENTRY_SIZE   0
#define URB_CLIP_ENTRIES      0

#define URB_SF_ENTRY_SIZE     2
#define URB_SF_ENTRIES	      32

/*
 * this program computes dA/dx and dA/dy for the texture coordinates along
 * with the base texture coordinate. It was extracted from the Mesa driver
 */

#define SF_KERNEL_NUM_GRF  16
#define SF_MAX_THREADS	   2

#define PS_KERNEL_NUM_GRF   32
#define PS_MAX_THREADS	    48

static const uint32_t sf_kernel[][4] = {
#include "exa_sf.g5b"
};

static const uint32_t sf_kernel_mask[][4] = {
#include "exa_sf_mask.g5b"
};

static const uint32_t ps_kernel_nomask_affine[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_affine.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_nomask_projective[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_projective.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_maskca_affine[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_affine.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_mask_affine.g5b"
#include "exa_wm_mask_sample_argb.g5b"
#include "exa_wm_ca.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_maskca_projective[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_projective.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_mask_projective.g5b"
#include "exa_wm_mask_sample_argb.g5b"
#include "exa_wm_ca.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_maskca_srcalpha_affine[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_affine.g5b"
#include "exa_wm_src_sample_a.g5b"
#include "exa_wm_mask_affine.g5b"
#include "exa_wm_mask_sample_argb.g5b"
#include "exa_wm_ca_srcalpha.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_maskca_srcalpha_projective[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_projective.g5b"
#include "exa_wm_src_sample_a.g5b"
#include "exa_wm_mask_projective.g5b"
#include "exa_wm_mask_sample_argb.g5b"
#include "exa_wm_ca_srcalpha.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_masknoca_affine[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_affine.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_mask_affine.g5b"
#include "exa_wm_mask_sample_a.g5b"
#include "exa_wm_noca.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_masknoca_projective[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_projective.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_mask_projective.g5b"
#include "exa_wm_mask_sample_a.g5b"
#include "exa_wm_noca.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_packed_static[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_affine.g5b"
#include "exa_wm_src_sample_argb.g5b"
#include "exa_wm_yuv_rgb.g5b"
#include "exa_wm_write.g5b"
};

static const uint32_t ps_kernel_planar_static[][4] = {
#include "exa_wm_xy.g5b"
#include "exa_wm_src_affine.g5b"
#include "exa_wm_src_sample_planar.g5b"
#include "exa_wm_yuv_rgb.g5b"
#include "exa_wm_write.g5b"
};

#define KERNEL(kernel_enum, kernel, masked) \
    [kernel_enum] = {&kernel, sizeof(kernel), masked}
static const struct wm_kernel_info {
	const void *data;
	unsigned int size;
	Bool has_mask;
} wm_kernels[] = {
	KERNEL(WM_KERNEL, ps_kernel_nomask_affine, FALSE),
	KERNEL(WM_KERNEL_PROJECTIVE, ps_kernel_nomask_projective, FALSE),

	KERNEL(WM_KERNEL_MASK, ps_kernel_masknoca_affine, TRUE),
	KERNEL(WM_KERNEL_MASK_PROJECTIVE, ps_kernel_masknoca_projective, TRUE),

	KERNEL(WM_KERNEL_MASKCA, ps_kernel_maskca_affine, TRUE),
	KERNEL(WM_KERNEL_MASKCA_PROJECTIVE, ps_kernel_maskca_projective, TRUE),

	KERNEL(WM_KERNEL_MASKCA_SRCALPHA,
	       ps_kernel_maskca_srcalpha_affine, TRUE),
	KERNEL(WM_KERNEL_MASKCA_SRCALPHA_PROJECTIVE,
	       ps_kernel_maskca_srcalpha_projective, TRUE),

	KERNEL(WM_KERNEL_VIDEO_PLANAR, ps_kernel_planar_static, FALSE),
	KERNEL(WM_KERNEL_VIDEO_PACKED, ps_kernel_packed_static, FALSE),
};
#undef KERNEL

static const struct blendinfo {
	Bool src_alpha;
	uint32_t src_blend;
	uint32_t dst_blend;
} gen5_blend_op[] = {
	/* Clear */	{0, GEN5_BLENDFACTOR_ZERO, GEN5_BLENDFACTOR_ZERO},
	/* Src */	{0, GEN5_BLENDFACTOR_ONE, GEN5_BLENDFACTOR_ZERO},
	/* Dst */	{0, GEN5_BLENDFACTOR_ZERO, GEN5_BLENDFACTOR_ONE},
	/* Over */	{1, GEN5_BLENDFACTOR_ONE, GEN5_BLENDFACTOR_INV_SRC_ALPHA},
	/* OverReverse */ {0, GEN5_BLENDFACTOR_INV_DST_ALPHA, GEN5_BLENDFACTOR_ONE},
	/* In */	{0, GEN5_BLENDFACTOR_DST_ALPHA, GEN5_BLENDFACTOR_ZERO},
	/* InReverse */	{1, GEN5_BLENDFACTOR_ZERO, GEN5_BLENDFACTOR_SRC_ALPHA},
	/* Out */	{0, GEN5_BLENDFACTOR_INV_DST_ALPHA, GEN5_BLENDFACTOR_ZERO},
	/* OutReverse */ {1, GEN5_BLENDFACTOR_ZERO, GEN5_BLENDFACTOR_INV_SRC_ALPHA},
	/* Atop */	{1, GEN5_BLENDFACTOR_DST_ALPHA, GEN5_BLENDFACTOR_INV_SRC_ALPHA},
	/* AtopReverse */ {1, GEN5_BLENDFACTOR_INV_DST_ALPHA, GEN5_BLENDFACTOR_SRC_ALPHA},
	/* Xor */	{1, GEN5_BLENDFACTOR_INV_DST_ALPHA, GEN5_BLENDFACTOR_INV_SRC_ALPHA},
	/* Add */	{0, GEN5_BLENDFACTOR_ONE, GEN5_BLENDFACTOR_ONE},
};

/**
 * Highest-valued BLENDFACTOR used in gen5_blend_op.
 *
 * This leaves out GEN5_BLENDFACTOR_INV_DST_COLOR,
 * GEN5_BLENDFACTOR_INV_CONST_{COLOR,ALPHA},
 * GEN5_BLENDFACTOR_INV_SRC1_{COLOR,ALPHA}
 */
#define GEN5_BLENDFACTOR_COUNT (GEN5_BLENDFACTOR_INV_DST_ALPHA + 1)

/* FIXME: surface format defined in gen5_defines.h, shared Sampling engine
 * 1.7.2
 */
static const struct formatinfo {
	CARD32 pict_fmt;
	uint32_t card_fmt;
} gen5_tex_formats[] = {
	{PICT_a8, GEN5_SURFACEFORMAT_A8_UNORM},
	{PICT_a8r8g8b8, GEN5_SURFACEFORMAT_B8G8R8A8_UNORM},
	{PICT_x8r8g8b8, GEN5_SURFACEFORMAT_B8G8R8X8_UNORM},
	{PICT_a8b8g8r8, GEN5_SURFACEFORMAT_R8G8B8A8_UNORM},
	{PICT_x8b8g8r8, GEN5_SURFACEFORMAT_R8G8B8X8_UNORM},
	{PICT_r8g8b8, GEN5_SURFACEFORMAT_R8G8B8_UNORM},
	{PICT_r5g6b5, GEN5_SURFACEFORMAT_B5G6R5_UNORM},
	{PICT_a1r5g5b5, GEN5_SURFACEFORMAT_B5G5R5A1_UNORM},
	{PICT_a2r10g10b10, GEN5_SURFACEFORMAT_B10G10R10A2_UNORM},
	{PICT_x2r10g10b10, GEN5_SURFACEFORMAT_B10G10R10X2_UNORM},
	{PICT_a2b10g10r10, GEN5_SURFACEFORMAT_R10G10B10A2_UNORM},
	{PICT_x2r10g10b10, GEN5_SURFACEFORMAT_B10G10R10X2_UNORM},
	{PICT_a4r4g4b4, GEN5_SURFACEFORMAT_B4G4R4A4_UNORM},
};

#define BLEND_OFFSET(s, d) \
	(((s) * GEN5_BLENDFACTOR_COUNT + (d)) * 64)

#define SAMPLER_OFFSET(sf, se, mf, me, k) \
	((((((sf) * EXTEND_COUNT + (se)) * FILTER_COUNT + (mf)) * EXTEND_COUNT + (me)) * KERNEL_COUNT + (k)) * 64)

static bool
gen5_emit_pipelined_pointers(struct sna *sna,
			     const struct sna_composite_op *op,
			     int blend, int kernel);

#define OUT_BATCH(v) batch_emit(sna, v)
#define OUT_VERTEX(x,y) vertex_emit_2s(sna, x,y)
#define OUT_VERTEX_F(v) vertex_emit(sna, v)

static inline bool too_large(int width, int height)
{
	return width > MAX_3D_SIZE || height > MAX_3D_SIZE;
}

static int
gen5_choose_composite_kernel(int op, Bool has_mask, Bool is_ca, Bool is_affine)
{
	int base;

	if (has_mask) {
		if (is_ca) {
			if (gen5_blend_op[op].src_alpha)
				base = WM_KERNEL_MASKCA_SRCALPHA;
			else
				base = WM_KERNEL_MASKCA;
		} else
			base = WM_KERNEL_MASK;
	} else
		base = WM_KERNEL;

	return base + !is_affine;
}

static void gen5_magic_ca_pass(struct sna *sna,
			       const struct sna_composite_op *op)
{
	struct gen5_render_state *state = &sna->render_state.gen5;

	if (!op->need_magic_ca_pass)
		return;

	assert(sna->render.vertex_index > sna->render.vertex_start);

	DBG(("%s: CA fixup\n", __FUNCTION__));

	gen5_emit_pipelined_pointers
		(sna, op, PictOpAdd,
		 gen5_choose_composite_kernel(PictOpAdd,
					      TRUE, TRUE, op->is_affine));

	OUT_BATCH(GEN5_3DPRIMITIVE |
		  GEN5_3DPRIMITIVE_VERTEX_SEQUENTIAL |
		  (_3DPRIM_RECTLIST << GEN5_3DPRIMITIVE_TOPOLOGY_SHIFT) |
		  (0 << 9) |
		  4);
	OUT_BATCH(sna->render.vertex_index - sna->render.vertex_start);
	OUT_BATCH(sna->render.vertex_start);
	OUT_BATCH(1);	/* single instance */
	OUT_BATCH(0);	/* start instance location */
	OUT_BATCH(0);	/* index buffer offset, ignored */

	state->last_primitive = sna->kgem.nbatch;
}

static void gen5_vertex_flush(struct sna *sna)
{
	assert(sna->render_state.gen5.vertex_offset);
	assert(sna->render.vertex_index > sna->render.vertex_start);

	DBG(("%s[%x] = %d\n", __FUNCTION__,
	     4*sna->render_state.gen5.vertex_offset,
	     sna->render.vertex_index - sna->render.vertex_start));
	sna->kgem.batch[sna->render_state.gen5.vertex_offset] =
		sna->render.vertex_index - sna->render.vertex_start;
	sna->render_state.gen5.vertex_offset = 0;
}

static int gen5_vertex_finish(struct sna *sna)
{
	struct kgem_bo *bo;
	unsigned int i;

	assert(sna->render.vertex_used);

	/* Note: we only need dword alignment (currently) */

	bo = sna->render.vbo;
	if (bo) {
		if (sna->render_state.gen5.vertex_offset)
			gen5_vertex_flush(sna);

		for (i = 0; i < ARRAY_SIZE(sna->render.vertex_reloc); i++) {
			if (sna->render.vertex_reloc[i]) {
				DBG(("%s: reloc[%d] = %d\n", __FUNCTION__,
				     i, sna->render.vertex_reloc[i]));

				sna->kgem.batch[sna->render.vertex_reloc[i]] =
					kgem_add_reloc(&sna->kgem,
						       sna->render.vertex_reloc[i],
						       bo,
						       I915_GEM_DOMAIN_VERTEX << 16,
						       0);
				sna->kgem.batch[sna->render.vertex_reloc[i]+1] =
					kgem_add_reloc(&sna->kgem,
						       sna->render.vertex_reloc[i]+1,
						       bo,
						       I915_GEM_DOMAIN_VERTEX << 16,
						       sna->render.vertex_used * 4 - 1);
				sna->render.vertex_reloc[i] = 0;
			}
		}

		sna->render.vertex_used = 0;
		sna->render.vertex_index = 0;
		sna->render_state.gen5.vb_id = 0;

		kgem_bo_destroy(&sna->kgem, bo);
	}

	sna->render.vertices = NULL;
	sna->render.vbo = kgem_create_linear(&sna->kgem, 256*1024);
	if (sna->render.vbo)
		sna->render.vertices = kgem_bo_map(&sna->kgem, sna->render.vbo);
	if (sna->render.vertices == NULL) {
		kgem_bo_destroy(&sna->kgem, sna->render.vbo);
		sna->render.vbo = NULL;
		return 0;
	}

	if (sna->render.vertex_used) {
		memcpy(sna->render.vertices,
		       sna->render.vertex_data,
		       sizeof(float)*sna->render.vertex_used);
	}
	sna->render.vertex_size = 64 * 1024 - 1;
	return sna->render.vertex_size - sna->render.vertex_used;
}

static void gen5_vertex_close(struct sna *sna)
{
	struct kgem_bo *bo, *free_bo = NULL;
	unsigned int i, delta = 0;

	assert(sna->render_state.gen5.vertex_offset == 0);

	DBG(("%s: used=%d, vbo active? %d\n",
	     __FUNCTION__, sna->render.vertex_used, sna->render.vbo != NULL));

	if (!sna->render.vertex_used) {
		assert(sna->render.vbo == NULL);
		assert(sna->render.vertices == sna->render.vertex_data);
		assert(sna->render.vertex_size == ARRAY_SIZE(sna->render.vertex_data));
		return;
	}

	bo = sna->render.vbo;
	if (bo) {
		if (IS_CPU_MAP(bo->map) ||
		    sna->render.vertex_size - sna->render.vertex_used < 64) {
			DBG(("%s: discarding vbo (was CPU mapped)\n",
			     __FUNCTION__));
			sna->render.vbo = NULL;
			sna->render.vertices = sna->render.vertex_data;
			sna->render.vertex_size = ARRAY_SIZE(sna->render.vertex_data);
			free_bo = bo;
		}
	} else {
		if (sna->kgem.nbatch + sna->render.vertex_used <= sna->kgem.surface) {
			DBG(("%s: copy to batch: %d @ %d\n", __FUNCTION__,
			     sna->render.vertex_used, sna->kgem.nbatch));
			memcpy(sna->kgem.batch + sna->kgem.nbatch,
			       sna->render.vertex_data,
			       sna->render.vertex_used * 4);
			delta = sna->kgem.nbatch * 4;
			bo = NULL;
			sna->kgem.nbatch += sna->render.vertex_used;
		} else {
			bo = kgem_create_linear(&sna->kgem, 4*sna->render.vertex_used);
			if (bo && !kgem_bo_write(&sna->kgem, bo,
						 sna->render.vertex_data,
						 4*sna->render.vertex_used)) {
				kgem_bo_destroy(&sna->kgem, bo);
				bo = NULL;
			}
			DBG(("%s: new vbo: %d\n", __FUNCTION__,
			     sna->render.vertex_used));
			free_bo = bo;
		}
	}

	for (i = 0; i < ARRAY_SIZE(sna->render.vertex_reloc); i++) {
		if (sna->render.vertex_reloc[i]) {
			DBG(("%s: reloc[%d] = %d\n", __FUNCTION__,
			     i, sna->render.vertex_reloc[i]));

			sna->kgem.batch[sna->render.vertex_reloc[i]] =
				kgem_add_reloc(&sna->kgem,
					       sna->render.vertex_reloc[i],
					       bo,
					       I915_GEM_DOMAIN_VERTEX << 16,
					       delta);
			sna->kgem.batch[sna->render.vertex_reloc[i]+1] =
				kgem_add_reloc(&sna->kgem,
					       sna->render.vertex_reloc[i]+1,
					       bo,
					       I915_GEM_DOMAIN_VERTEX << 16,
					       delta + sna->render.vertex_used * 4 - 1);
			sna->render.vertex_reloc[i] = 0;
		}
	}

	if (sna->render.vbo == NULL) {
		sna->render.vertex_used = 0;
		sna->render.vertex_index = 0;
	}

	if (free_bo)
		kgem_bo_destroy(&sna->kgem, free_bo);
}

static uint32_t gen5_get_blend(int op,
			       Bool has_component_alpha,
			       uint32_t dst_format)
{
	uint32_t src, dst;

	src = gen5_blend_op[op].src_blend;
	dst = gen5_blend_op[op].dst_blend;

	/* If there's no dst alpha channel, adjust the blend op so that we'll treat
	 * it as always 1.
	 */
	if (PICT_FORMAT_A(dst_format) == 0) {
		if (src == GEN5_BLENDFACTOR_DST_ALPHA)
			src = GEN5_BLENDFACTOR_ONE;
		else if (src == GEN5_BLENDFACTOR_INV_DST_ALPHA)
			src = GEN5_BLENDFACTOR_ZERO;
	}

	/* If the source alpha is being used, then we should only be in a
	 * case where the source blend factor is 0, and the source blend
	 * value is the mask channels multiplied by the source picture's alpha.
	 */
	if (has_component_alpha && gen5_blend_op[op].src_alpha) {
		if (dst == GEN5_BLENDFACTOR_SRC_ALPHA)
			dst = GEN5_BLENDFACTOR_SRC_COLOR;
		else if (dst == GEN5_BLENDFACTOR_INV_SRC_ALPHA)
			dst = GEN5_BLENDFACTOR_INV_SRC_COLOR;
	}

	DBG(("blend op=%d, dst=%x [A=%d] => src=%d, dst=%d => offset=%x\n",
	     op, dst_format, PICT_FORMAT_A(dst_format),
	     src, dst, BLEND_OFFSET(src, dst)));
	return BLEND_OFFSET(src, dst);
}

static uint32_t gen5_get_dest_format(PictFormat format)
{
	switch (format) {
	case PICT_a8r8g8b8:
	case PICT_x8r8g8b8:
	default:
		return GEN5_SURFACEFORMAT_B8G8R8A8_UNORM;
	case PICT_a8b8g8r8:
	case PICT_x8b8g8r8:
		return GEN5_SURFACEFORMAT_R8G8B8A8_UNORM;
	case PICT_a2r10g10b10:
	case PICT_x2r10g10b10:
		return GEN5_SURFACEFORMAT_B10G10R10A2_UNORM;
	case PICT_r5g6b5:
		return GEN5_SURFACEFORMAT_B5G6R5_UNORM;
	case PICT_x1r5g5b5:
	case PICT_a1r5g5b5:
		return GEN5_SURFACEFORMAT_B5G5R5A1_UNORM;
	case PICT_a8:
		return GEN5_SURFACEFORMAT_A8_UNORM;
	case PICT_a4r4g4b4:
	case PICT_x4r4g4b4:
		return GEN5_SURFACEFORMAT_B4G4R4A4_UNORM;
	}
}

static Bool gen5_check_dst_format(PictFormat format)
{
	switch (format) {
	case PICT_a8r8g8b8:
	case PICT_x8r8g8b8:
	case PICT_a8b8g8r8:
	case PICT_x8b8g8r8:
	case PICT_a2r10g10b10:
	case PICT_x2r10g10b10:
	case PICT_r5g6b5:
	case PICT_x1r5g5b5:
	case PICT_a1r5g5b5:
	case PICT_a8:
	case PICT_a4r4g4b4:
	case PICT_x4r4g4b4:
		return TRUE;
	default:
		DBG(("%s: unhandled format: %x\n", __FUNCTION__, (int)format));
		return FALSE;
	}
}

static bool gen5_check_format(uint32_t format)
{
	switch (format) {
	case PICT_a8r8g8b8:
	case PICT_x8r8g8b8:
	case PICT_a8b8g8r8:
	case PICT_x8b8g8r8:
	case PICT_a2r10g10b10:
	case PICT_x2r10g10b10:
	case PICT_r8g8b8:
	case PICT_r5g6b5:
	case PICT_a1r5g5b5:
	case PICT_a8:
	case PICT_a4r4g4b4:
	case PICT_x4r4g4b4:
		return true;
	default:
		DBG(("%s: unhandled format: %x\n", __FUNCTION__, format));
		return false;
	}
}

typedef struct gen5_surface_state_padded {
	struct gen5_surface_state state;
	char pad[32 - sizeof(struct gen5_surface_state)];
} gen5_surface_state_padded;

static void null_create(struct sna_static_stream *stream)
{
	/* A bunch of zeros useful for legacy border color and depth-stencil */
	sna_static_stream_map(stream, 64, 64);
}

static void
sampler_state_init(struct gen5_sampler_state *sampler_state,
		   sampler_filter_t filter,
		   sampler_extend_t extend)
{
	sampler_state->ss0.lod_preclamp = 1;	/* GL mode */

	/* We use the legacy mode to get the semantics specified by
	 * the Render extension. */
	sampler_state->ss0.border_color_mode = GEN5_BORDER_COLOR_MODE_LEGACY;

	switch (filter) {
	default:
	case SAMPLER_FILTER_NEAREST:
		sampler_state->ss0.min_filter = GEN5_MAPFILTER_NEAREST;
		sampler_state->ss0.mag_filter = GEN5_MAPFILTER_NEAREST;
		break;
	case SAMPLER_FILTER_BILINEAR:
		sampler_state->ss0.min_filter = GEN5_MAPFILTER_LINEAR;
		sampler_state->ss0.mag_filter = GEN5_MAPFILTER_LINEAR;
		break;
	}

	switch (extend) {
	default:
	case SAMPLER_EXTEND_NONE:
		sampler_state->ss1.r_wrap_mode = GEN5_TEXCOORDMODE_CLAMP_BORDER;
		sampler_state->ss1.s_wrap_mode = GEN5_TEXCOORDMODE_CLAMP_BORDER;
		sampler_state->ss1.t_wrap_mode = GEN5_TEXCOORDMODE_CLAMP_BORDER;
		break;
	case SAMPLER_EXTEND_REPEAT:
		sampler_state->ss1.r_wrap_mode = GEN5_TEXCOORDMODE_WRAP;
		sampler_state->ss1.s_wrap_mode = GEN5_TEXCOORDMODE_WRAP;
		sampler_state->ss1.t_wrap_mode = GEN5_TEXCOORDMODE_WRAP;
		break;
	case SAMPLER_EXTEND_PAD:
		sampler_state->ss1.r_wrap_mode = GEN5_TEXCOORDMODE_CLAMP;
		sampler_state->ss1.s_wrap_mode = GEN5_TEXCOORDMODE_CLAMP;
		sampler_state->ss1.t_wrap_mode = GEN5_TEXCOORDMODE_CLAMP;
		break;
	case SAMPLER_EXTEND_REFLECT:
		sampler_state->ss1.r_wrap_mode = GEN5_TEXCOORDMODE_MIRROR;
		sampler_state->ss1.s_wrap_mode = GEN5_TEXCOORDMODE_MIRROR;
		sampler_state->ss1.t_wrap_mode = GEN5_TEXCOORDMODE_MIRROR;
		break;
	}
}

static uint32_t gen5_get_card_format(PictFormat format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(gen5_tex_formats); i++) {
		if (gen5_tex_formats[i].pict_fmt == format)
			return gen5_tex_formats[i].card_fmt;
	}
	return -1;
}

static uint32_t gen5_filter(uint32_t filter)
{
	switch (filter) {
	default:
		assert(0);
	case PictFilterNearest:
		return SAMPLER_FILTER_NEAREST;
	case PictFilterBilinear:
		return SAMPLER_FILTER_BILINEAR;
	}
}

static uint32_t gen5_check_filter(PicturePtr picture)
{
	switch (picture->filter) {
	case PictFilterNearest:
	case PictFilterBilinear:
		return TRUE;
	default:
		DBG(("%s: unknown filter: %x\n", __FUNCTION__, picture->filter));
		return FALSE;
	}
}

static uint32_t gen5_repeat(uint32_t repeat)
{
	switch (repeat) {
	default:
		assert(0);
	case RepeatNone:
		return SAMPLER_EXTEND_NONE;
	case RepeatNormal:
		return SAMPLER_EXTEND_REPEAT;
	case RepeatPad:
		return SAMPLER_EXTEND_PAD;
	case RepeatReflect:
		return SAMPLER_EXTEND_REFLECT;
	}
}

static bool gen5_check_repeat(PicturePtr picture)
{
	if (!picture->repeat)
		return TRUE;

	switch (picture->repeatType) {
	case RepeatNone:
	case RepeatNormal:
	case RepeatPad:
	case RepeatReflect:
		return TRUE;
	default:
		DBG(("%s: unknown repeat: %x\n",
		     __FUNCTION__, picture->repeatType));
		return FALSE;
	}
}

static uint32_t
gen5_tiling_bits(uint32_t tiling)
{
	switch (tiling) {
	default: assert(0);
	case I915_TILING_NONE: return 0;
	case I915_TILING_X: return GEN5_SURFACE_TILED;
	case I915_TILING_Y: return GEN5_SURFACE_TILED | GEN5_SURFACE_TILED_Y;
	}
}

/**
 * Sets up the common fields for a surface state buffer for the given
 * picture in the given surface state buffer.
 */
static int
gen5_bind_bo(struct sna *sna,
	     struct kgem_bo *bo,
	     uint32_t width,
	     uint32_t height,
	     uint32_t format,
	     Bool is_dst)
{
	uint32_t domains;
	uint16_t offset;
	uint32_t *ss;

	/* After the first bind, we manage the cache domains within the batch */
	if (is_dst) {
		domains = I915_GEM_DOMAIN_RENDER << 16 | I915_GEM_DOMAIN_RENDER;
		kgem_bo_mark_dirty(bo);
	} else
		domains = I915_GEM_DOMAIN_SAMPLER << 16;

	if (!DBG_NO_SURFACE_CACHE) {
		offset = kgem_bo_get_binding(bo, format);
		if (offset)
			return offset;
	}

	offset = sna->kgem.surface - sizeof(struct gen5_surface_state_padded) / sizeof(uint32_t);
	offset *= sizeof(uint32_t);

	sna->kgem.surface -=
		sizeof(struct gen5_surface_state_padded) / sizeof(uint32_t);
	ss = sna->kgem.batch + sna->kgem.surface;

	ss[0] = (GEN5_SURFACE_2D << GEN5_SURFACE_TYPE_SHIFT |
		 GEN5_SURFACE_BLEND_ENABLED |
		 format << GEN5_SURFACE_FORMAT_SHIFT);

	ss[1] = kgem_add_reloc(&sna->kgem,
			       sna->kgem.surface + 1,
			       bo, domains, 0);

	ss[2] = ((width - 1)  << GEN5_SURFACE_WIDTH_SHIFT |
		 (height - 1) << GEN5_SURFACE_HEIGHT_SHIFT);
	ss[3] = (gen5_tiling_bits(bo->tiling) |
		 (bo->pitch - 1) << GEN5_SURFACE_PITCH_SHIFT);
	ss[4] = 0;
	ss[5] = 0;

	kgem_bo_set_binding(bo, format, offset);

	DBG(("[%x] bind bo(handle=%d, addr=%d), format=%d, width=%d, height=%d, pitch=%d, tiling=%d -> %s\n",
	     offset, bo->handle, ss[1],
	     format, width, height, bo->pitch, bo->tiling,
	     domains & 0xffff ? "render" : "sampler"));

	return offset;
}

fastcall static void
gen5_emit_composite_primitive_solid(struct sna *sna,
				    const struct sna_composite_op *op,
				    const struct sna_composite_rectangles *r)
{
	float *v;
	union {
		struct sna_coordinate p;
		float f;
	} dst;

	v = sna->render.vertices + sna->render.vertex_used;
	sna->render.vertex_used += 9;

	dst.p.x = r->dst.x + r->width;
	dst.p.y = r->dst.y + r->height;
	v[0] = dst.f;
	v[1] = 1.;
	v[2] = 1.;

	dst.p.x = r->dst.x;
	v[3] = dst.f;
	v[4] = 0.;
	v[5] = 1.;

	dst.p.y = r->dst.y;
	v[6] = dst.f;
	v[7] = 0.;
	v[8] = 0.;
}

fastcall static void
gen5_emit_composite_primitive_identity_source(struct sna *sna,
					      const struct sna_composite_op *op,
					      const struct sna_composite_rectangles *r)
{
	const float *sf = op->src.scale;
	float sx, sy, *v;
	union {
		struct sna_coordinate p;
		float f;
	} dst;

	v = sna->render.vertices + sna->render.vertex_used;
	sna->render.vertex_used += 9;

	sx = r->src.x + op->src.offset[0];
	sy = r->src.y + op->src.offset[1];

	dst.p.x = r->dst.x + r->width;
	dst.p.y = r->dst.y + r->height;
	v[0] = dst.f;
	v[1] = (sx + r->width) * sf[0];
	v[5] = v[2] = (sy + r->height) * sf[1];

	dst.p.x = r->dst.x;
	v[3] = dst.f;
	v[7] = v[4] = sx * sf[0];

	dst.p.y = r->dst.y;
	v[6] = dst.f;
	v[8] = sy * sf[1];
}

fastcall static void
gen5_emit_composite_primitive_affine_source(struct sna *sna,
					    const struct sna_composite_op *op,
					    const struct sna_composite_rectangles *r)
{
	union {
		struct sna_coordinate p;
		float f;
	} dst;
	float *v;

	v = sna->render.vertices + sna->render.vertex_used;
	sna->render.vertex_used += 9;

	dst.p.x = r->dst.x + r->width;
	dst.p.y = r->dst.y + r->height;
	v[0] = dst.f;
	_sna_get_transformed_coordinates(op->src.offset[0] + r->src.x + r->width,
					 op->src.offset[1] + r->src.y + r->height,
					 op->src.transform,
					 &v[1], &v[2]);
	v[1] *= op->src.scale[0];
	v[2] *= op->src.scale[1];

	dst.p.x = r->dst.x;
	v[3] = dst.f;
	_sna_get_transformed_coordinates(op->src.offset[0] + r->src.x,
					 op->src.offset[1] + r->src.y + r->height,
					 op->src.transform,
					 &v[4], &v[5]);
	v[4] *= op->src.scale[0];
	v[5] *= op->src.scale[1];

	dst.p.y = r->dst.y;
	v[6] = dst.f;
	_sna_get_transformed_coordinates(op->src.offset[0] + r->src.x,
					 op->src.offset[1] + r->src.y,
					 op->src.transform,
					 &v[7], &v[8]);
	v[7] *= op->src.scale[0];
	v[8] *= op->src.scale[1];
}

fastcall static void
gen5_emit_composite_primitive_identity_source_mask(struct sna *sna,
						   const struct sna_composite_op *op,
						   const struct sna_composite_rectangles *r)
{
	union {
		struct sna_coordinate p;
		float f;
	} dst;
	float src_x, src_y;
	float msk_x, msk_y;
	float w, h;
	float *v;

	src_x = r->src.x + op->src.offset[0];
	src_y = r->src.y + op->src.offset[1];
	msk_x = r->mask.x + op->mask.offset[0];
	msk_y = r->mask.y + op->mask.offset[1];
	w = r->width;
	h = r->height;

	v = sna->render.vertices + sna->render.vertex_used;
	sna->render.vertex_used += 15;

	dst.p.x = r->dst.x + r->width;
	dst.p.y = r->dst.y + r->height;
	v[0] = dst.f;
	v[1] = (src_x + w) * op->src.scale[0];
	v[2] = (src_y + h) * op->src.scale[1];
	v[3] = (msk_x + w) * op->mask.scale[0];
	v[4] = (msk_y + h) * op->mask.scale[1];

	dst.p.x = r->dst.x;
	v[5] = dst.f;
	v[6] = src_x * op->src.scale[0];
	v[7] = v[2];
	v[8] = msk_x * op->mask.scale[0];
	v[9] = v[4];

	dst.p.y = r->dst.y;
	v[10] = dst.f;
	v[11] = v[6];
	v[12] = src_y * op->src.scale[1];
	v[13] = v[8];
	v[14] = msk_y * op->mask.scale[1];
}

fastcall static void
gen5_emit_composite_primitive(struct sna *sna,
			      const struct sna_composite_op *op,
			      const struct sna_composite_rectangles *r)
{
	float src_x[3], src_y[3], src_w[3], mask_x[3], mask_y[3], mask_w[3];
	Bool is_affine = op->is_affine;
	const float *src_sf = op->src.scale;
	const float *mask_sf = op->mask.scale;

	if (is_affine) {
		sna_get_transformed_coordinates(r->src.x + op->src.offset[0],
						r->src.y + op->src.offset[1],
						op->src.transform,
						&src_x[0],
						&src_y[0]);

		sna_get_transformed_coordinates(r->src.x + op->src.offset[0],
						r->src.y + op->src.offset[1] + r->height,
						op->src.transform,
						&src_x[1],
						&src_y[1]);

		sna_get_transformed_coordinates(r->src.x + op->src.offset[0] + r->width,
						r->src.y + op->src.offset[1] + r->height,
						op->src.transform,
						&src_x[2],
						&src_y[2]);
	} else {
		if (!sna_get_transformed_coordinates_3d(r->src.x + op->src.offset[0],
							r->src.y + op->src.offset[1],
							op->src.transform,
							&src_x[0],
							&src_y[0],
							&src_w[0]))
			return;

		if (!sna_get_transformed_coordinates_3d(r->src.x + op->src.offset[0],
							r->src.y + op->src.offset[1] + r->height,
							op->src.transform,
							&src_x[1],
							&src_y[1],
							&src_w[1]))
			return;

		if (!sna_get_transformed_coordinates_3d(r->src.x + op->src.offset[0] + r->width,
							r->src.y + op->src.offset[1] + r->height,
							op->src.transform,
							&src_x[2],
							&src_y[2],
							&src_w[2]))
			return;
	}

	if (op->mask.bo) {
		if (is_affine) {
			sna_get_transformed_coordinates(r->mask.x + op->mask.offset[0],
							r->mask.y + op->mask.offset[1],
							op->mask.transform,
							&mask_x[0],
							&mask_y[0]);

			sna_get_transformed_coordinates(r->mask.x + op->mask.offset[0],
							r->mask.y + op->mask.offset[1] + r->height,
							op->mask.transform,
							&mask_x[1],
							&mask_y[1]);

			sna_get_transformed_coordinates(r->mask.x + op->mask.offset[0] + r->width,
							r->mask.y + op->mask.offset[1] + r->height,
							op->mask.transform,
							&mask_x[2],
							&mask_y[2]);
		} else {
			if (!sna_get_transformed_coordinates_3d(r->mask.x + op->mask.offset[0],
								r->mask.y + op->mask.offset[1],
								op->mask.transform,
								&mask_x[0],
								&mask_y[0],
								&mask_w[0]))
				return;

			if (!sna_get_transformed_coordinates_3d(r->mask.x + op->mask.offset[0],
								r->mask.y + op->mask.offset[1] + r->height,
								op->mask.transform,
								&mask_x[1],
								&mask_y[1],
								&mask_w[1]))
				return;

			if (!sna_get_transformed_coordinates_3d(r->mask.x + op->mask.offset[0] + r->width,
								r->mask.y + op->mask.offset[1] + r->height,
								op->mask.transform,
								&mask_x[2],
								&mask_y[2],
								&mask_w[2]))
				return;
		}
	}

	OUT_VERTEX(r->dst.x + r->width, r->dst.y + r->height);
	OUT_VERTEX_F(src_x[2] * src_sf[0]);
	OUT_VERTEX_F(src_y[2] * src_sf[1]);
	if (!is_affine)
		OUT_VERTEX_F(src_w[2]);
	if (op->mask.bo) {
		OUT_VERTEX_F(mask_x[2] * mask_sf[0]);
		OUT_VERTEX_F(mask_y[2] * mask_sf[1]);
		if (!is_affine)
			OUT_VERTEX_F(mask_w[2]);
	}

	OUT_VERTEX(r->dst.x, r->dst.y + r->height);
	OUT_VERTEX_F(src_x[1] * src_sf[0]);
	OUT_VERTEX_F(src_y[1] * src_sf[1]);
	if (!is_affine)
		OUT_VERTEX_F(src_w[1]);
	if (op->mask.bo) {
		OUT_VERTEX_F(mask_x[1] * mask_sf[0]);
		OUT_VERTEX_F(mask_y[1] * mask_sf[1]);
		if (!is_affine)
			OUT_VERTEX_F(mask_w[1]);
	}

	OUT_VERTEX(r->dst.x, r->dst.y);
	OUT_VERTEX_F(src_x[0] * src_sf[0]);
	OUT_VERTEX_F(src_y[0] * src_sf[1]);
	if (!is_affine)
		OUT_VERTEX_F(src_w[0]);
	if (op->mask.bo) {
		OUT_VERTEX_F(mask_x[0] * mask_sf[0]);
		OUT_VERTEX_F(mask_y[0] * mask_sf[1]);
		if (!is_affine)
			OUT_VERTEX_F(mask_w[0]);
	}
}

static void gen5_emit_vertex_buffer(struct sna *sna,
				    const struct sna_composite_op *op)
{
	int id = op->u.gen5.ve_id;

	assert((unsigned)id <= 3);

	OUT_BATCH(GEN5_3DSTATE_VERTEX_BUFFERS | 3);
	OUT_BATCH((id << VB0_BUFFER_INDEX_SHIFT) | VB0_VERTEXDATA |
		  (4*op->floats_per_vertex << VB0_BUFFER_PITCH_SHIFT));
	sna->render.vertex_reloc[id] = sna->kgem.nbatch;
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	sna->render_state.gen5.vb_id |= 1 << id;
}

static void gen5_emit_primitive(struct sna *sna)
{
	if (sna->kgem.nbatch == sna->render_state.gen5.last_primitive) {
		sna->render_state.gen5.vertex_offset = sna->kgem.nbatch - 5;
		return;
	}

	OUT_BATCH(GEN5_3DPRIMITIVE |
		  GEN5_3DPRIMITIVE_VERTEX_SEQUENTIAL |
		  (_3DPRIM_RECTLIST << GEN5_3DPRIMITIVE_TOPOLOGY_SHIFT) |
		  (0 << 9) |
		  4);
	sna->render_state.gen5.vertex_offset = sna->kgem.nbatch;
	OUT_BATCH(0);	/* vertex count, to be filled in later */
	OUT_BATCH(sna->render.vertex_index);
	OUT_BATCH(1);	/* single instance */
	OUT_BATCH(0);	/* start instance location */
	OUT_BATCH(0);	/* index buffer offset, ignored */
	sna->render.vertex_start = sna->render.vertex_index;

	sna->render_state.gen5.last_primitive = sna->kgem.nbatch;
}

static bool gen5_rectangle_begin(struct sna *sna,
				 const struct sna_composite_op *op)
{
	int id = op->u.gen5.ve_id;
	int ndwords;

	assert((unsigned)id <= 3);

	ndwords = op->need_magic_ca_pass ? 20 : 6;
	if ((sna->render_state.gen5.vb_id & (1 << id)) == 0)
		ndwords += 5;

	if (!kgem_check_batch(&sna->kgem, ndwords))
		return false;

	if ((sna->render_state.gen5.vb_id & (1 << id)) == 0)
		gen5_emit_vertex_buffer(sna, op);
	if (sna->render_state.gen5.vertex_offset == 0)
		gen5_emit_primitive(sna);

	return true;
}

static int gen5_get_rectangles__flush(struct sna *sna,
				      const struct sna_composite_op *op)
{
	if (!kgem_check_batch(&sna->kgem, op->need_magic_ca_pass ? 20 : 6))
		return 0;
	if (sna->kgem.nexec > KGEM_EXEC_SIZE(&sna->kgem) - 1)
		return 0;
	if (sna->kgem.nreloc > KGEM_RELOC_SIZE(&sna->kgem) - 2)
		return 0;

	if (op->need_magic_ca_pass && sna->render.vbo)
		return 0;

	return gen5_vertex_finish(sna);
}

inline static int gen5_get_rectangles(struct sna *sna,
				      const struct sna_composite_op *op,
				      int want,
				      void (*emit_state)(struct sna *sna,
							 const struct sna_composite_op *op))
{
	int rem;

start:
	rem = vertex_space(sna);
	if (rem < op->floats_per_rect) {
		DBG(("flushing vbo for %s: %d < %d\n",
		     __FUNCTION__, rem, op->floats_per_rect));
		rem = gen5_get_rectangles__flush(sna, op);
		if (unlikely (rem == 0))
			goto flush;
	}

	if (unlikely(sna->render_state.gen5.vertex_offset == 0 &&
		     !gen5_rectangle_begin(sna, op)))
		goto flush;

	if (want * op->floats_per_rect > rem)
		want = rem / op->floats_per_rect;

	sna->render.vertex_index += 3*want;
	return want;

flush:
	if (sna->render_state.gen5.vertex_offset) {
		gen5_vertex_flush(sna);
		gen5_magic_ca_pass(sna, op);
	}
	_kgem_submit(&sna->kgem);
	emit_state(sna, op);
	goto start;
}

static uint32_t *
gen5_composite_get_binding_table(struct sna *sna,
				 uint16_t *offset)
{
	uint32_t *table;

	sna->kgem.surface -=
		sizeof(struct gen5_surface_state_padded) / sizeof(uint32_t);
	/* Clear all surplus entries to zero in case of prefetch */
	table = memset(sna->kgem.batch + sna->kgem.surface,
		       0, sizeof(struct gen5_surface_state_padded));
	*offset = sna->kgem.surface;

	DBG(("%s(%x)\n", __FUNCTION__, 4*sna->kgem.surface));

	return table;
}

static void
gen5_emit_urb(struct sna *sna)
{
	int urb_vs_start, urb_vs_size;
	int urb_gs_start, urb_gs_size;
	int urb_clip_start, urb_clip_size;
	int urb_sf_start, urb_sf_size;
	int urb_cs_start, urb_cs_size;

	urb_vs_start = 0;
	urb_vs_size = URB_VS_ENTRIES * URB_VS_ENTRY_SIZE;
	urb_gs_start = urb_vs_start + urb_vs_size;
	urb_gs_size = URB_GS_ENTRIES * URB_GS_ENTRY_SIZE;
	urb_clip_start = urb_gs_start + urb_gs_size;
	urb_clip_size = URB_CLIP_ENTRIES * URB_CLIP_ENTRY_SIZE;
	urb_sf_start = urb_clip_start + urb_clip_size;
	urb_sf_size = URB_SF_ENTRIES * URB_SF_ENTRY_SIZE;
	urb_cs_start = urb_sf_start + urb_sf_size;
	urb_cs_size = URB_CS_ENTRIES * URB_CS_ENTRY_SIZE;

	OUT_BATCH(GEN5_URB_FENCE |
		  UF0_CS_REALLOC |
		  UF0_SF_REALLOC |
		  UF0_CLIP_REALLOC |
		  UF0_GS_REALLOC |
		  UF0_VS_REALLOC |
		  1);
	OUT_BATCH(((urb_clip_start + urb_clip_size) << UF1_CLIP_FENCE_SHIFT) |
		  ((urb_gs_start + urb_gs_size) << UF1_GS_FENCE_SHIFT) |
		  ((urb_vs_start + urb_vs_size) << UF1_VS_FENCE_SHIFT));
	OUT_BATCH(((urb_cs_start + urb_cs_size) << UF2_CS_FENCE_SHIFT) |
		  ((urb_sf_start + urb_sf_size) << UF2_SF_FENCE_SHIFT));

	/* Constant buffer state */
	OUT_BATCH(GEN5_CS_URB_STATE | 0);
	OUT_BATCH((URB_CS_ENTRY_SIZE - 1) << 4 | URB_CS_ENTRIES << 0);
}

static void
gen5_emit_state_base_address(struct sna *sna)
{
	assert(sna->render_state.gen5.general_bo->proxy == NULL);
	OUT_BATCH(GEN5_STATE_BASE_ADDRESS | 6);
	OUT_BATCH(kgem_add_reloc(&sna->kgem, /* general */
				 sna->kgem.nbatch,
				 sna->render_state.gen5.general_bo,
				 I915_GEM_DOMAIN_INSTRUCTION << 16,
				 BASE_ADDRESS_MODIFY));
	OUT_BATCH(kgem_add_reloc(&sna->kgem, /* surface */
				 sna->kgem.nbatch,
				 NULL,
				 I915_GEM_DOMAIN_INSTRUCTION << 16,
				 BASE_ADDRESS_MODIFY));
	OUT_BATCH(0); /* media */
	OUT_BATCH(kgem_add_reloc(&sna->kgem, /* instruction */
				 sna->kgem.nbatch,
				 sna->render_state.gen5.general_bo,
				 I915_GEM_DOMAIN_INSTRUCTION << 16,
				 BASE_ADDRESS_MODIFY));

	/* upper bounds, all disabled */
	OUT_BATCH(BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);
	OUT_BATCH(BASE_ADDRESS_MODIFY);
}

static void
gen5_emit_invariant(struct sna *sna)
{
	/* Ironlake errata workaround: Before disabling the clipper,
	 * you have to MI_FLUSH to get the pipeline idle.
	 *
	 * However, the kernel flushes the pipeline between batches,
	 * so we should be safe....
	 * OUT_BATCH(MI_FLUSH | MI_INHIBIT_RENDER_CACHE_FLUSH);
	 */
	OUT_BATCH(GEN5_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	gen5_emit_state_base_address(sna);

	sna->render_state.gen5.needs_invariant = FALSE;
}

static void
gen5_get_batch(struct sna *sna)
{
	kgem_set_mode(&sna->kgem, KGEM_RENDER);

	if (!kgem_check_batch_with_surfaces(&sna->kgem, 150, 4)) {
		DBG(("%s: flushing batch: %d < %d+%d\n",
		     __FUNCTION__, sna->kgem.surface - sna->kgem.nbatch,
		     150, 4*8));
		kgem_submit(&sna->kgem);
		_kgem_set_mode(&sna->kgem, KGEM_RENDER);
	}

	if (sna->render_state.gen5.needs_invariant)
		gen5_emit_invariant(sna);
}

static void
gen5_align_vertex(struct sna *sna, const struct sna_composite_op *op)
{
	if (op->floats_per_vertex != sna->render_state.gen5.floats_per_vertex) {
		if (sna->render.vertex_size - sna->render.vertex_used < 2*op->floats_per_rect)
			gen5_vertex_finish(sna);

		DBG(("aligning vertex: was %d, now %d floats per vertex, %d->%d\n",
		     sna->render_state.gen5.floats_per_vertex,
		     op->floats_per_vertex,
		     sna->render.vertex_index,
		     (sna->render.vertex_used + op->floats_per_vertex - 1) / op->floats_per_vertex));
		sna->render.vertex_index = (sna->render.vertex_used + op->floats_per_vertex - 1) / op->floats_per_vertex;
		sna->render.vertex_used = sna->render.vertex_index * op->floats_per_vertex;
		sna->render_state.gen5.floats_per_vertex = op->floats_per_vertex;
	}
}

static void
gen5_emit_binding_table(struct sna *sna, uint16_t offset)
{
	if (!DBG_NO_STATE_CACHE &&
	    sna->render_state.gen5.surface_table == offset)
		return;

	sna->render_state.gen5.surface_table = offset;

	/* Binding table pointers */
	OUT_BATCH(GEN5_3DSTATE_BINDING_TABLE_POINTERS | 4);
	OUT_BATCH(0);		/* vs */
	OUT_BATCH(0);		/* gs */
	OUT_BATCH(0);		/* clip */
	OUT_BATCH(0);		/* sf */
	/* Only the PS uses the binding table */
	OUT_BATCH(offset*4);
}

static bool
gen5_emit_pipelined_pointers(struct sna *sna,
			     const struct sna_composite_op *op,
			     int blend, int kernel)
{
	uint16_t offset = sna->kgem.nbatch, last;

	OUT_BATCH(GEN5_3DSTATE_PIPELINED_POINTERS | 5);
	OUT_BATCH(sna->render_state.gen5.vs);
	OUT_BATCH(GEN5_GS_DISABLE); /* passthrough */
	OUT_BATCH(GEN5_CLIP_DISABLE); /* passthrough */
	OUT_BATCH(sna->render_state.gen5.sf[op->mask.bo != NULL]);
	OUT_BATCH(sna->render_state.gen5.wm +
		  SAMPLER_OFFSET(op->src.filter, op->src.repeat,
				 op->mask.filter, op->mask.repeat,
				 kernel));
	OUT_BATCH(sna->render_state.gen5.cc +
		  gen5_get_blend(blend, op->has_component_alpha, op->dst.format));

	last = sna->render_state.gen5.last_pipelined_pointers;
	if (!DBG_NO_STATE_CACHE && last &&
	    sna->kgem.batch[offset + 1] == sna->kgem.batch[last + 1] &&
	    sna->kgem.batch[offset + 3] == sna->kgem.batch[last + 3] &&
	    sna->kgem.batch[offset + 4] == sna->kgem.batch[last + 4] &&
	    sna->kgem.batch[offset + 5] == sna->kgem.batch[last + 5] &&
	    sna->kgem.batch[offset + 6] == sna->kgem.batch[last + 6]) {
		sna->kgem.nbatch = offset;
		return false;
	} else {
		sna->render_state.gen5.last_pipelined_pointers = offset;
		return true;
	}
}

static void
gen5_emit_drawing_rectangle(struct sna *sna, const struct sna_composite_op *op)
{
	uint32_t limit = (op->dst.height - 1) << 16 | (op->dst.width - 1);
	uint32_t offset = (uint16_t)op->dst.y << 16 | (uint16_t)op->dst.x;

	assert(!too_large(op->dst.x, op->dst.y));
	assert(!too_large(op->dst.width, op->dst.height));

	if (!DBG_NO_STATE_CACHE &&
	    sna->render_state.gen5.drawrect_limit == limit &&
	    sna->render_state.gen5.drawrect_offset == offset)
		return;
	sna->render_state.gen5.drawrect_offset = offset;
	sna->render_state.gen5.drawrect_limit = limit;

	OUT_BATCH(GEN5_3DSTATE_DRAWING_RECTANGLE | (4 - 2));
	OUT_BATCH(0x00000000);
	OUT_BATCH(limit);
	OUT_BATCH(offset);
}

static void
gen5_emit_vertex_elements(struct sna *sna,
			  const struct sna_composite_op *op)
{
	/*
	 * vertex data in vertex buffer
	 *    position: (x, y)
	 *    texture coordinate 0: (u0, v0) if (is_affine is TRUE) else (u0, v0, w0)
	 *    texture coordinate 1 if (has_mask is TRUE): same as above
	 */
	struct gen5_render_state *render = &sna->render_state.gen5;
	Bool has_mask = op->mask.bo != NULL;
	Bool is_affine = op->is_affine;
	int nelem = has_mask ? 2 : 1;
	int selem = is_affine ? 2 : 3;
	uint32_t w_component;
	uint32_t src_format;
	int id = op->u.gen5.ve_id;

	assert((unsigned)id <= 3);
	if (!DBG_NO_STATE_CACHE && render->ve_id == id)
		return;

	render->ve_id = id;

	if (is_affine) {
		src_format = GEN5_SURFACEFORMAT_R32G32_FLOAT;
		w_component = GEN5_VFCOMPONENT_STORE_1_FLT;
	} else {
		src_format = GEN5_SURFACEFORMAT_R32G32B32_FLOAT;
		w_component = GEN5_VFCOMPONENT_STORE_SRC;
	}

	/* The VUE layout
	 *    dword 0-3: pad (0.0, 0.0, 0.0. 0.0)
	 *    dword 4-7: position (x, y, 1.0, 1.0),
	 *    dword 8-11: texture coordinate 0 (u0, v0, w0, 1.0)
	 *    dword 12-15: texture coordinate 1 (u1, v1, w1, 1.0)
	 *
	 * dword 4-15 are fetched from vertex buffer
	 */
	OUT_BATCH(GEN5_3DSTATE_VERTEX_ELEMENTS |
		((2 * (2 + nelem)) + 1 - 2));

	OUT_BATCH((id << VE0_VERTEX_BUFFER_INDEX_SHIFT) | VE0_VALID |
		  (GEN5_SURFACEFORMAT_R32G32B32A32_FLOAT << VE0_FORMAT_SHIFT) |
		  (0 << VE0_OFFSET_SHIFT));
	OUT_BATCH((GEN5_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_0_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_1_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_3_SHIFT));

	/* x,y */
	OUT_BATCH((id << VE0_VERTEX_BUFFER_INDEX_SHIFT) | VE0_VALID |
		  (GEN5_SURFACEFORMAT_R16G16_SSCALED << VE0_FORMAT_SHIFT) |
		  (0 << VE0_OFFSET_SHIFT)); /* offsets vb in bytes */
	OUT_BATCH((GEN5_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_2_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_3_SHIFT));

	/* u0, v0, w0 */
	OUT_BATCH((id << VE0_VERTEX_BUFFER_INDEX_SHIFT) | VE0_VALID |
		  (src_format << VE0_FORMAT_SHIFT) |
		  (4 << VE0_OFFSET_SHIFT));	/* offset vb in bytes */
	OUT_BATCH((GEN5_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT) |
		  (w_component << VE1_VFCOMPONENT_2_SHIFT) |
		  (GEN5_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_3_SHIFT));

	/* u1, v1, w1 */
	if (has_mask) {
		OUT_BATCH((id << VE0_VERTEX_BUFFER_INDEX_SHIFT) | VE0_VALID |
			  (src_format << VE0_FORMAT_SHIFT) |
			  (((1 + selem) * 4) << VE0_OFFSET_SHIFT)); /* vb offset in bytes */
		OUT_BATCH((GEN5_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT) |
			  (GEN5_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT) |
			  (w_component << VE1_VFCOMPONENT_2_SHIFT) |
			  (GEN5_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_3_SHIFT));
	}
}

static void
gen5_emit_state(struct sna *sna,
		const struct sna_composite_op *op,
		uint16_t offset)
{
	/* drawrect must be first for Ironlake BLT workaround */
	gen5_emit_drawing_rectangle(sna, op);

	gen5_emit_binding_table(sna, offset);
	if (gen5_emit_pipelined_pointers(sna, op, op->op, op->u.gen5.wm_kernel))
		gen5_emit_urb(sna);
	gen5_emit_vertex_elements(sna, op);

	if (kgem_bo_is_dirty(op->src.bo) || kgem_bo_is_dirty(op->mask.bo)) {
		OUT_BATCH(MI_FLUSH);
		kgem_clear_dirty(&sna->kgem);
		kgem_bo_mark_dirty(op->dst.bo);
	}
}

static void gen5_bind_surfaces(struct sna *sna,
			       const struct sna_composite_op *op)
{
	uint32_t *binding_table;
	uint16_t offset;

	gen5_get_batch(sna);

	binding_table = gen5_composite_get_binding_table(sna, &offset);

	binding_table[0] =
		gen5_bind_bo(sna,
			    op->dst.bo, op->dst.width, op->dst.height,
			    gen5_get_dest_format(op->dst.format),
			    TRUE);
	binding_table[1] =
		gen5_bind_bo(sna,
			     op->src.bo, op->src.width, op->src.height,
			     op->src.card_format,
			     FALSE);
	if (op->mask.bo)
		binding_table[2] =
			gen5_bind_bo(sna,
				     op->mask.bo,
				     op->mask.width,
				     op->mask.height,
				     op->mask.card_format,
				     FALSE);

	if (sna->kgem.surface == offset &&
	    *(uint64_t *)(sna->kgem.batch + sna->render_state.gen5.surface_table) == *(uint64_t*)binding_table &&
	    (op->mask.bo == NULL ||
	     sna->kgem.batch[sna->render_state.gen5.surface_table+2] == binding_table[2])) {
		sna->kgem.surface += sizeof(struct gen5_surface_state_padded) / sizeof(uint32_t);
		offset = sna->render_state.gen5.surface_table;
	}

	gen5_emit_state(sna, op, offset);
}

fastcall static void
gen5_render_composite_blt(struct sna *sna,
			  const struct sna_composite_op *op,
			  const struct sna_composite_rectangles *r)
{
	DBG(("%s: src=(%d, %d)+(%d, %d), mask=(%d, %d)+(%d, %d), dst=(%d, %d)+(%d, %d), size=(%d, %d)\n",
	     __FUNCTION__,
	     r->src.x, r->src.y, op->src.offset[0], op->src.offset[1],
	     r->mask.x, r->mask.y, op->mask.offset[0], op->mask.offset[1],
	     r->dst.x, r->dst.y, op->dst.x, op->dst.y,
	     r->width, r->height));

	gen5_get_rectangles(sna, op, 1, gen5_bind_surfaces);
	op->prim_emit(sna, op, r);
}

fastcall static void
gen5_render_composite_box(struct sna *sna,
			  const struct sna_composite_op *op,
			  const BoxRec *box)
{
	struct sna_composite_rectangles r;

	DBG(("  %s: (%d, %d), (%d, %d)\n",
	     __FUNCTION__,
	     box->x1, box->y1, box->x2, box->y2));

	gen5_get_rectangles(sna, op, 1, gen5_bind_surfaces);

	r.dst.x = box->x1;
	r.dst.y = box->y1;
	r.width  = box->x2 - box->x1;
	r.height = box->y2 - box->y1;
	r.mask = r.src = r.dst;

	op->prim_emit(sna, op, &r);
}

static void
gen5_render_composite_boxes(struct sna *sna,
			    const struct sna_composite_op *op,
			    const BoxRec *box, int nbox)
{
	DBG(("%s(%d) delta=(%d, %d), src=(%d, %d)/(%d, %d), mask=(%d, %d)/(%d, %d)\n",
	     __FUNCTION__, nbox, op->dst.x, op->dst.y,
	     op->src.offset[0], op->src.offset[1],
	     op->src.width, op->src.height,
	     op->mask.offset[0], op->mask.offset[1],
	     op->mask.width, op->mask.height));

	do {
		int nbox_this_time;

		nbox_this_time = gen5_get_rectangles(sna, op, nbox,
						     gen5_bind_surfaces);
		nbox -= nbox_this_time;

		do {
			struct sna_composite_rectangles r;

			DBG(("  %s: (%d, %d), (%d, %d)\n",
			     __FUNCTION__,
			     box->x1, box->y1, box->x2, box->y2));

			r.dst.x = box->x1;
			r.dst.y = box->y1;
			r.width  = box->x2 - box->x1;
			r.height = box->y2 - box->y1;
			r.mask = r.src = r.dst;
			op->prim_emit(sna, op, &r);
			box++;
		} while (--nbox_this_time);
	} while (nbox);
}

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static uint32_t gen5_bind_video_source(struct sna *sna,
				       struct kgem_bo *src_bo,
				       uint32_t src_offset,
				       int src_width,
				       int src_height,
				       int src_pitch,
				       uint32_t src_surf_format)
{
	struct gen5_surface_state *ss;

	sna->kgem.surface -= sizeof(struct gen5_surface_state_padded) / sizeof(uint32_t);

	ss = memset(sna->kgem.batch + sna->kgem.surface, 0, sizeof(*ss));
	ss->ss0.surface_type = GEN5_SURFACE_2D;
	ss->ss0.surface_format = src_surf_format;
	ss->ss0.color_blend = 1;

	ss->ss1.base_addr =
		kgem_add_reloc(&sna->kgem,
			       sna->kgem.surface + 1,
			       src_bo,
			       I915_GEM_DOMAIN_SAMPLER << 16,
			       src_offset);

	ss->ss2.width  = src_width - 1;
	ss->ss2.height = src_height - 1;
	ss->ss3.pitch  = src_pitch - 1;

	return sna->kgem.surface * sizeof(uint32_t);
}

static void gen5_video_bind_surfaces(struct sna *sna,
				     const struct sna_composite_op *op)
{
	struct sna_video_frame *frame = op->priv;
	uint32_t src_surf_format;
	uint32_t src_surf_base[6];
	int src_width[6];
	int src_height[6];
	int src_pitch[6];
	uint32_t *binding_table;
	int n_src, n;
	uint16_t offset;


	src_surf_base[0] = 0;
	src_surf_base[1] = 0;
	src_surf_base[2] = frame->VBufOffset;
	src_surf_base[3] = frame->VBufOffset;
	src_surf_base[4] = frame->UBufOffset;
	src_surf_base[5] = frame->UBufOffset;

	if (is_planar_fourcc(frame->id)) {
		src_surf_format = GEN5_SURFACEFORMAT_R8_UNORM;
		src_width[1]  = src_width[0]  = frame->width;
		src_height[1] = src_height[0] = frame->height;
		src_pitch[1]  = src_pitch[0]  = frame->pitch[1];
		src_width[4]  = src_width[5]  = src_width[2]  = src_width[3] =
			frame->width / 2;
		src_height[4] = src_height[5] = src_height[2] = src_height[3] =
			frame->height / 2;
		src_pitch[4]  = src_pitch[5]  = src_pitch[2]  = src_pitch[3] =
			frame->pitch[0];
		n_src = 6;
	} else {
		if (frame->id == FOURCC_UYVY)
			src_surf_format = GEN5_SURFACEFORMAT_YCRCB_SWAPY;
		else
			src_surf_format = GEN5_SURFACEFORMAT_YCRCB_NORMAL;

		src_width[0]  = frame->width;
		src_height[0] = frame->height;
		src_pitch[0]  = frame->pitch[0];
		n_src = 1;
	}

	gen5_get_batch(sna);
	binding_table = gen5_composite_get_binding_table(sna, &offset);

	binding_table[0] =
		gen5_bind_bo(sna,
			     op->dst.bo, op->dst.width, op->dst.height,
			     gen5_get_dest_format(op->dst.format),
			     TRUE);
	for (n = 0; n < n_src; n++) {
		binding_table[1+n] =
			gen5_bind_video_source(sna,
					       frame->bo,
					       src_surf_base[n],
					       src_width[n],
					       src_height[n],
					       src_pitch[n],
					       src_surf_format);
	}

	gen5_emit_state(sna, op, offset);
}

static Bool
gen5_render_video(struct sna *sna,
		  struct sna_video *video,
		  struct sna_video_frame *frame,
		  RegionPtr dstRegion,
		  short src_w, short src_h,
		  short drw_w, short drw_h,
		  PixmapPtr pixmap)
{
	struct sna_composite_op tmp;
	int nbox, dxo, dyo, pix_xoff, pix_yoff;
	float src_scale_x, src_scale_y;
	struct sna_pixmap *priv;
	BoxPtr box;

	DBG(("%s: %dx%d -> %dx%d\n", __FUNCTION__, src_w, src_h, drw_w, drw_h));

	priv = sna_pixmap_force_to_gpu(pixmap, MOVE_READ | MOVE_WRITE);
	if (priv == NULL)
		return FALSE;

	memset(&tmp, 0, sizeof(tmp));

	tmp.op = PictOpSrc;
	tmp.dst.pixmap = pixmap;
	tmp.dst.width  = pixmap->drawable.width;
	tmp.dst.height = pixmap->drawable.height;
	tmp.dst.format = sna_format_for_depth(pixmap->drawable.depth);
	tmp.dst.bo = priv->gpu_bo;

	tmp.src.filter = SAMPLER_FILTER_BILINEAR;
	tmp.src.repeat = SAMPLER_EXTEND_PAD;
	tmp.src.bo = frame->bo;
	tmp.mask.bo = NULL;
	tmp.u.gen5.wm_kernel =
		is_planar_fourcc(frame->id) ? WM_KERNEL_VIDEO_PLANAR : WM_KERNEL_VIDEO_PACKED;
	tmp.u.gen5.ve_id = 1;
	tmp.is_affine = TRUE;
	tmp.floats_per_vertex = 3;
	tmp.floats_per_rect = 9;
	tmp.priv = frame;

	if (!kgem_check_bo(&sna->kgem, tmp.dst.bo, frame->bo, NULL)) {
		kgem_submit(&sna->kgem);
		assert(kgem_check_bo(&sna->kgem, tmp.dst.bo, frame->bo, NULL));
	}

	gen5_video_bind_surfaces(sna, &tmp);
	gen5_align_vertex(sna, &tmp);

	/* Set up the offset for translating from the given region (in screen
	 * coordinates) to the backing pixmap.
	 */
#ifdef COMPOSITE
	pix_xoff = -pixmap->screen_x + pixmap->drawable.x;
	pix_yoff = -pixmap->screen_y + pixmap->drawable.y;
#else
	pix_xoff = 0;
	pix_yoff = 0;
#endif

	dxo = dstRegion->extents.x1;
	dyo = dstRegion->extents.y1;

	/* Use normalized texture coordinates */
	src_scale_x = ((float)src_w / frame->width) / (float)drw_w;
	src_scale_y = ((float)src_h / frame->height) / (float)drw_h;

	box = REGION_RECTS(dstRegion);
	nbox = REGION_NUM_RECTS(dstRegion);
	while (nbox--) {
		BoxRec r;

		r.x1 = box->x1 + pix_xoff;
		r.x2 = box->x2 + pix_xoff;
		r.y1 = box->y1 + pix_yoff;
		r.y2 = box->y2 + pix_yoff;

		gen5_get_rectangles(sna, &tmp, 1, gen5_video_bind_surfaces);

		OUT_VERTEX(r.x2, r.y2);
		OUT_VERTEX_F((box->x2 - dxo) * src_scale_x);
		OUT_VERTEX_F((box->y2 - dyo) * src_scale_y);

		OUT_VERTEX(r.x1, r.y2);
		OUT_VERTEX_F((box->x1 - dxo) * src_scale_x);
		OUT_VERTEX_F((box->y2 - dyo) * src_scale_y);

		OUT_VERTEX(r.x1, r.y1);
		OUT_VERTEX_F((box->x1 - dxo) * src_scale_x);
		OUT_VERTEX_F((box->y1 - dyo) * src_scale_y);

		if (!DAMAGE_IS_ALL(priv->gpu_damage)) {
			sna_damage_add_box(&priv->gpu_damage, &r);
			sna_damage_subtract_box(&priv->cpu_damage, &r);
		}
		box++;
	}
	priv->clear = false;

	gen5_vertex_flush(sna);
	return TRUE;
}

static int
gen5_composite_solid_init(struct sna *sna,
			  struct sna_composite_channel *channel,
			  uint32_t color)
{
	channel->filter = PictFilterNearest;
	channel->repeat = RepeatNormal;
	channel->is_affine = TRUE;
	channel->is_solid  = TRUE;
	channel->transform = NULL;
	channel->width  = 1;
	channel->height = 1;
	channel->card_format = GEN5_SURFACEFORMAT_B8G8R8A8_UNORM;

	channel->bo = sna_render_get_solid(sna, color);

	channel->scale[0]  = channel->scale[1]  = 1;
	channel->offset[0] = channel->offset[1] = 0;
	return channel->bo != NULL;
}

static int
gen5_composite_picture(struct sna *sna,
		       PicturePtr picture,
		       struct sna_composite_channel *channel,
		       int x, int y,
		       int w, int h,
		       int dst_x, int dst_y)
{
	PixmapPtr pixmap;
	uint32_t color;
	int16_t dx, dy;

	DBG(("%s: (%d, %d)x(%d, %d), dst=(%d, %d)\n",
	     __FUNCTION__, x, y, w, h, dst_x, dst_y));

	channel->is_solid = FALSE;
	channel->card_format = -1;

	if (sna_picture_is_solid(picture, &color))
		return gen5_composite_solid_init(sna, channel, color);

	if (picture->pDrawable == NULL)
		return sna_render_picture_fixup(sna, picture, channel,
						x, y, w, h, dst_x, dst_y);

	if (picture->alphaMap) {
		DBG(("%s -- fallback, alphamap\n", __FUNCTION__));
		return sna_render_picture_fixup(sna, picture, channel,
						x, y, w, h, dst_x, dst_y);
	}

	if (!gen5_check_repeat(picture))
		return sna_render_picture_fixup(sna, picture, channel,
						x, y, w, h, dst_x, dst_y);

	if (!gen5_check_filter(picture))
		return sna_render_picture_fixup(sna, picture, channel,
						x, y, w, h, dst_x, dst_y);

	channel->repeat = picture->repeat ? picture->repeatType : RepeatNone;
	channel->filter = picture->filter;

	pixmap = get_drawable_pixmap(picture->pDrawable);
	get_drawable_deltas(picture->pDrawable, pixmap, &dx, &dy);

	x += dx + picture->pDrawable->x;
	y += dy + picture->pDrawable->y;

	channel->is_affine = sna_transform_is_affine(picture->transform);
	if (sna_transform_is_integer_translation(picture->transform, &dx, &dy)) {
		DBG(("%s: integer translation (%d, %d), removing\n",
		     __FUNCTION__, dx, dy));
		x += dx;
		y += dy;
		channel->transform = NULL;
		channel->filter = PictFilterNearest;
	} else
		channel->transform = picture->transform;

	channel->card_format = gen5_get_card_format(picture->format);
	if (channel->card_format == -1)
		return sna_render_picture_convert(sna, picture, channel, pixmap,
						  x, y, w, h, dst_x, dst_y);

	if (too_large(pixmap->drawable.width, pixmap->drawable.height))
		return sna_render_picture_extract(sna, picture, channel,
						  x, y, w, h, dst_x, dst_y);

	return sna_render_pixmap_bo(sna, channel, pixmap,
				    x, y, w, h, dst_x, dst_y);
}

static void gen5_composite_channel_convert(struct sna_composite_channel *channel)
{
	channel->repeat = gen5_repeat(channel->repeat);
	channel->filter = gen5_filter(channel->filter);
	if (channel->card_format == (unsigned)-1)
		channel->card_format = gen5_get_card_format(channel->pict_format);
}

static void
gen5_render_composite_done(struct sna *sna,
			   const struct sna_composite_op *op)
{
	if (sna->render_state.gen5.vertex_offset) {
		gen5_vertex_flush(sna);
		gen5_magic_ca_pass(sna,op);
	}

	DBG(("%s()\n", __FUNCTION__));

	if (op->mask.bo)
		kgem_bo_destroy(&sna->kgem, op->mask.bo);
	if (op->src.bo)
		kgem_bo_destroy(&sna->kgem, op->src.bo);

	sna_render_composite_redirect_done(sna, op);
}

static Bool
gen5_composite_set_target(PicturePtr dst, struct sna_composite_op *op)
{
	struct sna_pixmap *priv;

	DBG(("%s: dst=%p\n", __FUNCTION__, dst));

	assert(gen5_check_dst_format(dst->format));

	op->dst.pixmap = get_drawable_pixmap(dst->pDrawable);
	priv = sna_pixmap(op->dst.pixmap);

	op->dst.width  = op->dst.pixmap->drawable.width;
	op->dst.height = op->dst.pixmap->drawable.height;
	op->dst.format = dst->format;

	DBG(("%s: pixmap=%p, format=%08x\n", __FUNCTION__,
	     op->dst.pixmap, (unsigned int)op->dst.format));

	op->dst.bo = NULL;
	if (priv && priv->gpu_bo == NULL) {
		op->dst.bo = priv->cpu_bo;
		op->damage = &priv->cpu_damage;
	}
	if (op->dst.bo == NULL) {
		priv = sna_pixmap_force_to_gpu(op->dst.pixmap, MOVE_READ | MOVE_WRITE);
		if (priv == NULL)
			return FALSE;

		op->dst.bo = priv->gpu_bo;
		op->damage = &priv->gpu_damage;
	}
	if (sna_damage_is_all(op->damage, op->dst.width, op->dst.height))
		op->damage = NULL;

	DBG(("%s: bo=%p, damage=%p\n", __FUNCTION__, op->dst.bo, op->damage));

	get_drawable_deltas(dst->pDrawable, op->dst.pixmap,
			    &op->dst.x, &op->dst.y);
	return TRUE;
}

static inline Bool
picture_is_cpu(PicturePtr picture)
{
	if (!picture->pDrawable)
		return FALSE;

	if (too_large(picture->pDrawable->width, picture->pDrawable->height))
		return TRUE;

	return is_cpu(picture->pDrawable) || is_dirty(picture->pDrawable);
}

static Bool
try_blt(struct sna *sna,
	PicturePtr dst, PicturePtr src,
	int width, int height)
{
	if (sna->kgem.mode != KGEM_RENDER) {
		DBG(("%s: already performing BLT\n", __FUNCTION__));
		return TRUE;
	}

	if (too_large(width, height)) {
		DBG(("%s: operation too large for 3D pipe (%d, %d)\n",
		     __FUNCTION__, width, height));
		return TRUE;
	}

	if (too_large(dst->pDrawable->width, dst->pDrawable->height))
		return TRUE;

	/* The blitter is much faster for solids */
	if (sna_picture_is_solid(src, NULL))
		return TRUE;

	/* is the source picture only in cpu memory e.g. a shm pixmap? */
	return picture_is_cpu(src);
}

static bool
is_gradient(PicturePtr picture)
{
	if (picture->pDrawable)
		return FALSE;

	return picture->pSourcePict->type != SourcePictTypeSolidFill;
}

static bool
has_alphamap(PicturePtr p)
{
	return p->alphaMap != NULL;
}

static bool
untransformed(PicturePtr p)
{
	return !p->transform || pixman_transform_is_int_translate(p->transform);
}

static bool
need_upload(PicturePtr p)
{
	return p->pDrawable && unattached(p->pDrawable) && untransformed(p);
}

static bool
source_fallback(PicturePtr p)
{
	if (sna_picture_is_solid(p, NULL))
		return false;

	return (has_alphamap(p) ||
		is_gradient(p) ||
		!gen5_check_filter(p) ||
		!gen5_check_repeat(p) ||
		!gen5_check_format(p->format) ||
		need_upload(p));
}

static bool
gen5_composite_fallback(struct sna *sna,
			PicturePtr src,
			PicturePtr mask,
			PicturePtr dst)
{
	struct sna_pixmap *priv;
	PixmapPtr src_pixmap;
	PixmapPtr mask_pixmap;
	PixmapPtr dst_pixmap;

	if (!gen5_check_dst_format(dst->format)) {
		DBG(("%s: unknown destination format: %d\n",
		     __FUNCTION__, dst->format));
		return TRUE;
	}

	dst_pixmap = get_drawable_pixmap(dst->pDrawable);
	src_pixmap = src->pDrawable ? get_drawable_pixmap(src->pDrawable) : NULL;
	mask_pixmap = (mask && mask->pDrawable) ? get_drawable_pixmap(mask->pDrawable) : NULL;

	/* If we are using the destination as a source and need to
	 * readback in order to upload the source, do it all
	 * on the cpu.
	 */
	if (src_pixmap == dst_pixmap && source_fallback(src)) {
		DBG(("%s: src is dst and will fallback\n",__FUNCTION__));
		return TRUE;
	}
	if (mask_pixmap == dst_pixmap && source_fallback(mask)) {
		DBG(("%s: mask is dst and will fallback\n",__FUNCTION__));
		return TRUE;
	}

	/* If anything is on the GPU, push everything out to the GPU */
	priv = sna_pixmap(dst_pixmap);
	if (priv && priv->gpu_damage && !priv->clear) {
		DBG(("%s: dst is already on the GPU, try to use GPU\n",
		     __FUNCTION__));
		return FALSE;
	}

	if (src_pixmap && !source_fallback(src)) {
		priv = sna_pixmap(src_pixmap);
		if (priv && priv->gpu_damage && !priv->cpu_damage) {
			DBG(("%s: src is already on the GPU, try to use GPU\n",
			     __FUNCTION__));
			return FALSE;
		}
	}
	if (mask_pixmap && !source_fallback(mask)) {
		priv = sna_pixmap(mask_pixmap);
		if (priv && priv->gpu_damage && !priv->cpu_damage) {
			DBG(("%s: mask is already on the GPU, try to use GPU\n",
			     __FUNCTION__));
			return FALSE;
		}
	}

	/* However if the dst is not on the GPU and we need to
	 * render one of the sources using the CPU, we may
	 * as well do the entire operation in place onthe CPU.
	 */
	if (source_fallback(src)) {
		DBG(("%s: dst is on the CPU and src will fallback\n",
		     __FUNCTION__));
		return TRUE;
	}

	if (mask && source_fallback(mask)) {
		DBG(("%s: dst is on the CPU and mask will fallback\n",
		     __FUNCTION__));
		return TRUE;
	}

	DBG(("%s: dst is not on the GPU and the operation should not fallback\n",
	     __FUNCTION__));
	return FALSE;
}

static int
reuse_source(struct sna *sna,
	     PicturePtr src, struct sna_composite_channel *sc, int src_x, int src_y,
	     PicturePtr mask, struct sna_composite_channel *mc, int msk_x, int msk_y)
{
	uint32_t color;

	if (src_x != msk_x || src_y != msk_y)
		return FALSE;

	if (src == mask) {
		DBG(("%s: mask is source\n", __FUNCTION__));
		*mc = *sc;
		mc->bo = kgem_bo_reference(mc->bo);
		return TRUE;
	}

	if (sna_picture_is_solid(mask, &color))
		return gen5_composite_solid_init(sna, mc, color);

	if (sc->is_solid)
		return FALSE;

	if (src->pDrawable == NULL || mask->pDrawable != src->pDrawable)
		return FALSE;

	DBG(("%s: mask reuses source drawable\n", __FUNCTION__));

	if (!sna_transform_equal(src->transform, mask->transform))
		return FALSE;

	if (!sna_picture_alphamap_equal(src, mask))
		return FALSE;

	if (!gen5_check_repeat(mask))
		return FALSE;

	if (!gen5_check_filter(mask))
		return FALSE;

	if (!gen5_check_format(mask->format))
		return FALSE;

	DBG(("%s: reusing source channel for mask with a twist\n",
	     __FUNCTION__));

	*mc = *sc;
	mc->repeat = gen5_repeat(mask->repeat ? mask->repeatType : RepeatNone);
	mc->filter = gen5_filter(mask->filter);
	mc->pict_format = mask->format;
	mc->card_format = gen5_get_card_format(mask->format);
	mc->bo = kgem_bo_reference(mc->bo);
	return TRUE;
}

static Bool
gen5_render_composite(struct sna *sna,
		      uint8_t op,
		      PicturePtr src,
		      PicturePtr mask,
		      PicturePtr dst,
		      int16_t src_x, int16_t src_y,
		      int16_t msk_x, int16_t msk_y,
		      int16_t dst_x, int16_t dst_y,
		      int16_t width, int16_t height,
		      struct sna_composite_op *tmp)
{
	DBG(("%s: %dx%d, current mode=%d\n", __FUNCTION__,
	     width, height, sna->kgem.mode));

	if (op >= ARRAY_SIZE(gen5_blend_op)) {
		DBG(("%s: unhandled blend op %d\n", __FUNCTION__, op));
		return FALSE;
	}

	if (mask == NULL &&
	    try_blt(sna, dst, src, width, height) &&
	    sna_blt_composite(sna, op,
			      src, dst,
			      src_x, src_y,
			      dst_x, dst_y,
			      width, height, tmp))
		return TRUE;

	if (gen5_composite_fallback(sna, src, mask, dst))
		return FALSE;

	if (need_tiling(sna, width, height))
		return sna_tiling_composite(op, src, mask, dst,
					    src_x, src_y,
					    msk_x, msk_y,
					    dst_x, dst_y,
					    width, height,
					    tmp);

	if (!gen5_composite_set_target(dst, tmp)) {
		DBG(("%s: failed to set composite target\n", __FUNCTION__));
		return FALSE;
	}

	if (mask == NULL && sna->kgem.mode == KGEM_BLT  &&
	    sna_blt_composite(sna, op,
			      src, dst,
			      src_x, src_y,
			      dst_x, dst_y,
			      width, height, tmp))
		return TRUE;

	sna_render_reduce_damage(tmp, dst_x, dst_y, width, height);

	if (too_large(tmp->dst.width, tmp->dst.height) &&
	    !sna_render_composite_redirect(sna, tmp,
					   dst_x, dst_y, width, height))
		return FALSE;

	DBG(("%s: preparing source\n", __FUNCTION__));
	switch (gen5_composite_picture(sna, src, &tmp->src,
				       src_x, src_y,
				       width, height,
				       dst_x, dst_y)) {
	case -1:
		DBG(("%s: failed to prepare source picture\n", __FUNCTION__));
		goto cleanup_dst;
	case 0:
		gen5_composite_solid_init(sna, &tmp->src, 0);
	case 1:
		gen5_composite_channel_convert(&tmp->src);
		break;
	}

	tmp->op = op;
	tmp->is_affine = tmp->src.is_affine;
	tmp->has_component_alpha = FALSE;
	tmp->need_magic_ca_pass = FALSE;

	tmp->prim_emit = gen5_emit_composite_primitive;
	if (mask) {
		if (mask->componentAlpha && PICT_FORMAT_RGB(mask->format)) {
			tmp->has_component_alpha = TRUE;

			/* Check if it's component alpha that relies on a source alpha and on
			 * the source value.  We can only get one of those into the single
			 * source value that we get to blend with.
			 */
			if (gen5_blend_op[op].src_alpha &&
			    (gen5_blend_op[op].src_blend != GEN5_BLENDFACTOR_ZERO)) {
				if (op != PictOpOver) {
					DBG(("%s: unhandled CA blend op %d\n", __FUNCTION__, op));
					goto cleanup_src;
				}

				tmp->need_magic_ca_pass = TRUE;
				tmp->op = PictOpOutReverse;
			}
		}

		if (!reuse_source(sna,
				  src, &tmp->src, src_x, src_y,
				  mask, &tmp->mask, msk_x, msk_y)) {
			DBG(("%s: preparing mask\n", __FUNCTION__));
			switch (gen5_composite_picture(sna, mask, &tmp->mask,
						       msk_x, msk_y,
						       width, height,
						       dst_x, dst_y)) {
			case -1:
				DBG(("%s: failed to prepare mask picture\n", __FUNCTION__));
				goto cleanup_src;
			case 0:
				gen5_composite_solid_init(sna, &tmp->mask, 0);
			case 1:
				gen5_composite_channel_convert(&tmp->mask);
				break;
			}
		}

		tmp->is_affine &= tmp->mask.is_affine;

		if (tmp->src.transform == NULL && tmp->mask.transform == NULL)
			tmp->prim_emit = gen5_emit_composite_primitive_identity_source_mask;

		tmp->floats_per_vertex = 5 + 2 * !tmp->is_affine;
	} else {
		if (tmp->src.is_solid)
			tmp->prim_emit = gen5_emit_composite_primitive_solid;
		else if (tmp->src.transform == NULL)
			tmp->prim_emit = gen5_emit_composite_primitive_identity_source;
		else if (tmp->src.is_affine)
			tmp->prim_emit = gen5_emit_composite_primitive_affine_source;

		tmp->floats_per_vertex = 3 + !tmp->is_affine;
	}
	tmp->floats_per_rect = 3*tmp->floats_per_vertex;

	tmp->u.gen5.wm_kernel =
		gen5_choose_composite_kernel(tmp->op,
					     tmp->mask.bo != NULL,
					     tmp->has_component_alpha,
					     tmp->is_affine);
	tmp->u.gen5.ve_id = (tmp->mask.bo != NULL) << 1 | tmp->is_affine;

	tmp->blt   = gen5_render_composite_blt;
	tmp->box   = gen5_render_composite_box;
	tmp->boxes = gen5_render_composite_boxes;
	tmp->done  = gen5_render_composite_done;

	if (!kgem_check_bo(&sna->kgem,
			   tmp->dst.bo, tmp->src.bo, tmp->mask.bo, NULL)) {
		kgem_submit(&sna->kgem);
		if (!kgem_check_bo(&sna->kgem,
				   tmp->dst.bo, tmp->src.bo, tmp->mask.bo, NULL))
			goto cleanup_mask;
	}

	gen5_bind_surfaces(sna, tmp);
	gen5_align_vertex(sna, tmp);
	return TRUE;

cleanup_mask:
	if (tmp->mask.bo)
		kgem_bo_destroy(&sna->kgem, tmp->mask.bo);
cleanup_src:
	if (tmp->src.bo)
		kgem_bo_destroy(&sna->kgem, tmp->src.bo);
cleanup_dst:
	if (tmp->redirect.real_bo)
		kgem_bo_destroy(&sna->kgem, tmp->dst.bo);
	return FALSE;
}

/* A poor man's span interface. But better than nothing? */
#if !NO_COMPOSITE_SPANS
static Bool
gen5_composite_alpha_gradient_init(struct sna *sna,
				   struct sna_composite_channel *channel)
{
	DBG(("%s\n", __FUNCTION__));

	channel->filter = PictFilterNearest;
	channel->repeat = RepeatPad;
	channel->is_affine = TRUE;
	channel->is_solid  = FALSE;
	channel->transform = NULL;
	channel->width  = 256;
	channel->height = 1;
	channel->card_format = GEN5_SURFACEFORMAT_B8G8R8A8_UNORM;

	channel->bo = sna_render_get_alpha_gradient(sna);

	channel->scale[0]  = channel->scale[1]  = 1;
	channel->offset[0] = channel->offset[1] = 0;
	return channel->bo != NULL;
}

inline static void
gen5_emit_composite_texcoord(struct sna *sna,
			     const struct sna_composite_channel *channel,
			     int16_t x, int16_t y)
{
	float t[3];

	if (channel->is_affine) {
		sna_get_transformed_coordinates(x + channel->offset[0],
						y + channel->offset[1],
						channel->transform,
						&t[0], &t[1]);
		OUT_VERTEX_F(t[0] * channel->scale[0]);
		OUT_VERTEX_F(t[1] * channel->scale[1]);
	} else {
		t[0] = t[1] = 0; t[2] = 1;
		sna_get_transformed_coordinates_3d(x + channel->offset[0],
						   y + channel->offset[1],
						   channel->transform,
						   &t[0], &t[1], &t[2]);
		OUT_VERTEX_F(t[0] * channel->scale[0]);
		OUT_VERTEX_F(t[1] * channel->scale[1]);
		OUT_VERTEX_F(t[2]);
	}
}

inline static void
gen5_emit_composite_texcoord_affine(struct sna *sna,
				    const struct sna_composite_channel *channel,
				    int16_t x, int16_t y)
{
	float t[2];

	sna_get_transformed_coordinates(x + channel->offset[0],
					y + channel->offset[1],
					channel->transform,
					&t[0], &t[1]);
	OUT_VERTEX_F(t[0] * channel->scale[0]);
	OUT_VERTEX_F(t[1] * channel->scale[1]);
}

inline static void
gen5_emit_composite_spans_vertex(struct sna *sna,
				 const struct sna_composite_spans_op *op,
				 int16_t x, int16_t y)
{
	OUT_VERTEX(x, y);
	gen5_emit_composite_texcoord(sna, &op->base.src, x, y);
}

fastcall static void
gen5_emit_composite_spans_primitive(struct sna *sna,
				    const struct sna_composite_spans_op *op,
				    const BoxRec *box,
				    float opacity)
{
	gen5_emit_composite_spans_vertex(sna, op, box->x2, box->y2);
	OUT_VERTEX_F(opacity);
	OUT_VERTEX_F(1);
	if (!op->base.is_affine)
		OUT_VERTEX_F(1);

	gen5_emit_composite_spans_vertex(sna, op, box->x1, box->y2);
	OUT_VERTEX_F(opacity);
	OUT_VERTEX_F(1);
	if (!op->base.is_affine)
		OUT_VERTEX_F(1);

	gen5_emit_composite_spans_vertex(sna, op, box->x1, box->y1);
	OUT_VERTEX_F(opacity);
	OUT_VERTEX_F(0);
	if (!op->base.is_affine)
		OUT_VERTEX_F(1);
}

fastcall static void
gen5_emit_composite_spans_solid(struct sna *sna,
				const struct sna_composite_spans_op *op,
				const BoxRec *box,
				float opacity)
{
	OUT_VERTEX(box->x2, box->y2);
	OUT_VERTEX_F(1); OUT_VERTEX_F(1);
	OUT_VERTEX_F(opacity); OUT_VERTEX_F(1);

	OUT_VERTEX(box->x1, box->y2);
	OUT_VERTEX_F(0); OUT_VERTEX_F(1);
	OUT_VERTEX_F(opacity); OUT_VERTEX_F(1);

	OUT_VERTEX(box->x1, box->y1);
	OUT_VERTEX_F(0); OUT_VERTEX_F(0);
	OUT_VERTEX_F(opacity); OUT_VERTEX_F(0);
}

fastcall static void
gen5_emit_composite_spans_affine(struct sna *sna,
				 const struct sna_composite_spans_op *op,
				 const BoxRec *box,
				 float opacity)
{
	OUT_VERTEX(box->x2, box->y2);
	gen5_emit_composite_texcoord_affine(sna, &op->base.src,
					    box->x2, box->y2);
	OUT_VERTEX_F(opacity);
	OUT_VERTEX_F(1);

	OUT_VERTEX(box->x1, box->y2);
	gen5_emit_composite_texcoord_affine(sna, &op->base.src,
					    box->x1, box->y2);
	OUT_VERTEX_F(opacity);
	OUT_VERTEX_F(1);

	OUT_VERTEX(box->x1, box->y1);
	gen5_emit_composite_texcoord_affine(sna, &op->base.src,
					    box->x1, box->y1);
	OUT_VERTEX_F(opacity);
	OUT_VERTEX_F(0);
}

fastcall static void
gen5_render_composite_spans_box(struct sna *sna,
				const struct sna_composite_spans_op *op,
				const BoxRec *box, float opacity)
{
	DBG(("%s: src=+(%d, %d), opacity=%f, dst=+(%d, %d), box=(%d, %d) x (%d, %d)\n",
	     __FUNCTION__,
	     op->base.src.offset[0], op->base.src.offset[1],
	     opacity,
	     op->base.dst.x, op->base.dst.y,
	     box->x1, box->y1,
	     box->x2 - box->x1,
	     box->y2 - box->y1));

	gen5_get_rectangles(sna, &op->base, 1, gen5_bind_surfaces);
	op->prim_emit(sna, op, box, opacity);
}

static void
gen5_render_composite_spans_boxes(struct sna *sna,
				  const struct sna_composite_spans_op *op,
				  const BoxRec *box, int nbox,
				  float opacity)
{
	DBG(("%s: nbox=%d, src=+(%d, %d), opacity=%f, dst=+(%d, %d)\n",
	     __FUNCTION__, nbox,
	     op->base.src.offset[0], op->base.src.offset[1],
	     opacity,
	     op->base.dst.x, op->base.dst.y));

	do {
		int nbox_this_time;

		nbox_this_time = gen5_get_rectangles(sna, &op->base, nbox,
						     gen5_bind_surfaces);
		nbox -= nbox_this_time;

		do {
			DBG(("  %s: (%d, %d) x (%d, %d)\n", __FUNCTION__,
			     box->x1, box->y1,
			     box->x2 - box->x1,
			     box->y2 - box->y1));

			op->prim_emit(sna, op, box++, opacity);
		} while (--nbox_this_time);
	} while (nbox);
}

fastcall static void
gen5_render_composite_spans_done(struct sna *sna,
				 const struct sna_composite_spans_op *op)
{
	if (sna->render_state.gen5.vertex_offset)
		gen5_vertex_flush(sna);

	DBG(("%s()\n", __FUNCTION__));

	if (op->base.src.bo)
		kgem_bo_destroy(&sna->kgem, op->base.src.bo);

	sna_render_composite_redirect_done(sna, &op->base);
}

static Bool
gen5_render_composite_spans(struct sna *sna,
			    uint8_t op,
			    PicturePtr src,
			    PicturePtr dst,
			    int16_t src_x,  int16_t src_y,
			    int16_t dst_x,  int16_t dst_y,
			    int16_t width,  int16_t height,
			    unsigned flags,
			    struct sna_composite_spans_op *tmp)
{
	DBG(("%s: %dx%d with flags=%x, current mode=%d\n", __FUNCTION__,
	     width, height, flags, sna->kgem.ring));

	if ((flags & COMPOSITE_SPANS_RECTILINEAR) == 0)
		return FALSE;

	if (op >= ARRAY_SIZE(gen5_blend_op))
		return FALSE;

	if (need_tiling(sna, width, height))
		return FALSE;

	if (gen5_composite_fallback(sna, src, NULL, dst))
		return FALSE;

	tmp->base.op = op;
	if (!gen5_composite_set_target(dst, &tmp->base))
		return FALSE;
	sna_render_reduce_damage(&tmp->base, dst_x, dst_y, width, height);

	if (too_large(tmp->base.dst.width, tmp->base.dst.height)) {
		if (!sna_render_composite_redirect(sna, &tmp->base,
						   dst_x, dst_y, width, height))
			return FALSE;
	}

	switch (gen5_composite_picture(sna, src, &tmp->base.src,
				       src_x, src_y,
				       width, height,
				       dst_x, dst_y)) {
	case -1:
		goto cleanup_dst;
	case 0:
		gen5_composite_solid_init(sna, &tmp->base.src, 0);
	case 1:
		gen5_composite_channel_convert(&tmp->base.src);
		break;
	}

	tmp->base.mask.bo = NULL;
	tmp->base.is_affine = tmp->base.src.is_affine;
	tmp->base.has_component_alpha = FALSE;
	tmp->base.need_magic_ca_pass = FALSE;

	gen5_composite_alpha_gradient_init(sna, &tmp->base.mask);

	tmp->prim_emit = gen5_emit_composite_spans_primitive;
	if (tmp->base.src.is_solid)
		tmp->prim_emit = gen5_emit_composite_spans_solid;
	else if (tmp->base.is_affine)
		tmp->prim_emit = gen5_emit_composite_spans_affine;
	tmp->base.floats_per_vertex = 5 + 2*!tmp->base.is_affine;
	tmp->base.floats_per_rect = 3 * tmp->base.floats_per_vertex;

	tmp->base.u.gen5.wm_kernel =
		gen5_choose_composite_kernel(tmp->base.op,
					     TRUE, FALSE,
					     tmp->base.is_affine);
	tmp->base.u.gen5.ve_id = 1 << 1 | tmp->base.is_affine;

	tmp->box   = gen5_render_composite_spans_box;
	tmp->boxes = gen5_render_composite_spans_boxes;
	tmp->done  = gen5_render_composite_spans_done;

	if (!kgem_check_bo(&sna->kgem,
			   tmp->base.dst.bo, tmp->base.src.bo,
			   NULL))  {
		kgem_submit(&sna->kgem);
		if (!kgem_check_bo(&sna->kgem,
				   tmp->base.dst.bo, tmp->base.src.bo,
				   NULL))
			goto cleanup_src;
	}

	gen5_bind_surfaces(sna, &tmp->base);
	gen5_align_vertex(sna, &tmp->base);
	return TRUE;

cleanup_src:
	if (tmp->base.src.bo)
		kgem_bo_destroy(&sna->kgem, tmp->base.src.bo);
cleanup_dst:
	if (tmp->base.redirect.real_bo)
		kgem_bo_destroy(&sna->kgem, tmp->base.dst.bo);
	return FALSE;
}
#endif

static void
gen5_copy_bind_surfaces(struct sna *sna,
			const struct sna_composite_op *op)
{
	uint32_t *binding_table;
	uint16_t offset;

	gen5_get_batch(sna);

	binding_table = gen5_composite_get_binding_table(sna, &offset);

	binding_table[0] =
		gen5_bind_bo(sna,
			     op->dst.bo, op->dst.width, op->dst.height,
			     gen5_get_dest_format(op->dst.format),
			     TRUE);
	binding_table[1] =
		gen5_bind_bo(sna,
			     op->src.bo, op->src.width, op->src.height,
			     op->src.card_format,
			     FALSE);

	if (sna->kgem.surface == offset &&
	    *(uint64_t *)(sna->kgem.batch + sna->render_state.gen5.surface_table) == *(uint64_t*)binding_table) {
		sna->kgem.surface += sizeof(struct gen5_surface_state_padded) / sizeof(uint32_t);
		offset = sna->render_state.gen5.surface_table;
	}

	gen5_emit_state(sna, op, offset);
}

static Bool
gen5_render_copy_boxes(struct sna *sna, uint8_t alu,
		       PixmapPtr src, struct kgem_bo *src_bo, int16_t src_dx, int16_t src_dy,
		       PixmapPtr dst, struct kgem_bo *dst_bo, int16_t dst_dx, int16_t dst_dy,
		       const BoxRec *box, int n)
{
	struct sna_composite_op tmp;

	if (sna_blt_compare_depth(&src->drawable, &dst->drawable) &&
	    sna_blt_copy_boxes(sna, alu,
			       src_bo, src_dx, src_dy,
			       dst_bo, dst_dx, dst_dy,
			       dst->drawable.bitsPerPixel,
			       box, n))
		return TRUE;

	if (!(alu == GXcopy || alu == GXclear) || src_bo == dst_bo) {
fallback_blt:
		if (!sna_blt_compare_depth(&src->drawable, &dst->drawable))
			return FALSE;

		return sna_blt_copy_boxes_fallback(sna, alu,
						   src, src_bo, src_dx, src_dy,
						   dst, dst_bo, dst_dx, dst_dy,
						   box, n);
	}

	memset(&tmp, 0, sizeof(tmp));

	if (dst->drawable.depth == src->drawable.depth) {
		tmp.dst.format = sna_render_format_for_depth(dst->drawable.depth);
		tmp.src.pict_format = tmp.dst.format;
	} else {
		tmp.dst.format = sna_format_for_depth(dst->drawable.depth);
		tmp.src.pict_format = sna_format_for_depth(src->drawable.depth);
	}
	if (!gen5_check_format(tmp.src.pict_format)) {
		DBG(("%s: unsupported source format, %x, use BLT\n",
		     __FUNCTION__, tmp.src.pict_format));
		goto fallback_blt;
	}

	DBG(("%s (%d, %d)->(%d, %d) x %d\n",
	     __FUNCTION__, src_dx, src_dy, dst_dx, dst_dy, n));

	tmp.op = alu == GXcopy ? PictOpSrc : PictOpClear;

	tmp.dst.pixmap = dst;
	tmp.dst.width  = dst->drawable.width;
	tmp.dst.height = dst->drawable.height;
	tmp.dst.x = tmp.dst.y = 0;
	tmp.dst.bo = dst_bo;
	tmp.damage = NULL;

	sna_render_composite_redirect_init(&tmp);
	if (too_large(tmp.dst.width, tmp.dst.height)) {
		BoxRec extents = box[0];
		int i;

		for (i = 1; i < n; i++) {
			if (extents.x1 < box[i].x1)
				extents.x1 = box[i].x1;
			if (extents.y1 < box[i].y1)
				extents.y1 = box[i].y1;

			if (extents.x2 > box[i].x2)
				extents.x2 = box[i].x2;
			if (extents.y2 > box[i].y2)
				extents.y2 = box[i].y2;
		}

		if (!sna_render_composite_redirect(sna, &tmp,
						   extents.x1 + dst_dx,
						   extents.y1 + dst_dy,
						   extents.x2 - extents.x1,
						   extents.y2 - extents.y1))
			goto fallback_tiled;
	}

	tmp.src.filter = SAMPLER_FILTER_NEAREST;
	tmp.src.repeat = SAMPLER_EXTEND_NONE;
	tmp.src.card_format = gen5_get_card_format(tmp.src.pict_format);
	if (too_large(src->drawable.width, src->drawable.height)) {
		BoxRec extents = box[0];
		int i;

		for (i = 1; i < n; i++) {
			if (extents.x1 < box[i].x1)
				extents.x1 = box[i].x1;
			if (extents.y1 < box[i].y1)
				extents.y1 = box[i].y1;

			if (extents.x2 > box[i].x2)
				extents.x2 = box[i].x2;
			if (extents.y2 > box[i].y2)
				extents.y2 = box[i].y2;
		}

		if (!sna_render_pixmap_partial(sna, src, src_bo, &tmp.src,
					       extents.x1 + src_dx,
					       extents.y1 + src_dy,
					       extents.x2 - extents.x1,
					       extents.y2 - extents.y1))
			goto fallback_tiled_dst;
	} else {
		tmp.src.bo = kgem_bo_reference(src_bo);
		tmp.src.width  = src->drawable.width;
		tmp.src.height = src->drawable.height;
		tmp.src.offset[0] = tmp.src.offset[1] = 0;
		tmp.src.scale[0] = 1.f/src->drawable.width;
		tmp.src.scale[1] = 1.f/src->drawable.height;
	}

	tmp.is_affine = TRUE;
	tmp.floats_per_vertex = 3;
	tmp.floats_per_rect = 9;
	tmp.u.gen5.wm_kernel = WM_KERNEL;
	tmp.u.gen5.ve_id = 1;

	if (!kgem_check_bo(&sna->kgem, dst_bo, src_bo, NULL)) {
		kgem_submit(&sna->kgem);
		if (!kgem_check_bo(&sna->kgem, dst_bo, src_bo, NULL))
			goto fallback_tiled_src;
	}

	dst_dx += tmp.dst.x;
	dst_dy += tmp.dst.y;
	tmp.dst.x = tmp.dst.y = 0;

	src_dx += tmp.src.offset[0];
	src_dy += tmp.src.offset[1];

	gen5_copy_bind_surfaces(sna, &tmp);
	gen5_align_vertex(sna, &tmp);

	do {
		int n_this_time;

		n_this_time = gen5_get_rectangles(sna, &tmp, n,
						  gen5_copy_bind_surfaces);
		n -= n_this_time;

		do {
			DBG(("	(%d, %d) -> (%d, %d) + (%d, %d)\n",
			     box->x1 + src_dx, box->y1 + src_dy,
			     box->x1 + dst_dx, box->y1 + dst_dy,
			     box->x2 - box->x1, box->y2 - box->y1));
			OUT_VERTEX(box->x2 + dst_dx, box->y2 + dst_dy);
			OUT_VERTEX_F((box->x2 + src_dx) * tmp.src.scale[0]);
			OUT_VERTEX_F((box->y2 + src_dy) * tmp.src.scale[1]);

			OUT_VERTEX(box->x1 + dst_dx, box->y2 + dst_dy);
			OUT_VERTEX_F((box->x1 + src_dx) * tmp.src.scale[0]);
			OUT_VERTEX_F((box->y2 + src_dy) * tmp.src.scale[1]);

			OUT_VERTEX(box->x1 + dst_dx, box->y1 + dst_dy);
			OUT_VERTEX_F((box->x1 + src_dx) * tmp.src.scale[0]);
			OUT_VERTEX_F((box->y1 + src_dy) * tmp.src.scale[1]);

			box++;
		} while (--n_this_time);
	} while (n);

	gen5_vertex_flush(sna);
	sna_render_composite_redirect_done(sna, &tmp);
	kgem_bo_destroy(&sna->kgem, tmp.src.bo);
	return TRUE;

fallback_tiled_src:
	kgem_bo_destroy(&sna->kgem, tmp.src.bo);
fallback_tiled_dst:
	if (tmp.redirect.real_bo)
		kgem_bo_destroy(&sna->kgem, tmp.dst.bo);
fallback_tiled:
	return sna_tiling_copy_boxes(sna, alu,
				     src, src_bo, src_dx, src_dy,
				     dst, dst_bo, dst_dx, dst_dy,
				     box, n);
}

static void
gen5_render_copy_blt(struct sna *sna,
		     const struct sna_copy_op *op,
		     int16_t sx, int16_t sy,
		     int16_t w,  int16_t h,
		     int16_t dx, int16_t dy)
{
	DBG(("%s: src=(%d, %d), dst=(%d, %d), size=(%d, %d)\n", __FUNCTION__,
	     sx, sy, dx, dy, w, h));

	gen5_get_rectangles(sna, &op->base, 1, gen5_copy_bind_surfaces);

	OUT_VERTEX(dx+w, dy+h);
	OUT_VERTEX_F((sx+w)*op->base.src.scale[0]);
	OUT_VERTEX_F((sy+h)*op->base.src.scale[1]);

	OUT_VERTEX(dx, dy+h);
	OUT_VERTEX_F(sx*op->base.src.scale[0]);
	OUT_VERTEX_F((sy+h)*op->base.src.scale[1]);

	OUT_VERTEX(dx, dy);
	OUT_VERTEX_F(sx*op->base.src.scale[0]);
	OUT_VERTEX_F(sy*op->base.src.scale[1]);
}

static void
gen5_render_copy_done(struct sna *sna,
		      const struct sna_copy_op *op)
{
	if (sna->render_state.gen5.vertex_offset)
		gen5_vertex_flush(sna);

	DBG(("%s()\n", __FUNCTION__));
}

static Bool
gen5_render_copy(struct sna *sna, uint8_t alu,
		 PixmapPtr src, struct kgem_bo *src_bo,
		 PixmapPtr dst, struct kgem_bo *dst_bo,
		 struct sna_copy_op *op)
{
	DBG(("%s (alu=%d)\n", __FUNCTION__, alu));

	if (sna_blt_compare_depth(&src->drawable, &dst->drawable) &&
	    sna_blt_copy(sna, alu,
			 src_bo, dst_bo,
			 dst->drawable.bitsPerPixel,
			 op))
		return TRUE;

	if (!(alu == GXcopy || alu == GXclear) || src_bo == dst_bo ||
	    too_large(src->drawable.width, src->drawable.height) ||
	    too_large(dst->drawable.width, dst->drawable.height)) {
fallback:
		if (!sna_blt_compare_depth(&src->drawable, &dst->drawable))
			return FALSE;

		return sna_blt_copy(sna, alu, src_bo, dst_bo,
				    dst->drawable.bitsPerPixel,
				    op);
	}

	if (dst->drawable.depth == src->drawable.depth) {
		op->base.dst.format = sna_render_format_for_depth(dst->drawable.depth);
		op->base.src.pict_format = op->base.dst.format;
	} else {
		op->base.dst.format = sna_format_for_depth(dst->drawable.depth);
		op->base.src.pict_format = sna_format_for_depth(src->drawable.depth);
	}
	if (!gen5_check_format(op->base.src.pict_format))
		goto fallback;

	op->base.op = alu == GXcopy ? PictOpSrc : PictOpClear;

	op->base.dst.pixmap = dst;
	op->base.dst.width  = dst->drawable.width;
	op->base.dst.height = dst->drawable.height;
	op->base.dst.bo = dst_bo;

	op->base.src.bo = src_bo;
	op->base.src.card_format =
		gen5_get_card_format(op->base.src.pict_format);
	op->base.src.width  = src->drawable.width;
	op->base.src.height = src->drawable.height;
	op->base.src.scale[0] = 1.f/src->drawable.width;
	op->base.src.scale[1] = 1.f/src->drawable.height;
	op->base.src.filter = SAMPLER_FILTER_NEAREST;
	op->base.src.repeat = SAMPLER_EXTEND_NONE;

	op->base.is_affine = true;
	op->base.floats_per_vertex = 3;
	op->base.floats_per_rect = 9;
	op->base.u.gen5.wm_kernel = WM_KERNEL;
	op->base.u.gen5.ve_id = 1;

	if (!kgem_check_bo(&sna->kgem, dst_bo, src_bo, NULL))  {
		kgem_submit(&sna->kgem);
		if (!kgem_check_bo(&sna->kgem, dst_bo, src_bo, NULL))
			goto fallback;
	}

	if (kgem_bo_is_dirty(src_bo)) {
		if (sna_blt_compare_depth(&src->drawable, &dst->drawable) &&
		    sna_blt_copy(sna, alu,
				 src_bo, dst_bo,
				 dst->drawable.bitsPerPixel,
				 op))
			return TRUE;
	}

	gen5_copy_bind_surfaces(sna, &op->base);
	gen5_align_vertex(sna, &op->base);

	op->blt  = gen5_render_copy_blt;
	op->done = gen5_render_copy_done;
	return TRUE;
}

static void
gen5_fill_bind_surfaces(struct sna *sna,
			const struct sna_composite_op *op)
{
	uint32_t *binding_table;
	uint16_t offset;

	gen5_get_batch(sna);

	binding_table = gen5_composite_get_binding_table(sna, &offset);

	binding_table[0] =
		gen5_bind_bo(sna,
			     op->dst.bo, op->dst.width, op->dst.height,
			     gen5_get_dest_format(op->dst.format),
			     TRUE);
	binding_table[1] =
		gen5_bind_bo(sna,
			     op->src.bo, 1, 1,
			     GEN5_SURFACEFORMAT_B8G8R8A8_UNORM,
			     FALSE);

	if (sna->kgem.surface == offset &&
	    *(uint64_t *)(sna->kgem.batch + sna->render_state.gen5.surface_table) == *(uint64_t*)binding_table) {
		sna->kgem.surface +=
			sizeof(struct gen5_surface_state_padded)/sizeof(uint32_t);
		offset = sna->render_state.gen5.surface_table;
	}

	gen5_emit_state(sna, op, offset);
}

static inline bool prefer_blt_fill(struct sna *sna)
{
#if PREFER_BLT_FILL
	return true;
#else
	return sna->kgem.mode != KGEM_RENDER;
#endif
}

static Bool
gen5_render_fill_boxes(struct sna *sna,
		       CARD8 op,
		       PictFormat format,
		       const xRenderColor *color,
		       PixmapPtr dst, struct kgem_bo *dst_bo,
		       const BoxRec *box, int n)
{
	struct sna_composite_op tmp;
	uint32_t pixel;

	DBG(("%s op=%x, color=%08x, boxes=%d x [((%d, %d), (%d, %d))...]\n",
	     __FUNCTION__, op, pixel, n, box->x1, box->y1, box->x2, box->y2));

	if (op >= ARRAY_SIZE(gen5_blend_op)) {
		DBG(("%s: fallback due to unhandled blend op: %d\n",
		     __FUNCTION__, op));
		return FALSE;
	}

	if (prefer_blt_fill(sna) ||
	    too_large(dst->drawable.width, dst->drawable.height) ||
	    !gen5_check_dst_format(format)) {
		uint8_t alu = -1;

		if (op == PictOpClear || (op == PictOpOutReverse && color->alpha >= 0xff00))
			alu = GXclear;

		if (op == PictOpSrc || (op == PictOpOver && color->alpha >= 0xff00)) {
			alu = GXcopy;
			if (color->alpha <= 0x00ff)
				alu = GXclear;
		}

		pixel = 0;
		if ((alu == GXclear ||
		     (alu == GXcopy &&
		      sna_get_pixel_from_rgba(&pixel,
					      color->red,
					      color->green,
					      color->blue,
					      color->alpha,
					      format))) &&
		    sna_blt_fill_boxes(sna, alu,
				       dst_bo, dst->drawable.bitsPerPixel,
				       pixel, box, n))
			return TRUE;

		if (!gen5_check_dst_format(format))
			return FALSE;

		if (too_large(dst->drawable.width, dst->drawable.height))
			return sna_tiling_fill_boxes(sna, op, format, color,
						     dst, dst_bo, box, n);
	}

	if (op == PictOpClear)
		pixel = 0;
	else if (!sna_get_pixel_from_rgba(&pixel,
					  color->red,
					  color->green,
					  color->blue,
					  color->alpha,
					  PICT_a8r8g8b8))
		return FALSE;

	memset(&tmp, 0, sizeof(tmp));

	tmp.op = op;

	tmp.dst.pixmap = dst;
	tmp.dst.width  = dst->drawable.width;
	tmp.dst.height = dst->drawable.height;
	tmp.dst.format = format;
	tmp.dst.bo = dst_bo;

	tmp.src.bo = sna_render_get_solid(sna, pixel);
	tmp.src.filter = SAMPLER_FILTER_NEAREST;
	tmp.src.repeat = SAMPLER_EXTEND_REPEAT;

	tmp.is_affine = TRUE;
	tmp.floats_per_vertex = 3;
	tmp.floats_per_rect = 9;
	tmp.u.gen5.wm_kernel = WM_KERNEL;
	tmp.u.gen5.ve_id = 1;

	if (!kgem_check_bo(&sna->kgem, dst_bo, NULL)) {
		kgem_submit(&sna->kgem);
		assert(kgem_check_bo(&sna->kgem, dst_bo, NULL));
	}

	gen5_fill_bind_surfaces(sna, &tmp);
	gen5_align_vertex(sna, &tmp);

	do {
		int n_this_time;

		n_this_time = gen5_get_rectangles(sna, &tmp, n,
						  gen5_fill_bind_surfaces);
		n -= n_this_time;

		do {
			DBG(("	(%d, %d), (%d, %d)\n",
			     box->x1, box->y1, box->x2, box->y2));
			OUT_VERTEX(box->x2, box->y2);
			OUT_VERTEX_F(1);
			OUT_VERTEX_F(1);

			OUT_VERTEX(box->x1, box->y2);
			OUT_VERTEX_F(0);
			OUT_VERTEX_F(1);

			OUT_VERTEX(box->x1, box->y1);
			OUT_VERTEX_F(0);
			OUT_VERTEX_F(0);

			box++;
		} while (--n_this_time);
	} while (n);

	gen5_vertex_flush(sna);
	kgem_bo_destroy(&sna->kgem, tmp.src.bo);
	return TRUE;
}

static void
gen5_render_fill_op_blt(struct sna *sna,
			const struct sna_fill_op *op,
			int16_t x, int16_t y, int16_t w, int16_t h)
{
	DBG(("%s (%d, %d)x(%d, %d)\n", __FUNCTION__, x,y,w,h));

	gen5_get_rectangles(sna, &op->base, 1, gen5_fill_bind_surfaces);

	OUT_VERTEX(x+w, y+h);
	OUT_VERTEX_F(1);
	OUT_VERTEX_F(1);

	OUT_VERTEX(x, y+h);
	OUT_VERTEX_F(0);
	OUT_VERTEX_F(1);

	OUT_VERTEX(x, y);
	OUT_VERTEX_F(0);
	OUT_VERTEX_F(0);
}

fastcall static void
gen5_render_fill_op_box(struct sna *sna,
			const struct sna_fill_op *op,
			const BoxRec *box)
{
	DBG(("%s: (%d, %d),(%d, %d)\n", __FUNCTION__,
	     box->x1, box->y1, box->x2, box->y2));

	gen5_get_rectangles(sna, &op->base, 1, gen5_fill_bind_surfaces);

	OUT_VERTEX(box->x2, box->y2);
	OUT_VERTEX_F(1);
	OUT_VERTEX_F(1);

	OUT_VERTEX(box->x1, box->y2);
	OUT_VERTEX_F(0);
	OUT_VERTEX_F(1);

	OUT_VERTEX(box->x1, box->y1);
	OUT_VERTEX_F(0);
	OUT_VERTEX_F(0);
}

fastcall static void
gen5_render_fill_op_boxes(struct sna *sna,
			  const struct sna_fill_op *op,
			  const BoxRec *box,
			  int nbox)
{
	DBG(("%s: (%d, %d),(%d, %d)... x %d\n", __FUNCTION__,
	     box->x1, box->y1, box->x2, box->y2, nbox));

	do {
		int nbox_this_time;

		nbox_this_time = gen5_get_rectangles(sna, &op->base, nbox,
						     gen5_fill_bind_surfaces);
		nbox -= nbox_this_time;

		do {
			OUT_VERTEX(box->x2, box->y2);
			OUT_VERTEX_F(1);
			OUT_VERTEX_F(1);

			OUT_VERTEX(box->x1, box->y2);
			OUT_VERTEX_F(0);
			OUT_VERTEX_F(1);

			OUT_VERTEX(box->x1, box->y1);
			OUT_VERTEX_F(0);
			OUT_VERTEX_F(0);
			box++;
		} while (--nbox_this_time);
	} while (nbox);
}

static void
gen5_render_fill_op_done(struct sna *sna,
			 const struct sna_fill_op *op)
{
	if (sna->render_state.gen5.vertex_offset)
		gen5_vertex_flush(sna);
	kgem_bo_destroy(&sna->kgem, op->base.src.bo);

	DBG(("%s()\n", __FUNCTION__));
}

static Bool
gen5_render_fill(struct sna *sna, uint8_t alu,
		 PixmapPtr dst, struct kgem_bo *dst_bo,
		 uint32_t color,
		 struct sna_fill_op *op)
{
	DBG(("%s(alu=%d, color=%08x)\n", __FUNCTION__, alu, color));

	if (prefer_blt_fill(sna) &&
	    sna_blt_fill(sna, alu,
			 dst_bo, dst->drawable.bitsPerPixel,
			 color,
			 op))
		return TRUE;

	if (!(alu == GXcopy || alu == GXclear) ||
	    too_large(dst->drawable.width, dst->drawable.height))
		return sna_blt_fill(sna, alu,
				    dst_bo, dst->drawable.bitsPerPixel,
				    color,
				    op);

	if (alu == GXclear)
		color = 0;

	op->base.op = color == 0 ? PictOpClear : PictOpSrc;

	op->base.dst.pixmap = dst;
	op->base.dst.width  = dst->drawable.width;
	op->base.dst.height = dst->drawable.height;
	op->base.dst.format = sna_format_for_depth(dst->drawable.depth);
	op->base.dst.bo = dst_bo;
	op->base.dst.x = op->base.dst.y = 0;

	op->base.need_magic_ca_pass = 0;
	op->base.has_component_alpha = 0;

	op->base.src.bo =
		sna_render_get_solid(sna,
				     sna_rgba_for_color(color,
							dst->drawable.depth));
	op->base.src.filter = SAMPLER_FILTER_NEAREST;
	op->base.src.repeat = SAMPLER_EXTEND_REPEAT;

	op->base.mask.bo = NULL;
	op->base.mask.filter = SAMPLER_FILTER_NEAREST;
	op->base.mask.repeat = SAMPLER_EXTEND_NONE;

	op->base.is_affine = TRUE;
	op->base.floats_per_vertex = 3;
	op->base.floats_per_rect = 9;
	op->base.u.gen5.wm_kernel = WM_KERNEL;
	op->base.u.gen5.ve_id = 1;

	if (!kgem_check_bo(&sna->kgem, dst_bo, NULL)) {
		kgem_submit(&sna->kgem);
		assert(kgem_check_bo(&sna->kgem, dst_bo, NULL));
	}

	gen5_fill_bind_surfaces(sna, &op->base);
	gen5_align_vertex(sna, &op->base);

	op->blt   = gen5_render_fill_op_blt;
	op->box   = gen5_render_fill_op_box;
	op->boxes = gen5_render_fill_op_boxes;
	op->done  = gen5_render_fill_op_done;
	return TRUE;
}

static Bool
gen5_render_fill_one_try_blt(struct sna *sna, PixmapPtr dst, struct kgem_bo *bo,
			     uint32_t color,
			     int16_t x1, int16_t y1, int16_t x2, int16_t y2,
			     uint8_t alu)
{
	BoxRec box;

	box.x1 = x1;
	box.y1 = y1;
	box.x2 = x2;
	box.y2 = y2;

	return sna_blt_fill_boxes(sna, alu,
				  bo, dst->drawable.bitsPerPixel,
				  color, &box, 1);
}

static Bool
gen5_render_fill_one(struct sna *sna, PixmapPtr dst, struct kgem_bo *bo,
		     uint32_t color,
		     int16_t x1, int16_t y1,
		     int16_t x2, int16_t y2,
		     uint8_t alu)
{
	struct sna_composite_op tmp;

#if NO_FILL_ONE
	return gen5_render_fill_one_try_blt(sna, dst, bo, color,
					    x1, y1, x2, y2, alu);
#endif

	/* Prefer to use the BLT if already engaged */
	if (prefer_blt_fill(sna) &&
	    gen5_render_fill_one_try_blt(sna, dst, bo, color,
					 x1, y1, x2, y2, alu))
		return TRUE;

	/* Must use the BLT if we can't RENDER... */
	if (!(alu == GXcopy || alu == GXclear) ||
	    too_large(dst->drawable.width, dst->drawable.height))
		return gen5_render_fill_one_try_blt(sna, dst, bo, color,
						    x1, y1, x2, y2, alu);

	if (alu == GXclear)
		color = 0;

	tmp.op = color == 0 ? PictOpClear : PictOpSrc;

	tmp.dst.pixmap = dst;
	tmp.dst.width  = dst->drawable.width;
	tmp.dst.height = dst->drawable.height;
	tmp.dst.format = sna_format_for_depth(dst->drawable.depth);
	tmp.dst.bo = bo;
	tmp.dst.x = tmp.dst.y = 0;

	tmp.src.bo =
		sna_render_get_solid(sna,
				     sna_rgba_for_color(color,
							dst->drawable.depth));
	tmp.src.filter = SAMPLER_FILTER_NEAREST;
	tmp.src.repeat = SAMPLER_EXTEND_REPEAT;

	tmp.mask.bo = NULL;
	tmp.mask.filter = SAMPLER_FILTER_NEAREST;
	tmp.mask.repeat = SAMPLER_EXTEND_NONE;

	tmp.is_affine = TRUE;
	tmp.floats_per_vertex = 3;
	tmp.floats_per_rect = 9;
	tmp.has_component_alpha = 0;
	tmp.need_magic_ca_pass = FALSE;

	tmp.u.gen5.wm_kernel = WM_KERNEL;
	tmp.u.gen5.ve_id = 1;

	if (!kgem_check_bo(&sna->kgem, bo, NULL)) {
		_kgem_submit(&sna->kgem);
		assert(kgem_check_bo(&sna->kgem, bo, NULL));
	}

	gen5_fill_bind_surfaces(sna, &tmp);
	gen5_align_vertex(sna, &tmp);

	gen5_get_rectangles(sna, &tmp, 1, gen5_fill_bind_surfaces);

	DBG(("	(%d, %d), (%d, %d)\n", x1, y1, x2, y2));
	OUT_VERTEX(x2, y2);
	OUT_VERTEX_F(1);
	OUT_VERTEX_F(1);

	OUT_VERTEX(x1, y2);
	OUT_VERTEX_F(0);
	OUT_VERTEX_F(1);

	OUT_VERTEX(x1, y1);
	OUT_VERTEX_F(0);
	OUT_VERTEX_F(0);

	gen5_vertex_flush(sna);
	kgem_bo_destroy(&sna->kgem, tmp.src.bo);

	return TRUE;
}

static void
gen5_render_flush(struct sna *sna)
{
	gen5_vertex_close(sna);
}

static void
gen5_render_context_switch(struct kgem *kgem,
			   int new_mode)
{
	/* Ironlake has a limitation that a 3D or Media command can't
	 * be the first command after a BLT, unless it's
	 * non-pipelined.
	 *
	 * We do this by ensuring that the non-pipelined drawrect
	 * is always emitted first following a switch from BLT.
	 */
	if (kgem->mode == KGEM_BLT) {
		struct sna *sna = to_sna_from_kgem(kgem);
		sna->render_state.gen5.drawrect_limit = -1;
	}
}

static void
gen5_render_retire(struct kgem *kgem)
{
	struct sna *sna;

	sna = container_of(kgem, struct sna, kgem);
	if (!kgem->need_retire && kgem->nbatch == 0 && sna->render.vbo) {
		DBG(("%s: discarding vbo\n", __FUNCTION__));
		kgem_bo_destroy(kgem, sna->render.vbo);
		sna->render.vbo = NULL;
		sna->render.vertices = sna->render.vertex_data;
		sna->render.vertex_size = ARRAY_SIZE(sna->render.vertex_data);
		sna->render.vertex_used = 0;
		sna->render.vertex_index = 0;
	}
}

static void gen5_render_reset(struct sna *sna)
{
	sna->render_state.gen5.needs_invariant = TRUE;
	sna->render_state.gen5.vb_id = 0;
	sna->render_state.gen5.ve_id = -1;
	sna->render_state.gen5.last_primitive = -1;
	sna->render_state.gen5.last_pipelined_pointers = 0;

	sna->render_state.gen5.drawrect_offset = -1;
	sna->render_state.gen5.drawrect_limit = -1;
	sna->render_state.gen5.surface_table = -1;
}

static void gen5_render_fini(struct sna *sna)
{
	kgem_bo_destroy(&sna->kgem, sna->render_state.gen5.general_bo);
}

static uint32_t gen5_create_vs_unit_state(struct sna_static_stream *stream)
{
	struct gen5_vs_unit_state *vs = sna_static_stream_map(stream, sizeof(*vs), 32);

	/* Set up the vertex shader to be disabled (passthrough) */
	vs->thread4.nr_urb_entries = URB_VS_ENTRIES >> 2;
	vs->thread4.urb_entry_allocation_size = URB_VS_ENTRY_SIZE - 1;
	vs->vs6.vs_enable = 0;
	vs->vs6.vert_cache_disable = 1;

	return sna_static_stream_offsetof(stream, vs);
}

static uint32_t gen5_create_sf_state(struct sna_static_stream *stream,
				     uint32_t kernel)
{
	struct gen5_sf_unit_state *sf_state;

	sf_state = sna_static_stream_map(stream, sizeof(*sf_state), 32);

	sf_state->thread0.grf_reg_count = GEN5_GRF_BLOCKS(SF_KERNEL_NUM_GRF);
	sf_state->thread0.kernel_start_pointer = kernel >> 6;
	sf_state->sf1.single_program_flow = 1;
	/* scratch space is not used in our kernel */
	sf_state->thread2.scratch_space_base_pointer = 0;
	sf_state->thread3.const_urb_entry_read_length = 0;	/* no const URBs */
	sf_state->thread3.const_urb_entry_read_offset = 0;	/* no const URBs */
	sf_state->thread3.urb_entry_read_length = 1;	/* 1 URB per vertex */
	/* don't smash vertex header, read start from dw8 */
	sf_state->thread3.urb_entry_read_offset = 1;
	sf_state->thread3.dispatch_grf_start_reg = 3;
	sf_state->thread4.max_threads = SF_MAX_THREADS - 1;
	sf_state->thread4.urb_entry_allocation_size = URB_SF_ENTRY_SIZE - 1;
	sf_state->thread4.nr_urb_entries = URB_SF_ENTRIES;
	sf_state->sf5.viewport_transform = FALSE;	/* skip viewport */
	sf_state->sf6.cull_mode = GEN5_CULLMODE_NONE;
	sf_state->sf6.scissor = 0;
	sf_state->sf7.trifan_pv = 2;
	sf_state->sf6.dest_org_vbias = 0x8;
	sf_state->sf6.dest_org_hbias = 0x8;

	return sna_static_stream_offsetof(stream, sf_state);
}

static uint32_t gen5_create_sampler_state(struct sna_static_stream *stream,
					  sampler_filter_t src_filter,
					  sampler_extend_t src_extend,
					  sampler_filter_t mask_filter,
					  sampler_extend_t mask_extend)
{
	struct gen5_sampler_state *sampler_state;

	sampler_state = sna_static_stream_map(stream,
					      sizeof(struct gen5_sampler_state) * 2,
					      32);
	sampler_state_init(&sampler_state[0], src_filter, src_extend);
	sampler_state_init(&sampler_state[1], mask_filter, mask_extend);

	return sna_static_stream_offsetof(stream, sampler_state);
}

static void gen5_init_wm_state(struct gen5_wm_unit_state *state,
			       Bool has_mask,
			       uint32_t kernel,
			       uint32_t sampler)
{
	state->thread0.grf_reg_count = GEN5_GRF_BLOCKS(PS_KERNEL_NUM_GRF);
	state->thread0.kernel_start_pointer = kernel >> 6;

	state->thread1.single_program_flow = 0;

	/* scratch space is not used in our kernel */
	state->thread2.scratch_space_base_pointer = 0;
	state->thread2.per_thread_scratch_space = 0;

	state->thread3.const_urb_entry_read_length = 0;
	state->thread3.const_urb_entry_read_offset = 0;

	state->thread3.urb_entry_read_offset = 0;
	/* wm kernel use urb from 3, see wm_program in compiler module */
	state->thread3.dispatch_grf_start_reg = 3;	/* must match kernel */

	state->wm4.sampler_count = 0;	/* hardware requirement */

	state->wm4.sampler_state_pointer = sampler >> 5;
	state->wm5.max_threads = PS_MAX_THREADS - 1;
	state->wm5.transposed_urb_read = 0;
	state->wm5.thread_dispatch_enable = 1;
	/* just use 16-pixel dispatch (4 subspans), don't need to change kernel
	 * start point
	 */
	state->wm5.enable_16_pix = 1;
	state->wm5.enable_8_pix = 0;
	state->wm5.early_depth_test = 1;

	/* Each pair of attributes (src/mask coords) is two URB entries */
	if (has_mask) {
		state->thread1.binding_table_entry_count = 3;	/* 2 tex and fb */
		state->thread3.urb_entry_read_length = 4;
	} else {
		state->thread1.binding_table_entry_count = 2;	/* 1 tex and fb */
		state->thread3.urb_entry_read_length = 2;
	}

	/* binding table entry count is only used for prefetching,
	 * and it has to be set 0 for Ironlake
	 */
	state->thread1.binding_table_entry_count = 0;
}

static uint32_t gen5_create_cc_viewport(struct sna_static_stream *stream)
{
	struct gen5_cc_viewport vp;

	vp.min_depth = -1.e35;
	vp.max_depth = 1.e35;

	return sna_static_stream_add(stream, &vp, sizeof(vp), 32);
}

static uint32_t gen5_create_cc_unit_state(struct sna_static_stream *stream)
{
	uint8_t *ptr, *base;
	uint32_t vp;
	int i, j;

	vp = gen5_create_cc_viewport(stream);
	base = ptr =
		sna_static_stream_map(stream,
				      GEN5_BLENDFACTOR_COUNT*GEN5_BLENDFACTOR_COUNT*64,
				      64);

	for (i = 0; i < GEN5_BLENDFACTOR_COUNT; i++) {
		for (j = 0; j < GEN5_BLENDFACTOR_COUNT; j++) {
			struct gen5_cc_unit_state *state =
				(struct gen5_cc_unit_state *)ptr;

			state->cc3.blend_enable =
				!(j == GEN5_BLENDFACTOR_ZERO && i == GEN5_BLENDFACTOR_ONE);
			state->cc4.cc_viewport_state_offset = vp >> 5;

			state->cc5.logicop_func = 0xc;	/* COPY */
			state->cc5.ia_blend_function = GEN5_BLENDFUNCTION_ADD;

			/* Fill in alpha blend factors same as color, for the future. */
			state->cc5.ia_src_blend_factor = i;
			state->cc5.ia_dest_blend_factor = j;

			state->cc6.blend_function = GEN5_BLENDFUNCTION_ADD;
			state->cc6.clamp_post_alpha_blend = 1;
			state->cc6.clamp_pre_alpha_blend = 1;
			state->cc6.src_blend_factor = i;
			state->cc6.dest_blend_factor = j;

			ptr += 64;
		}
	}

	return sna_static_stream_offsetof(stream, base);
}

static Bool gen5_render_setup(struct sna *sna)
{
	struct gen5_render_state *state = &sna->render_state.gen5;
	struct sna_static_stream general;
	struct gen5_wm_unit_state_padded *wm_state;
	uint32_t sf[2], wm[KERNEL_COUNT];
	int i, j, k, l, m;

	sna_static_stream_init(&general);

	/* Zero pad the start. If you see an offset of 0x0 in the batchbuffer
	 * dumps, you know it points to zero.
	 */
	null_create(&general);

	/* Set up the two SF states (one for blending with a mask, one without) */
	sf[0] = sna_static_stream_add(&general,
				      sf_kernel,
				      sizeof(sf_kernel),
				      64);
	sf[1] = sna_static_stream_add(&general,
				      sf_kernel_mask,
				      sizeof(sf_kernel_mask),
				      64);
	for (m = 0; m < KERNEL_COUNT; m++) {
		wm[m] = sna_static_stream_add(&general,
					      wm_kernels[m].data,
					      wm_kernels[m].size,
					      64);
	}

	state->vs = gen5_create_vs_unit_state(&general);

	state->sf[0] = gen5_create_sf_state(&general, sf[0]);
	state->sf[1] = gen5_create_sf_state(&general, sf[1]);


	/* Set up the WM states: each filter/extend type for source and mask, per
	 * kernel.
	 */
	wm_state = sna_static_stream_map(&general,
					  sizeof(*wm_state) * KERNEL_COUNT *
					  FILTER_COUNT * EXTEND_COUNT *
					  FILTER_COUNT * EXTEND_COUNT,
					  64);
	state->wm = sna_static_stream_offsetof(&general, wm_state);
	for (i = 0; i < FILTER_COUNT; i++) {
		for (j = 0; j < EXTEND_COUNT; j++) {
			for (k = 0; k < FILTER_COUNT; k++) {
				for (l = 0; l < EXTEND_COUNT; l++) {
					uint32_t sampler_state;

					sampler_state =
						gen5_create_sampler_state(&general,
									  i, j,
									  k, l);

					for (m = 0; m < KERNEL_COUNT; m++) {
						gen5_init_wm_state(&wm_state->state,
								   wm_kernels[m].has_mask,
								   wm[m],
								   sampler_state);
						wm_state++;
					}
				}
			}
		}
	}

	state->cc = gen5_create_cc_unit_state(&general);

	state->general_bo = sna_static_stream_fini(sna, &general);
	return state->general_bo != NULL;
}

Bool gen5_render_init(struct sna *sna)
{
	if (!gen5_render_setup(sna))
		return FALSE;

	sna->kgem.context_switch = gen5_render_context_switch;
	sna->kgem.retire = gen5_render_retire;

	sna->render.composite = gen5_render_composite;
#if !NO_COMPOSITE_SPANS
	sna->render.composite_spans = gen5_render_composite_spans;
#endif
	sna->render.video = gen5_render_video;

	sna->render.copy_boxes = gen5_render_copy_boxes;
	sna->render.copy = gen5_render_copy;

	sna->render.fill_boxes = gen5_render_fill_boxes;
	sna->render.fill = gen5_render_fill;
	sna->render.fill_one = gen5_render_fill_one;

	sna->render.flush = gen5_render_flush;
	sna->render.reset = gen5_render_reset;
	sna->render.fini = gen5_render_fini;

	sna->render.max_3d_size = MAX_3D_SIZE;
	sna->render.max_3d_pitch = 1 << 18;
	return TRUE;
}
