
/*
 * Copyright (c) 2009 Peter Poeml <poeml@suse.de> / Novell Inc.
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
 * mod_stats
 *
 * collect download statistics and log them to a database
 * see http://svn.mirrorbrain.org/svn/mod_stats and
 * http://mirrorbrain.org/download-statistics/
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
#include "apr_dbd.h"
#include "mod_dbd.h"
#include "mod_form.h"


#ifndef UNSET
#define UNSET (-1)
#endif

#define MOD_STATS_VER "1.2"
#define VERSION_COMPONENT "mod_stats/"MOD_STATS_VER

/* from ssl/ssl_engine_config.c */
#define cfgMerge(el,unset)  mrg->el = (add->el == (unset)) ? base->el : add->el
#define cfgMergeArray(el)   mrg->el = apr_array_append(p, add->el, base->el)
#define cfgMergeString(el)  cfgMerge(el, NULL)
#define cfgMergeBool(el)    cfgMerge(el, UNSET)
#define cfgMergeInt(el)     cfgMerge(el, UNSET)

module AP_MODULE_DECLARE_DATA stats_module;

/* A structure that represents a download */
typedef struct
{
    char *prj;
    char *repo;
    char *arch;
    char *fname;
    char *type;
    char *vers;
    char *rel;
    char *pac;
} download_t;

/* per-dir configuration */
typedef struct
{
    int stats_enabled;
    int debug;
    const char *query;
    const char *select_query;
    const char *insert_query;
    const char *delete_query;
    const char *stats_base;
    ap_regex_t *filemask;
    apr_array_header_t *admin_hosts;
    apr_array_header_t *admin_ips;

} stats_dir_conf;


/* optional function - look it up once in post_config */
static ap_dbd_t *(*stats_dbd_acquire_fn)(request_rec*) = NULL;
static void (*stats_dbd_prepare_fn)(server_rec*, const char*, const char*) = NULL;


static void debugLog(const request_rec *r, const stats_dir_conf *cfg,
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
                      r, "[mod_stats] %s", buf);
    }
}

static int stats_post_config(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp,
                 server_rec *s)
{
    ap_add_version_component(p, VERSION_COMPONENT);

    /* make sure that mod_form is loaded */
    if (ap_find_linked_module("mod_form.c") == NULL) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
                     "[mod_stats] Module mod_form missing. Mod_form "
                     "must be loaded in order for mod_zrkadlo to function properly");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    return OK;
}

static void *create_stats_dir_config(apr_pool_t *p, char *dirspec)
{
    stats_dir_conf *new =
      (stats_dir_conf *) apr_pcalloc(p, sizeof(stats_dir_conf));

    new->stats_enabled  = UNSET;
    new->debug          = UNSET;
    new->query          = NULL;
    new->select_query   = NULL;
    new->insert_query   = NULL;
    new->delete_query   = NULL;
    new->stats_base     = NULL;
    new->filemask       = NULL;
    new->admin_hosts = apr_array_make(p, 1, sizeof (char *));
    new->admin_ips = apr_array_make(p, 1, sizeof (apr_sockaddr_t));

    return (void *) new;
}

static void *merge_stats_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    stats_dir_conf *mrg  = (stats_dir_conf *) apr_pcalloc(p, sizeof(stats_dir_conf));
    stats_dir_conf *base = (stats_dir_conf *) basev;
    stats_dir_conf *add  = (stats_dir_conf *) addv;

    /* DBG("merge_stats_dir_config: new=%08lx  base=%08lx  overrides=%08lx",
     *        (long)mrg, (long)base, (long)add); */

    cfgMergeInt(stats_enabled);
    cfgMergeInt(debug);
    cfgMergeString(query);
    cfgMergeString(select_query);
    cfgMergeString(insert_query);
    cfgMergeString(delete_query);
    cfgMergeString(stats_base);
    mrg->filemask = (add->filemask == NULL) ? base->filemask : add->filemask;
    mrg->admin_hosts = apr_array_append(p, add->admin_hosts, base->admin_hosts);
    mrg->admin_ips = apr_array_append(p, add->admin_ips, base->admin_ips);

    return (void *) mrg;
}

