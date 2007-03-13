/*
 * Copyright (c) 2007 Peter Poeml <poeml@suse.de> / Novell Inc.
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
 * mod_zrkadlo
 *
 * redirect clients to mirror servers, based on sql database
 *
 * Credits:
 *
 * This module was inspired by mod_offload, written by
 * Ryan C. Gordon <icculus@icculus.org>.
 *
 * It uses code from mod_authn_dbd, mod_authnz_ldap, mod_status, 
 * apr_memcache, ssl_scache_memcache.c
 *
 */

#include "ap_config.h"
#include "httpd.h"
#include "http_request.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"

#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_fnmatch.h"
#include "apr_dbd.h"
#include "mod_dbd.h"

#include <GeoIP.h>
#include <apr_memcache-0/apr_memcache.h>
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

#define MOD_ZRKADLO_VER "1.0"
#define VERSION_COMPONENT "mod_zrkadlo/"MOD_ZRKADLO_VER

#define DEFAULT_GEOIPFILE "/usr/share/GeoIP/GeoIP.dat"
#define MEMCACHED_HOST "127.0.0.1"
#define MEMCACHED_PORT "11211"
#define DEFAULT_MEMCACHED_MIN 0
#define DEFAULT_MEMCACHED_SOFTMAX 1
#define DEFAULT_MEMCACHED_LIFETIME 600

#define DEFAULT_MIN_MIRROR_SIZE 4096

module AP_MODULE_DECLARE_DATA zrkadlo_module;

/* could also be put into the server config */
static const char *geoipfilename = DEFAULT_GEOIPFILE;
static GeoIP *gip = NULL;     /* geoip object */

/* The underlying apr_memcache system is thread safe.. */
static apr_memcache_t* memctxt;

/** A structure that represents a mirror */
typedef struct mirror_entry mirror_entry_t;

/* a mirror */
struct mirror_entry {
    int id;
    const char *identifier;
    char country_code[3];    /* the 2-letter-string */
    const char *region;
    int score;
    const char *baseurl;
    int is_static;
    int rank;
};

/* per-dir configuration */
typedef struct
{
    int engine_on;
    int debug;
    int min_size;
    int handle_dirindex_locally;
    int handle_headrequest_locally;
    const char *query;
    const char *mirror_base;
    apr_array_header_t *exclude_mime;
    apr_array_header_t *exclude_agents;
    ap_regex_t *exclude_filemask;
} zrkadlo_dir_conf;

/* per-server configuration */
typedef struct
{
    int memcached_on;
    const char *memcached_addr;
    int memcached_min;
    int memcached_softmax;
    int memcached_hardmax;
    int memcached_lifetime;
    apr_table_t *treat_country_as; /* treat country as another country */
} zrkadlo_server_conf;


static ap_dbd_t *(*zrkadlo_dbd_acquire_fn)(request_rec*) = NULL;
static void (*zrkadlo_dbd_prepare_fn)(server_rec*, const char*, const char*) = NULL;

static void debugLog(const request_rec *r, const zrkadlo_dir_conf *cfg,
                     const char *fmt, ...)
{
    if (cfg->debug) {
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
                      r, "[mod_zrkadlo] %s", buf);
    }
}

static void zrkadlo_die(void)
{
    /* This is used for fatal errors where it makes
     * sense to really exit from the complete program. */
    exit(1);
}

static apr_status_t zrkadlo_cleanup()
{
        GeoIP_delete(gip);
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, NULL, "[mod_zrkadlo] cleaned up geoipfile");
        return APR_SUCCESS;
}

