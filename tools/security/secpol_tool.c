/****************************************************************
 * secpol_tool.c
 *
 * Copyright (C) 2005 IBM Corporation
 *
 * Authors:
 * Reiner Sailer <sailer@watson.ibm.com>
 * Stefan Berger <stefanb@watson.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * sHype policy management tool. This code runs in a domain and
 *     manages the Xen security policy by interacting with the
 *     Xen access control module via the privcmd device,
 *     which is translated into a acm_op hypercall into Xen.
 *
 * indent -i4 -kr -nut
 */


#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>
#include <netinet/in.h>
#include <stdint.h>
#include <xen/acm.h>
#include <xen/acm_ops.h>

#include <xenctrl.h>

#define PERROR(_m, _a...) \
fprintf(stderr, "ERROR: " _m " (%d = %s)\n" , ## _a ,	\
                errno, strerror(errno))

#define ALIGN8(x)  (void *)(((long)(x) + 7) & ~7)

void usage(char *progname)
{
    printf("Usage: %s ACTION\n"
           "ACTION is one of:\n"
           "\t getpolicy\n"
           "\t dumpstats\n"
           "\t loadpolicy <binary policy file>\n", progname);
    exit(-1);
}

/*************************** DUMPS *******************************/

void acm_dump_chinesewall_buffer(void *buf, int buflen)
{

    struct acm_chwall_policy_buffer *cwbuf =
        (struct acm_chwall_policy_buffer *) buf;
    domaintype_t *ssids, *conflicts, *running_types, *conflict_aggregate;
    int i, j;


    if (htonl(cwbuf->policy_code) != ACM_CHINESE_WALL_POLICY) {
        printf("CHINESE WALL POLICY CODE not found ERROR!!\n");
        return;
    }
    printf("\n\nChinese Wall policy:\n");
    printf("====================\n");
    printf("Policy version= %x.\n", ntohl(cwbuf->policy_version));
    printf("Max Types     = %x.\n", ntohl(cwbuf->chwall_max_types));
    printf("Max Ssidrefs  = %x.\n", ntohl(cwbuf->chwall_max_ssidrefs));
    printf("Max ConfSets  = %x.\n", ntohl(cwbuf->chwall_max_conflictsets));
    printf("Ssidrefs Off  = %x.\n", ntohl(cwbuf->chwall_ssid_offset));
    printf("Conflicts Off = %x.\n",
           ntohl(cwbuf->chwall_conflict_sets_offset));
    printf("Runing T. Off = %x.\n",
           ntohl(cwbuf->chwall_running_types_offset));
    printf("C. Agg. Off   = %x.\n",
           ntohl(cwbuf->chwall_conflict_aggregate_offset));
    printf("\nSSID To CHWALL-Type matrix:\n");

    ssids = (domaintype_t *) (buf + ntohl(cwbuf->chwall_ssid_offset));
    for (i = 0; i < ntohl(cwbuf->chwall_max_ssidrefs); i++) {
        printf("\n   ssidref%2x:  ", i);
        for (j = 0; j < ntohl(cwbuf->chwall_max_types); j++)
            printf("%02x ",
                   ntohs(ssids[i * ntohl(cwbuf->chwall_max_types) + j]));
    }
    printf("\n\nConfict Sets:\n");
    conflicts =
        (domaintype_t *) (buf + ntohl(cwbuf->chwall_conflict_sets_offset));
    for (i = 0; i < ntohl(cwbuf->chwall_max_conflictsets); i++) {
        printf("\n   c-set%2x:    ", i);
        for (j = 0; j < ntohl(cwbuf->chwall_max_types); j++)
            printf("%02x ",
                   ntohs(conflicts
                         [i * ntohl(cwbuf->chwall_max_types) + j]));
    }
    printf("\n");

    printf("\nRunning\nTypes:         ");
    if (ntohl(cwbuf->chwall_running_types_offset)) {
        running_types =
            (domaintype_t *) (buf +
                              ntohl(cwbuf->chwall_running_types_offset));
        for (i = 0; i < ntohl(cwbuf->chwall_max_types); i++) {
            printf("%02x ", ntohs(running_types[i]));
        }
        printf("\n");
    } else {
        printf("Not Reported!\n");
    }
    printf("\nConflict\nAggregate Set: ");
    if (ntohl(cwbuf->chwall_conflict_aggregate_offset)) {
        conflict_aggregate =
            (domaintype_t *) (buf +
                              ntohl(cwbuf->
                                    chwall_conflict_aggregate_offset));
        for (i = 0; i < ntohl(cwbuf->chwall_max_types); i++) {
            printf("%02x ", ntohs(conflict_aggregate[i]));
        }
        printf("\n\n");
    } else {
        printf("Not Reported!\n");
    }
}

