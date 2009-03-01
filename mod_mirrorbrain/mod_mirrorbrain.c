/*
 * Copyright (c) 2007,2008 Peter Poeml <poeml@suse.de> / Novell Inc.
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
 * mod_mirrorbrain, the heart of the MirrorBrain, does
 *  - redirect clients to mirror servers, based on sql database
 *  - generate real-time metalinks
 *  - generate text or HTML mirror lists
 * See http://mirrorbrain.org/ 
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
#include "util_md5.h"

#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_fnmatch.h"
#include "apr_dbd.h"
#include "mod_dbd.h"

#include <unistd.h> /* for getpid */
#include <arpa/inet.h>
#ifdef NO_MOD_GEOIP
#include <GeoIP.h>
#endif
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

#define MOD_MIRRORBRAIN_VER "2.6"
#define VERSION_COMPONENT "mod_mirrorbrain/"MOD_MIRRORBRAIN_VER

#ifdef NO_MOD_GEOIP
#define DEFAULT_GEOIPFILE "/usr/share/GeoIP/GeoIP.dat"
#endif
#ifdef WITH_MEMCACHE
#define DEFAULT_MEMCACHED_LIFETIME 600
#endif
#define DEFAULT_MIN_MIRROR_SIZE 4096

#define DEFAULT_QUERY "SELECT id, identifier, region, country, " \
                             "asn, prefix, score, baseurl " \
                             "region_only, country_only, " \
                             "as_only, prefix_only, " \
                             "other_countries, file_maxsize " \
                      "FROM server " \
                      "WHERE id::smallint = any(" \
                          "(SELECT mirrors " \
                           "FROM filearr " \
                           "WHERE path = %s)::smallint[]) " \
                      "AND enabled AND status_baseurl AND score > 0"


module AP_MODULE_DECLARE_DATA mirrorbrain_module;

#ifdef NO_MOD_GEOIP
/* could also be put into the server config */
static const char *geoipfilename = DEFAULT_GEOIPFILE;
static GeoIP *gip = NULL;     /* geoip object */
#endif

/** A structure that represents a mirror */
typedef struct mirror_entry mirror_entry_t;

/* a mirror */
struct mirror_entry {
    int id;
    const char *identifier;
    const char *region;      /* 2-letter-string */
#ifdef NO_MOD_GEOIP
    char *country_code;      /* 2-letter-string */
#else
    const char *country_code;      /* 2-letter-string */
#endif
    const char *as;          /* autonomous system number as string */
    const char *prefix;      /* network prefix xxx.xxx.xxx.xxx/yy */
    unsigned char region_only;
    unsigned char country_only;
    unsigned char as_only;
    unsigned char prefix_only;
    int score;
    const char *baseurl;
    int file_maxsize;
    char *other_countries;   /* comma-separated 2-letter strings */
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
    const char *mirror_base;
    apr_array_header_t *exclude_mime;
    apr_array_header_t *exclude_agents;
    apr_array_header_t *exclude_networks;
    apr_array_header_t *exclude_ips;
    ap_regex_t *exclude_filemask;
    ap_regex_t *metalink_torrentadd_mask;
} mb_dir_conf;

/* per-server configuration */
typedef struct
{
#ifdef WITH_MEMCACHE
    const char *instance;
    int memcached_on;
    int memcached_lifetime;
#endif
    const char *metalink_hashes_prefix;
    const char *metalink_publisher_name;
    const char *metalink_publisher_url;
    const char *mirrorlist_stylesheet;
    const char *query;
    const char *query_prep;
} mb_server_conf;


static ap_dbd_t *(*mb_dbd_acquire_fn)(request_rec*) = NULL;
static void (*mb_dbd_prepare_fn)(server_rec*, const char*, const char*) = NULL;

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
#ifdef NO_MOD_GEOIP
        GeoIP_delete(gip);
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, NULL, "[mod_mirrorbrain] cleaned up geoipfile");
#endif
        return APR_SUCCESS;
}

static void mb_child_init(apr_pool_t *p, server_rec *s)
{
#ifdef NO_MOD_GEOIP
    if (!gip) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, 
                "[mod_mirrorbrain] opening geoip file %s", geoipfilename);
        gip = GeoIP_open(geoipfilename, GEOIP_STANDARD);
    }
    if (!gip) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, 
                "[mod_mirrorbrain] Error while opening geoip file '%s'", geoipfilename);
    }
#endif
    apr_pool_cleanup_register(p, NULL, mb_cleanup, mb_cleanup);

    srand((unsigned int)getpid());
}