/* from ssl_scache_memcache.c */
static void zrkadlo_mc_init(server_rec *s, apr_pool_t *p)
{       
    apr_status_t rv;
    int thread_limit = 0;
    int nservers = 0;
    char* cache_config;                 
    char* split;                        
    char* tok;                          
    zrkadlo_server_conf *conf = ap_get_module_config(s->module_config, &zrkadlo_module);

    ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);

    if (!conf->memcached_on) 
        return;

    /* find all the servers in the first run to get a total count */
    cache_config = apr_pstrdup(p, conf->memcached_addr);
    split = apr_strtok(cache_config, ",", &tok);
    while (split) {
        nservers++;
        split = apr_strtok(NULL,",", &tok);
    }

    rv = apr_memcache_create(p, nservers, 0, &memctxt);
    if (rv != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s,
                    "[mod_zrkadlo] ZrkadloMemcachedAddrPort: Failed to create Memcache Object of '%d' size.",
                     nservers);
        zrkadlo_die();
    }

    /* now add each server to the memcache */
    cache_config = apr_pstrdup(p, conf->memcached_addr);
    split = apr_strtok(cache_config, ",", &tok);
    while (split) {
        apr_memcache_server_t* st;
        char* host_str;
        char* scope_id;
        apr_port_t port;

        rv = apr_parse_addr_port(&host_str, &scope_id, &port, split, p);
        if (rv != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s,
                         "[mod_zrkadlo] ZrkadloMemcachedAddrPort: Failed to Parse Server: '%s'", split);
            zrkadlo_die();
        }

        if (host_str == NULL) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s,
                         "[mod_zrkadlo] ZrkadloMemcachedAddrPort: Failed to Parse Server, "
                         "no hostname specified: '%s'", split);
            zrkadlo_die();
        }

        if (port == 0) {
            port = atoi(MEMCACHED_PORT);
        }

        /* default pool size */
        if (conf->memcached_min == UNSET)
            conf->memcached_min = DEFAULT_MEMCACHED_MIN;
        if (conf->memcached_softmax == UNSET)
            conf->memcached_softmax = DEFAULT_MEMCACHED_SOFTMAX;
        /* Should Max Conns be (thread_limit / nservers) ? */
        if (conf->memcached_hardmax == UNSET)
            conf->memcached_hardmax = thread_limit;

        /* sanity checks */
        if (conf->memcached_hardmax > thread_limit) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s, "[mod_zrkadlo] ZrkadloMemcachedConnHardMax: "
                         "must be equal to ThreadLimit or less");
            zrkadlo_die();
        }
        if (conf->memcached_softmax > conf->memcached_hardmax) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s, "[mod_zrkadlo] ZrkadloMemcachedConnSoftMax: "
                         "must not be larger than ZrkadloMemcachedConnHardMax (%d)", 
                         conf->memcached_hardmax);
            zrkadlo_die();
        }
        if (conf->memcached_min > conf->memcached_softmax) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s, "[mod_zrkadlo] ZrkadloMemcachedConnMin: "
                         "must not be larger than ZrkadloMemcachedConnSoftMax");
            zrkadlo_die();
        }

        ap_log_error(APLOG_MARK, APLOG_NOTICE, rv, s,
                     "[mod_zrkadlo] Creating memcached connection pool; min %d, softmax %d, hardmax %d",
                                        conf->memcached_min,
                                        conf->memcached_softmax,
                                        conf->memcached_hardmax);
        rv = apr_memcache_server_create(p, host_str, port, 
                                        conf->memcached_min,
                                        conf->memcached_softmax,
                                        conf->memcached_hardmax,
                                        600, &st);
        if (rv != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s,
                         "[mod_zrkadlo] ZrkadloMemcachedAddrPort: Failed to Create Server: %s:%d",
                         host_str, port);
            zrkadlo_die();
        }

        rv = apr_memcache_add_server(memctxt, st);
        if (rv != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rv, s,
                         "[mod_zrkadlo] ZrkadloMemcachedAddrPort: Failed to Add Server: %s:%d",
                         host_str, port);
            zrkadlo_die();
        }

        split = apr_strtok(NULL,",", &tok);
    }

    return;
}

static void zrkadlo_child_init(apr_pool_t *p, server_rec *s)
{
    if (!gip) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, 
                "[mod_zrkadlo] opening geoip file %s", geoipfilename);
        gip = GeoIP_open(geoipfilename, GEOIP_MEMORY_CACHE);
    }
    if(!gip) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, 
                "[mod_zrkadlo] Error while opening geoip file '%s'", geoipfilename);
    }
    apr_pool_cleanup_register(p, NULL, zrkadlo_cleanup, zrkadlo_cleanup);

    srand((unsigned int)time(NULL));
}


static int zrkadlo_post_config(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp,
                 server_rec *s)
{
    ap_add_version_component(p, VERSION_COMPONENT);
    zrkadlo_mc_init(s, p);

    /* make sure that mod_form is loaded */
    if (ap_find_linked_module("mod_form.c") == NULL) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
                     "[mod_zrkadlo] Module mod_form missing. Mod_form "
                     "must be loaded in order for mod_zrkadlo to function properly");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    return OK;
}


static void *create_zrkadlo_dir_config(apr_pool_t *p, char *dirspec)
{
    zrkadlo_dir_conf *new =
      (zrkadlo_dir_conf *) apr_pcalloc(p, sizeof(zrkadlo_dir_conf));

    new->engine_on                  = UNSET;
    new->debug                      = UNSET;
    new->min_size                   = DEFAULT_MIN_MIRROR_SIZE;
    new->handle_dirindex_locally    = UNSET;
    new->handle_headrequest_locally = UNSET;
    new->query = NULL;
    new->mirror_base = NULL;
    new->exclude_mime = apr_array_make(p, 0, sizeof (char *));
    new->exclude_agents = apr_array_make(p, 0, sizeof (char *));
    new->exclude_filemask = NULL;

    return (void *) new;
}

static void *merge_zrkadlo_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    zrkadlo_dir_conf *mrg  = (zrkadlo_dir_conf *) apr_pcalloc(p, sizeof(zrkadlo_dir_conf));
    zrkadlo_dir_conf *base = (zrkadlo_dir_conf *) basev;
    zrkadlo_dir_conf *add  = (zrkadlo_dir_conf *) addv;

    /* debugLog("merge_zrkadlo_dir_config: new=%08lx  base=%08lx  overrides=%08lx",
     *         (long)mrg, (long)base, (long)add); */

    cfgMergeInt(engine_on);
    cfgMergeInt(debug);
    mrg->min_size = (add->min_size != DEFAULT_MIN_MIRROR_SIZE) ? add->min_size : base->min_size;
    cfgMergeInt(handle_dirindex_locally);
    cfgMergeInt(handle_headrequest_locally);
    cfgMergeString(query);
    cfgMergeString(mirror_base);
    mrg->exclude_mime = apr_array_append(p, base->exclude_mime, add->exclude_mime);
    mrg->exclude_agents = apr_array_append(p, base->exclude_agents, add->exclude_agents);
    mrg->exclude_filemask = (add->exclude_filemask == NULL) ? base->exclude_filemask : add->exclude_filemask;

    return (void *) mrg;
}