static const char *stats_cmd_stats(cmd_parms *cmd, void *config, int flag)
{
    stats_dir_conf *cfg = (stats_dir_conf *) config;
    cfg->stats_enabled = flag;
    cfg->stats_base = apr_pstrdup(cmd->pool, cmd->path);
    return NULL;
}

static const char *stats_cmd_debug(cmd_parms *cmd, void *config, int flag)
{
    stats_dir_conf *cfg = (stats_dir_conf *) config;
    cfg->debug = flag;
    return NULL;
}

static const char *stats_cmd_filemask(cmd_parms *cmd, void *config, const char *arg)
{
    stats_dir_conf *cfg = (stats_dir_conf *) config;
    cfg->filemask = ap_pregcomp(cmd->pool, arg, AP_REG_EXTENDED);
    if (cfg->filemask == NULL) {
        return "StatsFileMask regex could not be compiled";
    }
    return NULL;
}

static const char *stats_cmd_admin_hosts(cmd_parms *cmd, void *config,
                                const char *arg)
{
    stats_dir_conf *cfg = (stats_dir_conf *) config;
    char **hostelem = (char **) apr_array_push(cfg->admin_hosts);
    apr_sockaddr_t *addr = (apr_sockaddr_t *) apr_array_push(cfg->admin_ips);

    apr_sockaddr_t *resolved = NULL;
    apr_status_t rc;
    rc = apr_sockaddr_info_get(&resolved, arg, APR_UNSPEC, 0, 0, cmd->pool);
    if (rc != APR_SUCCESS) {
        apr_array_pop(cfg->admin_hosts);
        apr_array_pop(cfg->admin_ips);
        return "DNS lookup failure!";
    }
    memcpy(addr, resolved, sizeof (apr_sockaddr_t));

    *hostelem = apr_pstrdup(cmd->pool, arg);
    return NULL;
}


static const char *stats_dbd_prepare(cmd_parms *cmd, void *cfg, const char *query)
{
    static unsigned int label_num = 0;
    char *label;

    if (stats_dbd_prepare_fn == NULL) {
        stats_dbd_prepare_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_prepare);
        if (stats_dbd_prepare_fn == NULL) {
            return "You must load mod_dbd to enable mod_stats functions";
        }
        stats_dbd_acquire_fn = APR_RETRIEVE_OPTIONAL_FN(ap_dbd_acquire);
    }
    label = apr_psprintf(cmd->pool, "stats_dbd_%d", ++label_num);

    stats_dbd_prepare_fn(cmd->server, query, label);

    /* save the label here for our own use */
    return ap_set_string_slot(cmd, cfg, label);
}

/* like ap_getword(), but working backwards */
static char *stats_getlastword(apr_pool_t *atrans, char **line, char stop)
{
    char *pos, *res;
    int len = 0;

    pos = *line + strlen(*line);
    --pos;
    while ((pos >= *line) && (*pos != stop) && *pos) {
        --pos;
        ++len;
    }

    res = (char *)apr_palloc(atrans, len + 1);
    memcpy(res, pos + 1, len);
    res[len] = 0;

    *pos = 0;

    if (stop) {
        while (*pos == stop) {
            *pos = 0;
            --pos;
        }
    }

    return res;
}