static int mb_post_config(apr_pool_t *pconf, apr_pool_t *plog, 
                          apr_pool_t *ptemp, server_rec *s)
{

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
        cfg->query_prep = apr_psprintf(pconf, "mirrorbrain_dbd_%d", ++label_num);
        mb_dbd_prepare_fn(sp, cfg->query, cfg->query_prep);
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
    new->handle_dirindex_locally    = UNSET;
    new->handle_headrequest_locally = UNSET;
    new->mirror_base = NULL;
    new->exclude_mime = apr_array_make(p, 0, sizeof (char *));
    new->exclude_agents = apr_array_make(p, 0, sizeof (char *));
    new->exclude_networks = apr_array_make(p, 4, sizeof (char *));
    new->exclude_ips = apr_array_make(p, 4, sizeof (char *));
    new->exclude_filemask = NULL;
    new->metalink_torrentadd_mask = NULL;

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
    cfgMergeInt(handle_dirindex_locally);
    cfgMergeInt(handle_headrequest_locally);
    cfgMergeString(mirror_base);
    mrg->exclude_mime = apr_array_append(p, base->exclude_mime, add->exclude_mime);
    mrg->exclude_agents = apr_array_append(p, base->exclude_agents, add->exclude_agents);
    mrg->exclude_networks = apr_array_append(p, base->exclude_networks, add->exclude_networks);
    mrg->exclude_ips = apr_array_append(p, base->exclude_ips, add->exclude_ips);
    mrg->exclude_filemask = (add->exclude_filemask == NULL) ? base->exclude_filemask : add->exclude_filemask;
    mrg->metalink_torrentadd_mask = (add->metalink_torrentadd_mask == NULL) ? base->metalink_torrentadd_mask : add->metalink_torrentadd_mask;

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
    new->metalink_hashes_prefix = NULL;
    new->metalink_publisher_name = NULL;
    new->metalink_publisher_url = NULL;
    new->mirrorlist_stylesheet = NULL;
    new->query = DEFAULT_QUERY;
    new->query_prep = NULL;

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
    cfgMergeString(metalink_hashes_prefix);
    cfgMergeString(metalink_publisher_name);
    cfgMergeString(metalink_publisher_url);
    cfgMergeString(mirrorlist_stylesheet);
    mrg->query = (add->query != (char *) DEFAULT_QUERY) ? add->query : base->query;
    cfgMergeString(query_prep);

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
    cfg->min_size = atoi(arg1);
    if (cfg->min_size < 0)
        return "MirrorBrainMinSize requires a non-negative integer.";
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

static const char *mb_cmd_handle_dirindex_locally(cmd_parms *cmd, 
                                                  void *config, int flag)
{
    mb_dir_conf *cfg = (mb_dir_conf *) config;
    cfg->handle_dirindex_locally = flag;
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
static const char *mb_cmd_instance(cmd_parms *cmd, 
                                   void *config, const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg = 
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->instance = arg1;
    return NULL;
}
#endif

static const char *mb_cmd_dbdquery(cmd_parms *cmd, void *config, 
                                   const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg = 
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->query = arg1;
    return NULL;
}

#ifdef NO_MOD_GEOIP
static const char *mb_cmd_geoip_filename(cmd_parms *cmd, void *config,
                                         const char *arg1)
{
    geoipfilename = apr_pstrdup(cmd->pool, arg1);

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,
                 "[mod_mirrorbrain] Setting GeoIPFilename: '%s'", 
                 geoipfilename);
    return NULL;
}
#else
static const char *mb_cmd_geoip_filename(cmd_parms *cmd, void *config,
                                         const char *arg1)
{
    return "mod_mirrorbrain: the GeoIPFilename directive is obsolete. Use mod_geoip.";
}
#endif

static const char *mb_cmd_metalink_hashes_prefix(cmd_parms *cmd, 
                                                 void *config, 
                                                 const char *arg1)
{
    server_rec *s = cmd->server;
    mb_server_conf *cfg = 
        ap_get_module_config(s->module_config, &mirrorbrain_module);

    cfg->metalink_hashes_prefix = arg1;
    return NULL;
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

static int cmp_mirror_rank(const void *v1, const void *v2)
{
    mirror_entry_t *m1 = *(mirror_entry_t **)v1;
    mirror_entry_t *m2 = *(mirror_entry_t **)v2;
    return m1->rank - m2->rank;
}

static int mb_handler(request_rec *r)
{
    mb_dir_conf *cfg = NULL;
    mb_server_conf *scfg = NULL;
    char *uri = NULL;
    char *filename = NULL;
    const char *user_agent = NULL;
    const char *clientip = NULL;
    const char *query_country = NULL;
    char *query_asn = NULL;
    char fakefile = 0, newmirror = 0;
    char mirrorlist = 0, mirrorlist_txt = 0;
    char metalink_forced = 0;                   /* metalink was explicitely requested */
    char metalink = 0;                          /* metalink was negotiated */ 
                                                /* for negotiated metalinks, the exceptions are observed. */
    const char* continent_code;
#ifdef NO_MOD_GEOIP
    short int country_id;
    char* country_code;
#else
    const char* country_code;
#endif
    const char* as;                             /* autonomous system */
    const char* prefix;                         /* network prefix */
    int i;
    int mirror_cnt;
    apr_size_t len;
    mirror_entry_t *new;
    mirror_entry_t *mirror;
    mirror_entry_t **mirrorp;
    mirror_entry_t *chosen = NULL;
    apr_status_t rv;
    apr_dbd_prepared_t *statement;
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
    int cached_id;
#endif
    const char* (*form_lookup)(request_rec*, const char*);

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

    /* is there a password? */
    if (r->ap_auth_type != NULL) {
        debugLog(r, cfg, "URI '%s' requires auth", r->unparsed_uri);
        return DECLINED;
    }

    /* do we redirect if the request is for directories? */
    /* XXX should one actually respect all strings which are configured
     * as DirectoryIndex ? */
    if (cfg->handle_dirindex_locally && ap_strcasestr(r->uri, "index.html")) {
        debugLog(r, cfg, "serving index.html locally "
                "(MirrorBrainHandleDirectoryIndexLocally)");
        return DECLINED;
    }


    debugLog(r, cfg, "URI: '%s'", r->unparsed_uri);
    debugLog(r, cfg, "filename: '%s'", r->filename);
    //debugLog(r, cfg, "server_hostname: '%s'", r->server->server_hostname);


    /* parse query arguments if present, */
    /* using mod_form's form_value() */
    form_lookup = APR_RETRIEVE_OPTIONAL_FN(form_value);
    if (form_lookup && r->args) {
        if (form_lookup(r, "fakefile")) fakefile = 1;
        clientip = form_lookup(r, "clientip");
        query_country = form_lookup(r, "country");
        query_asn = (char *) form_lookup(r, "as");
        if (form_lookup(r, "newmirror")) newmirror = 1;
        if (form_lookup(r, "mirrorlist")) mirrorlist =1;
        if (form_lookup(r, "metalink")) metalink_forced = 1;
    }
    
    if (!query_country 
       || strlen(query_country) != 2
       || !apr_isalnum(query_country[0])
       || !apr_isalnum(query_country[1])) {
        query_country = NULL;
    }

    if (query_asn) {
        for (i = 0; apr_isdigit(query_asn[i]); i++)
            ;
        query_asn[i] = '\0';
    }

    if (!mirrorlist_txt && !metalink_forced && !mirrorlist) {
        const char *accepts;
        accepts = apr_table_get(r->headers_in, "Accept");
        if (accepts != NULL) {
            if (ap_strstr_c(accepts, "mirrorlist-txt")) {
                mirrorlist_txt = 1;
            } else if (ap_strstr_c(accepts, "metalink+xml")) {
                metalink = 1;
            } 
        }
    }

    if (clientip) {
#ifdef NO_MOD_GEOIP
        debugLog(r, cfg, "FAKE clientip address: '%s'", clientip);

        /* ensure that the string represents a valid IP address
         *
         * if clientip contains a colon, we should principally do the lookup
         * for AF_INET6 instead, but GeoIP doesn't support IPv6 anyway */
        struct in_addr addr;
        if (inet_pton(AF_INET, clientip, &addr) != 1) {
            debugLog(r, cfg, "FAKE clientip address not valid: '%s'", clientip);
            return HTTP_BAD_REQUEST;
        }
#else
        debugLog(r, cfg, "obsolete clientip address parameter: '%s'", clientip);
        ap_set_content_type(r, "text/html; charset=ISO-8859-1");
        ap_rputs(DOCTYPE_XHTML_1_0T
                 "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                 "<head>\n"
                 "  <title>Sorry</title>\n", r);
        ap_rputs("</head>\n<body>\n\n", r);
        ap_rprintf(r, "<p>\n<kbd>clientip</kbd> is no longer supported as query parameter. "
                      "Please use <kbd>country=xy</kbd> instead, where <kbd>xy</kbd> is a two-letter "
                      "<a href=\"http://en.wikipedia.org/wiki/ISO_3166-1\">"
                      "ISO 3166 country code</a>.\n</p>\n");
        ap_rputs("\n\n</body>\n</html>\n", r);
        return OK;
#endif
    } else 
        clientip = apr_pstrdup(r->pool, r->connection->remote_ip);

    /* These checks apply only if the server response is not faked for testing */
    if (fakefile) {
        debugLog(r, cfg, "FAKE File -- not looking at real files");

    } else {
        if (r->finfo.filetype == APR_DIR) {
        /* if (ap_is_directory(r->pool, r->filename)) { */
            debugLog(r, cfg, "'%s' is a directory", r->filename);
            return DECLINED;
        }   

        /* check if the file exists. Strip off optional .metalink extension. */
        if (r->finfo.filetype != APR_REG) {
            debugLog(r, cfg, "File does not exist acc. to r->finfo");
            char *ext;
            if ((ext = ap_strrchr(r->filename, '.')) == NULL) {
                return DECLINED;
            } else {
                if (strcmp(ext, ".metalink") == 0) {
                    debugLog(r, cfg, "Metalink requested by .metalink extension");
                    metalink_forced = 1;
                    /* we modify r->filename here. */
                    ext[0] = '\0';

                    /* strip the extension from r->uri as well */
                    if ((ext = ap_strrchr(r->uri, '.')) != NULL) {
                        if (strcmp(ext, ".metalink") == 0) {
                            ext[0] = '\0';
                        }
                    } 

                    /* fill in finfo */
                    if ( apr_stat(&r->finfo, r->filename, APR_FINFO_SIZE, r->pool)
                            != APR_SUCCESS ) {
                        return HTTP_NOT_FOUND;
                    }
                } else {
                    return DECLINED;
                }
            } 
        }

        /* is the requested file too small? DECLINED */
        if (!mirrorlist && !metalink_forced && (r->finfo.size < cfg->min_size)) {
            debugLog(r, cfg, "File '%s' too small (%d bytes, less than %d)", 
                    r->filename, (int) r->finfo.size, (int) cfg->min_size);
            return DECLINED;
        }
    }

    /* is this file excluded from mirroring? */
    if (!mirrorlist 
       && !metalink_forced
       && cfg->exclude_filemask 
       && !ap_regexec(cfg->exclude_filemask, r->uri, 0, NULL, 0) ) {
        debugLog(r, cfg, "File '%s' is excluded by MirrorBrainExcludeFileMask", r->uri);
        return DECLINED;
    }

    /* is the request originating from an ip address excluded from redirecting? */
    if (!mirrorlist && !metalink_forced && cfg->exclude_ips->nelts) {

        for (i = 0; i < cfg->exclude_ips->nelts; i++) {

            char *ip = ((char **) cfg->exclude_ips->elts)[i];

            if (strcmp(ip, clientip) == 0) {
                debugLog(r, cfg,
                    "URI request '%s' from ip '%s' is excluded from"
                    " redirecting because it matches IP '%s'",
                    r->unparsed_uri, clientip, ip);
                return DECLINED;
            }
        }
    }


    /* is the request originating from a network excluded from redirecting? */
    if (!mirrorlist && !metalink_forced && cfg->exclude_networks->nelts) {

        for (i = 0; i < cfg->exclude_networks->nelts; i++) {

            char *network = ((char **) cfg->exclude_networks->elts)[i];

            if (strncmp(network, clientip, strlen(network)) == 0) {
                debugLog(r, cfg,
                    "URI request '%s' from ip '%s' is excluded from"
                    " redirecting because it matches network '%s'",
                    r->unparsed_uri, clientip, network);
                return DECLINED;
            }
        }
    }


    /* is the file in the list of mimetypes to never mirror? */
    if (!mirrorlist && !metalink_forced && (r->content_type) && (cfg->exclude_mime->nelts)) {

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
    if (!mirrorlist && !metalink_forced && (user_agent) && (cfg->exclude_agents->nelts)) {

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
        return DECLINED;
    }


#ifdef NO_MOD_GEOIP
    /* GeoIP lookup 
     * if mod_geoip was loaded, it would suffice to retrieve GEOIP_COUNTRY_CODE
     * as supplied by it via the notes table, but since we also need the
     * continent we need to use libgeoip ourselves. Thus, we can do our own
     * lookup just as well. 
     * Update (2008/2009): mod_geoip supports continent code passing now; 
     * thus we made the compilation with GeoIP lookups optional. */
    country_id = GeoIP_id_by_addr(gip, clientip);
    country_code = apr_pstrdup(r->pool, GeoIP_country_code[country_id]);
    continent_code = GeoIP_country_continent[country_id];
#else
    country_code = apr_table_get(r->subprocess_env, "GEOIP_COUNTRY_CODE");
    continent_code = apr_table_get(r->subprocess_env, "GEOIP_CONTINENT_CODE");

    if (!country_code) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] could not resolve country");
        country_code = "--";
    }
    if (!continent_code) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] could not resolve continent");
        continent_code = "--";
    }