static void *create_zrkadlo_server_config(apr_pool_t *p, server_rec *s)
{
    zrkadlo_server_conf *new =
      (zrkadlo_server_conf *) apr_pcalloc(p, sizeof(zrkadlo_server_conf));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, 
            "[mod_zrkadlo] creating server config");

    new->memcached_on = UNSET;
    new->memcached_addr = MEMCACHED_HOST ":" MEMCACHED_PORT;
    new->memcached_min = UNSET;
    new->memcached_softmax = UNSET;
    new->memcached_hardmax = UNSET;
    new->memcached_lifetime = UNSET;
    new->treat_country_as = apr_table_make(p, 0);

    return (void *) new;
}

static void *merge_zrkadlo_server_config(apr_pool_t *p, void *basev, void *addv)
{
    zrkadlo_server_conf *base = (zrkadlo_server_conf *) basev;
    zrkadlo_server_conf *add = (zrkadlo_server_conf *) addv;
    zrkadlo_server_conf *mrg = apr_pcalloc(p, sizeof(zrkadlo_server_conf));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, 
            "[mod_zrkadlo] merging server config");

    cfgMergeBool(memcached_on);
    mrg->memcached_addr = (add->memcached_addr == NULL) ? base->memcached_addr : add->memcached_addr;
    cfgMergeInt(memcached_min);
    cfgMergeInt(memcached_softmax);
    cfgMergeInt(memcached_hardmax);
    cfgMergeInt(memcached_lifetime);
    mrg->treat_country_as = apr_table_overlay(p, add->treat_country_as, base->treat_country_as);

    return (void *) mrg;
}

static const char *zrkadlo_cmd_engine(cmd_parms *cmd, void *config, int flag)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->engine_on = flag;
    cfg->mirror_base = apr_pstrdup(cmd->pool, cmd->path);
    return NULL;
}

static const char *zrkadlo_cmd_debug(cmd_parms *cmd, void *config, int flag)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->debug = flag;
    return NULL;
}

static const char *zrkadlo_cmd_minsize(cmd_parms *cmd, void *config,
                                   const char *arg1)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->min_size = atoi(arg1);
    if (cfg->min_size < 0)
        return "ZrkadloMinSize requires a non-negative integer.";
    return NULL;
}

static const char *zrkadlo_cmd_excludemime(cmd_parms *cmd, void *config,
                                       const char *arg1)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    char **mimepattern = (char **) apr_array_push(cfg->exclude_mime);
    *mimepattern = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *zrkadlo_cmd_excludeagent(cmd_parms *cmd, void *config,
                                        const char *arg1)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    char **agentpattern = (char **) apr_array_push(cfg->exclude_agents);
    *agentpattern = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *zrkadlo_cmd_exclude_filemask(cmd_parms *cmd, void *config, const char *arg)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->exclude_filemask = ap_pregcomp(cmd->pool, arg, AP_REG_EXTENDED);
    if (cfg->exclude_filemask == NULL) {
        return "ZrkadloExcludeFileMask regex could not be compiled";
    }
    return NULL;
}

static const char *zrkadlo_cmd_handle_dirindex_locally(cmd_parms *cmd, 
            void *config, int flag)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->handle_dirindex_locally = flag;
    return NULL;
}

static const char *zrkadlo_cmd_handle_headrequest_locally(cmd_parms *cmd, 
        void *config, int flag)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->handle_headrequest_locally = flag;
    return NULL;
}

static const char *zrkadlo_cmd_geoip_filename(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    geoipfilename = apr_pstrdup(cmd->pool, arg1);

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,
                 "[mod_zrkadlo] Setting GeoIPFilename: '%s'", 
                 geoipfilename);
    return NULL;
}

static const char *zrkadlo_cmd_memcached_on(cmd_parms *cmd, void *config,
                                int flag)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->memcached_on = flag;
    return NULL;
}

static const char *zrkadlo_cmd_memcached_addr(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->memcached_addr = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *zrkadlo_cmd_memcached_min(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->memcached_min = atoi(arg1);
    if (cfg->memcached_min <= 0)
        return "ZrkadloMemcachedConnMin requires a non-negative integer.";
    return NULL;
}

static const char *zrkadlo_cmd_memcached_softmax(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->memcached_softmax = atoi(arg1);
    if (cfg->memcached_softmax <= 0)
        return "ZrkadloMemcachedConnSoftMax requires an integer > 0.";
    return NULL;
}

static const char *zrkadlo_cmd_memcached_hardmax(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->memcached_hardmax = atoi(arg1);
    if (cfg->memcached_hardmax <= 0)
        return "ZrkadloMemcachedConnHardMax requires an integer > 0.";
    return NULL;
}

static const char *zrkadlo_cmd_memcached_lifetime(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->memcached_lifetime = atoi(arg1);
    if (cfg->memcached_lifetime <= 0)
        return "ZrkadloMemcachedLifeTime requires an integer > 0.";
    return NULL;
}

static const char *zrkadlo_cmd_treat_country_as(cmd_parms *cmd, void *config,
                                const char *arg1, const char *arg2)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    apr_table_set(cfg->treat_country_as, arg1, arg2);
    return NULL;
}