void acm_dump_ste_buffer(void *buf, int buflen)
{

    struct acm_ste_policy_buffer *stebuf =
        (struct acm_ste_policy_buffer *) buf;
    domaintype_t *ssids;
    int i, j;


    if (ntohl(stebuf->policy_code) != ACM_SIMPLE_TYPE_ENFORCEMENT_POLICY) {
        printf("SIMPLE TYPE ENFORCEMENT POLICY CODE not found ERROR!!\n");
        return;
    }
    printf("\nSimple Type Enforcement policy:\n");
    printf("===============================\n");
    printf("Policy version= %x.\n", ntohl(stebuf->policy_version));
    printf("Max Types     = %x.\n", ntohl(stebuf->ste_max_types));
    printf("Max Ssidrefs  = %x.\n", ntohl(stebuf->ste_max_ssidrefs));
    printf("Ssidrefs Off  = %x.\n", ntohl(stebuf->ste_ssid_offset));
    printf("\nSSID To STE-Type matrix:\n");

    ssids = (domaintype_t *) (buf + ntohl(stebuf->ste_ssid_offset));
    for (i = 0; i < ntohl(stebuf->ste_max_ssidrefs); i++) {
        printf("\n   ssidref%2x: ", i);
        for (j = 0; j < ntohl(stebuf->ste_max_types); j++)
            printf("%02x ",
                   ntohs(ssids[i * ntohl(stebuf->ste_max_types) + j]));
    }
    printf("\n\n");
}

void acm_dump_policy_buffer(void *buf, int buflen)
{
    struct acm_policy_buffer *pol = (struct acm_policy_buffer *) buf;
    char *policy_reference_name =
        (buf + ntohl(pol->policy_reference_offset) +
         sizeof(struct acm_policy_reference_buffer));
    printf("\nPolicy dump:\n");
    printf("============\n");
    printf("POLICY REFERENCE = %s.\n", policy_reference_name);
    printf("PolicyVer = %x.\n", ntohl(pol->policy_version));
    printf("Magic     = %x.\n", ntohl(pol->magic));
    printf("Len       = %x.\n", ntohl(pol->len));
    printf("Primary   = %s (c=%x, off=%x).\n",
           ACM_POLICY_NAME(ntohl(pol->primary_policy_code)),
           ntohl(pol->primary_policy_code),
           ntohl(pol->primary_buffer_offset));
    printf("Secondary = %s (c=%x, off=%x).\n",
           ACM_POLICY_NAME(ntohl(pol->secondary_policy_code)),
           ntohl(pol->secondary_policy_code),
           ntohl(pol->secondary_buffer_offset));
    switch (ntohl(pol->primary_policy_code)) {
    case ACM_CHINESE_WALL_POLICY:
        acm_dump_chinesewall_buffer(ALIGN8(buf +
                                     ntohl(pol->primary_buffer_offset)),
                                    ntohl(pol->len) -
                                    ntohl(pol->primary_buffer_offset));
        break;

    case ACM_SIMPLE_TYPE_ENFORCEMENT_POLICY:
        acm_dump_ste_buffer(ALIGN8(buf + ntohl(pol->primary_buffer_offset)),
                            ntohl(pol->len) -
                            ntohl(pol->primary_buffer_offset));
        break;

    case ACM_NULL_POLICY:
        printf("Primary policy is NULL Policy (n/a).\n");
        break;

    default:
        printf("UNKNOWN POLICY!\n");
    }

    switch (ntohl(pol->secondary_policy_code)) {
    case ACM_CHINESE_WALL_POLICY:
        acm_dump_chinesewall_buffer(ALIGN8(buf +
                                     ntohl(pol->secondary_buffer_offset)),
                                    ntohl(pol->len) -
                                    ntohl(pol->secondary_buffer_offset));
        break;

    case ACM_SIMPLE_TYPE_ENFORCEMENT_POLICY:
        acm_dump_ste_buffer(ALIGN8(buf + ntohl(pol->secondary_buffer_offset)),
                            ntohl(pol->len) -
                            ntohl(pol->secondary_buffer_offset));
        break;

    case ACM_NULL_POLICY:
        printf("Secondary policy is NULL Policy (n/a).\n");
        break;

    default:
        printf("UNKNOWN POLICY!\n");
    }
}

