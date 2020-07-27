/*
 * Copyright 2007,2008,2009 Peter Poeml / Novell Inc.
 * Copyright 2007,2008,2009,2010,2011,2012,2013,2014 Peter Poeml
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * mod_mirrorbrain is the heart of MirrorBrain, which
 *  - redirects clients to mirror servers, based on an SQL database
 *  - generates metalinks in real-time
 *  - generates per-file HTML mirror lists
 *  - acts as a server for verification hashes
 * See http://mirrorbrain.org/ for more information.
 *
 * Credits:
 *
 * This module was inspired by mod_offload, written by
 * Ryan C. Gordon <icculus@icculus.org>.
 *
 * It uses code from mod_authn_dbd, mod_authnz_ldap, mod_status,
 * apr_memcache, ssl_scache_memcache.c */


/* Copyright notice for the hex_to_bin() function (hex_decode())
 *
 * Copyright (c) 2001-2009, PostgreSQL Global Development Group
 *
 * PostgreSQL Database Management System
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS. */


#include "ap_config.h"
#include "httpd.h"
#include "http_request.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "util_md5.h"

#include "apr_version.h"
#include "apu_version.h"
#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_fnmatch.h"
#include "apr_hash.h"
#include "apr_base64.h"
#include "apr_dbd.h"
#include "mod_dbd.h"

#include <unistd.h> /* for getpid */
#include <math.h>   /* for sqrt() and pow() */
#include <arpa/inet.h>
#ifdef WITH_MEMCACHE
#include "mod_memcache.h"
#include "apr_memcache.h"
#endif
#include "ap_mpm.h" /* for ap_mpm_query */
#include "mod_status.h"
#include "mod_form.h"

#define wild_match(p,s) (apr_fnmatch(p,s,APR_FNM_CASE_BLIND) == APR_SUCCESS)

/* from ssl/ssl_engine_config.c */
#define cfgMerge(el,unset)  mrg->el = (add->el == (unset)) ? base->el : add->el
#define cfgMergeArray(el)   mrg->el = apr_array_append(p, add->el, base->el)
#define cfgMergeString(el)  cfgMerge(el, NULL)
#define cfgMergeBool(el)    cfgMerge(el, UNSET)
#define cfgMergeInt(el)     cfgMerge(el, UNSET)

#ifndef UNSET
#define UNSET (-1)
#endif

/* available since APR 1.3 */
#ifndef APR_ARRAY_IDX
#define APR_ARRAY_IDX(ary,i,type) (((type *)(ary)->elts)[i])
#endif
#ifndef APR_ARRAY_PUSH
#define APR_ARRAY_PUSH(ary,type) (*((type *)apr_array_push(ary)))
#endif

#ifndef MOD_MIRRORBRAIN_VER
#define MOD_MIRRORBRAIN_VER "2.19.0"
#endif
#define VERSION_COMPONENT "mod_mirrorbrain/"MOD_MIRRORBRAIN_VER

/* no space for time zones is provided here */
#define RFC3339_DATE_LEN (21)

#define MD5_DIGESTSIZE 16
#define SHA1_DIGESTSIZE 20
#define SHA256_DIGESTSIZE 32
#define ZSYNC_DIGESTSIZE 4

#ifdef WITH_MEMCACHE
#define DEFAULT_MEMCACHED_LIFETIME 600
#endif
#define DEFAULT_MIN_MIRROR_SIZE 4096

#if (APR_MAJOR_VERSION == 1 && APR_MINOR_VERSION == 2)
#define DBD_LLD_FMT "d"
#else
#define DBD_LLD_FMT "lld"
#endif

#define DEFAULT_QUERY "SELECT server.id, identifier, region, country, " \
                             "lat, lng, " \
                             "asn, prefix, score, baseurl, " \
                             "region_only, country_only, " \
                             "as_only, prefix_only, " \
                             "other_countries, file_maxsize " \
                      "FROM server " \
                      "JOIN server_files ON server.id = server_id " \
                      "JOIN files ON file_id = files.id " \
                      "LEFT JOIN serverpfx ON server.id = serverid AND family(serverpfx.prefix) = family(ipaddress(%s)) " \
                      "WHERE path = %s " \
                      "AND enabled AND status_baseurl AND score > 0"
#define DEFAULT_QUERY_HASH "SELECT id, md5hex, sha1hex, sha256hex, " \
                                  "sha1piecesize, sha1pieceshex, btihhex, pgp, " \
                                  "zblocksize, zhashlens, zsumshex " \
                           "FROM hexhash " \
                           "WHERE path = %s " \
                           "AND size = %" DBD_LLD_FMT " " \
                           "AND mtime = to_timestamp(%" DBD_LLD_FMT ") " \
                           "LIMIT 1"

/* the smaller, the smaller the effect of a raised prio in comparison to distance */
/* 5000000 -> mirror in 200km distance is preferred already when it has prio 100
 * 1000000 -> mirror in 200km distance is preferred not before it has prio 300
 * (compared to 100 as normal priority for other mirrors, and tested in
 * Germany, which is a small country with many mirrors) */
#define DISTANCE_PRIO 2000000


module AP_MODULE_DECLARE_DATA mirrorbrain_module;

/* (meta) representations of a requested file */
enum { REDIRECT, META4, METALINK, MIRRORLIST, TORRENT,
       ZSYNC, MAGNET, MD5, SHA1, SHA256, BTIH, YUMLIST, UNKNOWN };
static struct {
        int     id;
        char    *ext;
} reps [] = {
        { REDIRECT,      "" },
        { META4,         "meta4" },
        { METALINK,      "metalink" },
        { MIRRORLIST,    "mirrorlist" },
        { TORRENT,       "torrent" },
        { ZSYNC,         "zsync" },
        { MAGNET,        "magnet" },
        { MD5,           "md5" },
        { SHA1,          "sha1" },
        { SHA256,        "sha256" },
        { BTIH,          "btih" },
        { YUMLIST,       "yumlist" },
        { UNKNOWN,       NULL }
};


/* A structure that represents a mirror */
typedef struct mirror_entry mirror_entry_t;

/* a mirror */
struct mirror_entry {
    int id;
    const char *identifier;
    const char *region;       /* 2-letter-string */
    const char *country_code; /* 2-letter-string */
    float lat;                /* geographical latitude */
    float lng;                /* geographical longitude */
    int dist;                 /* geographical distance to client */
    const char *as;           /* autonomous system number as string */
    const char *prefix;       /* network prefix xxx.xxx.xxx.xxx/yy */
    apr_ipsubnet_t *ipsub;   /* ip-subnet representation of the network prefix  */
    unsigned char region_only;
    unsigned char country_only;
    unsigned char as_only;
    unsigned char prefix_only;
    int score;
    const char *baseurl;
    apr_off_t file_maxsize;
    char *other_countries;    /* comma-separated 2-letter strings */
    int rank;
    int *nsame;               /* to be able to access the number of elements in
                                 the array from qsort() */
};

/* verification hashes of a file */
typedef struct hashbag hashbag_t;
struct hashbag {
    apr_off_t id;
    const char *md5hex;
    const char *sha1hex;
    const char *sha256hex;
    int sha1piecesize;
    apr_array_header_t *sha1pieceshex;
    apr_array_header_t *zsyncpieceshex;
    const char *btihhex;
    const char *pgp;
    int zblocksize;
    const char *zhashlens;
    const char *zsumshex;
};

/* per-dir configuration */
typedef struct
{
    int engine_on;
    int debug;
    apr_off_t min_size;
    int handle_headrequest_locally;
    const char *mirror_base;
    apr_array_header_t *fallbacks;
    apr_array_header_t *exclude_mime;
    apr_array_header_t *exclude_agents;
    apr_array_header_t *exclude_networks;
    apr_array_header_t *exclude_ips;
    ap_regex_t *exclude_filemask;
    ap_regex_t *metalink_torrentadd_mask;
    const char *stampkey;
} mb_dir_conf;

/* per-server configuration */
typedef struct
{
#ifdef WITH_MEMCACHE
    const char *instance;
    int memcached_on;
    int memcached_lifetime;
#endif
    const char *metalink_publisher_name;
    const char *metalink_publisher_url;
    apr_array_header_t *tracker_urls;
    apr_array_header_t *dhtnodes;
    const char *metalink_broken_test_mirrors;
    int metalink_magnets;
    apr_array_header_t *yumdirs;
    const char *mirrorlist_stylesheet;
    const char *mirrorlist_header;
    const char *mirrorlist_footer;
    int only_hash;
    const char *query;
    const char *query_label;
    const char *query_hash;
    const char *query_hash_label;
} mb_server_conf;

typedef struct dhtnode dhtnode_t;
struct dhtnode {
    char *name;
    int port;
};

typedef struct yumdir yumdir_t;
struct yumdir {
    char *dir;                /* base dir */
    char *file;               /* marker file within base dir */
    apr_array_header_t *args; /* query arguments */
};

typedef struct yumarg yumarg_t;
struct yumarg {
    char *key;
    ap_regex_t *regexp;
};


static ap_dbd_t *(*mb_dbd_acquire_fn)(request_rec*) = NULL;
static void (*mb_dbd_prepare_fn)(server_rec*, const char*, const char*) = NULL;

static apr_version_t vsn;
static int dbd_first_row;

static void debugLog(const request_rec *r, const mb_dir_conf *cfg,
                     const char *fmt, ...)
{
    if (cfg->debug == 1) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        apr_vsnprintf(buf, sizeof (buf), fmt, ap);
        va_end(ap);
        /* we use warn loglevel to be able to debug without
         * setting the entire server into debug logging mode */
        ap_log_rerror(APLOG_MARK,
                      APLOG_WARNING,
                      APR_SUCCESS,
                      r, "[mod_mirrorbrain] %s", buf);
    }
}

static apr_status_t mb_cleanup()
{
        return APR_SUCCESS;
}

static void mb_child_init(apr_pool_t *p, server_rec *s)
{
    apr_pool_cleanup_register(p, NULL, mb_cleanup, mb_cleanup);

    srand((unsigned int)getpid());
}

static int mb_post_config(apr_pool_t *pconf, apr_pool_t *plog,
                          apr_pool_t *ptemp, server_rec *s)
{

    apr_version(&vsn);
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, s,
                 "[mod_mirrorbrain] compiled with APR/APR-Util %s/%s",
                 APR_VERSION_STRING, APU_VERSION_STRING);
    if ((vsn.major == 1) && (vsn.minor == 2)) {
        /* database access semantics were changed between 1.2 and 1.3 (strictly
         * speaking, breaking the binary compatibility */
        dbd_first_row = 0;
    } else {
        dbd_first_row = 1;
    }

    /* be visible in the server signature */
    ap_add_version_component(pconf, VERSION_COMPONENT);

    /* make sure that mod_form is loaded */
    if (ap_find_linked_module("mod_form.c") == NULL) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
                     "[mod_mirrorbrain] Module mod_form missing. It must be "
                     "loaded in order for mod_mirrorbrain to function properly");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* make sure that mod_dbd is loaded */
    if (mb_dbd_prepare_fn == NULL) {
        mb_dbd_prepare_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_prepare);
        if (mb_dbd_prepare_fn == NULL) {
            ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
                         "[mod_mirrorbrain] You must load mod_dbd to enable MirrorBrain functions");
        return HTTP_INTERNAL_SERVER_ERROR;
        }
        mb_dbd_acquire_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_acquire);
    }

    /* prepare DBD SQL statements */
    static unsigned int label_num = 0;
    server_rec *sp;
    for (sp = s; sp; sp = sp->next) {
        mb_server_conf *cfg = ap_get_module_config(sp->module_config,
                                                        &mirrorbrain_module);
        /* make a label */
        cfg->query_label = apr_psprintf(pconf, "mirrorbrain_dbd_%d", ++label_num);
        cfg->query_hash_label = apr_psprintf(pconf, "mirrorbrain_dbd_hash_%d", ++label_num);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s,
                     "[mod_mirrorbrain] preparing stmt for server %s, label_num %d, label %s",
                     s->server_hostname, label_num, cfg->query_label);
        mb_dbd_prepare_fn(sp, cfg->query, cfg->query_label);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s,
                     "[mod_mirrorbrain] preparing stmt for server %s, label_num %d, label %s",
                     s->server_hostname, label_num, cfg->query_hash_label);
        mb_dbd_prepare_fn(sp, cfg->query_hash, cfg->query_hash_label);
    }

    return OK;
}


static void *create_mb_dir_config(apr_pool_t *p, char *dirspec)
{
    mb_dir_conf *new =
      (mb_dir_conf *) apr_pcalloc(p, sizeof(mb_dir_conf));

    new->engine_on                  = UNSET;
    new->debug                      = UNSET;
    new->min_size                   = DEFAULT_MIN_MIRROR_SIZE;
    new->handle_headrequest_locally = 0;
    new->mirror_base = NULL;
    new->fallbacks = apr_array_make(p, 10, sizeof (mirror_entry_t));
    new->exclude_mime = apr_array_make(p, 0, sizeof (char *));
    new->exclude_agents = apr_array_make(p, 0, sizeof (char *));
    new->exclude_networks = apr_array_make(p, 4, sizeof (char *));
    new->exclude_ips = apr_array_make(p, 4, sizeof (char *));
    new->exclude_filemask = NULL;
    new->metalink_torrentadd_mask = NULL;
    new->stampkey = NULL;

    return (void *) new;
}

static void *merge_mb_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    mb_dir_conf *mrg  = (mb_dir_conf *) apr_pcalloc(p, sizeof(mb_dir_conf));
    mb_dir_conf *base = (mb_dir_conf *) basev;
    mb_dir_conf *add  = (mb_dir_conf *) addv;

    /* debugLog("merge_mb_dir_config: new=%08lx  base=%08lx  overrides=%08lx",
     *         (long)mrg, (long)base, (long)add); */

    cfgMergeInt(engine_on);
    cfgMergeInt(debug);
    mrg->min_size = (add->min_size != DEFAULT_MIN_MIRROR_SIZE) ? add->min_size : base->min_size;
    cfgMergeInt(handle_headrequest_locally);
    cfgMergeString(mirror_base);
    /* TODO: inheriting makes sense. But does inheriting also make sense if an
     * inheriting directory has its own fallback mirror directives?
     * mrg->fallbacks = apr_is_empty_array(add->fallbacks) ? base->fallbacks : add->fallbacks;
     * it's a merge for now
     */
    mrg->fallbacks = apr_array_append(p, base->fallbacks, add->fallbacks);
    mrg->exclude_mime = apr_array_append(p, base->exclude_mime, add->exclude_mime);
    mrg->exclude_agents = apr_array_append(p, base->exclude_agents, add->exclude_agents);
    mrg->exclude_networks = apr_array_append(p, base->exclude_networks, add->exclude_networks);
    mrg->exclude_ips = apr_array_append(p, base->exclude_ips, add->exclude_ips);
    mrg->exclude_filemask = (add->exclude_filemask == NULL) ? base->exclude_filemask : add->exclude_filemask;
    mrg->metalink_torrentadd_mask = (add->metalink_torrentadd_mask == NULL) ? base->metalink_torrentadd_mask : add->metalink_torrentadd_mask;
    cfgMergeString(stampkey);

    return (void *) mrg;
}

static void *create_mb_server_config(apr_pool_t *p, server_rec *s)
{
    mb_server_conf *new =
      (mb_server_conf *) apr_pcalloc(p, sizeof(mb_server_conf));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s,
            "[mod_mirrorbrain] creating server config");

#ifdef WITH_MEMCACHE
    new->instance = "default";
    new->memcached_on = UNSET;
    new->memcached_lifetime = UNSET;
#endif
    new->metalink_publisher_name = NULL;
    new->metalink_publisher_url = NULL;
    new->tracker_urls = apr_array_make(p, 5, sizeof (char *));
    new->dhtnodes = apr_array_make(p, 5, sizeof (dhtnode_t));
    new->metalink_broken_test_mirrors = NULL;
    new->metalink_magnets = UNSET;
    new->yumdirs = apr_array_make(p, 10, sizeof (yumdir_t));
    new->mirrorlist_stylesheet = NULL;
    new->mirrorlist_header = NULL;
    new->mirrorlist_footer = NULL;
    new->only_hash = UNSET;
    new->query = DEFAULT_QUERY;
    new->query_label = NULL;
    new->query_hash = DEFAULT_QUERY_HASH;
    new->query_hash_label = NULL;

    return (void *) new;
}

static void *merge_mb_server_config(apr_pool_t *p, void *basev, void *addv)
{
    mb_server_conf *base = (mb_server_conf *) basev;
    mb_server_conf *add = (mb_server_conf *) addv;
    mb_server_conf *mrg = apr_pcalloc(p, sizeof(mb_server_conf));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,
            "[mod_mirrorbrain] merging server config");

#ifdef WITH_MEMCACHE
    cfgMergeString(instance);
    cfgMergeBool(memcached_on);
    cfgMergeInt(memcached_lifetime);
#endif
    cfgMergeString(metalink_publisher_name);
    cfgMergeString(metalink_publisher_url);
    mrg->tracker_urls = apr_array_append(p, base->tracker_urls, add->tracker_urls);
    mrg->dhtnodes = apr_array_append(p, base->dhtnodes, add->dhtnodes);
    cfgMergeString(metalink_broken_test_mirrors);
    cfgMergeBool(metalink_magnets);
    mrg->yumdirs = apr_array_append(p, base->yumdirs, add->yumdirs);
    cfgMergeString(mirrorlist_stylesheet);
    cfgMergeString(mirrorlist_header);
    cfgMergeString(mirrorlist_footer);
    cfgMergeBool(only_hash);
    mrg->query = (strcmp(add->query, (char *) DEFAULT_QUERY) != 0) ? add->query : base->query;
    cfgMergeString(query_label);
    mrg->query_hash = (strcmp(add->query_hash, (char *) DEFAULT_QUERY_HASH) != 0)
                      ? add->query_hash : base->query_hash;
    cfgMergeString(query_hash_label);

    return (void *) mrg;
}