static const char *zrkadlo_dbd_prepare(cmd_parms *cmd, void *cfg, const char *query)
{
    static unsigned int label_num = 0;
    char *label;

    if (zrkadlo_dbd_prepare_fn == NULL) {
        zrkadlo_dbd_prepare_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_prepare);
        if (zrkadlo_dbd_prepare_fn == NULL) {
            return "You must load mod_dbd to enable Zrkadlo functions";
        }
        zrkadlo_dbd_acquire_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_acquire);
    }
    label = apr_psprintf(cmd->pool, "zrkadlo_dbd_%d", ++label_num);

    zrkadlo_dbd_prepare_fn(cmd->server, query, label);

    /* save the label here for our own use */
    return ap_set_string_slot(cmd, cfg, label);
}

static int find_lowest_rank(apr_array_header_t *arr) 
{
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

static int zrkadlo_handler(request_rec *r)
{
    zrkadlo_dir_conf *cfg = NULL;
    zrkadlo_server_conf *scfg = NULL;
    char *uri = NULL;
    char *filename = NULL;
    const char *user_agent = NULL;
    const char *val = NULL;
    const char *clientip = NULL;
    const char *fakefile = NULL;
    const char *newmirror = NULL;
    const char *mirrorlist = NULL;
    short int country_id;
    char* country_code;
    const char* continent_code;
    int i;
    int cached_id;
    int mirror_cnt;
    int unusable;
    char *m_res;
    char *m_key, *m_val;
    apr_size_t len;
    mirror_entry_t *new;
    mirror_entry_t *mirror;
    mirror_entry_t **mirrorp;
    mirror_entry_t *chosen = NULL;
    apr_status_t rv;
    apr_dbd_prepared_t *statement;
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    apr_array_header_t *mirrors;                /* this holds all mirror_entrys */
    apr_array_header_t *mirrors_same_country;   /* pointers into the mirrors array */
    apr_array_header_t *mirrors_same_region;    /* pointers into the mirrors array */
    apr_array_header_t *mirrors_elsewhere;      /* pointers into the mirrors array */
    const char* (*form_lookup)(request_rec*, const char*);

    cfg = (zrkadlo_dir_conf *)     ap_get_module_config(r->per_dir_config, 
                                                        &zrkadlo_module);
    scfg = (zrkadlo_server_conf *) ap_get_module_config(r->server->module_config, 
                                                        &zrkadlo_module);

    /* is ZrkadloEngine disabled for this directory? */
    if (cfg->engine_on != 1) {
        return DECLINED;
    }
    debugLog(r, cfg, "ZrkadloEngine is On, mirror_base is '%s'", cfg->mirror_base);

    /* is it a HEAD request? */
    if (r->header_only && cfg->handle_headrequest_locally) {
        debugLog(r, cfg, "HEAD request for URI '%s'", r->unparsed_uri);
        return DECLINED;
    }

    if (r->method_number != M_GET) {
        debugLog(r, cfg, "Not a GET method for URI '%s'", r->unparsed_uri);
        return DECLINED;
    }

    /* is there a password? */
    if (r->ap_auth_type != NULL) {
        debugLog(r, cfg, "URI '%s' requires auth", r->unparsed_uri);
        return DECLINED;
    }

    debugLog(r, cfg, "URI: '%s'", r->unparsed_uri);
    debugLog(r, cfg, "filename: '%s'", r->filename);
    //debugLog(r, cfg, "server_hostname: '%s'", r->server->server_hostname);


    /* parse query arguments if present, */
    /* using mod_form's form_value() */
    form_lookup = APR_RETRIEVE_OPTIONAL_FN(form_value);
    if (form_lookup && r->args) {
        fakefile = form_lookup(r, "fakefile");
        clientip = form_lookup(r, "clientip");
        newmirror = form_lookup(r, "newmirror");
        mirrorlist = form_lookup(r, "mirrorlist");
    }
    if (clientip)
        debugLog(r, cfg, "FAKE Client IP: '%s'", clientip);
    else
        clientip = apr_pstrdup(r->pool, r->connection->remote_ip);

    /* do we redirect if the request is for directories? */
    /* XXX one should actually respect all strings which are configured
     * as DirectoryIndex */
    if (cfg->handle_dirindex_locally && ap_strcasestr(r->uri, "index.html")) {
        debugLog(r, cfg, "serving index.html locally "
                "(ZrkadloHandleDirectoryIndexLocally)");
        return DECLINED;
    }

    /* These checks apply only if the server response is not faked for testing */
    if (fakefile) {
        debugLog(r, cfg, "FAKE File -- not looking at real files");
    } else {

        if (r->finfo.filetype == APR_DIR) {
        /* if (ap_is_directory(r->pool, r->filename)) { */
            debugLog(r, cfg, "'%s' is a directory", r->filename);
            return DECLINED;
        }   

        /* is file missing? */
        if (r->finfo.filetype != APR_REG) {
            debugLog(r, cfg, "File '%s' does not exist acc. to r->finfo", r->filename);
            return DECLINED;
        }

        /* is the requested file too small? DECLINED */
        if (r->finfo.size < cfg->min_size) {
            debugLog(r, cfg, "File '%s' too small (%d bytes, less than %d)", 
                    r->filename, (int) r->finfo.size, (int) cfg->min_size);
            return DECLINED;
        }
    }

    /* is this file excluded from mirroring? */
    if (cfg->exclude_filemask && !ap_regexec(cfg->exclude_filemask, r->uri, 0, NULL, 0)) {
        debugLog(r, cfg, "File '%s' is excluded by ZrkadloExcludeFileMask", r->uri);
        return DECLINED;
    }

    /* is the file in the list of mimetypes to never mirror? */
    if ((r->content_type) && (cfg->exclude_mime->nelts)) {

        for (i = 0; i < cfg->exclude_mime->nelts; i++) {

            char *mimetype = ((char **) cfg->exclude_mime->elts)[i];
            if (wild_match(mimetype, r->content_type)) {
                debugLog(r, cfg,
                    "URI '%s' (%s) is excluded from redirecting"
                    " by mimetype pattern '%s'", r->unparsed_uri,
                    r->content_type, mimetype);
                return DECLINED;
            }
        }
    }

    /* is this User-Agent excluded from redirecting? */
    user_agent = (const char *) apr_table_get(r->headers_in, "User-Agent");
    if ((user_agent) && (cfg->exclude_agents->nelts)) {

        for (i = 0; i < cfg->exclude_agents->nelts; i++) {

            char *agent = ((char **) cfg->exclude_agents->elts)[i];

            if (wild_match(agent, user_agent)) {
                debugLog(r, cfg,
                    "URI request '%s' from agent '%s' is excluded from"
                    " redirecting by User-Agent pattern '%s'",
                    r->unparsed_uri, user_agent, agent);
                return DECLINED;
            }
        }
    }


    /* look for associated mirror in memcache */
    cached_id = 0;
    if (scfg->memcached_on) {
        m_key = apr_pstrcat(r->pool, "z_", clientip, NULL);
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


    if (cfg->query == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_zrkadlo] No ZrkadloDBDQuery configured!");
        return DECLINED;
    }


    /* GeoIP lookup 
     * if mod_geoip was loaded, it would suffice to retrieve GEOIP_COUNTRY_CODE
     * as supplied by it via the notes table, but since we also need the
     * continent we need to use libgeoip ourselves. Thus, we can do our own
     * lookup just as well. */
    country_id = GeoIP_country_id_by_addr(gip, clientip);
    country_code = apr_pstrdup(r->pool, GeoIP_country_code[country_id]);
    continent_code = GeoIP_country_continent[country_id];

    debugLog(r, cfg, "Country '%s' (%d), Continent '%s'", country_code, 
            country_id, 
            continent_code);

    /* does this country need to be treated as another one? */
    val = apr_table_get(scfg->treat_country_as, country_code);
    if (val) {
        apr_cpystrn(country_code, val, sizeof(country_code)); /* fixed length, two bytes */
        debugLog(r, cfg, "Treating as country '%s'", val);
    }


    /* ask the database and pick the matching server according to region */

    //ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "[mod_zrkadlo] Acquiring database connection");
    ap_dbd_t *dbd = zrkadlo_dbd_acquire_fn(r);
    if (dbd == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_zrkadlo] Error acquiring database connection");
        return DECLINED; /* fail gracefully */
    }
    //ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "[mod_zrkadlo] Successfully acquired database connection.");
    debugLog(r, cfg, "Successfully acquired database connection.");

    statement = apr_hash_get(dbd->prepared, cfg->query, APR_HASH_KEY_STRING);
    if (statement == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] No ZrkadloDBDQuery configured!");
        return DECLINED;
    }

    /* strip the leading directory
     * no need to escape it for the SQL query because we use a prepared 
     * statement with bound parameter */

    char *ptr = canonicalize_file_name(r->filename);
    if (ptr == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_zrkadlo] Error canonicalizing filename '%s'", r->filename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    /* XXX we should forbid symlinks in mirror_base */
    filename = apr_pstrdup(r->pool, ptr + strlen(cfg->mirror_base));
    free(ptr);
    debugLog(r, cfg, "SQL lookup for (canonicalized) '%s'", filename);

    if (apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, statement, 
                1, /* we don't need random access actually, but 
                      without it the mysql driver doesn't return results...  */
                filename, NULL) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_zrkadlo] Error looking up %s in database", filename);
        return DECLINED;
    }

    mirror_cnt = apr_dbd_num_tuples(dbd->driver, res);
    if (mirror_cnt > 0) {
        debugLog(r, cfg, "Found %d mirror%s", mirror_cnt,
                (mirror_cnt == 1) ? "" : "s");
    }
    else {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, 
                "[mod_zrkadlo] no mirrors found for %s", filename);
        return DECLINED;
    }


    /* allocate space for the expected results */
    mirrors              = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t));
    mirrors_same_country = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));
    mirrors_same_region  = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));
    mirrors_elsewhere    = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));


    /* need to remind myself... how to use the pointer arrays:
     *                                                          
     * 1) multi line version, allowing for easier access of last added element
     * void **new_same = (void **)apr_array_push(mirrors_same_country);
     * *new_same = new;
     *
     * ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "[mod_zrkadlo] new_same->identifier: %s",
     *        ((mirror_entry_t *)*new_same)->identifier);
     *
     * 2) one line version
     * *(void **)apr_array_push(mirrors_same_country) = new;                        */


    /* store the results which the database yielded, taking into account which
     * mirrors are in the same country, same reagion, or elsewhere */
    for (rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row, -1); 
             rv != -1;
             rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row, -1)) {
        if (rv != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "[mod_zrkadlo] Error looking up %s in database", filename);
            return DECLINED;
        }

        new = apr_array_push(mirrors);
        new->id = 0;
        new->identifier = NULL;
        new->country_code[0] = 0;
        new->region = NULL;
        new->score = 0;
        new->baseurl = NULL;

        unusable = 0; /* if crucial data is missing... */

        /* id */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 0)) == NULL) 
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for id");
        else
            new->id = atoi(val);

        /* identifier */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 1)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for identifier");
        else 
            new->identifier = apr_pstrdup(r->pool, val);

        /* country_code */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 2)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for country_code");
        else
            apr_cpystrn(new->country_code, val, sizeof(new->country_code)); /* fixed length, two bytes */

        /* region */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 3)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for region");
        else
            new->region = apr_pstrdup(r->pool, val);

        /* score */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 4)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for score");
            unusable = 1;
        } else
            new->score = atoi(val);

        /* baseurl */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 5)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for baseurl");
            unusable = 1;
        } else
            new->baseurl = apr_pstrdup(r->pool, val);

        /* this mirror comes from the database */
        /* XXX not implemented: statically configured fallback mirrors */
        /* such mirrors could be entered in the database like any other mirrors,
         * but simply skipped by the scanner -- probably a no_scan flag is needed */
        new->is_static = 0;

        /* now, take some decisions */

        /* rank it (randomized, weighted by "score" value) */
        /* not using thread-safe rand_r() here, because it shouldn't make 
         * a real difference here */
        new->rank = (rand()>>16) * ((RAND_MAX>>16) / new->score);
        debugLog(r, cfg, "Found mirror #%d '%s'", new->id, new->identifier);
        
        if (unusable) {
            /* discard */
            apr_array_pop(mirrors);
            continue;
        }

        if (new->id && (new->id == cached_id)) {
            debugLog(r, cfg, "Mirror '%s' associated in memcache (cached_id %d)", new->identifier, cached_id);
            chosen = new;
        }

        /* same country? */
        if (apr_strnatcasecmp(new->country_code, country_code) == 0) {
            *(void **)apr_array_push(mirrors_same_country) = new;
        } else if (apr_strnatcasecmp(new->region, continent_code) == 0) {
        /* same region? */
            *(void **)apr_array_push(mirrors_same_region) = new;
        } else {
            *(void **)apr_array_push(mirrors_elsewhere) = new;
        }

    }

