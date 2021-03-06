/*
 * DPVS is a software load balancer (Virtual Server) based on DPDK.
 *
 * Copyright (C) 2017 iQIYI (www.iqiyi.com).
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "common.h"
#include "dpip.h"
#include "conf/route.h"
#include "sockopt.h"

static void route_help(void)
{
    fprintf(stderr, 
        "Usage:\n"
        "    dpip route { show | flush | help }\n"
        "    dpip route { add | del | set } ROUTE\n"
        "Parameters:\n"
        "    ROUTE      := PREFIX [ via ADDR ] [ dev IFNAME ] [ OPTIONS ]\n"
        "    PREFIX     := { ADDR/PLEN | ADDR | default }\n"
        "    OPTIONS    := [ SCOPE | mtu MTU | src ADDR | tos TOS\n"
        "                    | metric NUM | PROTOCOL | FLAGS ]\n"
        "    SCOPE      := [ scope { host | link | global | NUM } ]\n"
        "    PROTOCOL   := [ proto { auto | boot | static | ra | NUM } ]\n"
        "    FLAGS      := [ onlink | local ]\n"
        "Examples:\n"
        "    dpip route show\n"
        "    dpip route add default via 10.0.0.1\n"
        "    dpip route add 172.0.0.0/16 via 172.0.0.3 dev dpdk0\n"
        "    dpip route add 192.168.0.0/24 dev dpdk0\n"
        "    dpip route del 172.0.0.0/16\n"
        "    dpip route set 172.0.0.0/16 via 172.0.0.1\n"
        "    dpip route flush\n"
        );
}

static const char *proto_itoa(int proto)
{
    struct {
        uint8_t iproto;
        const char *sproto;
    } proto_tab[] = {
        { ROUTE_CF_PROTO_AUTO, "auto" },
        { ROUTE_CF_PROTO_BOOT, "boot" },
        { ROUTE_CF_PROTO_STATIC, "static" },
        { ROUTE_CF_PROTO_RA, "ra" },
        { ROUTE_CF_PROTO_REDIRECT, "redirect" },
    };
    int i;
    static char num[64];

    num[0] = '\0';
    for (i = 0; i < NELEMS(proto_tab); i++) {
        if (proto == proto_tab[i].iproto)
            return proto_tab[i].sproto;
    }

    snprintf(num, sizeof(num), "%d", proto);
    return num;
}

static const char *scope_itoa(int scope)
{
    struct {
        uint8_t iscope;
        const char *sscope;
    } scope_tab[] = {
        { ROUTE_CF_SCOPE_HOST, "host" },
        { ROUTE_CF_SCOPE_KNI, "kni_host"},
        { ROUTE_CF_SCOPE_LINK, "link" },
        { ROUTE_CF_SCOPE_GLOBAL, "global" },
    };
    int i;
    static char num[64];

    num[0] = '\0';
    for (i = 0; i < NELEMS(scope_tab); i++) {
        if (scope == scope_tab[i].iscope)
            return scope_tab[i].sscope;
    }

    snprintf(num, sizeof(num), "%d", scope);
    return num;
}

static const char *flags_itoa(uint32_t flags)
{
    static char flags_buf[64];
    int left = sizeof(flags_buf);

    flags_buf[0] = '\0';

    if (flags & ROUTE_CF_FLAG_ONLINK)
        left -= snprintf(flags_buf + strlen(flags_buf), left, "%s ", "onlink");

    return flags_buf;
}

static void route_dump(const struct dp_vs_route_conf *route)
{
    char dst[64], via[64], src[64];

    printf("%s %s/%d via %s src %s dev %s"
            " mtu %d tos %d scope %s metric %d proto %s %s\n",
            af_itoa(route->af), 
            inet_ntop(route->af, &route->dst, dst, sizeof(dst)) ? dst : "::", 
            route->plen,
            inet_ntop(route->af, &route->via, via, sizeof(via)) ? via : "::",
            inet_ntop(route->af, &route->src, src, sizeof(src)) ? src : "::",
            route->ifname, route->mtu, route->tos, scope_itoa(route->scope),
            route->metric, proto_itoa(route->proto), flags_itoa(route->flags));

    return;
}

static int route_parse_args(struct dpip_conf *conf, 
                            struct dp_vs_route_conf *route)
{
    char *prefix = NULL;

    memset(route, 0, sizeof(*route));
    route->af = conf->af;
    route->scope = ROUTE_CF_SCOPE_NONE;

    while (conf->argc > 0) {
        if (strcmp(conf->argv[0], "via") == 0) {
            NEXTARG_CHECK(conf, "via");
            if (inet_pton_try(&route->af, conf->argv[0], &route->via) <= 0)
                return -1;
        } else if (strcmp(conf->argv[0], "dev") == 0) {
            NEXTARG_CHECK(conf, "dev");
            snprintf(route->ifname, sizeof(route->ifname), "%s", conf->argv[0]);
        } else if (strcmp(conf->argv[0], "tos") == 0) {
            NEXTARG_CHECK(conf, "tos");
            route->tos = atoi(conf->argv[0]);
        } else if (strcmp(conf->argv[0], "mtu") == 0) {
            NEXTARG_CHECK(conf, "mtu");
            route->mtu = atoi(conf->argv[0]);
        } else if (strcmp(conf->argv[0], "scope") == 0) {
            NEXTARG_CHECK(conf, "scope");

            if (strcmp(conf->argv[0], "host") == 0)
                route->scope = ROUTE_CF_SCOPE_HOST;
            else if (strcmp(conf->argv[0], "kni_host") == 0)
                route->scope = ROUTE_CF_SCOPE_KNI;
            else if (strcmp(conf->argv[0], "link") == 0)
                route->scope = ROUTE_CF_SCOPE_LINK;
            else if (strcmp(conf->argv[0], "global") == 0)
                route->scope = ROUTE_CF_SCOPE_GLOBAL;
            else 
                route->scope = atoi(conf->argv[0]);
        } else if (strcmp(conf->argv[0], "src") == 0) {
            NEXTARG_CHECK(conf, "src");
            if (inet_pton_try(&route->af, conf->argv[0], &route->src) <= 0)
                return -1;
        } else if (strcmp(conf->argv[0], "metric") == 0) {
            NEXTARG_CHECK(conf, "metric");
            route->metric = atoi(conf->argv[0]);
        } else if (strcmp(conf->argv[0], "proto") == 0) {
            NEXTARG_CHECK(conf, "proto");

            if (strcmp(conf->argv[0], "auto") == 0)
                route->proto = ROUTE_CF_PROTO_AUTO;
            else if (strcmp(conf->argv[0], "boot") == 0)
                route->proto = ROUTE_CF_PROTO_BOOT;
            else if (strcmp(conf->argv[0], "static") == 0)
                route->proto = ROUTE_CF_PROTO_STATIC;
            else if (strcmp(conf->argv[0], "ra") == 0)
                route->proto = ROUTE_CF_PROTO_RA;
            else 
                route->proto = atoi(conf->argv[0]);
        } else if (strcmp(conf->argv[0], "onlink") == 0) {
            ;/* on-link is output only */
        } else if (strcmp(conf->argv[0], "local") == 0) {
            route->scope = ROUTE_CF_SCOPE_HOST;
        } else {
            prefix = conf->argv[0];
        }

        NEXTARG(conf);
    }

    if (conf->argc > 0) {
        fprintf(stderr, "too many arguments\n");
        return -1;
    }

    if (conf->cmd == DPIP_CMD_SHOW)
        return 0;

    if (!prefix) {
        fprintf(stderr, "missing prefix\n");
        return -1;
    }

    /* PREFIX */
    if (strcmp(prefix, "default") == 0) {
        memset(&route->dst, 0, sizeof(route->dst));
        if (route->af == AF_UNSPEC)
            route->af = AF_INET;
    } else {
        char *addr, *plen;

        addr = prefix;
        if ((plen = strchr(addr, '/')) != NULL)
            *plen++ = '\0';

        if (inet_pton_try(&route->af, prefix, &route->dst) <= 0)
            return -1;

        route->plen = plen ? atoi(plen) : 0;
    }

    if (route->af != AF_INET && route->af != AF_INET6) {
        fprintf(stderr, "invalid family.\n");
        return -1;
    }

    /*
     * if scope is not set by user:
     *
     * IF [ @local is set ]; THEN
     *       scope == HOST
     * ELSE IF [ @via is set ]; THEN
     *       scope == GLOBAL
     * ELSE (@via is not set)
     *       scope == LINK
     */
    if (route->scope == ROUTE_CF_SCOPE_NONE) {
        if (inet_is_addr_any(route->af, &route->via)) {
            route->scope = ROUTE_CF_SCOPE_LINK;
            route->flags |= ROUTE_CF_FLAG_ONLINK;
        } else {
            route->scope = ROUTE_CF_SCOPE_GLOBAL;
        }
    }

    if (!route->plen && (strcmp(prefix, "default") != 0)) {
        if (route->af == AF_INET)
            route->plen = 32;
        else
            route->plen = 128;
    }

    if (conf->verbose)
        route_dump(route);

    return 0;
}