static const char *mb_cmd_engine(cmd_parms *cmd, void *config, int flag)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->engine_on = flag;
    cfg->mirror_base = apr_pstrdup(cmd->pool, cmd->path);
    return NULL;
}

static const char *mb_cmd_debug(cmd_parms *cmd, void *config, int flag)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->debug = flag;
    return NULL;
}

static const char *mb_cmd_minsize(cmd_parms *cmd, void *config,
                                  const char *arg1)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->min_size = apr_atoi64(arg1);
    if (cfg->min_size < 0)
        return "MirrorBrainMinSize requires a non-negative integer.";
    return NULL;
}

static const char *mb_cmd_fallback(cmd_parms *cmd, void *config,
                                   const char *arg1, const char *arg2,
                                   const char *arg3)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    mirror_entry_t *new;
    apr_uri_t uri;

    if (APR_SUCCESS != apr_uri_parse(cmd->pool, arg3, &uri)) {
        return "MirrorBrainFallback URI cannot be parsed";
    }

    new = apr_array_push(cfg->fallbacks);
    new->nsame = &cfg->fallbacks->nelts;
    new->id = 0;
    new->identifier = uri.hostname;
    new->region = apr_pstrdup(cmd->pool, arg1);
    new->country_code = apr_pstrdup(cmd->pool, arg2);
    new->lat = 0;
    new->lng = 0;
    new->dist = 0;
    new->other_countries = NULL;
    new->as = NULL;
    new->prefix = NULL;
    new->ipsub = NULL;
    new->region_only = 0;
    new->country_only = 0;
    new->as_only = 0;
    new->prefix_only = 0;
    new->score = 1; /* give it a minimal score (but with 0, it wouldn't be considered) */
    new->file_maxsize = 0;
    if (arg3[strlen(arg3) - 1] == '/') {
        new->baseurl = apr_pstrdup(cmd->pool, arg3);
    } else {
        new->baseurl = apr_pstrcat(cmd->pool, arg3, "/", NULL);
    }
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, NULL,
                 "[mod_mirrorbrain] configured fallback mirror (%s:%s): %s",
                 new->region, new->country_code, new->baseurl);

    return NULL;
}

static const char *mb_cmd_excludemime(cmd_parms *cmd, void *config,
                                      const char *arg1)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    char **mimepattern = (char **) apr_array_push(cfg->exclude_mime);
    *mimepattern = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *mb_cmd_excludeagent(cmd_parms *cmd, void *config,
                                       const char *arg1)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    char **agentpattern = (char **) apr_array_push(cfg->exclude_agents);
    *agentpattern = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *mb_cmd_excludenetwork(cmd_parms *cmd, void *config,
                                         const char *arg1)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    char **network = (char **) apr_array_push(cfg->exclude_networks);
    *network = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *mb_cmd_excludeip(cmd_parms *cmd, void *config,
                                    const char *arg1)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    char **ip = (char **) apr_array_push(cfg->exclude_ips);
    *ip = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *mb_cmd_exclude_filemask(cmd_parms *cmd, void *config,
                                           const char *arg)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->exclude_filemask = ap_pregcomp(cmd->pool, arg, AP_REG_EXTENDED);
    if (cfg->exclude_filemask == NULL) {
        return "MirrorBrainExcludeFileMask regex could not be compiled";
    }
    return NULL;
}

static const char *mb_cmd_handle_headrequest_locally(cmd_parms *cmd,
                                                     void *config, int flag)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->handle_headrequest_locally = flag;
    return NULL;
}

#ifdef WITH_MEMCACHE
static const char *mb_cmd_instance(cmd_parms *cmd, void *config,
                                   const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->instance = arg1;
    return NULL;
}
#endif

static const char *mb_cmd_dbd_query(cmd_parms *cmd, void *config,
                                    const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->query = arg1;
    return NULL;
}

static const char *mb_cmd_dbd_query_hash(cmd_parms *cmd, void *config,
                                         const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->query_hash = arg1;
    return NULL;
}

static const char *mb_cmd_metalink_hashes_prefix(cmd_parms *cmd,
                                                 void *config,
                                                 const char *arg1)
{
    return "mod_mirrorbrain: the MirrorBrainMetalinkHashesPathPrefix "
           "directive is obsolete. It has no effect. Simply remove it.";
}

static const char *mb_cmd_metalink_publisher(cmd_parms *cmd, void *config,
                                             const char *arg1,
                                             const char *arg2)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->metalink_publisher_name = arg1;
    cfg->metalink_publisher_url = arg2;
    return NULL;
}

static const char *mb_cmd_mirrorlist_header(cmd_parms *cmd, void *config,
                                            const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->mirrorlist_header = arg1;
    return NULL;
}

static const char *mb_cmd_mirrorlist_footer(cmd_parms *cmd, void *config,
                                            const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->mirrorlist_footer = arg1;
    return NULL;
}

static const char *mb_cmd_tracker_url(cmd_parms *cmd, void *config,
                                      const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    char **url = (char **) apr_array_push(cfg->tracker_urls);
    *url = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *mb_cmd_dht_node(cmd_parms *cmd, void *config,
                                   const char *arg1, const char *arg2)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    dhtnode_t *new = apr_array_push(cfg->dhtnodes);
    new->name = apr_pstrdup(cmd->pool, arg1);
    new->port = atoi(apr_pstrdup(cmd->pool, arg2));
    if (new->port <= 0)
        return "MirrorBrainDHTNode requires a positive integer "
               "as second argument (server port).";
    return NULL;
}

static const char *mb_cmd_hashes_suppress_filenames(cmd_parms *cmd, void *config,
                                                    int flag)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->only_hash = flag;
    return NULL;
}

static const char *mb_cmd_metalink_broken_test_mirrors(cmd_parms *cmd,
                                                       void *config,
                                                       const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->metalink_broken_test_mirrors = arg1;
    return NULL;
}

static const char *mb_cmd_metalink_magnet_links(cmd_parms *cmd,
                                                void *config,
                                                int flag)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->metalink_magnets = flag;
    return NULL;
}

static const char *mb_cmd_mirrorlist_stylesheet(cmd_parms *cmd, void *config,
                                                const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->mirrorlist_stylesheet = arg1;
    return NULL;
}

static const char *mb_cmd_metalink_torrentadd_mask(cmd_parms *cmd, void *config,
                                                   const char *arg)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->metalink_torrentadd_mask = ap_pregcomp(cmd->pool, arg, AP_REG_EXTENDED);
    if (cfg->metalink_torrentadd_mask == NULL) {
        return "MirrorBrainMetalinkTorrentAddMask regex could not be compiled";
    }
    return NULL;
}

static const char *mb_cmd_redirect_stamp_key(cmd_parms *cmd, void *config,
                                             const char *arg1)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->stampkey = arg1;
    return NULL;
}


static const char *mb_cmd_add_yumdir(cmd_parms *cmd, void *dummy,
                                     const char *arg)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    char *d = NULL; /* base dir */
    char *f = NULL; /* marker file within base dir */
    char *word;
    apr_array_header_t *yumargs = apr_array_make(cmd->pool, 3, sizeof (yumarg_t));

    while (*arg) {
        word = ap_getword_conf(cmd->pool, &arg);
        char *val = ap_strchr(word, '=');
        if (!val) {
            if (!d) {
                d = word;
                continue;
            } else if (!f) {
                f = word;
                continue;
            } else {
                return "Invalid MirrorBrainYumDir parameter. Parameter must be "
                       "in the form 'key=value'.";
            }
        }
        *val++ = '\0';

        yumarg_t *a = apr_array_push(yumargs);
        a->key = apr_pstrdup(cmd->pool, word);
        /* we better anchor the regexp to the start and end, because when user
         * data matches the regexp, we will substitute parts of the URL with it */
        a->regexp = ap_pregcomp(cmd->pool,
                                apr_pstrcat(cmd->pool, "^", val, "$", NULL),
                                AP_REG_EXTENDED);
        if (!a->regexp)
            return "Regular expression for ProxyRemoteMatch could not be compiled.";
    };

    if (d == NULL)
        return "MirrorBrainYumDir needs a (relative) base path";
    if (f == NULL)
        return "MirrorBrainYumDir needs a file name relative to the base path";

    if (apr_is_empty_array(yumargs))
        return "MirrorBrainYumDir needs at least one query argument";

    yumdir_t *new = apr_array_push(cfg->yumdirs);
    new->dir = apr_pstrdup(cmd->pool, d);
    new->file = apr_pstrdup(cmd->pool, f);
    new->args = yumargs;

    return NULL;
}


#ifdef WITH_MEMCACHE
static const char *mb_cmd_memcached_on(cmd_parms *cmd, void *config,
                                       int flag)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->memcached_on = flag;
    return NULL;
}

static const char *mb_cmd_memcached_lifetime(cmd_parms *cmd, void *config,
                                             const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg =
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->memcached_lifetime = atoi(arg1);
    if (cfg->memcached_lifetime <= 0)
        return "MirrorBrainMemcachedLifeTime requires an integer > 0.";
    return NULL;
}
#endif

static int find_lowest_rank(apr_array_header_t *arr)
{
    if (arr->nelts == 1) {
        return 0; /* the first and only element */
    }

    int i;
    int lowest_id = 0;
    int lowest = INT_MAX;
    mirror_entry_t *mirror;
    mirror_entry_t **mirrorp;

    mirrorp = (mirror_entry_t **)arr->elts;
    for (i = 0; i < arr->nelts; i++) {
        mirror = mirrorp[i];
        if (mirror->rank < lowest) {
            lowest = mirror->rank;
            lowest_id = i;
        }
    }
    return lowest_id;
}

static int find_closest_dist(apr_array_header_t *arr)
{
    if (arr->nelts == 1) {
        return 0; /* the first and only element */
    }

    int i, d;
    int closest_id = 0;
    int closest = INT_MAX;
    int closest_rank = INT_MAX;
    mirror_entry_t *mirror;
    mirror_entry_t **mirrorp;

    int distprio = DISTANCE_PRIO / arr->nelts;

    mirrorp = (mirror_entry_t **)arr->elts;
    for (i = 0; i < arr->nelts; i++) {
        mirror = mirrorp[i];
        d = mirror->dist + distprio / mirror->score;
        /* ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "[find_closest_dist] "
                        "i: %d, %-30s - dist: %d, score: %d, %d/score: %d, d: %d",
                        i, mirror->identifier, mirror->dist, mirror->score, distprio,
                        distprio/mirror->score, d); */
        if ( d < closest) {
            /* this mirror is closer */
            closest = d;
            closest_id = i;
            closest_rank = mirror->rank;
        } else if (d == closest) {
            /* this mirror is as close as the closest known mirror. So we pick
             * one of them randomly. */
            if (mirror->rank < closest_rank) {
                closest = d;
                closest_id = i;
                closest_rank = mirror->rank;
            }
        }
    }
    return closest_id;
}

static int cmp_mirror_rank(const void *v1, const void *v2)
{
    mirror_entry_t *m1 = *(mirror_entry_t **)v1;
    mirror_entry_t *m2 = *(mirror_entry_t **)v2;
    return m1->rank - m2->rank;
}

static int cmp_mirror_dist(const void *v1, const void *v2)
{
    mirror_entry_t *m1 = *(mirror_entry_t **)v1;
    mirror_entry_t *m2 = *(mirror_entry_t **)v2;

    int distprio = DISTANCE_PRIO / *m1->nsame;
    /* ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
     * "[cmp_mirror_dist] nsame: %d, distprio: %d", *m1->nsame, distprio); */

    return (m1->dist + distprio / m1->score) - (m2->dist + distprio / m2->score);
}

static apr_array_header_t *get_n_best_mirrors(request_rec *r, int n,
                                              apr_array_header_t *a1, apr_array_header_t *a2,
                                              apr_array_header_t *a3, apr_array_header_t *a4,
                                              apr_array_header_t *a5)
{
    int i;
    int found = 0;
    apr_array_header_t *mirrors_n_best;
    mirror_entry_t **mirrorp;

    mirrors_n_best  = apr_array_make(r->pool, n, sizeof (mirror_entry_t *));

    mirrorp = (mirror_entry_t **)a1->elts;
    for (i = 0; found < n && i < a1->nelts; i++, found++) {
        *(void **)apr_array_push(mirrors_n_best) = mirrorp[i];
    }
    mirrorp = (mirror_entry_t **)a2->elts;
    for (i = 0; found < n && i < a2->nelts; i++, found++) {
        *(void **)apr_array_push(mirrors_n_best) = mirrorp[i];
    }
    mirrorp = (mirror_entry_t **)a3->elts;
    for (i = 0; found < n && i < a3->nelts; i++, found++) {
        *(void **)apr_array_push(mirrors_n_best) = mirrorp[i];
    }
    mirrorp = (mirror_entry_t **)a4->elts;
    for (i = 0; found < n && i < a4->nelts; i++, found++) {
        *(void **)apr_array_push(mirrors_n_best) = mirrorp[i];
    }
    mirrorp = (mirror_entry_t **)a5->elts;
    for (i = 0; found < n && i < a5->nelts; i++, found++) {
        *(void **)apr_array_push(mirrors_n_best) = mirrorp[i];
    }

    return mirrors_n_best;
}

/* return the scheme of an URL, e.g. ftp for ftp://foo.example.com/ */
static const char *url_scheme(apr_pool_t *p, const char *url)
{
    const char *s;
    s = apr_pstrndup(p, url, strcspn(url, ":"));
    if (s && s[0]) {
        return s;
    }
    return "INVALID URL SCHEME";
}

/*
 * This routine returns an URL in the format needed for old (v3) or newer (IETF)
 * Metalinks.
 */
static void emit_metalink_url(request_rec *r, int rep,
                              const char *baseurl,
                              const char *country_code,
                              const char *filename, int v3prio, int prio)
{
    switch (rep) {
    case META4:
        ap_rprintf(r, "    <url location=\"%s\" priority=\"%d\">%s%s</url>\n",
                   country_code, prio, baseurl, filename);
        break;
    case METALINK:
        ap_rprintf(r, "    <url type=\"%s\" location=\"%s\" preference=\"%d\">%s%s</url>\n",
                   url_scheme(r->pool, baseurl), country_code, v3prio, baseurl, filename);
        break;
    }
}

/* set variables in the subprocess environment, to make it available for
 * logging via the CustomLog directive */
static void setenv_give(request_rec *r, const char *rep)
{
    apr_table_setn(r->subprocess_env, "GIVE", rep);
}
static void setenv_want(request_rec *r, const char *rep)
{
    apr_table_setn(r->subprocess_env, "WANT", rep);
}


/* Fast hex decoding function from PostgreSQL, src/backend/utils/adt/encode.c
 *
 * Note on binary data (bytea columns) in PostgreSQL:
 *
 * PostgreSQL escapes binary (BYTEA) data on output. But hex encoding is more
 * efficient than the traditionally (<8.5) used escaping method. Hex encoding
 * results in shorter strings, and thus less data to transfer over the wire,
 * and encoding is also done faster. Hex encoding might actually become the
 * default later. The escape format doesn't make sense for a new application
 * anymore (like us).
 * Storage on the other hand (in BYTEA data type) is as compact as could be.
 * Compact storage means that the datawill more likely fit into memory, which
 * is crucial. And the hex encoding function in PostgreSQL seems to be fast.
 *
 * This means that, on our side, we have to convert back the data from hex to
 * binary for output formats like e.g. torrents. Therefore we copied the hex
 * decoder from PostgreSQL here. */

static const int8_t hexlookup[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static char get_hex(apr_pool_t *p, char c)
{
    int         res = -1;

    if (c > 0 && c < 127)
        res = hexlookup[(unsigned char) c];

    if (res < 0)
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,
                     "[mod_mirrorbrain] invalid hexadecimal digit: \"%c\"", c);

    return (char) res;
}

static char *hex_to_bin(apr_pool_t *p, const char *src, unsigned dstlen)
{
    const char *s, *srcend;
    char *dst;
    char v1, v2, *d;

    if (!dstlen) {
        dstlen = (strlen(src) >> 1);
    }
    dst = apr_palloc(p, (dstlen));

    srcend = src + (dstlen << 1);
    s = src;
    d = dst;
    while (s < srcend) {
        v1 = get_hex(p, *s++) << 4;
        if (s >= srcend) {
            ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,
                         "[mod_mirrorbrain] invalid hexadecimal data: "
                          "odd number of digits");
        }
        v2 = get_hex(p, *s++);
        *d++ = v1 | v2;
    }

    return dst;
}

static char *hex_to_b64(apr_pool_t *p, const char *src, unsigned binlen)
{
    char *bin, *encoded;

    bin = hex_to_bin(p, src, binlen);

    encoded = (char *) apr_palloc(p, 1 + apr_base64_encode_len(binlen));
    binlen = apr_base64_encode(encoded, bin, binlen);
    encoded[binlen] = '\0'; /* make binary sequence into string */

    return encoded;
}

