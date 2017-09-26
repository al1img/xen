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

#include "libxl_internal.h"
#include "xen/io/sndif.h"

static int libxl__device_vsnd_setdefault(libxl__gc *gc, uint32_t domid,
                                         libxl_device_vsnd *vsnd,
                                         bool hotplug)
{
    return libxl__resolve_domid(gc, vsnd->backend_domname,
                                &vsnd->backend_domid);
}

static int libxl__device_from_vsnd(libxl__gc *gc, uint32_t domid,
                                   libxl_device_vsnd *vsnd,
                                   libxl__device *device)
{
   device->backend_devid   = vsnd->devid;
   device->backend_domid   = vsnd->backend_domid;
   device->backend_kind    = LIBXL__DEVICE_KIND_VSND;
   device->devid           = vsnd->devid;
   device->domid           = domid;
   device->kind            = LIBXL__DEVICE_KIND_VSND;

   return 0;
}

static int libxl__sample_rates_from_string(libxl__gc *gc, const char *str,
                                           libxl_vsnd_params *params)
{
    char *tmp = libxl__strdup(gc, str);

    params->num_sample_rates = 0;
    params->sample_rates = NULL;

    char *p = strtok(tmp, " ,");

    while (p != NULL) {
        params->sample_rates = realloc(params->sample_rates,
                                       sizeof(*params->sample_rates) *
                                       (params->num_sample_rates + 1));
        params->sample_rates[params->num_sample_rates++] = strtoul(p, NULL, 0);
        p = strtok(NULL, " ,");
    }

    return 0;
}

static int libxl__sample_formats_from_string(libxl__gc *gc, const char *str,
                                             libxl_vsnd_params *params)
{
    int rc;
    char *tmp = libxl__strdup(gc, str);

    params->num_sample_formats = 0;
    params->sample_formats = NULL;

    char *p = strtok(tmp, " ,");

    while (p != NULL) {
        params->sample_formats = realloc(params->sample_formats,
                                         sizeof(*params->sample_formats) *
                                         (params->num_sample_formats + 1));

        libxl_vsnd_pcm_format format;

        rc = libxl_vsnd_pcm_format_from_string(p, &format);
        if (rc) return rc;

        params->sample_formats[params->num_sample_formats++] = format;
        p = strtok(NULL, " ,");
    }

    return 0;
}

static int libxl__params_from_xenstore(libxl__gc *gc, const char *path,
                                       libxl_vsnd_params *params)
{
    const char *tmp;
    int rc;

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/"XENSND_FIELD_SAMPLE_RATES,
                                          path), &tmp);
    if (rc) return rc;

    if (tmp) {
        rc = libxl__sample_rates_from_string(gc, tmp, params);
        if (rc) return rc;
    }

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/"XENSND_FIELD_SAMPLE_FORMATS,
                                          path), &tmp);
    if (rc) return rc;

    if (tmp) {
        rc = libxl__sample_formats_from_string(gc, tmp, params);
        if (rc) return rc;
    }

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                 GCSPRINTF("%s/"XENSND_FIELD_CHANNELS_MIN,
                                           path), &tmp);
    if (rc) return rc;

    if (tmp) {
        params->channels_min = strtoul(tmp, NULL, 0);
    }

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                 GCSPRINTF("%s/"XENSND_FIELD_CHANNELS_MAX,
                                           path), &tmp);
    if (rc) return rc;

    if (tmp) {
        params->channels_max = strtoul(tmp, NULL, 0);
    }

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                 GCSPRINTF("%s/"XENSND_FIELD_BUFFER_SIZE,
                                           path), &tmp);
    if (rc) return rc;

    if (tmp) {
        params->buffer_size = strtoul(tmp, NULL, 0);
    }

    return 0;
}