static int route_do_cmd(struct dpip_obj *obj, dpip_cmd_t cmd,
                        struct dpip_conf *conf)
{
    struct dp_vs_route_conf route;
    struct dp_vs_route_conf_array *array;
    size_t size, i;
    int err;

    if (route_parse_args(conf, &route) != 0)
        return EDPVS_INVAL;

    switch (conf->cmd) {
    case DPIP_CMD_ADD:
        return dpvs_setsockopt(SOCKOPT_SET_ROUTE_ADD, &route, sizeof(route));

    case DPIP_CMD_DEL:
        return dpvs_setsockopt(SOCKOPT_SET_ROUTE_DEL, &route, sizeof(route));

    case DPIP_CMD_SET:
        return dpvs_setsockopt(SOCKOPT_SET_ROUTE_SET, &route, sizeof(route));

    case DPIP_CMD_FLUSH:
        return dpvs_setsockopt(SOCKOPT_SET_ROUTE_FLUSH, NULL, 0);

    case DPIP_CMD_SHOW:
        err = dpvs_getsockopt(SOCKOPT_GET_ROUTE_SHOW, &route, sizeof(route),
                              (void **)&array, &size);
        if (err != 0)
            return err;

        if (size < sizeof(*array) 
                || size != sizeof(*array) + \
                           array->nroute * sizeof(struct dp_vs_route_conf)) {
            fprintf(stderr, "corrupted response.\n");
            dpvs_sockopt_msg_free(array);
            return EDPVS_INVAL;
        }

        for (i = 0; i < array->nroute; i++)
            route_dump(&array->routes[i]);

        dpvs_sockopt_msg_free(array);
        return EDPVS_OK;
    default:
        return EDPVS_NOTSUPP;
    }
}

struct dpip_obj dpip_route = {
    .name   = "route",
    .help   = route_help,
    .do_cmd = route_do_cmd,
};

static void __init route_init(void)
{
    dpip_register_obj(&dpip_route);
} 

static void __exit route_exit(void)
{
    dpip_unregister_obj(&dpip_route);
}