static hashbag_t *hashbag_fill(request_rec *r, ap_dbd_t *dbd, char *filename)
{
    mb_server_conf *scfg = NULL;
    scfg = (mb_server_conf *) ap_get_module_config(r->server->module_config,
                                                   &mirrorbrain_module);

    if (scfg->query_hash == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] No MirrorBrainDBDQueryHash configured!");
        return NULL;
    }
    if (scfg->query_hash_label == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] No database hash query prepared!");
        return NULL;
    }
    if (dbd == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] Don't have a database connection for hashes");
        return NULL;
    }

    if (!dbd->prepared) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
            "[mod_mirrorbrain] dbd->prepared hash is NULL");
        dbd = NULL; /* don't try to use again */
        return NULL;
    }

    apr_dbd_prepared_t *stmt;
    stmt = apr_hash_get(dbd->prepared, scfg->query_hash_label, APR_HASH_KEY_STRING);
    if (stmt == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "[mod_mirrorbrain] Could not get prepared statement labelled '%s'",
                      scfg->query_hash_label);
        dbd = NULL;
        return NULL;
    }


    hashbag_t *h = apr_pcalloc(r->pool, sizeof(hashbag_t));
    h->id = 0;
    h->md5hex = NULL;
    h->sha1hex = NULL;
    h->sha256hex = NULL;
    h->sha1piecesize = 0;
    h->sha1pieceshex = NULL;
    h->zsyncpieceshex = NULL;
    h->btihhex = NULL;
    h->pgp = NULL;
    h->zblocksize = 0;
    h->zhashlens = NULL;
    h->zsumshex = NULL;


    apr_status_t rv;
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;

    if (apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, stmt, 0,
                filename,
                apr_off_t_toa(r->pool, r->finfo.size),
                apr_itoa(r->pool, apr_time_sec(r->finfo.mtime)), /* APR finfo times are in microseconds */
                NULL) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] Error looking up %s in database", filename);
        return NULL;
    }

    /* we care only about the 1st row, because our query uses 'limit 1' */
    rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row, dbd_first_row);
    if (rv != APR_SUCCESS) {
        const char *errmsg = apr_dbd_error(dbd->driver, dbd->handle, rv);
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, rv, r,
                      "[mod_mirrorbrain] Could not retrieve row from database for %s "
                      "(size: %s, mtime %s): %s Likely cause: there are no hashes in "
                      "the database (yet).",
                      filename,
                      apr_off_t_toa(r->pool, r->finfo.size),
                      apr_itoa(r->pool, apr_time_sec(r->finfo.mtime)),
                      (errmsg ? errmsg : "[???]"));
        return NULL;
    }

    const char *val = NULL;
    short col = 0; /* column that we are reading out */

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for file_id");
    else
        h->id = apr_atoi64(val);

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for md5");
    } else {
        if (val[0]) {
            h->md5hex = apr_pstrdup(r->pool, val);
        }
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for sha1");
    } else {
        if (val[0])
            h->sha1hex = apr_pstrdup(r->pool, val);
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for sha256");
    } else {
        if (val[0])
            h->sha256hex = apr_pstrdup(r->pool, val);
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for sha1piecesize");
    else
        h->sha1piecesize = atoi(val);

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for sha1pieces");
    } else {
        if (val[0] && (h->sha1piecesize > 0)) {

            /* split the string into an array of the actual pieces */

            apr_off_t n = (r->finfo.size  + h->sha1piecesize - 1) / h->sha1piecesize;
            // XXX ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: %" APR_INT64_T_FMT " sha1 pieces", n);

            h->sha1pieceshex = apr_array_make(r->pool, n, sizeof(const char *));
            int max = strlen(val);
            int i;
            for (i = 0; i < n; i++) {
                if (((i + 1) * SHA1_DIGESTSIZE*2) > max)
                        break;
                APR_ARRAY_PUSH(h->sha1pieceshex, char *) = apr_pstrndup(r->pool,
                        val + (i * SHA1_DIGESTSIZE * 2), (SHA1_DIGESTSIZE * 2));
            }
            // check if we have extra zsync data appended
            if (max == (SHA1_DIGESTSIZE + ZSYNC_DIGESTSIZE) * 2 * n) {
                h->zsyncpieceshex = apr_array_make(r->pool, n, sizeof(const char *));
                for (i = 0; i < n; i++) {
                    APR_ARRAY_PUSH(h->zsyncpieceshex, char *) = apr_pstrndup(r->pool,
                            val + (n * SHA1_DIGESTSIZE * 2) + (i * ZSYNC_DIGESTSIZE * 2), (ZSYNC_DIGESTSIZE * 2));
                }
            }
        }
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for btih");
    } else {
        if (val[0])
            h->btihhex = apr_pstrdup(r->pool, val);
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for pgp");
    } else {
        if (val[0])
            h->pgp = apr_pstrdup(r->pool, val);
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for zblocksize");
    else
        h->zblocksize = atoi(val);

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for zhashlens");
    } else {
        if (val[0])
            h->zhashlens = apr_pstrdup(r->pool, val);
    }

    if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] dbd: got NULL for zsums");
    } else {
        if (val[0])
            h->zsumshex = apr_pstrdup(r->pool, val);
    }



    /* clear the cursor by accessing invalid row */
    rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row, dbd_first_row + 1);
    if (rv != -1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "[mod_mirrorbrain] found too many rows when looking up hashes for %s",
                      filename);
        return NULL;
    }

    return h;


}


static int mb_handler(request_rec *r)
{
    mb_dir_conf *cfg = NULL;
    mb_server_conf *scfg = NULL;
    char *ptr = NULL;
    char *uri = NULL;
    char *filename = NULL;
    const char *basename = NULL;
    const char *mirror_base = NULL;
    char *realfile = NULL;
    const char *thisserver, *thisport;
    unsigned int port;
    yumdir_t *yum = NULL;
    const char *clientip = NULL;
    apr_sockaddr_t *clientaddr;
    const char *query_country = NULL;
    char *query_asn = NULL;
    char fakefile = 0, only_hash = 0;
    int rep = UNKNOWN;                          /* type of a requested representation */
    char *rep_ext = NULL;                       /* extension string of a requested representation */
    char meta_negotiated = 0;                   /* a metalink representation was chosed by negotiation, i.e.
                                                   the server might still decide to return the file itself
                                                   if it's excluded from redirection by configuration */
    const char *continent_code;
    const char *country_code;
    const char *country_name;
    const char *slat, *slng;
    float lat = 0, lng = 0;
    const char *state_id, *state_name;
    const char *as;                             /* autonomous system */
    const char *prefix;                         /* network prefix */
    int i;
    int mirror_cnt = 0;
    apr_size_t len, nr;
    mirror_entry_t *new;
    mirror_entry_t *mirror;
    mirror_entry_t **mirrorp;
    mirror_entry_t *chosen = NULL;
    hashbag_t *hashbag = NULL;
    apr_status_t rv;
    apr_dbd_prepared_t *statement = NULL;
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    /* this holds all mirror_entrys */
    apr_array_header_t *mirrors;
    /* the following arrays all hold pointers into the mirrors array */
    apr_array_header_t *mirrors_same_prefix;    /* in the same network prefix */
    apr_array_header_t *mirrors_same_as;        /* in the same autonomous system */
    apr_array_header_t *mirrors_same_country;
    apr_array_header_t *mirrors_fallback_country;
    apr_array_header_t *mirrors_same_region;
    apr_array_header_t *mirrors_elsewhere;
#ifdef WITH_MEMCACHE
    apr_memcache_t *memctxt;                    /* memcache context provided by mod_memcache */
    char *m_res;
    char *m_key, *m_val;
    char newmirror = 0;
    int cached_id;
#endif
    const char *(*form_lookup)(request_rec*, const char*);
    int (*cmp_mirror_best)(const void *, const void *);
    int (*find_best) (apr_array_header_t *);


    cfg = (mb_dir_conf *)     ap_get_module_config(r->per_dir_config,
                                                   &mirrorbrain_module);
    scfg = (mb_server_conf *) ap_get_module_config(r->server->module_config,
                                                   &mirrorbrain_module);

    /* is MirrorBrainEngine disabled for this directory? */
    if (cfg->engine_on != 1) {
        return DECLINED;
    }
#ifdef WITH_MEMCACHE
    debugLog(r, cfg, "MirrorBrainEngine On, instance '%s', mirror_base '%s'",
            scfg->instance, cfg->mirror_base);
#else
    debugLog(r, cfg, "MirrorBrainEngine On, mirror_base '%s'",
            cfg->mirror_base);
#endif

    /* is it a HEAD request? */
    if (r->header_only && cfg->handle_headrequest_locally) {
        debugLog(r, cfg, "HEAD request for URI '%s'", r->unparsed_uri);
        return DECLINED;
    }

    if (r->method_number != M_GET) {
        debugLog(r, cfg, "Not a GET method for URI '%s'", r->unparsed_uri);
        return DECLINED;
    }

    /* is there PATH_INFO, and are we supposed to accept it? */
    if ((r->path_info && *r->path_info)
            && (r->used_path_info != AP_REQ_ACCEPT_PATH_INFO)) {
        debugLog(r, cfg, "ignoring request with PATH_INFO='%s'", r->path_info);
        return DECLINED;
    }

    debugLog(r, cfg, "URI: '%s'", r->unparsed_uri);
    debugLog(r, cfg, "filename: '%s'", r->filename);
    //debugLog(r, cfg, "server_hostname: '%s'", r->server->server_hostname);
    setenv_want(r, "file");

    /* parse query arguments if present, */
    /* using mod_form's form_value() */
    form_lookup = APR_RETRIEVE_OPTIONAL_FN(form_value);
    if (form_lookup && r->args) {
        if (form_lookup(r, "fakefile")) fakefile = 1;
        query_country = form_lookup(r, "country");
        query_asn = (char *) form_lookup(r, "as");
        #ifdef WITH_MEMCACHE
        if (form_lookup(r, "newmirror")) newmirror = 1;
        #endif
        if (form_lookup(r, "meta4"))   { rep = META4; rep_ext = reps[META4].ext; };
        if (form_lookup(r, "metalink")) { rep = METALINK; rep_ext = reps[METALINK].ext; };
        if (form_lookup(r, "mirrorlist")) { rep = MIRRORLIST; rep_ext = reps[MIRRORLIST].ext; }
        if (form_lookup(r, "torrent")) { rep = TORRENT; rep_ext = reps[TORRENT].ext; }
        if (form_lookup(r, "zsync"))   { rep = ZSYNC;   rep_ext = reps[ZSYNC].ext; }
        if (form_lookup(r, "magnet"))  { rep = MAGNET;  rep_ext = reps[MAGNET].ext; }
        if (form_lookup(r, "md5"))     { rep = MD5;     rep_ext = reps[MD5].ext; };
        if (form_lookup(r, "sha1"))    { rep = SHA1;    rep_ext = reps[SHA1].ext; };
        if (form_lookup(r, "sha256"))  { rep = SHA256;  rep_ext = reps[SHA256].ext; };
        if (form_lookup(r, "btih"))    { rep = BTIH;    rep_ext = reps[BTIH].ext; };
        if (form_lookup(r, "only_hash")) { only_hash = 1; };

        /* yum query to be parsed? */
        if (!apr_is_empty_array(scfg->yumdirs)) {
            for (i = 0; i < scfg->yumdirs->nelts; i++) {
                yumdir_t y;
                yumarg_t a;
                char *d, *f;
                const char *val;
                int val_len_sum = 0;
                int n_matches = 0;

                y = ((yumdir_t *) scfg->yumdirs->elts)[i];
                d = y.dir;
                f = y.file;
                //debugLog(r, cfg, "checking against yum dir %s / %s", d, f);
                for (n_matches = 0; n_matches < y.args->nelts; n_matches++) {
                    a = ((yumarg_t *) y.args->elts)[n_matches];
                    val = form_lookup(r, a.key);
                    if (!val)
                        break;
                    if (ap_regexec(a.regexp, val, 0, NULL, 0))
                        break;
                    val_len_sum += strlen(val);
                    //debugLog(r, cfg, "value '%s' matches regexp for '%s'", val, a.key);
                }

                char *src, *dst;
                char c;

                if (n_matches == y.args->nelts) {

                    rep = YUMLIST;
                    yum = apr_pcalloc(r->pool, sizeof(yumdir_t));

                    debugLog(r, cfg, "match for yum dir %s / %s", d, f);
                    yum->file = apr_pstrdup(r->pool, f);

                    if ((ptr = ap_strchr(d, '$'))) {
                        //debugLog(r, cfg, "substitution to be done");

                        src = d;
                        yum->dir = dst = apr_pcalloc(r->pool, strlen(d) + val_len_sum + 1);

                        while ((c = *src++) != '\0') {
                            if (c == '$' && apr_isdigit(*src)) {
                                nr = *src++ - '0';

                                if (nr == 0) {
                                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] "
                                            "cannot substitute $0 in '%s' -- use 1 or a greater digit", d);
                                    return HTTP_INTERNAL_SERVER_ERROR;
                                } else if (nr > n_matches) {
                                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] "
                                            "cannot substitute $%" APR_SIZE_T_FMT " in '%s' -- only %d args are defined",
                                            nr, d, y.args->nelts);
                                    return HTTP_INTERNAL_SERVER_ERROR;
                                }

                                a = ((yumarg_t *) y.args->elts)[nr - 1];
                                val = form_lookup(r, a.key);

                                debugLog(r, cfg, "substituting $%" APR_SIZE_T_FMT " with '%s'", nr, val);
                                len = strlen(val);
                                memcpy(dst, val, len);
                                dst += len;
                            } else {
                                *dst++ = c;
                            }
                        }
                        *dst = '\0';

                    } else {
                        yum->dir = apr_pstrdup(r->pool, d);
                    }
                    debugLog(r, cfg, "yum->dir: '%s', yum->file: '%s'", yum->dir, yum->file);

                    /* FIXME: maybe don't break in debug mode, to discover double matches */
                    break;
                }

            }
            if (yum == NULL) {
                debugLog(r, cfg, "yum query received, but didn't match any of the rules");
                return HTTP_NOT_FOUND;
            }
        }
    }

    if (!(query_country
         && (strlen(query_country) == 2)
         && apr_isalnum(query_country[0])
         && apr_isalnum(query_country[1]))) {
        query_country = NULL;
    }

    if (query_asn) {
        for (i = 0; apr_isdigit(query_asn[i]); i++)
            ;
        query_asn[i] = '\0';
    }

    if (rep == UNKNOWN) {
        const char *accepts;
        accepts = apr_table_get(r->headers_in, "Accept");
        if (accepts != NULL) {
            if (ap_strstr_c(accepts, "metalink4+xml")) {
                rep = META4;
                rep_ext = reps[META4].ext;
                meta_negotiated = 1;
                setenv_want(r, reps[rep].ext);
            } else if (ap_strstr_c(accepts, "metalink+xml")) {
                rep = METALINK;
                rep_ext = reps[METALINK].ext;
                meta_negotiated = 1;
                setenv_want(r, reps[rep].ext);
            }
        }
    }

    /* We might be running as a backend which sees client IPs only through HTTP
     * headers */
    clientip = apr_table_get(r->subprocess_env, "MMDB_ADDR");
    if (clientip) {
      debugLog(r, cfg, "Got clientip from MMDB_ADDR");
    } else {
      debugLog(r, cfg, "Reading clientip from request...");
#if MODULE_MAGIC_NUMBER_MAJOR >= 20111025
      clientip = apr_pstrdup(r->pool, r->useragent_ip);
#else
      clientip = apr_pstrdup(r->pool, r->connection->remote_ip);
#endif
    }
    rv = apr_sockaddr_info_get(&clientaddr, clientip, APR_UNSPEC, 0, 0, r->pool);
    if(APR_STATUS_IS_EINVAL(rv) || (rv != APR_SUCCESS)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] "
                "Error in parsing GEOIP_ADDR: '%s'", clientip);
    }
    debugLog(r, cfg, "clientip: %s", clientip);

    /* These checks apply only if the server response is not faked for testing */
    if (fakefile) {
        debugLog(r, cfg, "FAKE File -- not looking at real files");

    } else {
        if ((r->finfo.filetype == APR_DIR) && (rep != YUMLIST)) {
        /* if (ap_is_directory(r->pool, r->filename)) */
            debugLog(r, cfg, "'%s' is a directory", r->filename);
            return DECLINED;
        }

        /* if the file doesn't exist, maybe a representation of it is requested */
        if ((r->finfo.filetype != APR_REG) && (rep != YUMLIST)) {
            debugLog(r, cfg, "File does not exist according to r->finfo");

            if (r->filename[strlen(r->filename) - 1] == '.') {
                debugLog(r, cfg, "invalid file extension '.'");
                return DECLINED;
            }

            /* Try if we find a valid .metalink/.meta4/... extension. */
            char *ext;
            if ((ext = ap_strrchr(r->filename, '.')) == NULL) {
                return DECLINED;
            }

            rep = UNKNOWN;
            for (i = 0; reps[i].ext; i++) {
                if (strcmp(ext + 1, reps[i].ext) == 0) {
                    rep = i;
                    rep_ext = reps[i].ext;
                    /* debugLog(r, cfg, "File ending .%s found", rep_ext); */
                    break;
                }
            }

            switch (rep) {
                case UNKNOWN:
                    return DECLINED;

                case META4:
                case METALINK:
                case MIRRORLIST:
                case TORRENT:
                case ZSYNC:
                case MAGNET:
                case MD5:
                case SHA1:
                case SHA256:
                case BTIH:
                    setenv_want(r, reps[rep].ext);
                    debugLog(r, cfg, "Representation chosen by .%s extension", rep_ext);
                    /* note this actually modifies r->filename. */
                    ext[0] = '\0';

                    /* strip the extension from r->uri as well */
                    debugLog(r, cfg, "r->uri: '%s'", r->uri);
                    if ((ext = ap_strrchr(r->uri, '.')) != NULL) {
                        if (strcmp(ext + 1, rep_ext) == 0) {
                            ext[0] = '\0';
                        }
                    }
                    debugLog(r, cfg, "r->uri: '%s'", r->uri);

                    /* fill in finfo */
                    if ( apr_stat(&r->finfo, r->filename, APR_FINFO_SIZE | APR_FINFO_MTIME, r->pool)
                            != APR_SUCCESS ) {
                        return HTTP_NOT_FOUND;
                    }
                    break;
            }
        }

    } /* end if(!fakefile) */


    if (rep == UNKNOWN)
        rep = REDIRECT;


    if ((rep == REDIRECT) || meta_negotiated) {

        /* is the requested file too small to be worth a redirect? */
        if (!fakefile && (r->finfo.size < cfg->min_size)) {
            debugLog(r, cfg, "File '%s' too small (%s bytes, less than %s)",
                    r->filename, apr_off_t_toa(r->pool, r->finfo.size),
                    apr_off_t_toa(r->pool, cfg->min_size));
            setenv_give(r, "file");
            return DECLINED;
        }

        /* is this file excluded from mirroring? */
        if (cfg->exclude_filemask
           && !ap_regexec(cfg->exclude_filemask, r->uri, 0, NULL, 0) ) {
            debugLog(r, cfg, "File '%s' is excluded by MirrorBrainExcludeFileMask", r->uri);
            setenv_give(r, "file");
            return DECLINED;
        }

        /* is the request originating from an ip address excluded from redirecting? */
        if (!apr_is_empty_array(cfg->exclude_ips)) {
            for (i = 0; i < cfg->exclude_ips->nelts; i++) {
                char *ip = ((char **) cfg->exclude_ips->elts)[i];
                if (strcmp(ip, clientip) == 0) {
                    debugLog(r, cfg,
                        "URI request '%s' from ip '%s' is excluded from"
                        " redirecting because it matches IP '%s'",
                        r->unparsed_uri, clientip, ip);
                    setenv_give(r, "file");
                    return DECLINED;
                }
            }
        }

        /* is the request originating from a network excluded from redirecting? */
        if (!apr_is_empty_array(cfg->exclude_networks)) {
            for (i = 0; i < cfg->exclude_networks->nelts; i++) {
                char *network = ((char **) cfg->exclude_networks->elts)[i];
                if (strncmp(network, clientip, strlen(network)) == 0) {
                    debugLog(r, cfg,
                        "URI request '%s' from ip '%s' is excluded from"
                        " redirecting because it matches network '%s'",
                        r->unparsed_uri, clientip, network);
                    setenv_give(r, "file");
                    return DECLINED;
                }
            }
        }


        /* is the file in the list of mimetypes to never mirror? */
        if ((r->content_type) && !apr_is_empty_array(cfg->exclude_mime)) {
            for (i = 0; i < cfg->exclude_mime->nelts; i++) {
                char *mimetype = ((char **) cfg->exclude_mime->elts)[i];
                if (wild_match(mimetype, r->content_type)) {
                    debugLog(r, cfg,
                        "URI '%s' (%s) is excluded from redirecting"
                        " by mimetype pattern '%s'", r->unparsed_uri,
                        r->content_type, mimetype);
                    setenv_give(r, "file");
                    return DECLINED;
                }
            }
        }

        /* is this User-Agent excluded from redirecting? */
        const char *user_agent =
            (const char *) apr_table_get(r->headers_in, "User-Agent");
        if (user_agent && !apr_is_empty_array(cfg->exclude_agents)) {
            for (i = 0; i < cfg->exclude_agents->nelts; i++) {
                char *agent = ((char **) cfg->exclude_agents->elts)[i];
                if (wild_match(agent, user_agent)) {
                    debugLog(r, cfg,
                        "URI request '%s' from agent '%s' is excluded from"
                        " redirecting by User-Agent pattern '%s'",
                        r->unparsed_uri, user_agent, agent);
                    setenv_give(r, "file");
                    return DECLINED;
                }
            }
        }

    } /* end if ((rep == REDIRECT) || meta_negotiated) */