static int libxl__stream_from_xenstore(libxl__gc *gc, const char *path,
                                       libxl_vsnd_stream *stream)
{
    const char *tmp;
    int rc;

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/"XENSND_FIELD_STREAM_UNIQUE_ID,
                                          path), &tmp);
    if (rc) return rc;

    if (tmp) {
        stream->id = strtoul(tmp, NULL, 0);
    }

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/"XENSND_FIELD_TYPE,
                                          path), &tmp);
    if (rc) return rc;

    if (tmp) {
        libxl_vsnd_stream_type type;

        rc = libxl_vsnd_stream_type_from_string(tmp, &type);
        if (rc) return rc;

        stream->type = type;
    }

    rc = libxl__params_from_xenstore(gc, path, &stream->params);
    if (rc) return rc;

    return 0;
}

static int libxl__pcm_from_xenstore(libxl__gc *gc, const char *path,
                                    libxl_vsnd_pcm *pcm)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    const char *tmp;
    int rc;

    pcm->name = xs_read(ctx->xsh, XBT_NULL,
                        GCSPRINTF("%s/"XENSND_FIELD_DEVICE_NAME, path), NULL);

    rc = libxl__params_from_xenstore(gc, path, &pcm->params);

    pcm->streams = NULL;
    pcm->num_vsnd_streams = 0;

    do {
        char *stream_path = GCSPRINTF("%s/%d", path, pcm->num_vsnd_streams);

        rc = libxl__xs_read_checked(gc, XBT_NULL, stream_path, &tmp);
        if (rc) return rc;

        if (tmp) {
            pcm->streams = realloc(pcm->streams, sizeof(*pcm->streams) *
                                   (++pcm->num_vsnd_streams));

            libxl_vsnd_stream_init(&pcm->streams[pcm->num_vsnd_streams - 1]);

            rc = libxl__stream_from_xenstore(gc, stream_path,
                                             &pcm->streams[pcm->num_vsnd_streams - 1]);
            if (rc) return rc;
        }
    } while (tmp);


    return 0;
}

static int libxl__vsnd_from_xenstore(libxl__gc *gc, const char *libxl_path,
                                     libxl_devid devid,
                                     libxl_device_vsnd *vsnd)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    const char *tmp;
    const char *fe_path;
    int rc;

    vsnd->devid = devid;
    rc = libxl__xs_read_mandatory(gc, XBT_NULL,
                                  GCSPRINTF("%s/backend", libxl_path),
                                  &tmp);
    if (rc) return rc;

    rc = libxl__backendpath_parse_domid(gc, tmp, &vsnd->backend_domid);
    if (rc) return rc;

    rc = libxl__xs_read_mandatory(gc, XBT_NULL,
                                  GCSPRINTF("%s/frontend", libxl_path),
                                  &fe_path);
    if (rc) return rc;

    vsnd->short_name = xs_read(ctx->xsh, XBT_NULL,
                               GCSPRINTF("%s/"XENSND_FIELD_VCARD_SHORT_NAME,
                               fe_path), NULL);

    vsnd->long_name = xs_read(ctx->xsh, XBT_NULL,
                              GCSPRINTF("%s/"XENSND_FIELD_VCARD_LONG_NAME,
                              fe_path), NULL);

    rc = libxl__params_from_xenstore(gc, fe_path, &vsnd->params);

    vsnd->pcms = NULL;
    vsnd->num_vsnd_pcms = 0;

    do {
        char *pcm_path = GCSPRINTF("%s/%d", fe_path, vsnd->num_vsnd_pcms);

        rc = libxl__xs_read_checked(gc, XBT_NULL, pcm_path, &tmp);
        if (rc) return rc;

        if (tmp) {
            vsnd->pcms = realloc(vsnd->pcms, sizeof(*vsnd->pcms) *
                                 (++vsnd->num_vsnd_pcms));

            libxl_vsnd_pcm_init(&vsnd->pcms[vsnd->num_vsnd_pcms - 1]);

            rc = libxl__pcm_from_xenstore(gc, pcm_path,
                                          &vsnd->pcms[vsnd->num_vsnd_pcms - 1]);
            if (rc) return rc;
        }
    } while (tmp);

    return 0;
}

static void libxl__update_config_vsnd(libxl__gc *gc,
                                      libxl_device_vsnd *dst,
                                      libxl_device_vsnd *src)
{
    dst->devid = src->devid;
}

static int libxl_device_vsnd_compare(libxl_device_vsnd *d1,
                                     libxl_device_vsnd *d2)
{
    return COMPARE_DEVID(d1, d2);
}