#endif

    if (query_country) {
        country_code = query_country;
    }

    debugLog(r, cfg, "Country '%s', Continent '%s'", country_code, 
            continent_code);

    /* save details for logging via a CustomLog */
    apr_table_setn(r->subprocess_env, "MIRRORBRAIN_FILESIZE",       /* FIXME: obsolete the long names soon */
            apr_off_t_toa(r->pool, r->finfo.size));
    apr_table_setn(r->subprocess_env, "MB_FILESIZE", 
            apr_off_t_toa(r->pool, r->finfo.size));
    apr_table_set(r->subprocess_env, "MIRRORBRAIN_COUNTRY_CODE", country_code);
    apr_table_set(r->subprocess_env, "MB_COUNTRY_CODE", country_code);
    apr_table_set(r->subprocess_env, "MIRRORBRAIN_CONTINENT_CODE", continent_code);
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
    debugLog(r, cfg, "AS '%s', Prefix '%s'", as, prefix);


    /* ask the database and pick the matching server according to region */

    if (scfg->query_prep == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] No database query prepared!");
        return DECLINED;
    }

    ap_dbd_t *dbd = mb_dbd_acquire_fn(r);
    if (dbd == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_mirrorbrain] Error acquiring database connection");
        return DECLINED; /* fail gracefully */
    }
    debugLog(r, cfg, "Successfully acquired database connection.");

    statement = apr_hash_get(dbd->prepared, scfg->query_prep, APR_HASH_KEY_STRING);
    if (statement == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] Could not get prepared statement!");
        return DECLINED;
    }

    /* strip the leading directory
     * no need to escape it for the SQL query because we use a prepared 
     * statement with bound parameter */

    char *ptr = canonicalize_file_name(r->filename);
    if (ptr == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_mirrorbrain] Error canonicalizing filename '%s'", r->filename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    /* XXX we should forbid symlinks in mirror_base */
    filename = apr_pstrdup(r->pool, ptr + strlen(cfg->mirror_base));
    free(ptr);
    debugLog(r, cfg, "SQL lookup for (canonicalized) '%s'", filename);

    if (apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, statement, 
                1, /* we don't need random access actually, but 
                      without it the mysql driver doesn't return results
                      once apr_dbd_num_tuples() has been called; 
                      apr_dbd_get_row() will only return -1 after that. */
                filename, NULL) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_mirrorbrain] Error looking up %s in database", filename);
        return DECLINED;
    }

    mirror_cnt = apr_dbd_num_tuples(dbd->driver, res);
    if (mirror_cnt > 0) {
        debugLog(r, cfg, "Found %d mirror%s", mirror_cnt,
                (mirror_cnt == 1) ? "" : "s");
    }
    else {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, 
                "[mod_mirrorbrain] no mirrors found for %s", filename);
        /* can be used for a CustomLog */
        apr_table_setn(r->subprocess_env, "MIRRORBRAIN_NOMIRROR", "1");
        apr_table_setn(r->subprocess_env, "MB_NOMIRROR", "1");

        if (mirrorlist) {
            debugLog(r, cfg, "empty mirrorlist");
            ap_set_content_type(r, "text/html; charset=ISO-8859-1");
            ap_rputs(DOCTYPE_XHTML_1_0T
                     "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                     "<head>\n"
                     "  <title>Mirror List</title>\n", r);
            if (scfg->mirrorlist_stylesheet) {
                ap_rprintf(r, "  <link type=\"text/css\" rel=\"stylesheet\" href=\"%s\" />\n",
                           scfg->mirrorlist_stylesheet);
            }
            ap_rputs("</head>\n\n" "<body>\n", r);

            ap_rprintf(r, "  <h2>Mirrors for <a href=\"http://%s%s\">http://%s%s</a></h2>\n" 
                       "  <br/>\n", 
                       r->hostname, r->uri, r->hostname, r->uri);
            /* ap_rprintf(r, "Client IP address: %s<br/>\n", clientip); */

            ap_rprintf(r, "I am very sorry, but no mirror was found. <br/>\n");
            ap_rprintf(r, "Feel free to download from the above URL.\n");

            ap_rputs("</body></html>\n", r);
            return OK;
        }

        /* deliver the file ourselves */
        return DECLINED;
    }


    /* allocate space for the expected results */
    mirrors              = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t));
    mirrors_same_prefix  = apr_array_make(r->pool, 1,          sizeof (mirror_entry_t *));
    mirrors_same_as      = apr_array_make(r->pool, 1,          sizeof (mirror_entry_t *));
    mirrors_same_country = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));
    mirrors_fallback_country = apr_array_make(r->pool, 5,      sizeof (mirror_entry_t *));
    mirrors_same_region  = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));
    mirrors_elsewhere    = apr_array_make(r->pool, mirror_cnt, sizeof (mirror_entry_t *));


    /* need to remind myself... how to use the pointer arrays:
     *                                                          
     * 1) multi line version, allowing for easier access of last added element
     * void **new_same = (void **)apr_array_push(mirrors_same_country);
     * *new_same = new;
     *
     * ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "[mod_mirrorbrain] new_same->identifier: %s",
     *        ((mirror_entry_t *)*new_same)->identifier);
     *
     * 2) one line version
     * *(void **)apr_array_push(mirrors_same_country) = new;                        */


    /* store the results which the database yielded, taking into account which
     * mirrors are in the same country, same reagion, or elsewhere */
    i = 1;
    while (i <= mirror_cnt) { 
        char unusable = 0; /* if crucial data is missing... */
        const char *val = NULL;
        short col = 0; /* incremented for the column we are reading out */

        rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row, i);
        if (rv != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "[mod_mirrorbrain] Error looking up %s in database", filename);
            return DECLINED;
        }

        new = apr_array_push(mirrors);
        new->id = 0;
        new->identifier = NULL;
        new->region = NULL;
        new->country_code = NULL;
        new->other_countries = NULL;
        new->as = NULL;
        new->prefix = NULL;
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

        /* autonomous system number */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for AS number");
        else
            new->as = apr_pstrdup(r->pool, val);

        /* network prefix */
        if ((val = apr_dbd_get_entry(dbd->driver, row, col++)) == NULL)
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_mirrorbrain] apr_dbd_get_entry found NULL for network prefix");
        else
            new->prefix = apr_pstrdup(r->pool, val);

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
            new->file_maxsize = atoi(val);



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
        /* debugLog(r, cfg, "Found mirror #%d '%s'", new->id, new->identifier); */
        

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
        else if (strcmp(new->prefix, prefix) == 0) {
            *(void **)apr_array_push(mirrors_same_prefix) = new;
        }

        /* same AS? */
        else if ((strcmp(new->as, as) == 0) 
                   && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_same_as) = new;
        }

        /* same country? */
        else if ((strcasecmp(new->country_code, country_code) == 0) 
                   && !new->as_only
                   && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_same_country) = new;
        }

        /* is the mirror's country_code a wildcard, indicating that the mirror should be
         * considered for every country? */
        else if (strcmp(new->country_code, "**") == 0) {
            *(void **)apr_array_push(mirrors_same_country) = new; 
            /* if so, forget memcache association, so the mirror is not ruled out */
            chosen = NULL; 
            /* set its country and region to that of the client */
            new->country_code = country_code;
            new->region = continent_code;
        }

        /* mirror from elsewhere, but suitable for this country? */
        else if (new->other_countries && ap_strcasestr(new->other_countries, country_code)) {
            *(void **)apr_array_push(mirrors_fallback_country) = new;
        }

        /* same region? */
        /* to be actually considered for this group, the mirror must be willing 
         * to take redirects from foreign country */
        else if ((strcasecmp(new->region, continent_code) == 0) 
                    && !new->country_only
                    && !new->as_only
                    && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_same_region) = new;
        }

        /* to be considered as "worldwide" mirror, it must be willing 
         * to take redirects from foreign regions.
         * (N.B. region_only implies country_only)  */
        else if (!new->region_only 
                    && !new->country_only
                    && !new->as_only
                    && !new->prefix_only) {
            *(void **)apr_array_push(mirrors_elsewhere) = new;
        }

        i++;
    }