#ifdef WITH_MEMCACHE
    memctxt = ap_memcache_client(r->server);
    if (memctxt == NULL) scfg->memcached_on = 0;

    /* look for associated mirror in memcache */
    cached_id = 0;
    if (scfg->memcached_on) {
        m_key = apr_pstrcat(r->pool, "mb_", scfg->instance, "_", clientip, NULL);
        if (newmirror) {
                debugLog(r, cfg, "client requested new mirror");
        } else {
            rv = apr_memcache_getp(memctxt, r->pool, m_key, &m_res, &len, NULL);
            if (rv == APR_SUCCESS) {
                cached_id = atoi(m_res);
                debugLog(r, cfg, "IP known in memcache: mirror id %d", cached_id);
            }
            else {
                debugLog(r, cfg, "IP unknown in memcache");
            }
        }
    }
#endif


    if (scfg->query == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] No MirrorBrainDBDQuery configured!");
        setenv_give(r, "file");
        return DECLINED;
    }



    /* IPv6 is experimentally supported in mod_geoip >= 1.2.7 and GeoIP >= 1.4.8
     * Unfortunately, the API returns IPv6 matches in different variables, so
     * we always have to check two variables. Since I hate copy&paste, we
     * cheat with a macro: */
#define GETGEOIPV6HELPER(v, e) \
    v = apr_table_get(r->subprocess_env, e); \
    if (!v) { \
        v = apr_table_get(r->subprocess_env, e "_V6"); \
    }
    GETGEOIPV6HELPER(country_code, "GEOIP_COUNTRY_CODE");
    GETGEOIPV6HELPER(country_name, "GEOIP_COUNTRY_NAME");
    GETGEOIPV6HELPER(continent_code, "GEOIP_CONTINENT_CODE");
    GETGEOIPV6HELPER(slat, "GEOIP_LATITUDE");
    GETGEOIPV6HELPER(slng, "GEOIP_LONGITUDE");
    if (slat && slng) {
        lat = atof(slat);
        lng = atof(slng);
    };
    GETGEOIPV6HELPER(state_id, "GEOIP_REGION");
    GETGEOIPV6HELPER(state_name, "GEOIP_REGION_NAME");


    if (!country_code) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] could not resolve country");
        country_code = "--";
    }
    if (!continent_code) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] could not resolve continent");
        continent_code = "--";
    }

    if (query_country) {
        country_code = query_country;
    }

    debugLog(r, cfg, "Country '%s', Continent '%s'", country_code,
            continent_code);

    /* save details for logging via a CustomLog */
    apr_table_setn(r->subprocess_env, "MB_FILESIZE",
            apr_off_t_toa(r->pool, r->finfo.size));
    apr_table_set(r->subprocess_env, "MB_COUNTRY_CODE", country_code);
    apr_table_set(r->subprocess_env, "MB_CONTINENT_CODE", continent_code);


    /* see if we find info about autonomous system and network prefix
     * in the subprocess environment - set for us by mod_asn */
    if (query_asn && (query_asn[0] != '\0')) {
        as = query_asn;
    } else {
        as = apr_table_get(r->subprocess_env, "ASN");
        if (!as) {
            as = "--";
        }
    }
    prefix = apr_table_get(r->subprocess_env, "PFX");
    if (!prefix) {
        prefix = "--";
    }
    debugLog(r, cfg, "AS '%s', Prefix '%s', lat/lng %f,%f state id %s, state '%s'",
             as, prefix, lat, lng, state_id, state_name);



    /* The basedir might contain symlinks. That needs to be taken into account.
     * See discussion in http://mirrorbrain.org/issues/issue17 */
    ptr = realpath(cfg->mirror_base, apr_palloc(r->pool, APR_PATH_MAX));
    if (ptr == NULL) {
        /* this should never happen, because the MirrorBrainEngine directive would never
         * be applied to a non-existing directory */
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] Document root \'%s\' does not seem to "
                "exist. Filesystem not mounted?", cfg->mirror_base);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    mirror_base = ptr;

    /* prepare the filename to look up */
    if (rep != YUMLIST) {
        filename = apr_pstrdup(r->pool, r->filename);
    } else {
        filename = apr_pstrcat(r->pool, mirror_base, "/", yum->dir, "/", yum->file, NULL);
        debugLog(r, cfg, "yum path on disk: %s", filename);
    }

    ptr = realpath(filename, apr_palloc(r->pool, APR_PATH_MAX));
    if (ptr == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] Error canonicalizing filename '%s'", filename);
        /* return HTTP_INTERNAL_SERVER_ERROR; */
        return HTTP_NOT_FOUND;
    }

    realfile = ptr;
    debugLog(r, cfg, "Canonicalized file on disk: %s", realfile);

    /* the leading directory needs to be stripped from the file path */
    /* a directory from Apache always ends in '/'; a result from realpath() doesn't */
    filename = realfile + strlen(mirror_base) + 1;

    if (rep != YUMLIST) {
        /* keep a filename version without leading path, because metalink clients
         * will otherwise place the downloaded file into a directory hierarchy */
        if ((basename = ap_strrchr_c(filename, '/')) == NULL)
            basename = filename;
        else
            ++basename;
    }
    debugLog(r, cfg, "SQL file to look up: %s", filename);



    if (scfg->query_label == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] No database query prepared!");
        setenv_give(r, "file");
        return DECLINED;
    }

    ap_dbd_t *dbd = mb_dbd_acquire_fn(r);
    if (dbd == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] Error acquiring database connection");
        if (apr_is_empty_array(cfg->fallbacks)) {
            setenv_give(r, "file (database_not_reached)");
            return DECLINED; /* fail gracefully */
        }

    }
    if (dbd && !dbd->prepared) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
            "[mod_mirrorbrain] dbd->prepared hash is NULL");
        if (apr_is_empty_array(cfg->fallbacks)) {
            setenv_give(r, "file (dbd_prepared_is_NULL)");
            return DECLINED; /* fail gracefully */
        }
        dbd = NULL; /* stay away! */
    }
    debugLog(r, cfg, "Successfully acquired database connection.");


    switch (rep) {
    case MD5:
    case SHA1:
    case SHA256:
    case BTIH:
    case TORRENT:
    case ZSYNC:
    case MAGNET:
        hashbag = hashbag_fill(r, dbd, filename);
        if (!hashbag) {
            debugLog(r, cfg, "no hashes found in database, but needed "
                             "for %s representation", rep_ext);
            return HTTP_NOT_FOUND;
        }
    }

    switch (rep) {
    case MD5:
    case SHA1:
    case SHA256:
    case BTIH: {
        const char *h = NULL;
        switch (rep) {
        case MD5: h = hashbag->md5hex; break;
        case SHA1: h = hashbag->sha1hex; break;
        case SHA256: h = hashbag->sha256hex; break;
        case BTIH: h = hashbag->btihhex; break;
        }

        if (h && h[0]) {
            ap_set_content_type(r, "text/plain; charset=UTF-8");
            if (only_hash || (scfg->only_hash == 1) ) {
                ap_rprintf(r, "%s\n", h);
            }
            else {
                ap_rprintf(r, "%s  %s\n", h, basename);
            }
            setenv_give(r, reps[rep].ext);
            return OK;
        }
        return HTTP_NOT_FOUND;
        }
    }


    if (dbd) {

        statement = apr_hash_get(dbd->prepared, scfg->query_label, APR_HASH_KEY_STRING);
        if (!statement) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "[mod_mirrorbrain] Could not get prepared statement labelled '%s'",
                          scfg->query_label);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "[mod_mirrorbrain] Hint: connection strings defined with "
                          "DBDParams must be unique. The same string cannot be used "
                          "in two vhosts. Workaround: use a differing connect_timeout parameter");
            /* log existing prepared statements. It might help with figuring out
             * misconfigurations */
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                          "[mod_mirrorbrain] dbd->prepared hash contains %d key/value pairs",
                          apr_hash_count(dbd->prepared));
            apr_hash_index_t *hi;
            const char *label, *query;
            for (hi = apr_hash_first(r->pool, dbd->prepared); hi; hi = apr_hash_next(hi)) {
                apr_hash_this(hi, (void*) &label, NULL, (void*) &query);
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                              "[mod_mirrorbrain] dbd->prepared dump: key %s, value 0x%08lx", label, (long)query);
            }

            dbd = NULL; /* don't use */

            if (apr_is_empty_array(cfg->fallbacks)) {
                setenv_give(r, "file (dbd_statement_is_NULL)");
                return DECLINED;
            }
        }

    } else {
        if (apr_is_empty_array(cfg->fallbacks)) {
            setenv_give(r, "file");
            return DECLINED;
        }
    }


    /* no need to escape for the SQL query because we use a prepared
     * statement with bound parameters */
    if (dbd && apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, statement,
                1, /* we don't need random access actually, but
                      without it the mysql driver doesn't return results
                      once apr_dbd_num_tuples() has been called;
                      apr_dbd_get_row() will only return -1 after that. */
                clientip, filename, NULL) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "[mod_mirrorbrain] Error looking up %s in database", filename);
        if (apr_is_empty_array(cfg->fallbacks)) {
            return DECLINED;
        }
    }
    if (dbd && res) {
        mirror_cnt = apr_dbd_num_tuples(dbd->driver, res);
    }

    if (mirror_cnt > 0) {
        debugLog(r, cfg, "Found %d mirror%s", mirror_cnt,
                (mirror_cnt == 1) ? "" : "s");
    }
    /* handling of the case mirror_cnt==0 is further below. It may happen that
     * we have mirrors, but no usable ones. */

    /* allocate space for the expected results */
    mirrors              = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t));
    /* n.b., the following arrays only hold pointers into the above array */
    mirrors_same_prefix  = apr_array_make(r->pool, 1,          sizeof (mirror_entry_t *));
    mirrors_same_as      = apr_array_make(r->pool, 1,          sizeof (mirror_entry_t *));
    mirrors_same_country = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));
    mirrors_fallback_country = apr_array_make(r->pool, 5,      sizeof (mirror_entry_t *));
    mirrors_same_region  = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));
    mirrors_elsewhere    = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));


    /* store the results which the database yields, taking into account which
     * mirrors are in the same country, same region, or elsewhere */
    /* we copy all values to pool memory, because not all database drivers
     * behave the same (see http://marc.info/?l=apr-dev&m=122982975912314&w=2 ) */
    i = 1;
    while (i <= mirror_cnt) {
        char unusable = 0; /* if crucial data is missing... */
        const char *val = NULL;
        short col = 0; /* incremented for the column we are reading out */

        rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row,
#if (APR_MAJOR_VERSION == 1 && APR_MINOR_VERSION == 2)
                             /* APR 1.2 was the first version to support the DBD
                              * framework, and had a different way of counting
                              * rows, see http://mirrorbrain.org/issues/issue7
                              * */
                             i - 1
#else
                             i
#endif
                             );


        if (rv != APR_SUCCESS) {
            const char *errmsg = apr_dbd_error(dbd->driver, dbd->handle, rv);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "[mod_mirrorbrain] Error looking up %s in database: %s",
                      filename, (errmsg ? errmsg : "[???]"));
            return DECLINED;
        }

        new = apr_array_push(mirrors);
        new->nsame = &mirrors->nelts;
        new->id = 0;
        new->identifier = NULL;
        new->region = NULL;
        new->country_code = NULL;
        new->lat = 0;
        new->lng = 0;
        new->dist = 9999999;
        new->other_countries = NULL;
        new->as = NULL;
        new->prefix = NULL;
        new->ipsub = NULL;
        new->region_only = 0;
        new->country_only = 0;
        new->as_only = 0;
        new->prefix_only = 0;
        new->score = 0;
        new->file_maxsize = 0;
        new->baseurl = NULL;

        /* id */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for id");
        else
            new->id = atoi(val);

        /* identifier */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for identifier");
        else
            new->identifier = apr_pstrdup(r->pool, val);

        /* region */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for region");
        else
            new->region = apr_pstrdup(r->pool, val);

        /* country_code */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for country_code");
        else
            new->country_code = apr_pstrndup(r->pool, val, 2); /* fixed length, two bytes */

        /* latitude */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for lat");
        } else
            new->lat = atof(val);

        /* longitude */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for lng");
        } else
            new->lng = atof(val);

        /* FIXME: would be sufficient to do it only for the "interesting" mirrors */
        if (new->lat != 0 && new->lng != 0 && lat != 0 && lng != 0) {
            new->dist = (int) ( sqrt( pow((lat - new->lat), 2) + pow((lng - new->lng), 2) ) * 1000 );
        }

        /* autonomous system number */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for AS number");
        else
            new->as = apr_pstrdup(r->pool, val);

        /* network prefix */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for network prefix");
        else {
            char *s;
            new->prefix = apr_pstrdup(r->pool, val);
            if ((s = ap_strchr(val, '/'))) {
                *s++ = '\0';
                rv = apr_ipsubnet_create(&new->ipsub, val, s, r->pool);
                if(APR_STATUS_IS_EINVAL(rv) || (rv != APR_SUCCESS)) {
                    /* looked nothing like an IP address, or could not be converted */
                    new->ipsub = NULL;
                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] "
                            "Error in parsing network prefix of %s: %s/%s",
                            new->identifier, val, s);
                }
            }
        }

        /* score */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for score");
            unusable = 1;
        } else
            new->score = atoi(val);

        /* baseurl */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for baseurl");
            unusable = 1;
        } else if (!val[0]) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] mirror '%s' (#%d) has empty baseurl",
                          new->identifier, new->id);
            unusable = 1;
        } else {
            new->baseurl = apr_pstrdup(r->pool, val);
            if (new->baseurl[strlen(new->baseurl) - 1] != '/') {
                new->baseurl = apr_pstrcat(r->pool, new->baseurl, "/", NULL);
            }
        }

        /* region_only */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for region_only");
        } else
            new->region_only = ((val[0] == 't') || (val[0] == '1')) ? 1 : 0;

        /* country_only */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for country_only");
        } else
            new->country_only = ((val[0] == 't') || (val[0] == '1')) ? 1 : 0;

        /* as_only */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for as_only");
        } else
            new->as_only = ((val[0] == 't') || (val[0] == '1')) ? 1 : 0;

        /* prefix_only */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for prefix_only");
        } else
            new->prefix_only = ((val[0] == 't') || (val[0] == '1')) ? 1 : 0;

        /* other_countries */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for other_countries");
        else
            new->other_countries = apr_pstrdup(r->pool, val);

        /* file_maxsize */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for file_maxsize");
            unusable = 1;
        } else
            new->file_maxsize = apr_atoi64(val);



        /* now, take some decisions */

        if (unusable) {
            /* discard */
            apr_array_pop(mirrors);
            i++;
            continue;
        }

        /* rank it (randomized, weighted by "score" value) */
        /* not using thread-safe rand_r() here, because it shouldn't make
         * a real difference here */
        new->rank = (rand()>>16) * ((RAND_MAX>>16) / new->score);