/******************************* get policy ******************************/

#define PULL_CACHE_SIZE		8192
uint8_t pull_buffer[PULL_CACHE_SIZE];

int acm_domain_getpolicy(int xc_handle)
{
    struct acm_getpolicy getpolicy;
    int ret;

    memset(pull_buffer, 0x00, sizeof(pull_buffer));
    getpolicy.interface_version = ACM_INTERFACE_VERSION;
    set_xen_guest_handle(getpolicy.pullcache, pull_buffer);
    getpolicy.pullcache_size = sizeof(pull_buffer);
    ret = xc_acm_op(xc_handle, ACMOP_getpolicy, &getpolicy, sizeof(getpolicy));

    if (ret < 0) {
        printf("ACM operation failed: errno=%d\n", errno);
        if (errno == EACCES)
            fprintf(stderr, "ACM operation failed -- need to"
                    " rebuild the user-space tool set?\n");
    }

    /* dump policy  */
    acm_dump_policy_buffer(pull_buffer, sizeof(pull_buffer));
    return ret;
}

/************************ load binary policy ******************************/

int acm_domain_loadpolicy(int xc_handle, const char *filename)
{
    struct stat mystat;
    int ret, fd;
    off_t len;
    uint8_t *buffer;

    if ((ret = stat(filename, &mystat))) {
        printf("File %s not found.\n", filename);
        goto out;
    }

    len = mystat.st_size;
    if ((buffer = malloc(len)) == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    if ((fd = open(filename, O_RDONLY)) <= 0) {
        ret = -ENOENT;
        printf("File %s not found.\n", filename);
        goto free_out;
    }
    if (len == read(fd, buffer, len)) {
        struct acm_setpolicy setpolicy;
        /* dump it and then push it down into xen/acm */
        acm_dump_policy_buffer(buffer, len);
        setpolicy.interface_version = ACM_INTERFACE_VERSION;
        set_xen_guest_handle(setpolicy.pushcache, buffer);
        setpolicy.pushcache_size = len;
        ret = xc_acm_op(xc_handle, ACMOP_setpolicy, &setpolicy, sizeof(setpolicy));

        if (ret)
            printf
                ("ERROR setting policy.\n");
        else
            printf("Successfully changed policy.\n");

    } else {
        ret = -1;
    }
    close(fd);
  free_out:
    free(buffer);
  out:
    return ret;
}

/************************ dump hook statistics ******************************/
void dump_ste_stats(struct acm_ste_stats_buffer *ste_stats)
{
    printf("STE-Policy Security Hook Statistics:\n");
    printf("ste: event_channel eval_count      = %d\n",
           ntohl(ste_stats->ec_eval_count));
    printf("ste: event_channel denied_count    = %d\n",
           ntohl(ste_stats->ec_denied_count));
    printf("ste: event_channel cache_hit_count = %d\n",
           ntohl(ste_stats->ec_cachehit_count));
    printf("ste:\n");
    printf("ste: grant_table   eval_count      = %d\n",
           ntohl(ste_stats->gt_eval_count));
    printf("ste: grant_table   denied_count    = %d\n",
           ntohl(ste_stats->gt_denied_count));
    printf("ste: grant_table   cache_hit_count = %d\n",
           ntohl(ste_stats->gt_cachehit_count));
}

#define PULL_STATS_SIZE		8192
int acm_domain_dumpstats(int xc_handle)
{
    uint8_t stats_buffer[PULL_STATS_SIZE];
    struct acm_dumpstats dumpstats;
    int ret;
    struct acm_stats_buffer *stats;

    memset(stats_buffer, 0x00, sizeof(stats_buffer));
    dumpstats.interface_version = ACM_INTERFACE_VERSION;
    set_xen_guest_handle(dumpstats.pullcache, stats_buffer);
    dumpstats.pullcache_size = sizeof(stats_buffer);
    ret = xc_acm_op(xc_handle, ACMOP_dumpstats, &dumpstats, sizeof(dumpstats));

    if (ret < 0) {
        printf
            ("ERROR dumping policy stats. Try 'xm dmesg' to see details.\n");
        return ret;
    }
    stats = (struct acm_stats_buffer *) stats_buffer;

    printf("\nPolicy dump:\n");
    printf("============\n");
    printf("Magic     = %x.\n", ntohl(stats->magic));
    printf("Len       = %x.\n", ntohl(stats->len));

    switch (ntohl(stats->primary_policy_code)) {
    case ACM_NULL_POLICY:
        printf("NULL Policy: No statistics apply.\n");
        break;

    case ACM_CHINESE_WALL_POLICY:
        printf("Chinese Wall Policy: No statistics apply.\n");
        break;

    case ACM_SIMPLE_TYPE_ENFORCEMENT_POLICY:
        dump_ste_stats((struct acm_ste_stats_buffer *) (stats_buffer +
                                                        ntohl(stats->
                                                              primary_stats_offset)));
        break;

    default:
        printf("UNKNOWN PRIMARY POLICY ERROR!\n");
    }

    switch (ntohl(stats->secondary_policy_code)) {
    case ACM_NULL_POLICY:
        printf("NULL Policy: No statistics apply.\n");
        break;

    case ACM_CHINESE_WALL_POLICY:
        printf("Chinese Wall Policy: No statistics apply.\n");
        break;

    case ACM_SIMPLE_TYPE_ENFORCEMENT_POLICY:
        dump_ste_stats((struct acm_ste_stats_buffer *) (stats_buffer +
                                                        ntohl(stats->
                                                              secondary_stats_offset)));
        break;

    default:
        printf("UNKNOWN SECONDARY POLICY ERROR!\n");
    }
    return ret;
}

/***************************** main **************************************/

int main(int argc, char **argv)
{

    int xc_handle, ret = 0;

    if (argc < 2)
        usage(argv[0]);

    if ((xc_handle = xc_interface_open()) <= 0) {
        printf("ERROR: Could not open xen privcmd device!\n");
        exit(-1);
    }

    if (!strcmp(argv[1], "getpolicy")) {
        if (argc != 2)
            usage(argv[0]);
        ret = acm_domain_getpolicy(xc_handle);
    } else if (!strcmp(argv[1], "loadpolicy")) {
        if (argc != 3)
            usage(argv[0]);
        ret = acm_domain_loadpolicy(xc_handle, argv[2]);
    } else if (!strcmp(argv[1], "dumpstats")) {
        if (argc != 2)
            usage(argv[0]);
        ret = acm_domain_dumpstats(xc_handle);
    } else
        usage(argv[0]);

    xc_interface_close(xc_handle);
    return ret;
}
