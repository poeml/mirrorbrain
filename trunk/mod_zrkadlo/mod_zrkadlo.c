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
#include "util_md5.h"

#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_fnmatch.h"
#include "apr_dbd.h"
#include "mod_dbd.h"

#include <unistd.h> /* for getpid */
#include <arpa/inet.h>
#include <GeoIP.h>
#include "mod_memcache.h"
#include "apr_memcache.h"
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

#define MOD_ZRKADLO_VER "1.8"
#define VERSION_COMPONENT "mod_zrkadlo/"MOD_ZRKADLO_VER

#define DEFAULT_GEOIPFILE "/usr/share/GeoIP/GeoIP.dat"
#define DEFAULT_MEMCACHED_LIFETIME 600
#define DEFAULT_MIN_MIRROR_SIZE 4096

module AP_MODULE_DECLARE_DATA zrkadlo_module;

/* could also be put into the server config */
static const char *geoipfilename = DEFAULT_GEOIPFILE;
static GeoIP *gip = NULL;     /* geoip object */

/** A structure that represents a mirror */
typedef struct mirror_entry mirror_entry_t;

/* a mirror */
struct mirror_entry {
    int id;
    const char *identifier;
    char *country_code;      /* 2-letter-string */
    const char *region;      /* 2-letter-string */
    short country_only;
    short region_only;
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
    int metalink_add_torrent;
    const char *query;
    const char *mirror_base;
    apr_array_header_t *exclude_mime;
    apr_array_header_t *exclude_agents;
    apr_array_header_t *exclude_networks;
    apr_array_header_t *exclude_ips;
    ap_regex_t *exclude_filemask;
} zrkadlo_dir_conf;

/* per-server configuration */
typedef struct
{
    const char *instance;
    int memcached_on;
    int memcached_lifetime;
    apr_table_t *treat_country_as; /* treat country as another country */
    const char *metalink_hashes_prefix;
    const char *metalink_publisher_name;
    const char *metalink_publisher_url;
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

static apr_status_t zrkadlo_cleanup()
{
        GeoIP_delete(gip);
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, NULL, "[mod_zrkadlo] cleaned up geoipfile");
        return APR_SUCCESS;
}

static void zrkadlo_child_init(apr_pool_t *p, server_rec *s)
{
    if (!gip) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, 
                "[mod_zrkadlo] opening geoip file %s", geoipfilename);
        gip = GeoIP_open(geoipfilename, GEOIP_MEMORY_CACHE);
    }
    if (!gip) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, 
                "[mod_zrkadlo] Error while opening geoip file '%s'", geoipfilename);
    }
    apr_pool_cleanup_register(p, NULL, zrkadlo_cleanup, zrkadlo_cleanup);

    srand((unsigned int)getpid());
}


static int zrkadlo_post_config(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp,
                 server_rec *s)
{
    ap_add_version_component(p, VERSION_COMPONENT);

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
    new->metalink_add_torrent       = UNSET;
    new->query = NULL;
    new->mirror_base = NULL;
    new->exclude_mime = apr_array_make(p, 0, sizeof (char *));
    new->exclude_agents = apr_array_make(p, 0, sizeof (char *));
    new->exclude_networks = apr_array_make(p, 4, sizeof (char *));
    new->exclude_ips = apr_array_make(p, 4, sizeof (char *));
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
    cfgMergeInt(metalink_add_torrent);
    cfgMergeString(query);
    cfgMergeString(mirror_base);
    mrg->exclude_mime = apr_array_append(p, base->exclude_mime, add->exclude_mime);
    mrg->exclude_agents = apr_array_append(p, base->exclude_agents, add->exclude_agents);
    mrg->exclude_networks = apr_array_append(p, base->exclude_networks, add->exclude_networks);
    mrg->exclude_ips = apr_array_append(p, base->exclude_ips, add->exclude_ips);
    mrg->exclude_filemask = (add->exclude_filemask == NULL) ? base->exclude_filemask : add->exclude_filemask;

    return (void *) mrg;
}

static void *create_zrkadlo_server_config(apr_pool_t *p, server_rec *s)
{
    zrkadlo_server_conf *new =
      (zrkadlo_server_conf *) apr_pcalloc(p, sizeof(zrkadlo_server_conf));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, 
            "[mod_zrkadlo] creating server config");

    new->instance = "default";
    new->memcached_on = UNSET;
    new->memcached_lifetime = UNSET;
    new->treat_country_as = apr_table_make(p, 0);
    new->metalink_hashes_prefix = NULL;
    new->metalink_publisher_name = NULL;
    new->metalink_publisher_url = NULL;

    return (void *) new;
}

