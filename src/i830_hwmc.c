/*
 * Copyright © 2007 Intel Corporation
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
 *    Zhenyu Wang <zhenyu.z.wang@intel.com>
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "i830.h"
#include "i830_hwmc.h"

struct intel_xvmc_driver *xvmc_driver;

/* set global current driver for xvmc */
Bool intel_xvmc_set_driver(struct intel_xvmc_driver *d)
{
    if (xvmc_driver) {
	ErrorF("XvMC driver already set!\n");
	return FALSE;
    } else
	xvmc_driver = d;
    return TRUE;
}

/* check chip type and load xvmc driver */
/* This must be first called! */
Bool intel_xvmc_probe(ScrnInfoPtr pScrn)
{
    I830Ptr pI830 = I830PTR(pScrn);
    Bool ret = FALSE;

    if (IS_I9XX(pI830)) {
	if (!IS_I965G(pI830))
	    ret = intel_xvmc_set_driver(&i915_xvmc_driver);
	/*
	else
	    ret = intel_xvmc_set_driver(&i965_xvmc_driver);
	 */
    } else {
	ErrorF("Your chipset doesn't support XvMC.\n");
	return FALSE;
    }
    return TRUE;
}

void intel_xvmc_finish(ScrnInfoPtr pScrn)
{
    if (!xvmc_driver)
	return;
    (*xvmc_driver->fini)(pScrn);
}

Bool intel_xvmc_driver_init(ScreenPtr pScreen, XF86VideoAdaptorPtr xv_adaptor)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    if (!xvmc_driver) {
	ErrorF("Failed to probe XvMC driver.\n");
	return FALSE;
    }

    if (!(*xvmc_driver->init)(pScrn, xv_adaptor)) {
	ErrorF("XvMC driver initialize failed.\n");
	return FALSE;
    }
    return TRUE;
}

Bool intel_xvmc_screen_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    I830Ptr pI830 = I830PTR(pScrn);

    if (!xvmc_driver)
	return FALSE;

    if (xf86XvMCScreenInit(pScreen, 1, &xvmc_driver->adaptor)) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"[XvMC] %s driver initialized.\n",
		xvmc_driver->name);
	pI830->XvMCEnabled = TRUE;
    } else {
	intel_xvmc_finish(pScrn);
	pI830->XvMCEnabled = FALSE;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"[XvMC] Failed to initialize XvMC.\n");
	return FALSE;
    }
    return TRUE;
}

int intel_xvmc_putimage_size(ScrnInfoPtr pScrn)
{
    return (*xvmc_driver->putimage_size)(pScrn);
}
