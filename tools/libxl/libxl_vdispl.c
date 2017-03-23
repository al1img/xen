/*
 * Copyright (C) 2016 EPAM Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#include "libxl_osdeps.h"

#include "libxl_internal.h"

static int libxl__device_vdispl_setdefault(libxl__gc *gc, uint32_t domid,
                                           libxl_device_vdispl *vdispl)
{
    int rc;

    rc = libxl__resolve_domid(gc, vdispl->backend_domname,
                              &vdispl->backend_domid);

    if (vdispl->devid == -1) {
        vdispl->devid = libxl__device_nextid(gc, domid, "vdispl");
    }

    return rc;
}

static int libxl__from_xenstore_vdispl(libxl__gc *gc, const char *be_path,
                                       uint32_t devid,
                                       libxl_device_vdispl *vdispl)
{
    vdispl->devid = devid;

    return libxl__backendpath_parse_domid(gc, be_path, &vdispl->backend_domid);
}

static int libxl__device_from_vdispl(libxl__gc *gc, uint32_t domid,
                                     libxl_device_vdispl *vdispl,
                                     libxl__device *device)
{
   device->backend_devid   = vdispl->devid;
   device->backend_domid   = vdispl->backend_domid;
   device->backend_kind    = LIBXL__DEVICE_KIND_VDISPL;
   device->devid           = vdispl->devid;
   device->domid           = domid;
   device->kind            = LIBXL__DEVICE_KIND_VDISPL;

   return 0;
}

static void libxl__set_xenstore_vdispl(libxl__gc *gc, uint32_t domid,
                                       libxl_device_vdispl *vdispl,
                                       flexarray_t **front, flexarray_t **back)
{
    *front = flexarray_make(gc, 16, 1);
    *back = flexarray_make(gc, 16, 1);

    flexarray_append(*back, "frontend-id");
    flexarray_append(*back, GCSPRINTF("%d", domid));
    flexarray_append(*back, "online");
    flexarray_append(*back, "1");
    flexarray_append(*back, "state");
    flexarray_append(*back, GCSPRINTF("%d", XenbusStateInitialising));
    flexarray_append(*back, "handle");
    flexarray_append(*back, GCSPRINTF("%d", vdispl->devid));

    flexarray_append(*front, "backend-id");
    flexarray_append(*front, GCSPRINTF("%d", vdispl->backend_domid));
    flexarray_append(*front, "state");
    flexarray_append(*front, GCSPRINTF("%d", XenbusStateInitialising));
    flexarray_append(*front, "handle");
    flexarray_append(*front, GCSPRINTF("%d", vdispl->devid));
    flexarray_append(*front, "be_alloc");
    flexarray_append(*front, GCSPRINTF("%d", vdispl->be_alloc));
}

static void libxl__update_config_vdispl(libxl__gc *gc,
                                        libxl_device_vdispl *dst,
                                        libxl_device_vdispl *src)
{
    dst->devid = src->devid;
    dst->be_alloc = src->be_alloc;
}

static int libxl_device_vdispl_compare(libxl_device_vdispl *d1,
                                       libxl_device_vdispl *d2)
{
    return COMPARE_DEVID(d1, d2);
}

static void libxl__device_vdispl_add(libxl__egc *egc, uint32_t domid,
                                     libxl_device_vdispl *vdispl,
                                     libxl__ao_device *aodev)
{
    libxl__device_add(egc, domid, &libxl__vdispl_devtype, vdispl, aodev);
}

libxl_device_vdispl *libxl_device_vdispl_list(libxl_ctx *ctx, uint32_t domid,
                                              int *num)
{
    return libxl__device_list(&libxl__vdispl_devtype, ctx, domid, num);
}

void libxl_device_vdispl_list_free(libxl_device_vdispl* list, int num)
{
    libxl__device_list_free(&libxl__vdispl_devtype, list, num);
}

int libxl_device_vdispl_getinfo(libxl_ctx *ctx, uint32_t domid,
                                libxl_device_vdispl *vdispl,
                                libxl_vdisplinfo *info)
{
    GC_INIT(ctx);
    char *libxl_path, *dompath, *devpath;
    char *val;
    int rc;

    libxl_vdisplinfo_init(info);
    dompath = libxl__xs_get_dompath(gc, domid);
    info->devid = vdispl->devid;

    devpath = GCSPRINTF("%s/device/vdispl/%d", dompath, info->devid);
    libxl_path = GCSPRINTF("%s/device/vdispl/%d",
                           libxl__xs_libxl_path(gc, domid),
                           info->devid);
    info->backend = xs_read(ctx->xsh, XBT_NULL,
                            GCSPRINTF("%s/backend", libxl_path),
                            NULL);
    if (!info->backend) { rc = ERROR_FAIL; goto out; }

    rc = libxl__backendpath_parse_domid(gc, info->backend, &info->backend_id);
    if (rc) { goto out; }

    val = libxl__xs_read(gc, XBT_NULL, GCSPRINTF("%s/state", devpath));
    info->state = val ? strtoul(val, NULL, 10) : -1;

    info->frontend = xs_read(ctx->xsh, XBT_NULL,
                             GCSPRINTF("%s/frontend", libxl_path),
                             NULL);
    info->frontend_id = domid;

    val = libxl__xs_read(gc, XBT_NULL, GCSPRINTF("%s/be_alloc", libxl_path));
    info->be_alloc = val ? strtoul(val, NULL, 10) : 0;

    rc = 0;

out:
     GC_FREE;
     return rc;
}

int libxl_devid_to_device_vdispl(libxl_ctx *ctx, uint32_t domid,
                                 int devid, libxl_device_vdispl *vdispl)
{
    libxl_device_vdispl *vdispls = NULL;
    int n, i;
    int rc;

    libxl_device_vdispl_init(vdispl);

    vdispls = libxl_device_vdispl_list(ctx, domid, &n);

    if (!vdispls) { rc = ERROR_NOTFOUND; goto out; }

    for (i = 0; i < n; ++i) {
        if (devid == vdispls[i].devid) {
            libxl_device_vdispl_copy(ctx, vdispl, &vdispls[i]);
            rc = 0;
            goto out;
        }
    }

    rc = ERROR_NOTFOUND;

out:

    if (vdispls) {
        libxl_device_vdispl_list_free(vdispls, n);
    }
    return rc;
}

LIBXL_DEFINE_DEVICE_ADD(vdispl)
static LIBXL_DEFINE_DEVICES_ADD(vdispl)
LIBXL_DEFINE_DEVICE_REMOVE(vdispl)

DEFINE_DEVICE_TYPE_STRUCT(vdispl,
    .update_config = (void (*)(libxl__gc *, void *, void *))
                     libxl__update_config_vdispl,
    .from_xenstore = (int (*)(libxl__gc *, const char *, uint32_t, void *))
                     libxl__from_xenstore_vdispl,
    .set_xenstore_config = (void (*)(libxl__gc *, uint32_t, void *,
                           flexarray_t **, flexarray_t **))
                           libxl__set_xenstore_vdispl
);

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