#ifdef WITH_MEMCACHE
        if (new->id && (new->id == cached_id)) {
            debugLog(r, cfg, "Mirror '%s' associated in memcache (cached_id %d)", new->identifier, cached_id);
            chosen = new;
        }
#endif


        /* file too large for this mirror? */
        if (new->file_maxsize > 0 && r->finfo.size > new->file_maxsize) {
            debugLog(r, cfg, "Mirror '%s' is configured to not handle files larger than %d bytes",
                     new->identifier, new->file_maxsize);
            /* but keep it as reserve - after all, it could be the only one */
            *(void **)apr_array_push(mirrors_elsewhere) = new;
        }

        /* same prefix? */
        else if (new->ipsub
                && apr_ipsubnet_test(new->ipsub, clientaddr)) {
            *(void **)apr_array_push(mirrors_same_prefix) = new;
            new->nsame = &mirrors_same_prefix->nelts;
            debugLog(r, cfg, "Mirror '%s' in same prefix (%s)", new->identifier, new->prefix);
        }

        /* same AS? */
        else if ((strcmp(new->as, as) == 0)
                   && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_same_as) = new;
            new->nsame = &mirrors_same_as->nelts;
        }

        /* same country? */
        else if ((strcasecmp(new->country_code, country_code) == 0)
                   && !new->as_only
                   && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_same_country) = new;
            new->nsame = &mirrors_same_country->nelts;
        }

        /* is the mirror's country_code a wildcard, indicating that the mirror should be
         * considered for every country? */
        else if (strcmp(new->country_code, "**") == 0) {
            *(void **)apr_array_push(mirrors_same_country) = new;
            new->nsame = &mirrors_same_country->nelts;
            /* if so, forget memcache association, so the mirror is not ruled out */
            chosen = NULL;
            /* set its country and region to that of the client */
            new->country_code = country_code;
            new->region = continent_code;
        }

        /* mirror from elsewhere, but suitable for this country? */
        else if (new->other_countries && ap_strcasestr(new->other_countries, country_code)) {
            *(void **)apr_array_push(mirrors_fallback_country) = new;
            new->nsame = &mirrors_fallback_country->nelts;
        }

        /* same region? */
        /* to be actually considered for this group, the mirror must be willing
         * to take redirects from foreign country */
        else if ((strcasecmp(new->region, continent_code) == 0)
                    && !new->country_only
                    && !new->as_only
                    && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_same_region) = new;
            new->nsame = &mirrors_same_region->nelts;
        }

        /* to be considered as "worldwide" mirror, it must be willing
         * to take redirects from foreign regions.
         * (N.B. region_only implies country_only)  */
        else if (!new->region_only
                    && !new->country_only
                    && !new->as_only
                    && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_elsewhere) = new;
            new->nsame = &mirrors_elsewhere->nelts;
        }

        i++;
    }

#if 0
    /* dump the mirror array */
    mirror_entry_t *elts;
    elts = (mirror_entry_t *) mirrors->elts;
    for (i = 0; i < mirrors->nelts; i++) {
        debugLog(r, cfg, "mirror  %3d  %-30s", elts[i].id, elts[i].identifier);
    }
#endif


    /* 2nd pass */

    /* if we didn't find a mirror in the country: are other mirrors set to
     * handle this country? */
    if (apr_is_empty_array(mirrors_same_country)
            && !apr_is_empty_array(mirrors_fallback_country)) {
        mirrors_same_country = mirrors_fallback_country;
        debugLog(r, cfg, "no mirror in country, but found fallback_country mirrors");
    }


    /* 3rd pass */
    if (apr_is_empty_array(mirrors) && ! apr_is_empty_array(cfg->fallbacks)) {

        debugLog(r, cfg, "ok, need to add fallback mirrors (%d configured)",
                 cfg->fallbacks->nelts);

        /* we copy the array, so we don't modify the one in the config */
        mirrors = apr_array_copy(r->pool, cfg->fallbacks);

        mirror_entry_t *elts;
        elts = (mirror_entry_t *) mirrors->elts;
        for (i = 0; i < mirrors->nelts; i++) {

            elts[i].rank = (rand()>>16) * ((RAND_MAX>>16) / elts[i].score);
            /* elts[i].identifier = apr_psprintf(r->pool, "fallback_%02d(%s)",
                                              i, elts[i].baseurl); */

            if (strcasecmp(elts[i].country_code, country_code) == 0) {
                *(void **)apr_array_push(mirrors_same_country) = &(elts[i]);
                debugLog(r, cfg, "adding fallback mirror in same country: %s:%s %s",
                         elts[i].region, elts[i].country_code, elts[i].baseurl);
            }
            else if (strcasecmp(elts[i].region, continent_code) == 0) {
                *(void **)apr_array_push(mirrors_same_region) = &(elts[i]);
                debugLog(r, cfg, "adding fallback mirror in same region: %s:%s %s",
                         elts[i].region, elts[i].country_code, elts[i].baseurl);
            }
            else {
                *(void **)apr_array_push(mirrors_elsewhere) = &(elts[i]);
                debugLog(r, cfg, "adding fallback mirror elsewhere: %s:%s %s",
                         elts[i].region, elts[i].country_code, elts[i].baseurl);
            }
        }
    }

    if (lat != 0 && lng != 0) {
        debugLog(r, cfg, "[mod_mirrorbrain] taking geo distance into account");
        find_best = find_closest_dist;
        cmp_mirror_best = cmp_mirror_dist;
    } else {
        debugLog(r, cfg, "[mod_mirrorbrain] no distance data - using rank selection");
        find_best = find_lowest_rank;
        cmp_mirror_best = cmp_mirror_rank;
    }

    /*
    * Sorting the mirror list(s):
    * - is needed only when metalink (or mirrorlist) is requested
    * - sorting the mirrorlist itself would invalidates the pointer lists
    *   mirrors_same_country et al., as they are already done.
    * The sorting could be done _before_ picking up mirrors_same_country et al.
    * - but those are not needed also when doing a metalink
    * - and since the ranking is not global, we still need to iterate over the
    *   mirrors_same_country et al. when doing the metalink
    *
    * The sorting might invalidate the location where "chosen" points at -- if
    *   "mirrors" itself is sorted.
    *
    * => best to sort the mirrors_same_country et al. individually, right?
    */
    switch (rep) {
    case META4:
    case METALINK:
    case MIRRORLIST:
    case ZSYNC:
    case YUMLIST:
    case REDIRECT:
        qsort(mirrors_same_prefix->elts, mirrors_same_prefix->nelts,
              mirrors_same_prefix->elt_size, cmp_mirror_best);
        qsort(mirrors_same_as->elts, mirrors_same_as->nelts,
              mirrors_same_as->elt_size, cmp_mirror_best);
        qsort(mirrors_same_country->elts, mirrors_same_country->nelts,
              mirrors_same_country->elt_size, cmp_mirror_best);
        qsort(mirrors_same_region->elts, mirrors_same_region->nelts,
              mirrors_same_region->elt_size, cmp_mirror_best);
        qsort(mirrors_elsewhere->elts, mirrors_elsewhere->nelts,
              mirrors_elsewhere->elt_size, cmp_mirror_best);
        break;
    }

    if (cfg->debug) {

        /* list the sorted result */
        /* Brad's mod_edir hdir.c helped me here.. thanks to his kind help */
        mirror = NULL;

        /* list the same-prefix mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
        for (i = 0; i < mirrors_same_prefix->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same prefix: %-30s (score %4d) (rank %10d) (dist %d)",
                    mirror->identifier, mirror->score, mirror->rank, mirror->dist);
        }

        /* list the same-AS mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
        for (i = 0; i < mirrors_same_as->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same AS: %-30s (score %4d) (rank %10d) (dist %d)",
                    mirror->identifier, mirror->score, mirror->rank, mirror->dist);
        }

        /* list the same-country mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same country: %-30s (score %4d) (rank %10d) (dist %d)",
                    mirror->identifier, mirror->score, mirror->rank, mirror->dist);
        }

        /* list the same-region mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same region:  %-30s (score %4d) (rank %10d) (dist %d)",
                    mirror->identifier, mirror->score, mirror->rank, mirror->dist);
        }

        /* list all other mirrors */
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "elsewhere:    %-30s (score %4d) (rank %10d) (dist %d)",
                    mirror->identifier, mirror->score, mirror->rank, mirror->dist);
        }

        debugLog(r, cfg, "classifying %d mirror%s: %d prefix, %d AS, %d country, "
                "%d region, %d elsewhere",
                mirror_cnt, (mirror_cnt == 1) ? "" : "s",
                mirrors_same_prefix->nelts,
                mirrors_same_as->nelts,
                mirrors_same_country->nelts,
                mirrors_same_region->nelts,
                mirrors_elsewhere->nelts);
    }