static void libxl__device_vsnd_add(libxl__egc *egc, uint32_t domid,
                                     libxl_device_vsnd *vsnd,
                                     libxl__ao_device *aodev)
{
    libxl__device_add_async(egc, domid, &libxl__vsnd_devtype, vsnd, aodev);
}

static unsigned int libxl__rates_to_str_vsnd(char *str, uint32_t *sample_rates,
                                             int num_sample_rates)
{
    unsigned int len;
    int i;

    len = 0;

    if (num_sample_rates == 0) {
        return len;
    }

    for (i = 0; i < num_sample_rates - 1; i++) {
        if (str) {
            len += sprintf(&str[len], "%u,", sample_rates[i]);
        } else {
            len += snprintf(NULL, 0, "%u,", sample_rates[i]);
        }
    }

    if (str) {
        len += sprintf(&str[len], "%u", sample_rates[i]);
    } else {
        len += snprintf(NULL, 0, "%u", sample_rates[i]);
    }

    return len;
}

static unsigned int libxl__formats_to_str_vsnd(char *str,
                                               libxl_vsnd_pcm_format *sample_formats,
                                               int num_sample_formats)
{
    unsigned int len;
    int i;

    len = 0;

    if (num_sample_formats == 0) {
        return len;
    }

    for (i = 0; i < num_sample_formats - 1; i++) {
        if (str) {
            len += sprintf(&str[len], "%s,",
                           libxl_vsnd_pcm_format_to_string(sample_formats[i]));
        } else {
            len += snprintf(NULL, 0, "%s,",
                            libxl_vsnd_pcm_format_to_string(sample_formats[i]));
        }
    }

    if (str) {
        len += sprintf(&str[len], "%s",
                       libxl_vsnd_pcm_format_to_string(sample_formats[i]));
    } else {
        len += snprintf(NULL, 0, "%s",
                        libxl_vsnd_pcm_format_to_string(sample_formats[i]));
    }

    return len;
}

static int libxl__set_params_vsnd(libxl__gc *gc, char *path,
                                  libxl_vsnd_params *params, flexarray_t *front)
{
    char *buffer;
    int len;
    int rc;

    if (params->sample_rates) {
        // calculate required string size;
        len = libxl__rates_to_str_vsnd(NULL, params->sample_rates,
                                       params->num_sample_rates);

        if (len) {
            buffer = libxl__malloc(gc, len + 1);

            libxl__rates_to_str_vsnd(buffer, params->sample_rates,
                                     params->num_sample_rates);
            rc = flexarray_append_pair(front,
                                       GCSPRINTF("%s"XENSND_FIELD_SAMPLE_RATES,
                                                 path), buffer);
            if (rc) return rc;
        }
    }

    if (params->sample_formats) {
        // calculate required string size;
        len = libxl__formats_to_str_vsnd(NULL, params->sample_formats,
                                         params->num_sample_formats);

        if (len) {
            buffer = libxl__malloc(gc, len + 1);

            libxl__formats_to_str_vsnd(buffer, params->sample_formats,
                                     params->num_sample_formats);
            rc = flexarray_append_pair(front,
                                       GCSPRINTF("%s"XENSND_FIELD_SAMPLE_FORMATS,
                                                 path), buffer);
            if (rc) return rc;
        }
    }

    if (params->channels_min) {
        rc = flexarray_append_pair(front,
                                   GCSPRINTF("%s"XENSND_FIELD_CHANNELS_MIN, path),
                                   GCSPRINTF("%u", params->channels_min));
        if (rc) return rc;
    }

    if (params->channels_max) {
        rc = flexarray_append_pair(front,
                                   GCSPRINTF("%s"XENSND_FIELD_CHANNELS_MAX, path),
                                   GCSPRINTF("%u", params->channels_max));
        if (rc) return rc;
    }

    if (params->buffer_size) {
        rc = flexarray_append_pair(front,
                                   GCSPRINTF("%s"XENSND_FIELD_BUFFER_SIZE, path),
                                   GCSPRINTF("%u", params->buffer_size));
        if (rc) return rc;
    }

    return 0;
}