static void *merge_zrkadlo_server_config(apr_pool_t *p, void *basev, void *addv)
{
    zrkadlo_server_conf *base = (zrkadlo_server_conf *) basev;
    zrkadlo_server_conf *add = (zrkadlo_server_conf *) addv;
    zrkadlo_server_conf *mrg = apr_pcalloc(p, sizeof(zrkadlo_server_conf));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, 
            "[mod_zrkadlo] merging server config");

    cfgMergeString(instance);
    cfgMergeBool(memcached_on);
    cfgMergeInt(memcached_lifetime);
    mrg->treat_country_as = apr_table_overlay(p, add->treat_country_as, base->treat_country_as);
    cfgMergeString(metalink_hashes_prefix);
    cfgMergeString(metalink_publisher_name);
    cfgMergeString(metalink_publisher_url);

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

static const char *zrkadlo_cmd_excludenetwork(cmd_parms *cmd, void *config,
                                        const char *arg1)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    char **network = (char **) apr_array_push(cfg->exclude_networks);
    *network = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *zrkadlo_cmd_excludeip(cmd_parms *cmd, void *config,
                                        const char *arg1)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    char **ip = (char **) apr_array_push(cfg->exclude_ips);
    *ip = apr_pstrdup(cmd->pool, arg1);
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

static const char *zrkadlo_cmd_instance(cmd_parms *cmd, 
                                void *config, const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->instance = arg1;
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

static const char *zrkadlo_cmd_metalink_hashes_prefix(cmd_parms *cmd, 
                                void *config, const char *arg1)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->metalink_hashes_prefix = arg1;
    return NULL;
}

static const char *zrkadlo_cmd_metalink_publisher(cmd_parms *cmd, 
                                void *config, const char *arg1, 
                                const char *arg2)
{
    server_rec *s = cmd->server;
    zrkadlo_server_conf *cfg = 
        ap_get_module_config(s->module_config, &zrkadlo_module);

    cfg->metalink_publisher_name = arg1;
    cfg->metalink_publisher_url = arg2;
    return NULL;
}