static download_t *stats_parse_req(request_rec *r, stats_dir_conf *cfg, 
                                   char *req_filename, download_t *d) {
    char *file;
    int i, len;
    int j = 0;

    d->fname = stats_getlastword(r->pool, &req_filename, '/');
    d->arch  = stats_getlastword(r->pool, &req_filename, '/');
    d->repo  = stats_getlastword(r->pool, &req_filename, '/');

    /* make a copy of the remaining string but don't copy '/' characters,
     * so devel:/libraries:/hardware becomes devel:libraries:hardware */
    len = strlen(req_filename);
    d->prj = apr_palloc(r->pool, len + 1);
    for (i = 0; i < len; i++) {
        if (req_filename[i] != '/') {
            d->prj[j] = req_filename[i];
            j++;
        }
    }
    d->prj[j] = 0;

    file = apr_pstrdup(r->pool, d->fname);
    debugLog(r, cfg, "stats_parse_req(): file: '%s'", file);

    d->type = stats_getlastword(r->pool, &file, '.');
    debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping type", file);

    /* skip the arch string, which we already know at this place, and skip
     * an additional separator which is '.' for .rpm and '_' for .deb files */
    file[strlen(file) - strlen(d->arch) -1] = 0;
    debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping arch", file);

    if (apr_strnatcmp(d->type, "deb") == 0) { /* deb package */
        /* deb package names cannot contain underscores */
        /* http://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Package */
        /* http://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Version */
        d->pac = ap_getword_nc(r->pool, &file, '_');
        debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping package name", file);

        /* release is optional. Do we have one? */
        if (ap_strstr(file, "-"))
            d->rel = stats_getlastword(r->pool, &file, '-');
        else 
            d->rel = "";
        debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping release", file);
        debugLog(r, cfg, "rel '%s'", d->rel);

        d->vers = stats_getlastword(r->pool, &file, '_');

    } else { /* rpm  package */
        d->rel = stats_getlastword(r->pool, &file, '-');
        debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping release", file);

        d->vers = stats_getlastword(r->pool, &file, '-');
        d->pac = file;
    }

    debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping version", file);

    return d;
}