static int libxl__set_streams_vsnd(libxl__gc *gc, char *path,
                                   libxl_vsnd_stream *streams,
                                   int num_streams, flexarray_t *front)
{
    int i;
    int rc;

    for (i = 0; i < num_streams; i++) {
        rc = flexarray_append_pair(front,
                 GCSPRINTF("%s%d/"XENSND_FIELD_STREAM_UNIQUE_ID, path, i),
                 GCSPRINTF("%u", streams[i].id));
        if (rc) return rc;

        const char *type = libxl_vsnd_stream_type_to_string(streams[i].type);

        if (type) {
            rc = flexarray_append_pair(front,
                     GCSPRINTF("%s%d/"XENSND_FIELD_TYPE, path, i),
                     (char *)type);
            if (rc) return rc;
        }

        rc = libxl__set_params_vsnd(gc, GCSPRINTF("%s%d/", path, i),
                                    &streams[i].params, front);
        if (rc) return rc;
    }

    return 0;
}

static int libxl__set_pcms_vsnd(libxl__gc *gc, libxl_vsnd_pcm *pcms,
                                int num_pcms, flexarray_t *front)
{
    int i;
    int rc;

    for (i = 0; i < num_pcms; i++) {
        if (pcms[i].name) {
            rc = flexarray_append_pair(front,
                                       GCSPRINTF("%d/"XENSND_FIELD_DEVICE_NAME, i),
                                       pcms[i].name);
            if (rc) return rc;
        }

        char *path = GCSPRINTF("%d/", i);

        rc = libxl__set_params_vsnd(gc, path, &pcms[i].params, front);
        if (rc) return rc;

        rc = libxl__set_streams_vsnd(gc, path, pcms[i].streams,
                                     pcms[i].num_vsnd_streams, front);
        if (rc) return rc;
    }

    return 0;
}

static int libxl__set_xenstore_vsnd(libxl__gc *gc, uint32_t domid,
                                    libxl_device_vsnd *vsnd,
                                    flexarray_t *back, flexarray_t *front,
                                    flexarray_t *ro_front)
{
    int rc;

    if (vsnd->long_name) {
        rc = flexarray_append_pair(front, XENSND_FIELD_VCARD_LONG_NAME,
                                   vsnd->long_name);
        if (rc) return rc;
    }

    if (vsnd->short_name) {
        rc = flexarray_append_pair(front, XENSND_FIELD_VCARD_SHORT_NAME,
                                   vsnd->short_name);
        if (rc) return rc;
    }

    rc = libxl__set_params_vsnd(gc, "", &vsnd->params, front);
    if (rc) return rc;

    rc = libxl__set_pcms_vsnd(gc, vsnd->pcms, vsnd->num_vsnd_pcms, front);
    if (rc) return rc;

    return 0;
}

static int libxl__device_stream_getinfo(libxl__gc *gc, const char *path,
                                        libxl_vsnd_pcm* pcm,
                                        libxl_pcminfo *info)
{
    const char *tmp;
    int i;
    int rc;

    info->num_vsnd_streams = pcm->num_vsnd_streams;
    info->streams = malloc(sizeof(*info->streams) * info->num_vsnd_streams);

    for (i = 0; i < info->num_vsnd_streams; i++)
    {
        libxl_streaminfo_init(&info->streams[i]);

        rc = libxl__xs_read_checked(gc, XBT_NULL,
                                    GCSPRINTF("%s/%d/"XENSND_FIELD_RING_REF,
                                    path, i), &tmp);
        if (rc) return rc;

        info->streams[i].req_rref = tmp ? strtoul(tmp, NULL, 10) : -1;

        rc = libxl__xs_read_checked(gc, XBT_NULL,
                                    GCSPRINTF("%s/%d/"XENSND_FIELD_EVT_CHNL,
                                    path, i), &tmp);
        if (rc) return rc;

        info->streams[i].req_evtch = tmp ? strtoul(tmp, NULL, 10) : -1;
    }

    return 0;
}