#if 0
    if ((mirror_cnt <= 0) || (!mirrors_same_prefix->nelts && !mirrors_same_as->nelts
                              && !mirrors_same_country->nelts && !mirrors_same_region->nelts
                              && !mirrors_elsewhere->nelts)) {
#endif
    if (!mirrors_same_prefix->nelts && !mirrors_same_as->nelts && !mirrors_same_country->nelts
            && !mirrors_same_region->nelts && !mirrors_elsewhere->nelts) {
        if (apr_is_empty_array(cfg->fallbacks))  {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                    "[mod_mirrorbrain] no mirrors found for %s", filename);
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                    "[mod_mirrorbrain] no mirrors found for %s, "
                    "but fallback mirrors are available", filename);
        }

        /* can be used with a CustomLog directive, conditionally logging these requests */
        apr_table_setn(r->subprocess_env, "MB_NOMIRROR", "1");

        if (apr_is_empty_array(cfg->fallbacks)) {
            switch (rep) {
            case META4:
            case METALINK:
                if (meta_negotiated) {
                    debugLog(r, cfg, "would have to send empty metalink... -> deliver directly");
                    setenv_give(r, "file");
                    return DECLINED;
                } else {
                    debugLog(r, cfg, "would have to send empty metalink... -> 404");
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                            "[mod_mirrorbrain] Can't send metalink for %s (no mirrors)", filename);
                    return HTTP_NOT_FOUND;
                }
            }
        }
    }



    if ((rep != YUMLIST) && (!hashbag)) {
        hashbag = hashbag_fill(r, dbd, filename);
        if (hashbag == NULL) {
            debugLog(r, cfg, "no hashes found in database");
        }
    }


    /* for building URLs to self */
    thisserver = ap_get_server_name(r);
    port = ap_get_server_port(r);
    if (ap_is_default_port(port, r)) {
        thisport = "";
    } else {
        thisport = apr_psprintf(r->pool, ":%u", port);
    }



    /* if it makes sense, build a magnet link for later inclusion */
    char *magnet = NULL;
    if (hashbag != NULL) {
        switch (rep) {
        case META4:
        case METALINK:
        case MAGNET: {

            apr_array_header_t *m;
            m = apr_array_make(r->pool, 7, sizeof(char *));

            /* BitTorrent info hash */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool, "magnet:?xt=urn:btih:%s", hashbag->btihhex);
#if 0
            /* SHA-1 */
            /* As far as I can see, this hash would actually need to be Base32
             * encoded, not hex. But it's probably not worth adding Base32
             * encoder just for this. */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool, "&amp;xt=urn:sha1:%s", hashbag->sha1hex);
#endif
            /* MD5 */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool, "&amp;xt=urn:md5:%s", hashbag->md5hex);

            /* size */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool, "&amp;xl=%s", apr_off_t_toa(r->pool, r->finfo.size));

            /* file basename */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool, "&amp;dn=%s", ap_escape_uri(r->pool, basename));

            /* a HTTP link to the file */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool, "&amp;as=%s://%s%s%s", ap_http_scheme(r),
                                                             ap_escape_uri(r->pool, thisserver),
                                                             thisport,
                                                             ap_escape_uri(r->pool, r->uri));

            if (!apr_is_empty_array(scfg->tracker_urls)) {
                for (i = 0; i < scfg->tracker_urls->nelts; i++) {
                    char *url = ((char **) scfg->tracker_urls->elts)[i];
                    APR_ARRAY_PUSH(m, char *) =
                        apr_psprintf(r->pool, "&amp;tr=%s", ap_escape_uri(r->pool, url));
                }
            }

            magnet = apr_array_pstrcat(r->pool, m, '\0');
        }
        }
    }


    /* So, which representation are we going to send back? */
    switch (rep) {

    case META4:
    case METALINK:

        debugLog(r, cfg, "Sending metalink");
        setenv_give(r, reps[rep].ext);

        /* tell caches that this is negotiated response and that not every client will take it */
        apr_table_mergen(r->headers_out, "Vary", "accept");

        /* add rfc2183 header for filename, with .metalink appended
         * because some clients trigger on that extension */
        apr_table_setn(r->headers_out,
                       "Content-Disposition",
                       apr_pstrcat(r->pool,
                                   "attachment; filename=\"",
                                   basename, ".", rep_ext, "\"", NULL));


        char *time_str = NULL;

        switch (rep) {
        case META4:
            ap_set_content_type(r, "application/metalink4+xml; charset=UTF-8");
            ap_rputs(     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<metalink xmlns=\"urn:ietf:params:xml:ns:metalink\">\n", r);

            /* current time */
            time_str = apr_palloc(r->pool, RFC3339_DATE_LEN);
            apr_time_exp_t tm;
            /* r->request_time should be filled out already, and save us the syscall to time()
             * through apr_time_now() */
            apr_time_exp_gmt(&tm, r->request_time);
            apr_strftime(time_str, &len, RFC3339_DATE_LEN, "%Y-%m-%dT%H:%M:%SZ", &tm);

            ap_rputs(     "  <generator>MirrorBrain/"MOD_MIRRORBRAIN_VER"</generator>\n", r);
            /* The origin URL is meant to specify the location for revalidation of this metalink.
             *
             * We use r->uri, not r->unparsed_uri, so we don't need to escape query strings for xml.
             */
            ap_rprintf(r, "  <origin dynamic=\"true\">%s://%s%s%s.%s</origin>\n",
                       ap_http_scheme(r), thisserver, thisport, r->uri, rep_ext);
            ap_rprintf(r, "  <published>%s</published>\n", time_str);

            if (scfg->metalink_publisher_name && scfg->metalink_publisher_url) {
                ap_rputs(     "  <publisher>\n", r);
                ap_rprintf(r, "    <name>%s</name>\n", scfg->metalink_publisher_name);
                ap_rprintf(r, "    <url>%s</url>\n", scfg->metalink_publisher_url);
                ap_rputs(     "  </publisher>\n\n", r);
            }
            break;

        case METALINK:
            ap_set_content_type(r, "application/metalink+xml; charset=UTF-8");
            ap_rputs(     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\"\n", r);

            /* current time */
            time_str = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
            apr_rfc822_date(time_str, apr_time_now());

            ap_rprintf(r, "  origin=\"%s://%s%s%s.%s\"\n", ap_http_scheme(r), thisserver, thisport, r->uri, rep_ext);
            ap_rputs(     "  generator=\"MirrorBrain "MOD_MIRRORBRAIN_VER" (see http://mirrorbrain.org/)\"\n", r);
            ap_rputs(     "  type=\"dynamic\"", r);
            ap_rprintf(r, "  pubdate=\"%s\"", time_str);
            ap_rprintf(r, "  refreshdate=\"%s\">\n\n", time_str);

            if (scfg->metalink_publisher_name && scfg->metalink_publisher_url) {
                ap_rputs(     "  <publisher>\n", r);
                ap_rprintf(r, "    <name>%s</name>\n", scfg->metalink_publisher_name);
                ap_rprintf(r, "    <url>%s</url>\n", scfg->metalink_publisher_url);
                ap_rputs(     "  </publisher>\n\n", r);
            }

            ap_rputs(     "  <files>\n", r);
            break;
        }


        ap_rprintf(r, "  <file name=\"%s\">\n", basename);
        ap_rprintf(r, "    <size>%s</size>\n\n", apr_off_t_toa(r->pool, r->finfo.size));
        ap_rprintf(r, "    <!-- <mtime>%" APR_INT64_T_FMT "</mtime> -->\n\n",
                   apr_time_sec(r->finfo.mtime)); /* APR finfo times are in microseconds */


        if (hashbag != NULL) {
            if (hashbag->id) {
                ap_rprintf(r, "    <!-- internal id: %s -->\n",
                           apr_off_t_toa(r->pool, hashbag->id));
            }

            switch (rep) {
                case META4:
                    if (hashbag->pgp) {
                        ap_rputs("    <signature mediatype=\"application/pgp-signature\">\n", r);
                        ap_rputs(hashbag->pgp, r);
                        ap_rputs("    </signature>\n", r);
                    }

                    if (hashbag->md5hex)
                        ap_rprintf(r, "    <hash type=\"md5\">%s</hash>\n", hashbag->md5hex);
                    if (hashbag->sha1hex)
                        ap_rprintf(r, "    <hash type=\"sha-1\">%s</hash>\n", hashbag->sha1hex);
                    if (hashbag->sha256hex)
                        ap_rprintf(r, "    <hash type=\"sha-256\">%s</hash>\n", hashbag->sha256hex);
                    if (hashbag->zsyncpieceshex
                        && (hashbag->sha1piecesize > 0)
                        && !apr_is_empty_array(hashbag->zsyncpieceshex)) {
                        ap_rprintf(r, "    <pieces length=\"%d\" type=\"zsync\">\n",
                                   hashbag->sha1piecesize);

                        char **p = (char **)hashbag->zsyncpieceshex->elts;
                        for (i = 0; i < hashbag->zsyncpieceshex->nelts; i++) {
                            ap_rprintf(r, "      <hash>%s</hash>\n", p[i]);
                        }
                        ap_rputs("    </pieces>\n", r);
                    }
                    if (hashbag->sha1pieceshex
                        && (hashbag->sha1piecesize > 0)
                        && !apr_is_empty_array(hashbag->sha1pieceshex)) {
                        ap_rprintf(r, "    <pieces length=\"%d\" type=\"sha-1\">\n",
                                   hashbag->sha1piecesize);

                        char **p = (char **)hashbag->sha1pieceshex->elts;
                        for (i = 0; i < hashbag->sha1pieceshex->nelts; i++) {
                            ap_rprintf(r, "      <hash>%s</hash>\n", p[i]);
                        }
                        ap_rputs("    </pieces>\n", r);
                    }
                    break;

                case METALINK:
                    /* There are a few slight differences to the newer meta4 format */
                    ap_rputs("      <verification>\n", r);

                    if (hashbag->pgp) {
                        ap_rprintf(r, "    <signature type=\"pgp\" file=\"%s.asc\">\n", basename);
                        ap_rputs(hashbag->pgp, r);
                        ap_rputs("    </signature>\n", r);
                    }

                    if (hashbag->md5hex)
                        ap_rprintf(r, "        <hash type=\"md5\">%s</hash>\n", hashbag->md5hex);
                    if (hashbag->sha1hex)
                        ap_rprintf(r, "        <hash type=\"sha1\">%s</hash>\n", hashbag->sha1hex);
                    if (hashbag->sha256hex)
                        ap_rprintf(r, "        <hash type=\"sha256\">%s</hash>\n", hashbag->sha256hex);
                    if (hashbag->zsyncpieceshex
                        && (hashbag->sha1piecesize > 0)
                        && !apr_is_empty_array(hashbag->zsyncpieceshex)) {
                        ap_rprintf(r, "        <pieces length=\"%d\" type=\"zsync\">\n",
                                   hashbag->sha1piecesize);

                        char **p = (char **)hashbag->zsyncpieceshex->elts;
                        for (i = 0; i < hashbag->zsyncpieceshex->nelts; i++) {
                            ap_rprintf(r, "          <hash piece=\"%d\">%s</hash>\n", i, p[i]);
                        }
                        ap_rputs("        </pieces>\n", r);
                    }
                    if (hashbag->sha1pieceshex
                        && (hashbag->sha1piecesize > 0)
                        && !apr_is_empty_array(hashbag->sha1pieceshex)) {
                        ap_rprintf(r, "        <pieces length=\"%d\" type=\"sha1\">\n",
                                   hashbag->sha1piecesize);

                        char **p = (char **)hashbag->sha1pieceshex->elts;
                        for (i = 0; i < hashbag->sha1pieceshex->nelts; i++) {
                            ap_rprintf(r, "          <hash piece=\"%d\">%s</hash>\n", i, p[i]);
                        }
                        ap_rputs("        </pieces>\n", r);
                    }

                    ap_rputs("      </verification>\n", r);

                    break;
            }

        }

        if (rep == METALINK) {
            ap_rputs(     "    <resources>\n\n", r);
        }

        apr_finfo_t sb;

        if (cfg->metalink_torrentadd_mask
            && !ap_regexec(cfg->metalink_torrentadd_mask, r->filename, 0, NULL, 0)
            && apr_stat(&sb, apr_pstrcat(r->pool, r->filename, ".torrent", NULL), APR_FINFO_MIN, r->pool) == APR_SUCCESS) {
            debugLog(r, cfg, "found torrent file");
            ap_rprintf(r, "    <url type=\"bittorrent\" preference=\"%d\">%s://%s%s%s.torrent</url>\n\n",
                       100,
                       ap_http_scheme(r),
                       thisserver,
                       thisport,
                       r->uri);
        }

        if ((scfg->metalink_magnets == 1) && (hashbag != NULL) && (magnet != NULL)) {
            switch (rep) {
            case META4:
                /* inclusion of torrents and other metaurls should probably happen here
                 *
                 * restrict the use of the new metaurl element to new metalinks */

                /* <metaurl mediatype="torrent">http://example.com/example.ext.torrent</metaurl> */
                ap_rputs("\n\n    <!-- Meta URLs -->\n", r);
                ap_rprintf(r, "    <metaurl mediatype=\"torrent\">%s</metaurl>\n", magnet);
                break;
            case METALINK:
                ap_rprintf(r, "    <url type=\"bittorrent\" preference=\"%d\">%s</url>\n\n",
                           100, magnet);
            }
        }

        ap_rprintf(r, "\n\n    <!-- Found %d mirror%s: %d in the same network prefix, %d in the same "
                   "autonomous system,\n         %d handling this country, %d in the same "
                   "region, %d elsewhere -->\n",
                   mirror_cnt,
                   (mirror_cnt == 1) ? "" : "s",
                   mirrors_same_prefix->nelts,
                   mirrors_same_as->nelts,
                   mirrors_same_country->nelts,
                   mirrors_same_region->nelts,
                   mirrors_elsewhere->nelts);

        /* metalink resource priority */
        int prio = 0;
        /* the highest v3 metalink preference according to the spec is 100, and
         * we'll decrement it for each mirror by one, until zero is reached */
        int v3prio = 101;


        /* insert broken mirrors at the top, for failover testing? */
        if(scfg->metalink_broken_test_mirrors
                && (ptr = (char*) apr_table_get(r->headers_in, "X-Broken-Mirrors"))
                && (apr_stat(&sb, scfg->metalink_broken_test_mirrors,
                         APR_FINFO_MIN, r->pool) == APR_SUCCESS)) {

            debugLog(r, cfg, "adding broken mirrors (requested via X-Broken-Mirrors header)");
            apr_table_mergen(r->headers_out, "Cache-Control", "no-store,max-age=0");
            apr_table_mergen(r->headers_out, "Vary", "X-Broken-Mirrors");
            apr_table_addn(r->headers_out, "X-Broken-Mirrors", "true");

            apr_file_t *fh;
            if (apr_file_open(&fh, scfg->metalink_broken_test_mirrors,
                              APR_READ, APR_OS_DEFAULT, r->pool) == APR_SUCCESS) {
                ap_send_fd(fh, r, 0, sb.size, &len);
                apr_file_close(fh);
            }

            if (strcmp(ptr, "only") == 0) {
                /* finish here */
                switch (rep) {
                case META4:
                    ap_rputs(     "  </file>\n"
                                  "</metalink>\n", r);
                    break;
                case METALINK:
                    ap_rputs(     "    </resources>\n"
                                  "    </file>\n"
                                  "  </files>\n"
                                  "</metalink>\n", r);
                    break;
                }
                return OK;
            }

            /* we leave a gap for insertion of 15 such non-working URLs,
             * still keeping decrementing the preference in order */
            v3prio = 85;
        }

        ap_rprintf(r, "\n    <!-- Mirrors in the same network (%s): -->\n",
                   (strcmp(prefix, "--") == 0) ? "unknown" : prefix);
        mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
        for (i = 0; i < mirrors_same_prefix->nelts; i++) {
            mirror = mirrorp[i];
            prio++;
            if (v3prio) v3prio--;
            emit_metalink_url(r, rep, mirror->baseurl, mirror->country_code, filename, v3prio, prio);
        }

        ap_rprintf(r, "\n    <!-- Mirrors in the same AS (%s): -->\n",
                   (strcmp(as, "--") == 0) ? "unknown" : as);
        mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
        for (i = 0; i < mirrors_same_as->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only)
                continue;
            prio++;
            if (v3prio) v3prio--;
            emit_metalink_url(r, rep, mirror->baseurl, mirror->country_code, filename, v3prio, prio);
        }

        /* failed geoip lookups yield country='--', which leads to invalid XML */
        ap_rprintf(r, "\n    <!-- Mirrors which handle this country (%s): -->\n",
                   (strcmp(country_code, "--") == 0) ? "unknown" : country_code);
        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only || mirror->as_only)
                continue;
            prio++;
            if (v3prio) v3prio--;
            emit_metalink_url(r, rep, mirror->baseurl, mirror->country_code, filename, v3prio, prio);
        }

        ap_rprintf(r, "\n    <!-- Mirrors in the same continent (%s): -->\n",
                   (strcmp(continent_code, "--") == 0) ? "unknown" : continent_code);
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only || mirror->as_only || mirror->country_only)
                continue;
            prio++;
            if (v3prio) v3prio--;
            emit_metalink_url(r, rep, mirror->baseurl, mirror->country_code, filename, v3prio, prio);
        }

        ap_rputs("\n    <!-- Mirrors in the rest of the world: -->\n", r);
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only || mirror->as_only
                    || mirror->country_only || mirror->region_only) {
                continue;
            }
            prio++;
            if (v3prio) v3prio--;
            emit_metalink_url(r, rep, mirror->baseurl, mirror->country_code, filename, v3prio, prio);
        }

        switch (rep) {
        case META4:
            ap_rputs(     "  </file>\n"
                          "</metalink>\n", r);
            break;
        case METALINK:
            ap_rputs(     "    </resources>\n"
                          "    </file>\n"
                          "  </files>\n"
                          "</metalink>\n", r);
            break;
        }

        return OK;


    /* send an HTML list instead of doing a redirect? */
    case MIRRORLIST:

        setenv_give(r, "mirrorlist");
        debugLog(r, cfg, "Sending mirrorlist");

        ap_set_content_type(r, "text/html; charset=UTF-8");

        if (scfg->mirrorlist_header) {
            /* send the configured custom header */
            apr_file_t *fh;
            rv = apr_stat(&sb, scfg->mirrorlist_header, APR_FINFO_MIN, r->pool);
            if (rv != APR_SUCCESS) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                              "[mod_mirrorbrain] could not stat mirrorlist header file '%s'.",
                              scfg->mirrorlist_header);
            } else {
                rv = apr_file_open(&fh, scfg->mirrorlist_header, APR_READ, APR_OS_DEFAULT, r->pool);
                if (rv == APR_SUCCESS) {
                    ap_send_fd(fh, r, 0, sb.size, &len);
                    apr_file_close(fh);
                } else {
                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                                  "[mod_mirrorbrain] could not open mirrorlist header '%s'.",
                                  scfg->mirrorlist_header);
                }
            }
        } else {
            /* standard header */
            ap_rputs(DOCTYPE_XHTML_1_0T
                     "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                     "<head>\n"
                     "  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n"
                     "  <title>Mirror List</title>\n", r);
            if (scfg->mirrorlist_stylesheet) {
                ap_rprintf(r, "  <link type=\"text/css\" rel=\"stylesheet\" href=\"%s\" />\n",
                           scfg->mirrorlist_stylesheet);
            }
            ap_rputs("</head>\n\n" "<body>\n", r);
        }

        ap_rputs("<div id=\"mirrorbrain-details\">\n", r);
        ap_rprintf(r, "  <h2>Mirrors for <a href=\"%s\">%s</a></h2>\n",
                   r->uri, basename);

        /* Metadata */
        ap_rputs("<div id=\"mirrorbrain-fileinfo\">\n"
                 "<h3>File information</h3>\n"
                 "<ul>\n", r);
        char buf[5];
        ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">Filename:</span> %s</li>\n", basename);
	ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">Path:</span> %s</li>\n", r->uri);
        ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">Size:</span> %s (%s bytes)</li>\n",
                   apr_strfsize(r->finfo.size, buf),
                   apr_off_t_toa(r->pool, r->finfo.size));
        time_str = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
        apr_rfc822_date(time_str, r->finfo.mtime);
        ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">Last modified:</span> "
                      "%s (Unix time: %" APR_INT64_T_FMT ")</li>\n",
                   time_str, apr_time_sec(r->finfo.mtime));

        if (hashbag != NULL) {
            if (hashbag->sha256hex)
                ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">"
                              "<a href=\"%s.sha256\">SHA-256 Hash</a>:</span> <tt>%s</tt>"
                              "</li>\n", r->uri, hashbag->sha256hex);
            if (hashbag->sha1hex)
                ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">"
                              "<a href=\"%s.sha1\">SHA-1 Hash</a>:</span> <tt>%s</tt>"
                              "</li>\n", r->uri, hashbag->sha1hex);
            if (hashbag->md5hex)
                ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">"
                              "<a href=\"%s.md5\">MD5 Hash</a>:</span> <tt>%s</tt>"
                              "</li>\n", r->uri, hashbag->md5hex);
            if (hashbag->btihhex && !apr_is_empty_array(scfg->tracker_urls))
                ap_rprintf(r, "  <li><span class=\"mirrorbrain-label\">"
                              "<a href=\"%s.btih\">BitTorrent Information Hash</a>:</span> <tt>%s</tt>"
                              "</li>\n", r->uri, hashbag->btihhex);

            if (hashbag->pgp) {
                /* contrary to the hashes, we don't have a handler for .asc files, because
                 * the database always only gets a signature when one already exists on-disk */
                ap_rprintf(r, "  <li>PGP signature <a href=\"%s.asc\">available</a> "
                              "</li>\n", r->uri);
            }
        }

        /* Direct download link */
        ap_rputs("</ul>\n", r);
        ap_rprintf(r, "<p><a href=\"%s\" class=\"mirrorbrain-btn\">Download file from preferred mirror</a></p>\n", r->uri);
        ap_rputs("</div>\n\n", r);

        /* Metalink / P2P / zsync section */
        ap_rputs("<div id=\"mirrorbrain-links\">\n"
                 "<h3>Reliable downloads</h3>\n", r);

        /* Metalink info */
        ap_rputs("<div class=\"mirrorbrain-links-grp\">\n"
                 "<h4>Metalink</h4>\n"
                 "<ul>\n", r);
        ap_rprintf(r, "  <li><a href=\"%s.meta4\">%s.meta4</a> (IETF Metalink)</li>\n",
                   r->uri, r->uri);
        ap_rprintf(r, "  <li><a href=\"%s.metalink\">%s.metalink</a> (old (v3) Metalink)</li>\n",
                   r->uri, r->uri);
        ap_rputs("</ul>\n" "</div>\n", r);

        if (hashbag) {
            if (!apr_is_empty_array(scfg->tracker_urls)) {
                /* Torrent downloads */
                ap_rputs("<div class=\"mirrorbrain-links-grp\">\n"
                         "<h4>P2P links</h4>\n"
                         "<ul>\n", r);
                ap_rprintf(r, "  <li><a href=\"%s.torrent\">%s.torrent</a> (BitTorrent)</li>\n",
                           r->uri, r->uri);
                ap_rprintf(r, "  <li><a href=\"%s.magnet\">%s.magnet</a> (Magnet)</li>\n",
                           r->uri, r->uri);
                ap_rputs("</ul>\n" "</div>\n", r);
            }

            if (hashbag->sha1hex && (hashbag->zblocksize > 0)
                    && hashbag->zhashlens && hashbag->zsumshex) {
                /* zSync */
                ap_rputs("<div class=\"mirrorbrain-links-grp\">\n"
                         "<h4>Zsync links</h4>\n"
                         "<ul>\n", r);
                ap_rprintf(r, "  <li><a href=\"%s.zsync\">%s.zsync</a></li>\n",
                           r->uri, r->uri);
                ap_rputs("</ul>\n" "</div>\n", r);
            }
        }

        /* End of Reliable downloads section */
        ap_rputs("</div>\n\n", r);

        /* Mirrors */
        ap_rputs("<div id=\"mirrorbrain-mirrors\">\n"
                 "<h3>Mirrors</h3>\n"
                 "<p>", r);

        /* Nice string where the user is located */
        ap_rprintf(r, "List of best mirrors for IP address %s, located ", clientip);
        if (lat != 0 && lng != 0) {
            ap_rprintf(r, "at %f,%f ", lat, lng);
        }
        if (strcmp(country_code, "--") != 0) {
            ap_rprintf(r, "in %s (%s)", country_name, country_code);
        } else {
            ap_rputs("in an unknown country", r);
        }
        if (strcmp(prefix, "--") != 0) {
            ap_rprintf(r, ", network %s (autonomous system %s)", prefix, as);
        }
        ap_rputs(".</p>\n\n", r);

        /* Link to static map of user and mirror locations */
        if (lat != 0 && lng != 0) {
            apr_array_header_t *topten = get_n_best_mirrors(r, 9, mirrors_same_prefix, mirrors_same_as,
                                                             mirrors_same_country, mirrors_same_region,
                                                             mirrors_elsewhere);
            mirrorp = (mirror_entry_t **)topten->elts;
            ap_rprintf(r, "<p><a href=\"http://maps.google.com/maps/dir/");
            for (i = 0; i < topten->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "/%f,%f", mirror->lat, mirror->lng);
            }
            ap_rprintf(r, "/%f,%f", lat, lng);
            ap_rputs("\">Map showing the closest mirrors</a></p>\n\n", r);
        }

        if ((mirror_cnt <= 0) || (!mirrors_same_prefix->nelts && !mirrors_same_as->nelts
                                  && !mirrors_same_country->nelts && !mirrors_same_region->nelts
                                  && !mirrors_elsewhere->nelts)) {
            ap_rputs("<div id=\"mirrorbrain-mirrors-none\">\n"
                     "<h4>No mirror was found</h4>\n", r);
            ap_rputs("<p>I am very sorry, but no mirror was found. Feel free to download directly:<br />\n", r);
            ap_rprintf(r, "  <a href=\"%s\">%s</a></p>\n",
                       r->uri, r->uri);
            ap_rputs("</div>\n" "</div>\n", r);
            ap_rputs("<address>Powered by <a href=\"http://mirrorbrain.org/\">MirrorBrain</a></address>\n", r);
            ap_rputs("</div><!-- mirrorbrain-details -->\n", r);
            ap_rputs("</body>\n" "</html>\n", r);
            return OK;
        }

        /* prefix */
        if (!apr_is_empty_array(mirrors_same_prefix)) {
            ap_rprintf(r, "<div class=\"mirrorbrain-mirrors-grp\">\n"
                          "<h4>Found %d mirror%s directly nearby (within the same network prefix: %s)</h4>\n"
                          "<ul>\n",
                       mirrors_same_prefix->nelts,
                       (mirrors_same_prefix->nelts == 1) ? "" : "s",
                       prefix);
            mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
            for (i = 0; i < mirrors_same_prefix->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "  <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n",
                        mirror->baseurl, filename,
                        mirror->baseurl, filename,
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("</ul>\n" "</div>\n\n", r);
        }

        /* AS */
        if (!apr_is_empty_array(mirrors_same_as)) {
            ap_rprintf(r, "<div class=\"mirrorbrain-mirrors-grp\">\n"
                          "<h4>Found %d mirror%s very close (within the same autonomous system (AS%s)</h4>\n"
                          "<ul>\n",
                       mirrors_same_as->nelts,
                       (mirrors_same_as->nelts == 1) ? "" : "s",
                       as);
            mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
            for (i = 0; i < mirrors_same_as->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "  <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n",
                        mirror->baseurl, filename,
                        mirror->baseurl, filename,
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("</ul>\n" "</div>\n\n", r);
        }

        /* country */
        if (!apr_is_empty_array(mirrors_same_country)) {
            ap_rprintf(r, "<div class=\"mirrorbrain-mirrors-grp\">\n"
                          "<h4>Found %d mirror%s which handle this country (%s)</h4>\n"
                          "<ul>\n",
                       mirrors_same_country->nelts,
                       (mirrors_same_country->nelts == 1) ? "" : "s",
                       country_code);
            mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
            for (i = 0; i < mirrors_same_country->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, " <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n",
                        mirror->baseurl, filename,
                        mirror->baseurl, filename,
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("</ul>\n" "</div>\n\n", r);
        }

        /* region */
        if (!apr_is_empty_array(mirrors_same_region)) {
            ap_rprintf(r, "<div class=\"mirrorbrain-mirrors-grp\">\n"
                          "<h4>Found %d mirror%s in other countries, but same continent (%s)</h4>\n"
                          "<ul>\n",
                       mirrors_same_region->nelts,
                       (mirrors_same_region->nelts == 1) ? "" : "s",
                       continent_code);
            mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
            for (i = 0; i < mirrors_same_region->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "  <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n",
                        mirror->baseurl, filename,
                        mirror->baseurl, filename,
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("</ul>\n" "</div>\n\n", r);
        }

        /* elsewhere */
        if (!apr_is_empty_array(mirrors_elsewhere)) {
            ap_rprintf(r, "<div class=\"mirrorbrain-mirrors-grp\">\n"
                          "<h4>Found %d mirror%s in other parts of the world</h4>\n"
                          "<ul>\n",
                       mirrors_elsewhere->nelts,
                       (mirrors_elsewhere->nelts == 1) ? "" : "s");
            mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
            for (i = 0; i < mirrors_elsewhere->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "  <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n",
                        mirror->baseurl, filename,
                        mirror->baseurl, filename,
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("</ul>\n" "</div>\n\n", r);
        }

        ap_rputs("</div>\n"
                 "<address>Powered by <a href=\"http://mirrorbrain.org/\">MirrorBrain</a></address>\n"
                 "</div><!-- mirrorbrain-details -->\n", r);

        if (scfg->mirrorlist_footer) {
            /* send the configured custom footer */
            apr_file_t *fh;
            rv = apr_stat(&sb, scfg->mirrorlist_footer, APR_FINFO_MIN, r->pool);
            if (rv != APR_SUCCESS) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                              "[mod_mirrorbrain] could not stat mirrorlist footer file '%s'.",
                              scfg->mirrorlist_footer);
            } else {
                rv = apr_file_open(&fh, scfg->mirrorlist_footer, APR_READ, APR_OS_DEFAULT, r->pool);
                if (rv == APR_SUCCESS) {
                    ap_send_fd(fh, r, 0, sb.size, &len);
                    apr_file_close(fh);
                } else {
                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                                  "[mod_mirrorbrain] could not open mirrorlist footer '%s'.",
                                  scfg->mirrorlist_footer);
                }
            }
        } else {
            ap_rputs("</body>\n", r);
            ap_rputs("</html>\n", r);
        }
        return OK;

    case TORRENT:
    {
        if (!hashbag || (hashbag->sha1piecesize <= 0) || apr_is_empty_array(hashbag->sha1pieceshex)) {
            debugLog(r, cfg, "Torrent requested, but no hashes found for %s", filename);
            return HTTP_NOT_FOUND;
        }

        if (apr_is_empty_array(scfg->tracker_urls)) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                    "[mod_mirrorbrain] Cannot create torrent: at least one MirrorBrainTorrentTrackerURL must configured");
            return HTTP_NOT_FOUND;
        }

        setenv_give(r, "torrent");
        debugLog(r, cfg, "Sending torrent");
        ap_set_content_type(r, "application/x-bittorrent");

        char *tracker = ((char **) scfg->tracker_urls->elts)[0];
        ap_rprintf(r, "d"
                          "8:announce"
                              "%" APR_SIZE_T_FMT ":%s", strlen(tracker), tracker);

        ap_rputs(         "13:announce-listll", r);
        for (i = 0; i < scfg->tracker_urls->nelts; i++) {
            tracker = ((char **) scfg->tracker_urls->elts)[i];
            ap_rprintf(r,     "%" APR_SIZE_T_FMT ":%s", strlen(tracker), tracker);
        }
        ap_rputs(             "e"
                          "e", r);

        ap_rprintf(r,     "7:comment"
                              "%" APR_SIZE_T_FMT ":%s", strlen(basename), basename);

                          /* This is meant to be the creation time of the torrent,
                           * but let's take the mtime of the file since we can generate the
                           * torrent any time */
        ap_rprintf(r,     "10:created by"
                              "%" APR_SIZE_T_FMT ":MirrorBrain/%s",
                              strlen("MirrorBrain/") + strlen(MOD_MIRRORBRAIN_VER),
                              MOD_MIRRORBRAIN_VER);
        ap_rprintf(r,     "13:creation date"
                              "i%se", apr_itoa(r->pool, apr_time_sec(r->finfo.mtime)));

        ap_rprintf(r,     "4:info"
                              "d"
                                  "6:length"
                                      "i%se",
                                      apr_off_t_toa(r->pool, r->finfo.size));
        ap_rprintf(r,             "6:md5sum"
                                      "%d:%s", MD5_DIGESTSIZE * 2, hashbag->md5hex);
        ap_rprintf(r,             "4:name"
                                      "%" APR_SIZE_T_FMT ":%s"
                                  "12:piece length"
                                      "i%de"
                                  "6:pieces"
                                      "%d:", strlen(basename),
                                             basename,
                                             hashbag->sha1piecesize,
                                             (hashbag->sha1pieceshex->nelts * SHA1_DIGESTSIZE));
        char **p = (char **)hashbag->sha1pieceshex->elts;
        for (i = 0; i < hashbag->sha1pieceshex->nelts; i++) {
            ap_rwrite(hex_to_bin(r->pool, p[i], SHA1_DIGESTSIZE), SHA1_DIGESTSIZE, r);
        }
        ap_rprintf(r,             "4:sha1"
                                      "%d:", SHA1_DIGESTSIZE);
        ap_rwrite(                    hex_to_bin(r->pool, hashbag->sha1hex, SHA1_DIGESTSIZE),
                SHA1_DIGESTSIZE, r);
        ap_rprintf(r,             "6:sha256"
                                      "%d:", SHA256_DIGESTSIZE);
        ap_rwrite(                    hex_to_bin(r->pool, hashbag->sha256hex, SHA256_DIGESTSIZE),
                SHA256_DIGESTSIZE, r);

        /* end of info hash: */
        ap_rputs(             "e", r);

        if (!apr_is_empty_array(scfg->dhtnodes)) {

            ap_rputs(     "5:nodes"
                              "l", r);
            for (i = 0; i < scfg->dhtnodes->nelts; i++) {
                dhtnode_t node = ((dhtnode_t *) scfg->dhtnodes->elts)[i];
                ap_rprintf(r,     "l" "%" APR_SIZE_T_FMT ":%s" "i%de" "e", strlen(node.name), node.name,
                                                         node.port);
            }
            ap_rputs(     "e", r);
        }

        /* Web seeds
         *
         * There's a trick: send this stuff _after_ the sha1 pieces.
         * The original BitTorrent client doesn't ignore unknown keys, but
         * refuses to grok the torrent and says "is not a valid torrent file
         * (not a valid bencoded string)". (Which is wrong.) However, it does
         * _not_ seam to read past the pieces; at least it doesn't complain
         * about stuff occurring afterwards. */

        {
            apr_array_header_t *m;
            m = apr_array_make(r->pool, 11, sizeof(char *));
            int found_urls = 0;

            mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
            for (i = 0; i < mirrors_same_prefix->nelts; i++, found_urls++) {
                mirror = mirrorp[i];
                APR_ARRAY_PUSH(m, char *) =
                    apr_psprintf(r->pool, "%" APR_SIZE_T_FMT ":%s%s", (strlen(mirror->baseurl) + strlen(filename)),
                                                     mirror->baseurl, filename);
            }
            if (!found_urls) {
                mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
                for (i = 0; i < mirrors_same_as->nelts; i++, found_urls++) {
                    mirror = mirrorp[i];
                    APR_ARRAY_PUSH(m, char *) =
                        apr_psprintf(r->pool, "%" APR_SIZE_T_FMT ":%s%s", (strlen(mirror->baseurl) + strlen(filename)),
                                                         mirror->baseurl, filename);
                }
            }
            if (!found_urls) {
                mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
                for (i = 0; i < mirrors_same_country->nelts; i++, found_urls++) {
                    mirror = mirrorp[i];
                    APR_ARRAY_PUSH(m, char *) =
                        apr_psprintf(r->pool, "%" APR_SIZE_T_FMT ":%s%s", (strlen(mirror->baseurl) + strlen(filename)),
                                                         mirror->baseurl, filename);
                }
            }
            if (!found_urls) {
                mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
                for (i = 0; i < mirrors_same_region->nelts; i++, found_urls++) {
                    mirror = mirrorp[i];
                    APR_ARRAY_PUSH(m, char *) =
                        apr_psprintf(r->pool, "%" APR_SIZE_T_FMT ":%s%s", (strlen(mirror->baseurl) + strlen(filename)),
                                                         mirror->baseurl, filename);
                }
            }
            if (!found_urls) {
                mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
                for (i = 0; i < mirrors_elsewhere->nelts; i++, found_urls++) {
                    mirror = mirrorp[i];
                    APR_ARRAY_PUSH(m, char *) =
                        apr_psprintf(r->pool, "%" APR_SIZE_T_FMT ":%s%s", (strlen(mirror->baseurl) + strlen(filename)),
                                                         mirror->baseurl, filename);
                }
            }
            /* add the redirector, in case there wasn't any mirror */
            if (!found_urls) {
                APR_ARRAY_PUSH(m, char *) =
                    apr_psprintf(r->pool, "%" APR_SIZE_T_FMT ":%s://%s%s%s",
                                           (strlen(ap_http_scheme(r)) + 3 + strlen(thisserver) + strlen(thisport) + strlen(r->uri)),
                                           ap_http_scheme(r), thisserver, thisport, r->uri);
            }

#if 0
            /* it would be simple to just list the URL of the redirector itself, but aria2c
             * retrieves a Metalink then and doesn't expect it in that situation. Maybe later */
            APR_ARRAY_PUSH(m, char *) =
                apr_psprintf(r->pool,     "8:url-list"
                                          "%" APR_SIZE_T_FMT ":%s://%s%s%s",
                                          (strlen(ap_http_scheme(r)) + 3 + strlen(thisserver) + strlen(thisport) + strlen(r->uri)),
                                          ap_http_scheme(r), thisserver, thisport, r->uri);
#endif

            if (!apr_is_empty_array(m)) {
                ap_rputs(         "7:sourcesl", r);
                for (i = 0; i < m->nelts; i++) {
                    char *e = ((char **) m->elts)[i];
                    ap_rputs(e, r);
                }
                ap_rputs(         "e8:url-listl", r);
                for (i = 0; i < m->nelts; i++) {
                    char *e = ((char **) m->elts)[i];
                    ap_rputs(e, r);
                }
            }
        }

        ap_rputs(         "e", r);

        ap_rputs(     "e", r);

        return OK;
    }

    case ZSYNC:

        if (!hashbag || !hashbag->sha1hex || (hashbag->zblocksize == 0)
                || !hashbag->zhashlens || !hashbag->zsumshex) {
            debugLog(r, cfg, "zsync requested, but required data is missing");
            return HTTP_NOT_FOUND;
        }

        setenv_give(r, "zsync");
        debugLog(r, cfg, "Sending zsync");
        ap_set_content_type(r, "application/x-zsync");

        ap_rputs("zsync: 0.6.1\n", r);
        ap_rprintf(r, "Filename: %s\n", basename);

        time_str = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
        apr_rfc822_date(time_str, r->finfo.mtime);
        ap_rprintf(r, "MTime: %s\n", time_str);

        ap_rprintf(r, "Blocksize: %d\n", hashbag->zblocksize);
        ap_rprintf(r, "Length: %s\n", apr_off_t_toa(r->pool, r->finfo.size));
        ap_rprintf(r, "Hash-Lengths: %s\n", hashbag->zhashlens);

        /* URLs */
        /* The zsync client (as of 0.6.1) tries the provided URLs in random order.
         * Thus, we need to restrict the list of URLs to the ones that are
         * closest; otherwise, it will download from anywhere in the world. */
        int found_urls = 0;
        mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
        for (i = 0; i < mirrors_same_prefix->nelts; i++, found_urls++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "URL: %s%s\n", mirror->baseurl, filename);
        }
        if (!found_urls) {
            mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
            for (i = 0; i < mirrors_same_as->nelts; i++, found_urls++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "URL: %s%s\n", mirror->baseurl, filename);
            }
        }
        if (!found_urls) {
            mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
            for (i = 0; i < mirrors_same_country->nelts; i++, found_urls++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "URL: %s%s\n", mirror->baseurl, filename);
            }
        }
        if (!found_urls) {
            mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
            for (i = 0; i < mirrors_same_region->nelts; i++, found_urls++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "URL: %s%s\n", mirror->baseurl, filename);
            }
        }
        if (!found_urls) {
            mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
            for (i = 0; i < mirrors_elsewhere->nelts; i++, found_urls++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "URL: %s%s\n", mirror->baseurl, filename);
            }
        }
        /* add the redirector, in case there wasn't any mirror */
        if (!found_urls) {
            ap_rprintf(r, "URL: %s://%s%s%s\n", ap_http_scheme(r), thisserver, thisport, r->uri);
        }


        ap_rprintf(r, "SHA-1: %s\n\n", hashbag->sha1hex);

        if (!hashbag->zsumshex || !hashbag->zsumshex[0]) {
            /* A zero-length file will correctly have zero zsync checksums */
            return OK;
        }
        int l = strlen(hashbag->zsumshex);
        ap_rwrite(hex_to_bin(r->pool, hashbag->zsumshex, l/2),
                  l/2, r);
        return OK;


    case MAGNET:
        if (!hashbag || !magnet) {
            return HTTP_NOT_FOUND;
        }
        ap_set_content_type(r, "text/plain; charset=UTF-8");
        ap_rprintf(r, "%s\n", magnet);
        setenv_give(r, "magnet");
        return OK;

    case YUMLIST:
        ap_set_content_type(r, "text/plain; charset=UTF-8");
        apr_array_header_t *topten = get_n_best_mirrors(r, 10, mirrors_same_prefix, mirrors_same_as,
                                                         mirrors_same_country, mirrors_same_region,
                                                         mirrors_elsewhere);
        if (topten->nelts > 0) {
            mirrorp = (mirror_entry_t **)topten->elts;
            for (i = 0; i < topten->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "%s%s/\n", mirror->baseurl, yum->dir);
            }
        } else {
            ap_rprintf(r, "%s://%s%s/%s/\n", ap_http_scheme(r), thisserver, thisport, yum->dir);
        }
        setenv_give(r, "yumlist");
        return OK;

    } /* end switch representation */

    const char *found_in;
    /* choose from country, then from region, then from elsewhere */
    if (!chosen) {
        if (!apr_is_empty_array(mirrors_same_prefix)) {
            mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
            chosen = mirrorp[find_best(mirrors_same_prefix)];
            found_in = "prefix";
        } else if (!apr_is_empty_array(mirrors_same_as)) {
            mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
            chosen = mirrorp[find_best(mirrors_same_as)];
            found_in = "AS";
        } else if (!apr_is_empty_array(mirrors_same_country)) {
            mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
            chosen = mirrorp[find_best(mirrors_same_country)];
            if (strcasecmp(chosen->country_code, country_code) == 0) {
                found_in = "country";
            } else {
                found_in = "other_country";
            }
        } else if (!apr_is_empty_array(mirrors_same_region)) {
            mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
            chosen = mirrorp[find_best(mirrors_same_region)];
            found_in = "region";
        } else if (!apr_is_empty_array(mirrors_elsewhere)) {
            mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
            chosen = mirrorp[find_best(mirrors_elsewhere)];
            found_in = "other";
        }
    }

    if (!chosen) {
        ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r,
            "[mod_mirrorbrain] '%s': no usable mirrors after classification. Have to deliver directly.",
            filename);
        setenv_give(r, "file");
        return DECLINED;
    }
    debugLog(r, cfg, "Chose server %s", chosen->identifier);



    /* Build target URI */
    if (cfg->stampkey) {
        const char *epoch = apr_itoa(r->pool, apr_time_sec(r->request_time));
        const char *epochkey = apr_pstrcat(r->pool, epoch, " ", cfg->stampkey, NULL);
        const char *stamp = ap_md5(r->pool, (unsigned const char *)epochkey);

        debugLog(r, cfg, "stamp: '%s' -> %s", epochkey, stamp);
        uri = apr_pstrcat(r->pool, chosen->baseurl, filename,
                          "?time=", epoch,
                          "&stamp=", stamp, NULL);
    } else {
        uri = apr_pstrcat(r->pool, chosen->baseurl, filename, NULL);
    }

    /* Send it away: set a "Location:" header and 302 redirect. */
    debugLog(r, cfg, "Redirect to '%s'", uri);

    /* for _conditional_ logging, leave some mark */
    apr_table_setn(r->subprocess_env, "MB_REDIRECTED", "1");
    apr_table_setn(r->subprocess_env, "MB_REALM", apr_pstrdup(r->pool, found_in));

    apr_table_setn(r->err_headers_out, "X-MirrorBrain-Mirror", chosen->identifier);
    apr_table_setn(r->err_headers_out, "X-MirrorBrain-Realm", found_in);


    /* add HTTP headers according to RFC 5988 (Web Linking)
     * and RFC 5854/6249 (Metalink/HTTP: Mirrors and Hashes) */
    /* rel=describedby */
    apr_table_addn(r->err_headers_out, "Link",
                   apr_pstrcat(r->pool,
                               "<", ap_http_scheme(r), "://", thisserver, thisport, r->uri, ".meta4>; "
                               "rel=describedby; type=\"application/metalink4+xml\"",
                               NULL));
    if (hashbag && hashbag->pgp) {
        apr_table_addn(r->err_headers_out, "Link",
                       apr_pstrcat(r->pool,
                                   "<", ap_http_scheme(r), "://", thisserver, thisport, r->uri, ".asc>; "
                                   "rel=describedby; type=\"application/pgp-signature\"",
                                   NULL));
    }
    if (!apr_is_empty_array(scfg->tracker_urls) && hashbag && hashbag->btihhex) {
        apr_table_addn(r->err_headers_out, "Link",
                       apr_pstrcat(r->pool,
                                   "<", ap_http_scheme(r), "://", thisserver, thisport, r->uri, ".torrent>; "
                                   "rel=describedby; type=\"application/x-bittorrent\"",
                                   NULL));
    }

    /* rel=duplicate */
    apr_array_header_t *topten = get_n_best_mirrors(r, 5, mirrors_same_prefix, mirrors_same_as,
                                                     mirrors_same_country, mirrors_same_region,
                                                     mirrors_elsewhere);
    if (topten->nelts > 0) {
        mirrorp = (mirror_entry_t **)topten->elts;
        for (i = 0; i < topten->nelts; i++) {
            mirror = mirrorp[i];
            apr_table_addn(r->err_headers_out, "Link",
                           apr_pstrcat(r->pool,
                                       "<", mirror->baseurl, filename,
                                       ">; rel=duplicate"
                                       "; pri=", apr_itoa(r->pool, i+1),
                                       "; geo=", mirror->country_code,
                                       NULL));
        }
    }

    /* RFC 3230 HTTP Instance Digests (including updates from RFC 5843) */
    if (hashbag) {
        if (hashbag->md5hex) {
            apr_table_addn(r->err_headers_out, "Digest",
                           apr_pstrcat(r->pool, "MD5=",
                                       hex_to_b64(r->pool, hashbag->md5hex, MD5_DIGESTSIZE),
                                       NULL));
        }
        if (hashbag->sha1hex) {
            apr_table_addn(r->err_headers_out, "Digest",
                           apr_pstrcat(r->pool, "SHA=",
                                       hex_to_b64(r->pool, hashbag->sha1hex, SHA1_DIGESTSIZE),
                                       NULL));
        }
        if (hashbag->sha256hex) {
            apr_table_addn(r->err_headers_out, "Digest",
                           apr_pstrcat(r->pool, "SHA-256=",
                                       hex_to_b64(r->pool, hashbag->sha256hex, SHA256_DIGESTSIZE),
                                       NULL));
        }
    }

    apr_table_setn(r->headers_out, "Location", uri);