#if 0
    mirror_entry_t *elts;
    elts = (mirror_entry_t *) mirrors->elts;
    for (i = 0; i < mirrors->nelts; i++) {
        debugLog(r, cfg, "mirrors  %d   %s", i, elts[i].identifier);
    }
#endif

    if (cfg->debug) {

        /* list the same-country mirrors */
        /* Brad's mod_edir hdir.c helped me here.. thanks to his kind help */
        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        mirror = NULL;

        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same country: %s (score %d) (rank %d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        /* list the same-region mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same region: %s (score %d) (rank %d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        /* list all other mirrors */
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "elsewhere: %s (score %d) (rank %d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        debugLog(r, cfg, "Found %d mirror%s: %d country, %d region, %d elsewhere", mirror_cnt,
                (mirror_cnt == 1) ? "" : "s",
                mirrors_same_country->nelts,
                mirrors_same_region->nelts,
                mirrors_elsewhere->nelts);
    }

    /* choose from country, then from region, then from elsewhere */
    if (!chosen) {
        if (mirrors_same_country->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_same_country)];
        } else if (mirrors_same_region->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_same_region)];
        } else if (mirrors_elsewhere->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_elsewhere)];
        }
    }

    if (!chosen) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
            "[mod_zrkadlo] could not chose a server. Shouldn't have happened.");
        return DECLINED;
    }

    debugLog(r, cfg, "Chose server %s", chosen->identifier);

    /* send a HTML list instead of doing a redirect? */
    if (mirrorlist) {
        debugLog(r, cfg, "mirrorlist");
        ap_set_content_type(r, "text/html");
        ap_rputs(DOCTYPE_HTML_3_2
                 "<html><head>\n<title>Mirror List</title>\n</head><body>\n",
                 r);

        ap_rprintf(r, "Filename: %s<br>\n", filename);
        ap_rprintf(r, "Client IP address: %s<br>\n", clientip);
        ap_rprintf(r, "Found %d mirror%s: %d country, %d region, %d elsewhere\n", mirror_cnt,
                (mirror_cnt == 1) ? "" : "s",
                mirrors_same_country->nelts,
                mirrors_same_region->nelts,
                mirrors_elsewhere->nelts);

        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        mirror = NULL;

        ap_rprintf(r, "<h3>Mirrors in the same country (%s):</h3>", country_code);
        ap_rputs("<pre>", r);
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "<a href=\"%s%s\">%s%s</a> (score %d)<br>", 
                    mirror->baseurl, filename, 
                    mirror->baseurl, filename, 
                    mirror->score);
        }
        ap_rputs("</pre>", r);

        ap_rprintf(r, "<h3>Mirrors in the same continent (%s):</h3>", continent_code);
        ap_rputs("<pre>", r);
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "<a href=\"%s%s\">%s%s</a> (score %d)<br>", 
                    mirror->baseurl, filename, 
                    mirror->baseurl, filename, 
                    mirror->score);
        }
        ap_rputs("</pre>", r);

        ap_rputs("<h3>Mirrors in the rest of the world:</h3>", r);
        ap_rputs("<pre>", r);
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "<a href=\"%s%s\">%s%s</a> (score %d)<br>", 
                    mirror->baseurl, filename, 
                    mirror->baseurl, filename, 
                    mirror->score);
        }
        ap_rputs("</pre>", r);

        ap_rputs("</body>\n", r);
        return OK;
    }

    /* Send it away: set a "Location:" header and 302 redirect. */
    uri = apr_pstrcat(r->pool, chosen->baseurl, filename, NULL);
    debugLog(r, cfg, "Redirect to '%s'", uri);

    apr_table_setn(r->err_headers_out, "X-Zrkadlo-Chose-Mirror", chosen->identifier);
    //apr_table_setn(r->err_headers_out, "X-Zrkadlo", "Have a lot of fun...");
    apr_table_setn(r->headers_out, "Location", uri);


    if (scfg->memcached_on) {
        /* memorize IP<->mirror association in memcache */
        m_val = apr_itoa(r->pool, chosen->id);
        debugLog(r, cfg, "memcache insert: '%s' -> '%s'", m_key, m_val);
        if (scfg->memcached_lifetime == UNSET)
            scfg->memcached_lifetime = DEFAULT_MEMCACHED_LIFETIME;
        rv = apr_memcache_set(memctxt, m_key, m_val, strlen(m_val), scfg->memcached_lifetime, 0);
        if (rv != APR_SUCCESS)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                         "[mod_zrkadlo] memcache error setting key '%s' "
                         "with %d bytes of data", 
                         m_key, (int) strlen(m_val));
    }

    return HTTP_MOVED_TEMPORARILY;
}