static const char *zrkadlo_cmd_metalink_add_torrent(cmd_parms *cmd, 
        void *config, int flag)
{
    zrkadlo_dir_conf *cfg = (zrkadlo_dir_conf *) config;
    cfg->metalink_add_torrent = flag;
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

static int cmp_mirror_rank(const void *v1, const void *v2)
{
    mirror_entry_t *m1 = *(mirror_entry_t **)v1;
    mirror_entry_t *m2 = *(mirror_entry_t **)v2;
    return m1->rank - m2->rank;
}

/* return base64 encoded string of a (binary) md5 hash */
static char *zrkadlo_md5b64_enc(apr_pool_t *p, char *s)
{
    apr_md5_ctx_t context;

    apr_md5_init(&context);
    apr_md5_update(&context, s, strlen(s));
    return ap_md5contextTo64(p, &context);
}


static int zrkadlo_handler(request_rec *r)
{
    zrkadlo_dir_conf *cfg = NULL;
    zrkadlo_server_conf *scfg = NULL;
    char *uri = NULL;
    char *filename = NULL;
    char *filename_hash = NULL;
    const char *user_agent = NULL;
    const char *val = NULL;
    const char *clientip = NULL;
    char fakefile = 0, newmirror = 0;
    char mirrorlist = 0, mirrorlist_txt = 0;
    char metalink_forced = 0;                   /* metalink was explicitely requested */
    char metalink = 0;                          /* metalink was negotiated */ 
                                                /* for negotiated metalinks, the exceptions are observed. */
    short int country_id;
    char* country_code;
    const char* continent_code;
    int i;
    int cached_id;
    int mirror_cnt;
    char unusable;
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
    apr_memcache_t *memctxt;                    /* memcache context provided by mod_memcache */
    const char* (*form_lookup)(request_rec*, const char*);

    cfg = (zrkadlo_dir_conf *)     ap_get_module_config(r->per_dir_config, 
                                                        &zrkadlo_module);
    scfg = (zrkadlo_server_conf *) ap_get_module_config(r->server->module_config, 
                                                        &zrkadlo_module);

    /* is ZrkadloEngine disabled for this directory? */
    if (cfg->engine_on != 1) {
        return DECLINED;
    }
    debugLog(r, cfg, "ZrkadloEngine On, instance '%s', mirror_base '%s'", 
            scfg->instance, cfg->mirror_base);

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
                "(ZrkadloHandleDirectoryIndexLocally)");
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
        if (form_lookup(r, "newmirror")) newmirror = 1;
        if (form_lookup(r, "mirrorlist")) mirrorlist =1;
        if (form_lookup(r, "metalink")) metalink_forced = 1;
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
                    ext[0] = '\0';

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
        debugLog(r, cfg, "File '%s' is excluded by ZrkadloExcludeFileMask", r->uri);
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
    country_id = GeoIP_id_by_addr(gip, clientip);
    country_code = apr_pstrdup(r->pool, GeoIP_country_code[country_id]);
    continent_code = GeoIP_country_continent[country_id];

    debugLog(r, cfg, "Country '%s' (%d), Continent '%s'", country_code, 
            country_id, 
            continent_code);

    /* save details for logging via a CustomLog */
    apr_table_setn(r->subprocess_env, "ZRKADLO_FILESIZE", 
            apr_off_t_toa(r->pool, r->finfo.size));
    apr_table_set(r->subprocess_env, "ZRKADLO_COUNTRY_CODE", country_code);
    apr_table_set(r->subprocess_env, "ZRKADLO_CONTINENT_CODE", continent_code);

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

    filename_hash = zrkadlo_md5b64_enc(r->pool, filename);
    if (strlen(filename_hash) != 24) {
        ap_log_rerror(APLOG_MARK, APLOG_CRIT, 0, r, 
                "[mod_zrkadlo] Error hashing filename '%s'", r->filename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    /* strip the '==' trailing the base64 encoding */
    filename_hash[22] = '\0';
    debugLog(r, cfg, "filename_hash: %s", filename_hash);

    if (apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, statement, 
                1, /* we don't need random access actually, but 
                      without it the mysql driver doesn't return results
                      once apr_dbd_num_tuples() has been called; 
                      apr_dbd_get_row() will only return -1 after that. */
                filename_hash, NULL) != 0) {
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
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, 
                "[mod_zrkadlo] no mirrors found for %s", filename);
        /* can be used for a CustomLog */
        apr_table_setn(r->subprocess_env, "ZRKADLO_NOMIRROR", "1");

        if (mirrorlist) {
            debugLog(r, cfg, "empty mirrorlist");
            ap_set_content_type(r, "text/html; charset=ISO-8859-1");
            ap_rputs(DOCTYPE_HTML_3_2
                     "<html><head>\n<title>Mirror List</title>\n</head><body>\n",
                     r);

            ap_rprintf(r, "Filename: %s<br>\n", filename);
            ap_rprintf(r, "Client IP address: %s<br>\n", clientip);
            ap_rprintf(r, "No mirror found.\n");

            ap_rputs("</body>\n", r);
            return OK;
        }

        /* deliver the file ourselves */
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
    i = 1;
    while (i <= mirror_cnt) { 
        rv = apr_dbd_get_row(dbd->driver, r->pool, res, &row, i);
        if (rv != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "[mod_zrkadlo] Error looking up %s in database", filename);
            return DECLINED;
        }

        new = apr_array_push(mirrors);
        new->id = 0;
        new->identifier = NULL;
        new->country_code = NULL;
        new->region = NULL;
        new->region_only = 0;
        new->country_only = 0;
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
            new->country_code = apr_pstrndup(r->pool, val, 2); /* fixed length, two bytes */

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
        } else {
            new->baseurl = apr_pstrdup(r->pool, val);
            if (new->baseurl[strlen(new->baseurl) - 1] != '/') { 
                new->baseurl = apr_pstrcat(r->pool, new->baseurl, "/", NULL); 
            }
        }

        /* country_only */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 6)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for country_only");
        } else
            new->country_only = (short)atoi(val);

        /* region_only */
        if ((val = apr_dbd_get_entry(dbd->driver, row, 7)) == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_zrkadlo] apr_dbd_get_entry found NULL for region_only");
        } else
            new->region_only = (short)atoi(val);

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
        if (strcasecmp(new->country_code, country_code) == 0) {
            *(void **)apr_array_push(mirrors_same_country) = new;

        /* is the mirror's country_code a wildcard indicating that the mirror should be
         * considered for every country? */
        } else if (strcmp(new->country_code, "**") == 0) {
            *(void **)apr_array_push(mirrors_same_country) = new; 
            /* if so, forget memcache association, so the mirror is not ruled out */
            chosen = NULL; 
            /* set its country and region to that of the client */
            new->country_code = country_code;
            new->region = continent_code;

        /* same region? */
        /* to be actually considered for this group, the mirror must be willing 
         * to take redirects from foreign country */
        } else if ((strcasecmp(new->region, continent_code) == 0) 
                    && (new->country_only != 1)) {
            *(void **)apr_array_push(mirrors_same_region) = new;

        /* to be considered as "worldwide" mirror, it must be willing 
         * to take redirects from foreign regions.
         * (N.B. region_only implies country_only)  */
        } else if ((new->region_only != 1) && (new->country_only != 1)) {
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
        qsort(mirrors_same_country->elts, mirrors_same_country->nelts, 
              mirrors_same_country->elt_size, cmp_mirror_rank);
        qsort(mirrors_same_region->elts, mirrors_same_region->nelts, 
              mirrors_same_region->elt_size, cmp_mirror_rank);
        qsort(mirrors_elsewhere->elts, mirrors_elsewhere->nelts, 
              mirrors_elsewhere->elt_size, cmp_mirror_rank);
    }

    if (cfg->debug) {

        /* list the same-country mirrors */
        /* Brad's mod_edir hdir.c helped me here.. thanks to his kind help */
        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        mirror = NULL;

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

        debugLog(r, cfg, "Found %d mirror%s: %d country, %d region, %d elsewhere", mirror_cnt,
                (mirror_cnt == 1) ? "" : "s",
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
        ap_rprintf(r, "  origin=\"http://%s%s\"\n", r->hostname, r->uri);
        ap_rputs(     "  generator=\"mod_zrkadlo Download Redirector - http://mirrorbrain.org/\"\n", r);
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

        if (cfg->metalink_add_torrent == 1 
            && apr_stat(&sb, apr_pstrcat(r->pool, r->filename, ".torrent", NULL), APR_FINFO_MIN, r->pool) == APR_SUCCESS) {
            debugLog(r, cfg, "found torrent file");
            ap_rprintf(r, "      <url type=\"bittorrent\" preference=\"%d\">http://%s%s.torrent</url>\n\n", 
                       100,
                       r->hostname, 
                       r->uri);
        }

        ap_rprintf(r, "      <!-- Found %d mirror%s: %d in the same country, %d in the same region, %d elsewhere -->\n", 
                   mirror_cnt,
                   (mirror_cnt == 1) ? "" : "s",
                   mirrors_same_country->nelts,
                   mirrors_same_region->nelts,
                   mirrors_elsewhere->nelts);

        /* the highest metalink preference according to the spec is 100, and
         * we'll decrement it for each mirror by one, until zero is reached */
        int pref = 101;

        mirrorp = (mirror_entry_t **)mirrors_same_country->elts;
        mirror = NULL;

        /* failed geoip lookup yields country='--' which leads to invalid XML */
        ap_rprintf(r, "\n      <!-- Mirrors in the same country (%s): -->\n", 
                   (strcmp(country_code, "--") == 0) ? "unknown" : country_code);
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            if (pref) pref--;
            mirror = mirrorp[i];
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        ap_rprintf(r, "\n      <!-- Mirrors in the same continent (%s): -->\n", 
                   (strcmp(continent_code, "--") == 0) ? "unknown" : continent_code);
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            if (pref) pref--;
            mirror = mirrorp[i];
            ap_rprintf(r, "      <url type=\"http\" location=\"%s\" preference=\"%d\">%s%s</url>\n", 
                       mirror->country_code,
                       pref,
                       mirror->baseurl, filename);
        }

        ap_rputs("\n      <!-- Mirrors in the rest of the world: -->\n", r);
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            if (pref) pref--;
            mirror = mirrorp[i];
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

        ap_rprintf(r, "\n<h3>Mirrors in the same country (%s):</h3>\n", country_code);
        ap_rputs("<pre>\n", r);
        for (i = 0; i < mirrors_same_country->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "<a href=\"%s%s\">%s%s</a> (score %d)\n", 
                    mirror->baseurl, filename, 
                    mirror->baseurl, filename, 
                    mirror->score);
        }
        ap_rputs("</pre>\n", r);

        ap_rprintf(r, "\n<h3>Mirrors in the same continent (%s):</h3>\n", continent_code);
        ap_rputs("<pre>\n", r);
        mirrorp = (mirror_entry_t **)mirrors_same_region->elts;
        for (i = 0; i < mirrors_same_region->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "<a href=\"%s%s\">%s%s</a> (score %d)\n", 
                    mirror->baseurl, filename, 
                    mirror->baseurl, filename, 
                    mirror->score);
        }
        ap_rputs("</pre>\n", r);

        ap_rputs("\n<h3>Mirrors in the rest of the world:</h3>\n", r);
        ap_rputs("<pre>\n", r);
        mirrorp = (mirror_entry_t **)mirrors_elsewhere->elts;
        for (i = 0; i < mirrors_elsewhere->nelts; i++) {
            mirror = mirrorp[i];
            ap_rprintf(r, "<a href=\"%s%s\">%s%s</a> (score %d)\n", 
                    mirror->baseurl, filename, 
                    mirror->baseurl, filename, 
                    mirror->score);
        }
        ap_rputs("</pre>\n", r);

        ap_rputs("</body>\n", r);
        return OK;
    } /* end mirrorlist */


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

    /* Send it away: set a "Location:" header and 302 redirect. */
    uri = apr_pstrcat(r->pool, chosen->baseurl, filename, NULL);
    debugLog(r, cfg, "Redirect to '%s'", uri);

    /* for _conditional_ logging, leave some mark */
    apr_table_setn(r->subprocess_env, "ZRKADLO_REDIRECTED", "1");

    apr_table_setn(r->err_headers_out, "X-Zrkadlo-Chose-Mirror", chosen->identifier);
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
    apr_memcache_t *memctxt;                    /* memcache context provided by mod_memcache */
    apr_memcache_stats_t *stats;
    zrkadlo_server_conf *sc = ap_get_module_config(r->server->module_config, &zrkadlo_module);

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
    AP_INIT_TAKE1("ZrkadloExcludeNetwork", zrkadlo_cmd_excludenetwork, 0, 
                  OR_OPTIONS,
                  "Network to always exclude from redirecting (simple string prefix)"),
    AP_INIT_TAKE1("ZrkadloExcludeIP", zrkadlo_cmd_excludeip, 0,
                  OR_OPTIONS,
                  "IP address to always exclude from redirecting"),
    AP_INIT_TAKE1("ZrkadloExcludeFileMask", zrkadlo_cmd_exclude_filemask, NULL,
                  ACCESS_CONF,
                  "Regexp which determines which files will be excluded form redirecting"),

    AP_INIT_FLAG("ZrkadloHandleDirectoryIndexLocally", zrkadlo_cmd_handle_dirindex_locally, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to handle directory listings locally (don't redirect)"),
    AP_INIT_FLAG("ZrkadloHandleHEADRequestLocally", zrkadlo_cmd_handle_headrequest_locally, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to handle HEAD requests locally (don't redirect)"),

    AP_INIT_FLAG("ZrkadloMetalinkAddTorrent", zrkadlo_cmd_metalink_add_torrent, NULL, 
                  OR_OPTIONS,
                  "Set to On/Off to look for .torrent files and, if present, add them into generated metalinks"),

    /* to be used only in server context */
    AP_INIT_TAKE1("ZrkadloInstance", zrkadlo_cmd_instance, NULL, 
                  RSRC_CONF, 
                  "Name of the Zrkadlo instance"),

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

    AP_INIT_TAKE1("ZrkadloMemcachedLifeTime", zrkadlo_cmd_memcached_lifetime, NULL,
                  RSRC_CONF, 
                  "Lifetime (in seconds) associated with stored objects in "
                  "memcache daemon(s). Default is 600 s."),

    AP_INIT_TAKE2("ZrkadloTreatCountryAs", zrkadlo_cmd_treat_country_as, NULL, 
                  OR_FILEINFO,
                  "Set country to be treated as another. E.g.: nz au"),

    AP_INIT_TAKE1("ZrkadloMetalinkHashesPathPrefix", zrkadlo_cmd_metalink_hashes_prefix, NULL, 
                  RSRC_CONF, 
                  "Prefix this path when looking for prepared hashes to inject into metalinks"),

    AP_INIT_TAKE2("ZrkadloMetalinkPublisher", zrkadlo_cmd_metalink_publisher, NULL, 
                  RSRC_CONF, 
                  "Name and URL for the metalinks publisher elements"),

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