static int stats_logger(request_rec *r)
{
    download_t *d;
    int i, nrows = 0;
    char *req_filename = NULL;
    apr_dbd_prepared_t *statement;
    apr_dbd_results_t *res = NULL;
    stats_dir_conf *cfg = NULL;
    const char* (*form_lookup)(request_rec*, const char*);
    const char* qs_cmd = NULL;      /* query string command */
    const char* qs_package = NULL;  /* query string package name */
    apr_sockaddr_t *list = NULL;
    char *admin_host = NULL;
    int admin_allowed = 0;

    cfg = (stats_dir_conf *) ap_get_module_config(r->per_dir_config,
                                                      &stats_module);
    if (cfg->stats_enabled != 1) {
        return DECLINED;
    }
    debugLog(r, cfg, "Stats enabled, stats_base '%s'", cfg->stats_base);

    if (!cfg->filemask) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_stats] No StatsFilemask configured!");
        return DECLINED;
    }
    if (ap_regexec(cfg->filemask, r->uri, 0, NULL, 0)) {
        debugLog(r, cfg, "File '%s' does not match StatsFileMask", r->uri);
        return DECLINED;
    }

    /* decline if HTTP code is not 200 or similar */
    switch(r->status) {
        case HTTP_OK:
        case HTTP_MOVED_PERMANENTLY:
        case HTTP_MOVED_TEMPORARILY:
        case HTTP_TEMPORARY_REDIRECT:
        case HTTP_SEE_OTHER:
            break;
        default:
            debugLog(r, cfg, "not counting for status code %d", r->status);
            return DECLINED;
    }

    debugLog(r, cfg, "filename: '%s'", r->filename);
    debugLog(r, cfg, "uri: '%s'", r->uri);
    req_filename = apr_pstrdup(r->pool, r->filename + strlen(cfg->stats_base));

    /* workaround for the old redirector */
    if (apr_strnatcmp(r->filename, "redirect:/redirect.php") == 0) {
        req_filename = apr_pstrdup(r->pool, r->uri + strlen("/download/"));
    }
    debugLog(r, cfg, "req_filename: '%s'", req_filename);

    d = (download_t *) apr_pcalloc(r->pool, sizeof(download_t));
    stats_parse_req(r, cfg, req_filename, d);

    debugLog(r, cfg, "fname:   '%s'", d->fname);
    debugLog(r, cfg, "project: '%s'", d->prj);
    debugLog(r, cfg, "repo:    '%s'", d->repo);
    debugLog(r, cfg, "package: '%s'", d->pac);
    debugLog(r, cfg, "version: '%s'", d->vers);
    debugLog(r, cfg, "release: '%s'", d->rel);
    debugLog(r, cfg, "arch:    '%s'", d->arch);
    debugLog(r, cfg, "type:    '%s'", d->type);

    if (!cfg->query) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_stats] No StatsDBDQuery configured!");
        return DECLINED;
    }

    ap_dbd_t *dbd = stats_dbd_acquire_fn(r);
    if (dbd == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_stats] Error acquiring database connection");
        return DECLINED;
    }
    debugLog(r, cfg, "Successfully acquired database connection.");



    /* parse query arguments if present, */
    /* using mod_form's form_value() */
    /* XXX
     * ?cmd=setpackage&package=foo
     * ?cmd=deleted
     */
    form_lookup = APR_RETRIEVE_OPTIONAL_FN(form_value);
    if (form_lookup && r->args) {
        qs_cmd = form_lookup(r, "cmd");
        qs_package = form_lookup(r, "package");
    }
    if (qs_cmd) 
        debugLog(r, cfg, "cmd=%s", qs_cmd);
    if (qs_package) 
        debugLog(r, cfg, "package=%s", qs_package);

    /* actions triggered by optional query string are allowed only to certain hosts */
    /* is this request from one of the listed admin servers? */
    list = (apr_sockaddr_t *) cfg->admin_ips->elts;
    for (i = 0; i < cfg->admin_ips->nelts; i++) {
        if (apr_sockaddr_equal(r->connection->remote_addr, &list[i])) {
            admin_allowed = 1;
            admin_host = ((char **) cfg->admin_hosts->elts)[i];
            debugLog(r, cfg, "Host %s is a StatsAdminHost", admin_host);
            continue;
        }
    }

    if (qs_cmd && !admin_allowed) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, 
                "[mod_stats] Admin access attempted, but host is not configured as StatsAdminHost");
        return DECLINED;
    }

    /* delete a package? */
    if ( qs_cmd && cfg->delete_query && (apr_strnatcmp(qs_cmd, "deleted") == 0) ) {
        statement = apr_hash_get(dbd->prepared, cfg->delete_query, APR_HASH_KEY_STRING);
        if (statement == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] Could not get StatsDBDDeleteQuery prepared statement");
            return DECLINED;
        }
        if (apr_dbd_pvquery(dbd->driver, r->pool, dbd->handle, 
                    &nrows, statement,
                    d->prj, d->repo, d->arch, d->pac, d->type, d->vers, d->rel, NULL) != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] Error deleting %s in database", r->filename);
        }
        /* done */
        return DECLINED;
    }

    /* create new package? */
    if ( qs_cmd && cfg->select_query 
            && cfg->insert_query 
            && qs_package 
            && (apr_strnatcmp(qs_cmd, "setpackage") == 0) ) {

        debugLog(r, cfg, "checking if file %s exists", r->filename);

        statement = apr_hash_get(dbd->prepared, cfg->select_query, APR_HASH_KEY_STRING);
        if (statement == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] Could not get StatsDBDSelectQuery prepared statement");
            return DECLINED;
        }
        if (apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, statement, 1, 
                             d->prj, d->repo, d->arch, d->pac, d->type, d->vers, d->rel, NULL) != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] Error looking up %s in database", r->filename);
        }
        if (res == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] apr_dbd_pvselect() claimed success, but returned no result");
            return DECLINED;
        }

        nrows = apr_dbd_num_tuples(dbd->driver, res);
        debugLog(r, cfg, "nrows: %d", nrows);
        if (nrows > 0) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, 
                    "[mod_stats] File %s does already exist. Not inserting", r->filename);
            return DECLINED;
        }

        /* insert */
        debugLog(r, cfg, "inserting package %s, file %s", qs_package, r->filename);
        statement = apr_hash_get(dbd->prepared, cfg->insert_query, APR_HASH_KEY_STRING);
        if (statement == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] Could not get StatsDBDInsertQuery prepared statement");
            return DECLINED;
        }
        if (apr_dbd_pvquery(dbd->driver, r->pool, dbd->handle, 
                    &nrows, statement,
                    d->prj, d->repo, d->arch, 
                    d->pac, d->type, d->vers, d->rel,
                    qs_package) != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "[mod_stats] Error inserting %s into database", r->filename);
        }
        /* done; we don't want to update the new package's counter from this request */
        return DECLINED;
    }


    /* update */
    statement = apr_hash_get(dbd->prepared, cfg->query, APR_HASH_KEY_STRING);
    if (statement == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "[mod_stats] No StatsDBDQuery configured!");
        return DECLINED;
    }
    if (apr_dbd_pvquery(dbd->driver, r->pool, dbd->handle, 
                &nrows, statement, 
                d->prj, d->repo, d->arch, 
                d->pac, d->type, d->vers, d->rel) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "[mod_stats] Got error with update query for %s", r->filename);
        return DECLINED;
    }

    debugLog(r, cfg, "%d row%s updated", nrows,
            (nrows == 1) ? "" : "s");

    /* done! */
    return DECLINED;
}