static int zrkadlo_status_hook(request_rec *r, int flags)
{
    apr_uint16_t i;
    apr_status_t rv;
    apr_memcache_stats_t *stats;
    zrkadlo_server_conf *sc = ap_get_module_config(r->server->module_config, &zrkadlo_module);

    if (sc == NULL || flags & AP_STATUS_SHORT)
        return OK;

    if (!sc->memcached_on)
        return OK;

    for (i = 0; i < memctxt->ntotal; i++) {
        rv = apr_memcache_stats(memctxt->live_servers[i], r->pool, &stats);

        ap_rputs("<hr />\n", r);
        ap_rprintf(r, "<h1>MemCached Status for %s:%d</h1>\n\n", 
                memctxt->live_servers[i]->host,
                memctxt->live_servers[i]->port);

        ap_rputs("\n\n<table border=\"0\">", r);
        ap_rprintf(r, "<tr><td>version:               </td><td>%s</td>\n", stats->version); 
        ap_rprintf(r, "<tr><td>pid:                   </td><td>%d</td>\n", stats->pid);
        ap_rprintf(r, "<tr><td>uptime:                </td><td>\t%d</td>\n", stats->uptime);
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
        ap_rprintf(r, "<tr><td>bytes_read:            </td><td>\t%" APR_UINT64_T_FMT "</td>\n", stats->bytes_read);
        ap_rprintf(r, "<tr><td>bytes_written:         </td><td>\t%" APR_UINT64_T_FMT "</td>\n", stats->bytes_written);
        ap_rprintf(r, "<tr><td>limit_maxbytes:        </td><td>\t%d</td>\n", stats->limit_maxbytes);
        ap_rputs("</table>\n", r);
    }

    return OK;
}