#ifdef WITH_MEMCACHE
    if (scfg->memcached_on) {
        /* memorize IP<->mirror association in memcache */
        m_val = apr_itoa(r->pool, chosen->id);
        debugLog(r, cfg, "memcache insert: '%s' -> '%s'", m_key, m_val);
        if (scfg->memcached_lifetime == UNSET)
            scfg->memcached_lifetime = DEFAULT_MEMCACHED_LIFETIME;
        rv = apr_memcache_set(memctxt, m_key, m_val, strlen(m_val), scfg->memcached_lifetime, 0);
        if (rv != APR_SUCCESS)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                         "[mod_mirrorbrain] memcache error setting key '%s' "
                         "with %d bytes of data",
                         m_key, (int) strlen(m_val));
    }
#endif

    setenv_give(r, "redirect");
    return HTTP_MOVED_TEMPORARILY;
}

#ifdef WITH_MEMCACHE
static int mb_status_hook(request_rec *r, int flags)
{
    apr_uint16_t i;
    apr_status_t rv;
    apr_memcache_t *memctxt;                    /* memcache context provided by mod_memcache */
    apr_memcache_stats_t *stats;
    mb_server_conf *sc = ap_get_module_config(r->server->module_config, &mirrorbrain_module);

    if (sc == NULL || flags & AP_STATUS_SHORT)
        return OK;

    if (!sc->memcached_on)
        return OK;

    memctxt = ap_memcache_client(r->server);

    for (i = 0; i < memctxt->ntotal; i++) {
        rv = apr_memcache_stats(memctxt->live_servers[i], r->pool, &stats);

        if (rv == APR_SUCCESS) {
            ap_rputs("<hr />\n", r);
            ap_rprintf(r, "<h1>MemCached Status for %s:%d</h1>\n\n",
                    memctxt->live_servers[i]->host,
                    memctxt->live_servers[i]->port);

            ap_rputs("\n\n<table border=\"0\">", r);
            ap_rprintf(r, "<tr><td>version:               </td><td>%s</td>\n", stats->version);
            ap_rprintf(r, "<tr><td>pid:                   </td><td>%d</td>\n", stats->pid);
            ap_rprintf(r, "<tr><td>uptime:                </td><td>\t%d</td>\n", stats->uptime);
            ap_rprintf(r, "<tr><td>pointer_size:          </td><td>\t%d</td>\n", stats->pointer_size);
            ap_rprintf(r, "<tr><td>rusage_user:           </td><td>\t%" APR_INT64_T_FMT "</td>\n", stats->rusage_user);
            ap_rprintf(r, "<tr><td>rusage_system:         </td><td>\t%" APR_INT64_T_FMT "</td>\n", stats->rusage_system);
            ap_rprintf(r, "<tr><td>curr_items:            </td><td>\t%d</td>\n", stats->curr_items);
            ap_rprintf(r, "<tr><td>total_items:           </td><td>\t%d</td>\n", stats->total_items);
            ap_rprintf(r, "<tr><td>bytes used:            </td><td>\t%" APR_UINT64_T_FMT "</td>\n", stats->bytes);
            ap_rprintf(r, "<tr><td>curr_connections:      </td><td>\t%d</td>\n", stats->curr_connections);
            ap_rprintf(r, "<tr><td>total_connections:     </td><td>\t%d</td>\n", stats->total_connections);
            ap_rprintf(r, "<tr><td>connection_structures: </td><td>\t%d</td>\n", stats->connection_structures);
            ap_rprintf(r, "<tr><td>cmd_get:               </td><td>\t%d</td>\n", stats->cmd_get);
            ap_rprintf(r, "<tr><td>cmd_set:               </td><td>\t%d</td>\n", stats->cmd_set);
            ap_rprintf(r, "<tr><td>get_hits:              </td><td>\t%d</td>\n", stats->get_hits);
            ap_rprintf(r, "<tr><td>get_misses:            </td><td>\t%d</td>\n", stats->get_misses);
            ap_rprintf(r, "<tr><td>evictions:             </td><td>\t%" APR_UINT64_T_FMT "</td>\n", stats->evictions);
            ap_rprintf(r, "<tr><td>bytes_read:            </td><td>\t%" APR_UINT64_T_FMT "</td>\n", stats->bytes_read);
            ap_rprintf(r, "<tr><td>bytes_written:         </td><td>\t%" APR_UINT64_T_FMT "</td>\n", stats->bytes_written);
            ap_rprintf(r, "<tr><td>limit_maxbytes:        </td><td>\t%d</td>\n", stats->limit_maxbytes);
            ap_rprintf(r, "<tr><td>threads:               </td><td>\t%d</td>\n", stats->threads);
            ap_rputs("</table>\n", r);
        }
        else {
            ap_rprintf(r, "<h1>Failed to load MemCached Status for %s:%d</h1>\n\n",
                    memctxt->live_servers[i]->host,
                    memctxt->live_servers[i]->port);
        };
    }

    return OK;
}