static const command_rec stats_cmds[] =
{
    /* to be used only in Directory et al. */
    AP_INIT_FLAG("Stats", stats_cmd_stats, NULL, 
                 ACCESS_CONF,
                 "Set to On or Off to enable or disable stats"),
    AP_INIT_FLAG("StatsDebug", stats_cmd_debug, NULL, 
                 ACCESS_CONF,
                 "Set to On or Off to enable or disable debug logging to error log"),
    AP_INIT_TAKE1("StatsFileMask", stats_cmd_filemask, NULL, 
                 ACCESS_CONF,
                 "Regexp which determines for which files stats will be done"),
    AP_INIT_TAKE1("StatsAdminHost", stats_cmd_admin_hosts, NULL, OR_OPTIONS,
                  "Hostname or IP address of server allowed to issue deletes and inserts "
                  "via optional query args appended to the URL"),

    /* to be used only in server context */
    AP_INIT_TAKE1("StatsDBDQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, query), 
                  RSRC_CONF,
                  "the SQL query string to update the statistics database"),
    AP_INIT_TAKE1("StatsDBDSelectQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, select_query), 
                  RSRC_CONF,
                  "optional SQL query string to check for existance of objects"),
    AP_INIT_TAKE1("StatsDBDInsertQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, insert_query), 
                  RSRC_CONF,
                  "optional SQL query string to create non-existant objects"),
    AP_INIT_TAKE1("StatsDBDDeleteQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, delete_query), 
                  RSRC_CONF,
                  "optional SQL query string to delete existing objects"),

    { NULL }
};

static void stats_register_hooks(apr_pool_t *p)
{
    ap_hook_post_config    (stats_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(stats_logger, NULL, NULL, APR_HOOK_MIDDLE);
}


#ifdef AP_DECLARE_MODULE
AP_DECLARE_MODULE(stats) =
#else
/* pre-2.4 */
module AP_MODULE_DECLARE_DATA stats_module =
#endif
{
    STANDARD20_MODULE_STUFF,
    create_stats_dir_config,    /* create per-directory config structures */
    merge_stats_dir_config,     /* merge per-directory config structures  */
    NULL,                       /* create per-server config structures    */
    NULL,                       /* merge per-server config structures     */
    stats_cmds,                 /* command handlers */
    stats_register_hooks        /* register hooks */
};


/* vim: set ts=4 sw=4 expandtab smarttab: */