void zrkadlo_status_register(apr_pool_t *p)
{
    APR_OPTIONAL_HOOK(ap, status_hook, zrkadlo_status_hook, NULL, NULL, APR_HOOK_MIDDLE);
}

static int zrkadlo_pre_config(apr_pool_t *pconf,
                              apr_pool_t *plog,
                              apr_pool_t *ptemp)
{
    /* Register to handle mod_status status page generation */
    zrkadlo_status_register(pconf);

    return OK;
}



static const command_rec zrkadlo_cmds[] =
{
    /* to be used only in Directory et al. */
    AP_INIT_FLAG("ZrkadloEngine", zrkadlo_cmd_engine, NULL, 
                 ACCESS_CONF,
                 "Set to On or Off to enable or disable redirecting"),
    AP_INIT_FLAG("ZrkadloDebug", zrkadlo_cmd_debug, NULL, 
                 ACCESS_CONF,
                 "Set to On or Off to enable or disable debug logging to error log"),

    /* to be used everywhere */
    AP_INIT_TAKE1("ZrkadloMinSize", zrkadlo_cmd_minsize, NULL, 
                  OR_OPTIONS,
                  "Minimum size, in bytes, that a file must be to be mirrored"),
    AP_INIT_TAKE1("ZrkadloExcludeMimeType", zrkadlo_cmd_excludemime, 0, 
                  OR_OPTIONS,
                  "Mimetype to always exclude from redirecting (wildcards allowed)"),
    AP_INIT_TAKE1("ZrkadloExcludeUserAgent", zrkadlo_cmd_excludeagent, 0, 
                  OR_OPTIONS,
                  "User-Agent to always exclude from redirecting (wildcards allowed)"),
    AP_INIT_TAKE1("ZrkadloExcludeFileMask", zrkadlo_cmd_exclude_filemask, NULL,
                  ACCESS_CONF,
                  "Regexp which determines which files will be excluded form redirecting"),

    AP_INIT_FLAG("ZrkadloHandleDirectoryIndexLocally", zrkadlo_cmd_handle_dirindex_locally, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to handle directory listings locally (don't redirect)"),
    AP_INIT_FLAG("ZrkadloHandleHEADRequestLocally", zrkadlo_cmd_handle_headrequest_locally, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to handle HEAD requests locally (don't redirect)"),

    /* to be used only in server context */
    AP_INIT_TAKE1("ZrkadloDBDQuery", zrkadlo_dbd_prepare, 
                  (void *)APR_OFFSETOF(zrkadlo_dir_conf, query), 
                  RSRC_CONF,
                  "the SQL query string to fetch the mirrors from the backend database"),
    AP_INIT_TAKE1("ZrkadloGeoIPFile", zrkadlo_cmd_geoip_filename, NULL, 
                  RSRC_CONF, 
                  "Path to GeoIP Data File"),

    AP_INIT_FLAG("ZrkadloMemcached", zrkadlo_cmd_memcached_on, NULL,
                  RSRC_CONF, 
                  "Set to On/Off to use memcached to give clients repeatedly the same mirror"),

    AP_INIT_TAKE1("ZrkadloMemcachedAddrPort", zrkadlo_cmd_memcached_addr, NULL,
                  RSRC_CONF, 
                  "ip or host adresse(s) and port (':' separated) of "
                  "memcache daemon(s) to be used, comma separated"),

    AP_INIT_TAKE1("ZrkadloMemcachedConnMin", zrkadlo_cmd_memcached_min, NULL,
                  RSRC_CONF, 
                  "Minimum number of connections that will be opened to the "
                  "memcache daemon(s). Default is 0."),

    AP_INIT_TAKE1("ZrkadloMemcachedConnSoftMax", zrkadlo_cmd_memcached_softmax, NULL,
                  RSRC_CONF, 
                  "Soft maximum number of connections that will be opened to the "
                  "memcache daemon(s). Default is 1."),

    AP_INIT_TAKE1("ZrkadloMemcachedConnHardMax", zrkadlo_cmd_memcached_hardmax, NULL,
                  RSRC_CONF, 
                  "Hard maximum number of connections that will be opened to the "
                  "memcache daemon(s). If unset, the value of ThreadLimit will be used."),

    AP_INIT_TAKE1("ZrkadloMemcachedLifeTime", zrkadlo_cmd_memcached_lifetime, NULL,
                  RSRC_CONF, 
                  "Lifetime (in seconds) associated with stored objects in "
                  "memcache daemon(s). Default is 600 s."),

    AP_INIT_TAKE2("ZrkadloTreatCountryAs", zrkadlo_cmd_treat_country_as, NULL, 
                  OR_FILEINFO,
                  "Set country to be treated as another. E.g.: nz au"),

    { NULL }
};

/* Tell Apache what phases of the transaction we handle */
static void zrkadlo_register_hooks(apr_pool_t *p)
{
    ap_hook_pre_config    (zrkadlo_pre_config,  NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config   (zrkadlo_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler       (zrkadlo_handler,     NULL, NULL, APR_HOOK_LAST);
    ap_hook_child_init    (zrkadlo_child_init,  NULL, NULL, APR_HOOK_MIDDLE );
}

module AP_MODULE_DECLARE_DATA zrkadlo_module =
{
    STANDARD20_MODULE_STUFF,
    create_zrkadlo_dir_config,    /* create per-directory config structures */
    merge_zrkadlo_dir_config,     /* merge per-directory config structures  */
    create_zrkadlo_server_config, /* create per-server config structures    */
    merge_zrkadlo_server_config,  /* merge per-server config structures     */
    zrkadlo_cmds,                 /* command handlers */
    zrkadlo_register_hooks        /* register hooks */
};


/* vim: set ts=4 sw=4 expandtab smarttab: */