void mb_status_register(apr_pool_t *p)
{
    APR_OPTIONAL_HOOK(ap, status_hook, mb_status_hook, NULL, NULL, APR_HOOK_MIDDLE);
}

static int mb_pre_config(apr_pool_t *pconf,
                              apr_pool_t *plog,
                              apr_pool_t *ptemp)
{
    /* Register to handle mod_status status page generation */
    mb_status_register(pconf);

    return OK;
}
#endif



static const command_rec mb_cmds[] =
{
    /* to be used only in Directory et al. */
    AP_INIT_FLAG("MirrorBrainEngine", mb_cmd_engine, NULL,
                 ACCESS_CONF,
                 "Set to On or Off to enable or disable redirecting"),
    AP_INIT_FLAG("MirrorBrainDebug", mb_cmd_debug, NULL,
                 ACCESS_CONF,
                 "Set to On or Off to enable or disable debug logging to error log"),

    /* to be used everywhere */
    AP_INIT_TAKE1("MirrorBrainMinSize", mb_cmd_minsize, NULL,
                  OR_OPTIONS,
                  "Minimum size, in bytes, that a file must be, in order to redirect "
                  "requests to a mirror. Smaller files will be delivered directly. "
                  "Default: 4096 bytes."),
    AP_INIT_TAKE1("MirrorBrainExcludeMimeType", mb_cmd_excludemime, 0,
                  OR_OPTIONS,
                  "Mimetype to always exclude from redirecting (wildcards allowed)"),
    AP_INIT_TAKE1("MirrorBrainExcludeUserAgent", mb_cmd_excludeagent, 0,
                  OR_OPTIONS,
                  "User-Agent to always exclude from redirecting (wildcards allowed)"),
    AP_INIT_TAKE1("MirrorBrainExcludeNetwork", mb_cmd_excludenetwork, 0,
                  OR_OPTIONS,
                  "Network to always exclude from redirecting (simple string prefix)"),
    AP_INIT_TAKE1("MirrorBrainExcludeIP", mb_cmd_excludeip, 0,
                  OR_OPTIONS,
                  "IP address to always exclude from redirecting"),
    AP_INIT_TAKE1("MirrorBrainExcludeFileMask", mb_cmd_exclude_filemask, NULL,
                  ACCESS_CONF,
                  "Regexp which determines which files will be excluded form redirecting"),

    AP_INIT_FLAG("MirrorBrainHandleHEADRequestLocally", mb_cmd_handle_headrequest_locally, NULL,
                  OR_OPTIONS,
                  "Set to On to handle HEAD requests locally (instead of redirecting "
                  "them to a mirror). Default: Off."),

    AP_INIT_TAKE1("MirrorBrainMetalinkTorrentAddMask", mb_cmd_metalink_torrentadd_mask, NULL,
                  ACCESS_CONF,
                  "Regexp which determines for which files to look for correspondant "
                  ".torrent files, and add them into generated metalinks"),

    AP_INIT_TAKE3("MirrorBrainFallback", mb_cmd_fallback, NULL,
                  ACCESS_CONF,
                  "region code, country code and base URL of a mirror that is used when no "
                  "mirror can be found in the database. These mirrors are assumed to have "
                  "*all* files. (Or they could be configured per directory.)"),

    AP_INIT_TAKE1("MirrorBrainRedirectStampKey", mb_cmd_redirect_stamp_key, NULL,
                  ACCESS_CONF,
                  "Causes MirrorBrain to append a signed timestamp to redirection URLs. The "
                  "argument is a string that defines the key to encrypt the timestamp with. "
                  "Can be configured on directory-level."),
    AP_INIT_RAW_ARGS("MirrorBrainYumDir", mb_cmd_add_yumdir, NULL,
                  RSRC_CONF, /* RSRC_CONF|ACCESS_CONF, */
                  "Specify query arguments mapping to a directory that must have a certain file. "
                  "Syntax: arg1=<regexp> arg2=<regexp> <basedir> <mandatory_file>. "
                  "Parts of basedir can be substituted with query arguments $1-$9. "
                  "Patterns are forced to be anchored to start and end for security reasons. "
                  "Example: MirrorBrainYumDir release=(5\\.5) repo=(os|updates) arch=i586  "
                  "$1/$2/i386 repodata/repomd.xml"),

    /* to be used only in server context */
    AP_INIT_TAKE1("MirrorBrainDBDQuery", mb_cmd_dbd_query, NULL,
                  RSRC_CONF,
                  "The SQL query for fetching the mirrors from the backend database"),
    AP_INIT_TAKE1("MirrorBrainDBDQueryHash", mb_cmd_dbd_query_hash, NULL,
                  RSRC_CONF,
                  "The SQL query for fetching verification hashes from the backend database"),

#ifdef WITH_MEMCACHE
    AP_INIT_TAKE1("MirrorBrainInstance", mb_cmd_instance, NULL,
                  RSRC_CONF,
                  "Name of the MirrorBrain instance (used by Memcache)"),

    AP_INIT_FLAG("MirrorBrainMemcached", mb_cmd_memcached_on, NULL,
                  RSRC_CONF,
                  "Set to On/Off to use memcached to give clients repeatedly the same mirror"),

    AP_INIT_TAKE1("MirrorBrainMemcachedLifeTime", mb_cmd_memcached_lifetime, NULL,
                  RSRC_CONF,
                  "Lifetime (in seconds) associated with stored objects in "
                  "memcache daemon(s). Default is 600 s."),
#endif

    AP_INIT_TAKE1("MirrorBrainMetalinkHashesPathPrefix", mb_cmd_metalink_hashes_prefix, NULL,
                  RSRC_CONF,
                  "Prefix this path when looking for prepared hashes to inject into metalinks. "
                  "This directive is obsolete (with 2.13.0) and is going to be removed."),

    AP_INIT_FLAG("MirrorBrainHashesSuppressFilenames", mb_cmd_hashes_suppress_filenames, NULL,
                  RSRC_CONF,
                  "Set to On to suppress the filename included when hashes are sent. "
                  "Normally, they come as \"99eaed37390ba0571f8d285829ff63fc  foobar\" "
                  "as in the format well-known from the md5sum/sha1sum tools. Default: Off"),

    AP_INIT_TAKE2("MirrorBrainMetalinkPublisher", mb_cmd_metalink_publisher, NULL,
                  RSRC_CONF,
                  "Name and URL for the metalinks publisher elements"),

    AP_INIT_TAKE1("MirrorBrainTorrentTrackerURL", mb_cmd_tracker_url, NULL,
                  RSRC_CONF,
                  "Define the URL a BitTorrent Tracker to be included in Torrents and in Magnet "
                  "links. Directive can be repeated to specify multiple URLs."),

    AP_INIT_TAKE2("MirrorBrainDHTNode", mb_cmd_dht_node, NULL,
                  RSRC_CONF,
                  "Define a DHT node to be included in Torrents "
                  "links. Directive can be repeated to specify multiple nodes, and takes "
                  "two arguments (hostname, port)."),

    AP_INIT_FLAG("MirrorBrainMetalinkMagnetLinks", mb_cmd_metalink_magnet_links, NULL,
                  RSRC_CONF,
                  "If set to On, Magnet links will be included in Metalinks. Default is Off."),

    AP_INIT_TAKE1("MirrorBrainMetalinkBrokenTestMirrors", mb_cmd_metalink_broken_test_mirrors, NULL,
                  RSRC_CONF,
                  "Filename with snippet to include at the top of a metalink's "
                  "<resources> section, for testing broken mirrors"),

    AP_INIT_TAKE1("MirrorBrainMirrorlistStyleSheet", mb_cmd_mirrorlist_stylesheet, NULL,
                  RSRC_CONF,
                  "Sets a CSS stylesheet to add to mirror lists"),
    AP_INIT_TAKE1("MirrorBrainMirrorlistHeader", mb_cmd_mirrorlist_header, NULL,
                  RSRC_CONF,
                  "Absolute path to header to be included at the top of the mirror "
                  "lists/details page, instead of the built-in header."),
    AP_INIT_TAKE1("MirrorBrainMirrorlistFooter", mb_cmd_mirrorlist_footer, NULL,
                  RSRC_CONF,
                  "Absolute path to footer to be appended to the mirror "
                  "lists/details pages, instead of the built-in footer."),

    { NULL }
};

/* Tell Apache what phases of the transaction we handle */
static void mb_register_hooks(apr_pool_t *p)
{
#ifdef WITH_MEMCACHE
    ap_hook_pre_config    (mb_pre_config,  NULL, NULL, APR_HOOK_MIDDLE);
#endif
    ap_hook_post_config   (mb_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler       (mb_handler,     NULL, NULL, APR_HOOK_FIRST);
    ap_hook_child_init    (mb_child_init,  NULL, NULL, APR_HOOK_MIDDLE );
}

#ifdef AP_DECLARE_MODULE
AP_DECLARE_MODULE(mirrorbrain) =
#else
/* pre-2.4 */
module AP_MODULE_DECLARE_DATA mirrorbrain_module =
#endif
{
    STANDARD20_MODULE_STUFF,
    create_mb_dir_config,    /* create per-directory config structures */
    merge_mb_dir_config,     /* merge per-directory config structures  */
    create_mb_server_config, /* create per-server config structures    */
    merge_mb_server_config,  /* merge per-server config structures     */
    mb_cmds,                 /* command handlers */
    mb_register_hooks        /* register hooks */
};


/* vim: set ts=4 sw=4 expandtab smarttab: */