#if 0
    /* dump the mirror array */
    mirror_entry_t *elts;
    elts = (mirror_entry_t *) mirrors->elts;
    for (i = 0; i < mirrors->nelts; i++) {
        debugLog(r, cfg, "mirror  %3d  %-30ss", elts[i].id, elts[i].identifier);
    }
#endif


    /* 2nd pass */

    /* if we didn't found a mirror in the country: are other mirrors set to
     * handle this country? */
    if (apr_is_empty_array(mirrors_same_country) 
            && !apr_is_empty_array(mirrors_fallback_country)) {
        mirrors_same_country = mirrors_fallback_country;
        debugLog(r, cfg, "no mirror in country, but found fallback_country mirrors");
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
    if (mirrorlist_txt || metalink || metalink_forced || mirrorlist) {
        qsort(mirrors_same_prefix->elts, mirrors_same_prefix->nelts, 
              mirrors_same_prefix->elt_size, cmp_mirror_rank);
        qsort(mirrors_same_as->elts, mirrors_same_as->nelts, 
              mirrors_same_as->elt_size, cmp_mirror_rank);
        qsort(mirrors_same_country->elts, mirrors_same_country->nelts, 
              mirrors_same_country->elt_size, cmp_mirror_rank);
        qsort(mirrors_same_region->elts, mirrors_same_region->nelts, 
              mirrors_same_region->elt_size, cmp_mirror_rank);
        qsort(mirrors_elsewhere->elts, mirrors_elsewhere->nelts, 
              mirrors_elsewhere->elt_size, cmp_mirror_rank);
    }

    if (cfg->debug) {

        /* list the sorted result */
        /* Brad's mod_edir hdir.c helped me here.. thanks to his kind help */
        mirror = NULL;

        /* list the same-prefix mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
        for (i = 0; i < mirrors_same_prefix->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same prefix: %-30s (score %4d) (rank %10d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        /* list the same-AS mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
        for (i = 0; i < mirrors_same_as->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same AS: %-30s (score %4d) (rank %10d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        /* list the same-country mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same country: %-30s (score %4d) (rank %10d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        /* list the same-region mirrors */
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "same region:  %-30s (score %4d) (rank %10d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        /* list all other mirrors */
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            debugLog(r, cfg, "elsewhere:    %-30s (score %4d) (rank %10d)", 
                    mirror->identifier, mirror->score, mirror->rank);
        }

        debugLog(r, cfg, "Found %d mirror%s: %d prefix, %d AS, %d country, "
                "%d region, %d elsewhere", 
                mirror_cnt, (mirror_cnt == 1) ? "" : "s",
                mirrors_same_prefix->nelts,
                mirrors_same_as->nelts,
                mirrors_same_country->nelts,
                mirrors_same_region->nelts,
                mirrors_elsewhere->nelts);
    }

    /* return a mirrorlist_txt instead of doing a redirect? */
    if (mirrorlist_txt) {
        debugLog(r, cfg, "Sending mirrorlist-txt");

        /* tell caches that this is negotiated response and that not every client will take it */
        apr_table_mergen(r->headers_out, "Vary", "accept");

        ap_set_content_type(r, "application/mirrorlist-txt; charset=UTF-8");

        ap_rputs("# mirrorlist-txt version=1.0\n", r);
        ap_rputs("# url baseurl_len mirrorid region:country power\n", r);

        /* for failover testing, insert broken mirrors at the top */
        const char *ua;
        ua = apr_table_get(r->headers_in, "User-Agent");
        if (ua != NULL) {
            if (ap_strstr_c(ua, "getPrimaryFailover-agent/0.1")) {
                /* hostname does not resolve */
                ap_rprintf(r, "http://doesnotexist/%s 20 10001 EU:DE 100\n", filename);
                /* 404 Not found */
                ap_rputs("http://www.poeml.de/nonexisting_file_for_libzypp 20 10002 EU:DE 100\n", r);
                /* Connection refused */
                ap_rputs("http://www.poeml.de:83/nonexisting_file_for_libzypp 23 10003 EU:DE 100\n", r);
                /* Totally off reply ("server busy") */
                ap_rputs("http://ftp.opensuse.org:21/foobar 27 10004 EU:DE 100\n", r);
                /* 403 Forbidden */
                ap_rputs("http://download.opensuse.org/server-status 42 10005 EU:DE 100\n", r);
                /* Times out */
                /* I'll leave this one commented for now, so the timeouts don't hinder initial 
                 * testing and progress too much. */
                /* ap_rputs("http://widehat.opensuse.org:22/foobar 37 10006 EU:DE 100\n", r); */
            } 
        }


        mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
        for (i = 0; i < mirrors_same_prefix->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "%s%s %d %d %s:%s %d\n", 
                       mirror->baseurl, filename,
                       (int) strlen(mirror->baseurl),
                       mirror->id,
                       mirror->region,
                       mirror->country_code,
                       mirror->score);
        }

        mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
        for (i = 0; i < mirrors_same_as->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "%s%s %d %d %s:%s %d\n", 
                       mirror->baseurl, filename,
                       (int) strlen(mirror->baseurl),
                       mirror->id,
                       mirror->region,
                       mirror->country_code,
                       mirror->score);
        }

        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "%s%s %d %d %s:%s %d\n", 
                       mirror->baseurl, filename,
                       (int) strlen(mirror->baseurl),
                       mirror->id,
                       mirror->region,
                       mirror->country_code,
                       mirror->score);
        }

        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "%s%s %d %d %s:%s %d\n", 
                       mirror->baseurl, filename,
                       (int) strlen(mirror->baseurl),
                       mirror->id,
                       mirror->region,
                       mirror->country_code,
                       mirror->score);
        }

        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "%s%s %d %d %s:%s %d\n", 
                       mirror->baseurl, filename,
                       (int) strlen(mirror->baseurl),
                       mirror->id,
                       mirror->region,
                       mirror->country_code,
                       mirror->score);
        }

        return OK;
    } /* end mirrorlist-txt */

    /* return a metalink instead of doing a redirect? */
    if (metalink || metalink_forced) {
        debugLog(r, cfg, "Sending metalink");

        /* tell caches that this is negotiated response and that not every client will take it */
        apr_table_mergen(r->headers_out, "Vary", "accept");

        /* drop the path leading up to the file name, because metalink clients
         * will otherwise place the downloaded file into a directory hierarchy */
        const char *basename;
        if ((basename = ap_strrchr_c(filename, '/')) == NULL) {
            basename = filename;
        } else {
            ++basename;
        }

        /* add rfc2183 header for filename, with .metalink appended 
         * because some clients trigger on that extension */
        apr_table_setn(r->headers_out,
                       "Content-Disposition",
                       apr_pstrcat(r->pool,
                                   "attachment; filename=\"",
                                   basename, ".metalink\"", NULL));

        /* the current time in rfc 822 format */
        char *time_str = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
        apr_rfc822_date(time_str, apr_time_now());

        ap_set_content_type(r, "application/metalink+xml; charset=UTF-8");
        ap_rputs(     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<metalink version=\"3.0\" xmlns=\"http://www.metalinker.org/\"\n", r);


        /* The origin URL is meant to specify the location for revalidation of this metalink
         *
         * Unfortunately, r->parsed_uri.scheme and r->parsed_uri.hostname don't
         * seem to be filled out (why?). But we can put it together from
         * r->hostname and r->uri. Actually we should add the port.
         *
         * We could use r->server->server_hostname instead, which would be the configured server name.
         *
         * We use r->uri, not r->unparsed_uri, so we don't need to escape query strings for xml.
         */
        ap_rprintf(r, "  origin=\"http://%s%s.metalink\"\n", r->hostname, r->uri);
        ap_rputs(     "  generator=\"mod_mirrorbrain Download Redirector - http://mirrorbrain.org/\"\n", r);
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
        ap_rprintf(r, "    <file name=\"%s\">\n", basename);
        ap_rprintf(r, "      <size>%s</size>\n\n", apr_off_t_toa(r->pool, r->finfo.size));


        /* inject hashes, if they are prepared on-disk */
        apr_finfo_t sb;
        const char *hashfilename;
        hashfilename = apr_pstrcat(r->pool, 
                                   scfg->metalink_hashes_prefix ? scfg->metalink_hashes_prefix : "", 
                                   r->filename, 
                                   ".metalink-hashes", 
                                   NULL);
        if (apr_stat(&sb, hashfilename, APR_FINFO_MIN, r->pool) == APR_SUCCESS
            && (sb.filetype == APR_REG) && (sb.mtime >= r->finfo.mtime)) {
            debugLog(r, cfg, "Found up-to-date hashfile '%s', injecting", hashfilename);

            apr_file_t *fh;
            rv = apr_file_open(&fh, hashfilename, APR_READ, APR_OS_DEFAULT, r->pool);
            if (rv == APR_SUCCESS) {
                ap_send_fd(fh, r, 0, sb.size, &len);

                apr_file_close(fh);
            }
        }

        ap_rputs(     "      <resources>\n\n", r);

        if (cfg->metalink_torrentadd_mask
            && !ap_regexec(cfg->metalink_torrentadd_mask, r->filename, 0, NULL, 0)
            && apr_stat(&sb, apr_pstrcat(r->pool, r->filename, ".torrent", NULL), APR_FINFO_MIN, r->pool) == APR_SUCCESS) {
            debugLog(r, cfg, "found torrent file");
            ap_rprintf(r, "      <url type=\"bittorrent\" preference=\"%d\">http://%s%s.torrent</url>\n\n", 
                       100,
                       r->hostname, 
                       r->uri);
        }

        ap_rprintf(r, "      <!-- Found %d mirror%s: %d in the same network prefix, %d in the same "
                   "autonomous system,\n           %d handling this country, %d in the same "
                   "region, %d elsewhere -->\n", 
                   mirror_cnt,
                   (mirror_cnt == 1) ? "" : "s",
                   mirrors_same_prefix->nelts,
                   mirrors_same_as->nelts,
                   mirrors_same_country->nelts,
                   mirrors_same_region->nelts,
                   mirrors_elsewhere->nelts);

        /* the highest metalink preference according to the spec is 100, and
         * we'll decrement it for each mirror by one, until zero is reached */
        int pref = 101;

        ap_rprintf(r, "\n      <!-- Mirrors in the same network (%s): -->\n",
                   (strcmp(prefix, "--") == 0) ? "unknown" : prefix);
        mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
        for (i = 0; i < mirrors_same_prefix->nelts; i++) {
            if (pref) pref--;
            mirror = mirrorp[i];
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        ap_rprintf(r, "\n      <!-- Mirrors in the same AS (%s): -->\n",
                   (strcmp(as, "--") == 0) ? "unknown" : as);
        mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
        for (i = 0; i < mirrors_same_as->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only)
                continue;
            if (pref) pref--;
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        /* failed geoip lookups yield country='--', which leads to invalid XML */
        ap_rprintf(r, "\n      <!-- Mirrors which handle this country (%s): -->\n", 
                   (strcmp(country_code, "--") == 0) ? "unknown" : country_code);
        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only || mirror->as_only)
                continue;
            if (pref) pref--;
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        ap_rprintf(r, "\n      <!-- Mirrors in the same continent (%s): -->\n", 
                   (strcmp(continent_code, "--") == 0) ? "unknown" : continent_code);
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only || mirror->as_only || mirror->country_only)
                continue;
            if (pref) pref--;
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        ap_rputs("\n      <!-- Mirrors in the rest of the world: -->\n", r);
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            if (mirror->prefix_only || mirror->as_only 
                    || mirror->country_only || mirror->region_only) {
                continue;
            }
            if (pref) pref--;
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        ap_rputs(     "      </resources>\n"
                      "    </file>\n"
                      "  </files>\n"
                      "</metalink>\n", r);
        return OK;
    } /* end metafile */

    /* send an HTML list instead of doing a redirect? */
    if (mirrorlist) {
        debugLog(r, cfg, "Sending mirrorlist");

        ap_set_content_type(r, "text/html; charset=ISO-8859-1");
        ap_rputs(DOCTYPE_XHTML_1_0T
                 "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                 "<head>\n"
                 "  <title>Mirror List</title>\n", r);
        if (scfg->mirrorlist_stylesheet) {
            ap_rprintf(r, "  <link type=\"text/css\" rel=\"stylesheet\" href=\"%s\" />\n",
                       scfg->mirrorlist_stylesheet);
        }
        ap_rputs("</head>\n\n" "<body>\n", r);

        ap_rprintf(r, "  <h2>Mirrors for <a href=\"http://%s%s\">http://%s%s</a></h2>\n" 
                   "  <br/>\n", 
                   r->hostname, r->uri, r->hostname, r->uri);

        ap_rputs("  <address>Powered by <a href=\"http://mirrorbrain.org/\">MirrorBrain</a></address>\n", r);

        ap_rputs("  <br/>\n" 
                 "  <blockquote>Hint: For larger downloads, a <a href=\"http://metalinker.org\">Metalink</a> client "
                 "  is best -- easier, more reliable, self healing downloads.\n" 
                 "  <br/>\n", r);
        ap_rprintf(r, "  The metalink for this file is: "
                   "<a href=\"http://%s%s.metalink\">http://%s%s.metalink</a></blockquote>"
                   "  <br/>\n", 
                r->hostname, r->uri, r->hostname, r->uri);


        ap_rprintf(r, "  <p>List of best mirrors for IP address %s, located in country %s, %s (AS%s).</p>\n", 
                   clientip, country_code, prefix, as);


        /* prefix */
        if (mirrors_same_prefix->nelts) {
            ap_rprintf(r, "\n  <h3>Found %d mirror%s within the same network prefix :-) (%s):</h3>\n", 
                       mirrors_same_prefix->nelts, 
                       (mirrors_same_prefix->nelts == 1) ? "" : "s",
                       prefix);
            ap_rputs("  <ul>\n", r);
            mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
            for (i = 0; i < mirrors_same_prefix->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "    <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n", 
                        mirror->baseurl, filename, 
                        mirror->baseurl, filename, 
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("  </ul>\n", r);
        }

        /* AS */
        if (mirrors_same_as->nelts) {
            ap_rprintf(r, "\n  <h3>Found %d mirror%s within the same autonomous system :-) (AS%s):</h3>\n", 
                       mirrors_same_as->nelts, 
                       (mirrors_same_as->nelts == 1) ? "" : "s",
                       as);
            ap_rputs("  <ul>\n", r);
            mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
            for (i = 0; i < mirrors_same_as->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "    <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n", 
                        mirror->baseurl, filename, 
                        mirror->baseurl, filename, 
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("  </ul>\n", r);
        }

        /* country */
        if (mirrors_same_country->nelts) {
            ap_rprintf(r, "\n  <h3>Found %d mirror%s which handle this country (%s):</h3>\n", 
                       mirrors_same_country->nelts, 
                       (mirrors_same_country->nelts == 1) ? "" : "s",
                       country_code);
            ap_rputs("  <ul>\n", r);
            mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
            for (i = 0; i < mirrors_same_country->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "    <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n", 
                        mirror->baseurl, filename, 
                        mirror->baseurl, filename, 
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("  </ul>\n", r);
        }

        /* region */
        if (mirrors_same_region->nelts) {
            ap_rprintf(r, "\n  <h3>Found %d mirror%s in other countries, but same continent (%s):</h3>\n", 
                       mirrors_same_region->nelts,
                       (mirrors_same_region->nelts == 1) ? "" : "s",
                       continent_code);
            ap_rputs("  <ul>\n", r);
            mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
            for (i = 0; i < mirrors_same_region->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "    <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n", 
                        mirror->baseurl, filename, 
                        mirror->baseurl, filename, 
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("  </ul>\n", r);
        }

        /* elsewhere */
        if (mirrors_elsewhere->nelts) {
            ap_rprintf(r, "\n   <h3>Found %d mirror%s in other parts of the world:</h3>\n", 
                       mirrors_elsewhere->nelts,
                       (mirrors_elsewhere->nelts == 1) ? "" : "s");
            ap_rputs("  <ul>\n", r);
            mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
            for (i = 0; i < mirrors_elsewhere->nelts; i++) {
                mirror = mirrorp[i];
                ap_rprintf(r, "    <li><a href=\"%s%s\">%s%s</a> (%s, prio %d)</li>\n", 
                        mirror->baseurl, filename, 
                        mirror->baseurl, filename, 
                        mirror->country_code,
                        mirror->score);
            }
            ap_rputs("  </ul>\n", r);
        }

        ap_rputs("</body>\n", r);
        ap_rputs("</html>\n", r);
        return OK;
    } /* end mirrorlist */


    const char *found_in;
    /* choose from country, then from region, then from elsewhere */
    if (!chosen) {
        if (mirrors_same_prefix->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_same_prefix->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_same_prefix)];
            found_in = "prefix";
        } else if (mirrors_same_as->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_same_as->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_same_as)];
            found_in = "AS";
        } else if (mirrors_same_country->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_same_country)];
            if (strcasecmp(chosen->country_code, country_code) == 0) {
                found_in = "country";
            } else {
                found_in = "other_country";
            }
        } else if (mirrors_same_region->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_same_region)];
            found_in = "region";
        } else if (mirrors_elsewhere->nelts) {
            mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
            chosen = mirrorp[find_lowest_rank(mirrors_elsewhere)];
            found_in = "other";
        }
    }

    if (!chosen) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
            "[mod_mirrorbrain] could not chose a server. Shouldn't have happened.");
        return DECLINED;
    }

    debugLog(r, cfg, "Chose server %s", chosen->identifier);

    /* Send it away: set a "Location:" header and 302 redirect. */
    uri = apr_pstrcat(r->pool, chosen->baseurl, filename, NULL);
    debugLog(r, cfg, "Redirect to '%s'", uri);

    /* for _conditional_ logging, leave some mark */
    apr_table_setn(r->subprocess_env, "MIRRORBRAIN_REDIRECTED", "1");
    apr_table_setn(r->subprocess_env, "MB_REDIRECTED", "1");
    apr_table_setn(r->subprocess_env, "MB_REALM", apr_pstrdup(r->pool, found_in));

    apr_table_setn(r->err_headers_out, "X-MirrorBrain-Mirror", chosen->identifier);
    apr_table_setn(r->err_headers_out, "X-MirrorBrain-Realm", found_in);
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
                  "Minimum size, in bytes, that a file must be to be mirrored"),
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

    AP_INIT_FLAG("MirrorBrainHandleDirectoryIndexLocally", mb_cmd_handle_dirindex_locally, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to handle directory listings locally (don't redirect)"),
    AP_INIT_FLAG("MirrorBrainHandleHEADRequestLocally", mb_cmd_handle_headrequest_locally, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to handle HEAD requests locally (don't redirect)"),

    AP_INIT_TAKE1("MirrorBrainMetalinkTorrentAddMask", mb_cmd_metalink_torrentadd_mask, NULL, 
                  ACCESS_CONF,
                  "Regexp which determines for which files to look for correspondant "
                  ".torrent files, and add them into generated metalinks"),

    /* to be used only in server context */
    AP_INIT_TAKE1("MirrorBrainDBDQuery", mb_cmd_dbdquery, NULL,
                  RSRC_CONF,
                  "the SQL query string to fetch the mirrors from the backend database"),

#ifdef NO_MOD_GEOIP
    AP_INIT_TAKE1("MirrorBrainGeoIPFile", mb_cmd_geoip_filename, NULL, 
                  RSRC_CONF, 
                  "Path to GeoIP Data File"),
#else
    AP_INIT_TAKE1("MirrorBrainGeoIPFile", mb_cmd_geoip_filename, NULL, 
                  RSRC_CONF, 
                  "Obsolete directive - use mod_geoip, please."),
#endif

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
                  "Prefix this path when looking for prepared hashes to inject into metalinks"),

    AP_INIT_TAKE2("MirrorBrainMetalinkPublisher", mb_cmd_metalink_publisher, NULL, 
                  RSRC_CONF, 
                  "Name and URL for the metalinks publisher elements"),

    AP_INIT_TAKE1("MirrorBrainMirrorlistStyleSheet", mb_cmd_mirrorlist_stylesheet, NULL, 
                  RSRC_CONF, 
                  "Sets a CSS stylesheet to add to mirror lists"),

    { NULL }
};

/* Tell Apache what phases of the transaction we handle */
static void mb_register_hooks(apr_pool_t *p)
{
#ifdef WITH_MEMCACHE
    ap_hook_pre_config    (mb_pre_config,  NULL, NULL, APR_HOOK_MIDDLE);
#endif
    ap_hook_post_config   (mb_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler       (mb_handler,     NULL, NULL, APR_HOOK_LAST);
    ap_hook_child_init    (mb_child_init,  NULL, NULL, APR_HOOK_MIDDLE );
}

module AP_MODULE_DECLARE_DATA mirrorbrain_module =
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