static int libxl__device_pcm_getinfo(libxl__gc *gc, const char *path,
                                     libxl_device_vsnd *vsnd,
                                     libxl_vsndinfo *info)
{
    int i;
    int rc;

    info->num_vsnd_pcms = vsnd->num_vsnd_pcms;
    info->pcms = malloc(sizeof(*info->pcms) * info->num_vsnd_pcms);

    for (i = 0; i < info->num_vsnd_pcms; i++)
    {
        libxl_pcminfo_init(&info->pcms[i]);

        rc = libxl__device_stream_getinfo(gc, GCSPRINTF("%s/%d", path, i),
                                          &vsnd->pcms[i], &info->pcms[i]);
        if (rc) return rc;
    }

    return 0;
}

int libxl_device_vsnd_getinfo(libxl_ctx *ctx, uint32_t domid,
                              libxl_device_vsnd *vsnd,
                              libxl_vsndinfo *info)
{
    GC_INIT(ctx);
    char *libxl_path, *dompath, *devpath;
    const char *val;
    int rc;

    libxl_vsndinfo_init(info);
    dompath = libxl__xs_get_dompath(gc, domid);
    info->devid = vsnd->devid;

    devpath = GCSPRINTF("%s/device/%s/%d", dompath, libxl__vsnd_devtype.entry,
                                           info->devid);
    libxl_path = GCSPRINTF("%s/device/%s/%d",
                           libxl__xs_libxl_path(gc, domid),
                           libxl__vsnd_devtype.entry, info->devid);

    info->backend = xs_read(ctx->xsh, XBT_NULL,
                            GCSPRINTF("%s/backend", libxl_path), NULL);

    rc = libxl__backendpath_parse_domid(gc, info->backend, &info->backend_id);
    if (rc) goto out;

    val = xs_read(ctx->xsh, XBT_NULL, GCSPRINTF("%s/state", devpath), NULL);

    info->state = val ? strtoul(val, NULL, 10) : -1;

    info->frontend = xs_read(ctx->xsh, XBT_NULL,
                             GCSPRINTF("%s/frontend", libxl_path), NULL);

    info->frontend_id = domid;

    rc = libxl__device_pcm_getinfo(gc, devpath, vsnd, info);
    if (rc) goto out;

    rc = 0;

out:
     GC_FREE;
     return rc;
}

int libxl_devid_to_device_vsnd(libxl_ctx *ctx, uint32_t domid,
                               int devid, libxl_device_vsnd *vsnd)
{
    GC_INIT(ctx);

    libxl_device_vsnd *vsnds = NULL;
    int n, i;
    int rc;

    libxl_device_vsnd_init(vsnd);

    vsnds = libxl__device_list(gc, &libxl__vsnd_devtype, domid, &n);

    if (!vsnds) { rc = ERROR_NOTFOUND; goto out; }

    for (i = 0; i < n; ++i) {
        if (devid == vsnds[i].devid) {
            libxl_device_vsnd_copy(ctx, vsnd, &vsnds[i]);
            rc = 0;
            goto out;
        }
    }

    rc = ERROR_NOTFOUND;

out:

    if (vsnds)
        libxl__device_list_free(&libxl__vsnd_devtype, vsnds, n);

    GC_FREE;
    return rc;
}

LIBXL_DEFINE_DEVICE_ADD(vsnd)
static LIBXL_DEFINE_DEVICES_ADD(vsnd)
LIBXL_DEFINE_DEVICE_REMOVE(vsnd)
static LIBXL_DEFINE_UPDATE_DEVID(vsnd, "vsnd")
LIBXL_DEFINE_DEVICE_LIST(vsnd)

DEFINE_DEVICE_TYPE_STRUCT(vsnd,
    .update_config = (void (*)(libxl__gc *, void *, void *))
                     libxl__update_config_vsnd,
    .from_xenstore = (int (*)(libxl__gc *, const char *, libxl_devid, void *))
                     libxl__vsnd_from_xenstore,
    .set_xenstore_config = (int (*)(libxl__gc *, uint32_t, void *,
                                    flexarray_t *back, flexarray_t *front,
                                    flexarray_t *ro_front))
                           libxl__set_xenstore_vsnd
);

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
