
/*
 * mod_stats
 *
 * collect statistics on OpenSUSE build service downloads and push them 
 * into an sql database
 *
 * Copyright (c) 2007 Peter Poeml <poeml@suse.de> / Novell Inc.
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


#ifndef UNSET
#define UNSET (-1)
#endif

#define MOD_STATS_VER "0.1"
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
} download_t;

/* per-dir configuration */
typedef struct
{
    int stats_enabled;
    int debug;
    const char *query;
    const char *select_query;
    const char *insert_query;
    const char *stats_base;
    ap_regex_t *filemask;

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
    new->stats_base     = NULL;
    new->filemask       = NULL;

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
    cfgMergeString(stats_base);
    mrg->filemask = (add->filemask == NULL) ? base->filemask : add->filemask;

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
    while ((*pos != stop) && *pos) {
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
    char *res;
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

    d->rel = stats_getlastword(r->pool, &file, '-');
    debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping release", file);

    /* XXX don't know whether version of deb files can contain underscores */
    if (apr_strnatcmp(d->type, "deb") == 0)
        d->vers = stats_getlastword(r->pool, &file, '_');
    else
        d->vers = stats_getlastword(r->pool, &file, '-');
    debugLog(r, cfg, "stats_parse_req(): file: '%s' after stripping version", file);

    return d;
}

static int stats_logger(request_rec *r)
{
    download_t *d;
    int nrows = 0;
    char *req_filename = NULL;
    apr_dbd_prepared_t *statement;
    apr_dbd_results_t *res = NULL;
    stats_dir_conf *cfg = NULL;

    cfg = (stats_dir_conf *) ap_get_module_config(r->per_dir_config,
                                                      &stats_module);
    if (cfg->stats_enabled != 1) {
        return DECLINED;
    }
    debugLog(r, cfg, "Stats are enabled stats_base is '%s'", cfg->stats_base);

    if (!cfg->filemask) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "No StatsFilemask configured!");
        return DECLINED;
    }
    if (ap_regexec(cfg->filemask, r->filename, 0, NULL, 0)) {
        debugLog(r, cfg, "File '%s' does not match StatsFileMask", r->filename);
        return DECLINED;
    }

    /* decline if HTTP code is not 200 or similar */
    switch(r->status) {
        case HTTP_OK:
        case HTTP_MOVED_PERMANENTLY:
        case HTTP_TEMPORARY_REDIRECT:
        case HTTP_SEE_OTHER:
            break;
        default:
            return DECLINED;
    }

    req_filename = r->filename + strlen(cfg->stats_base);
    debugLog(r, cfg, "req_filename: '%s'", req_filename);

    d = (download_t *) apr_pcalloc(r->pool, sizeof(download_t));
    stats_parse_req(r, cfg, req_filename, d);

    debugLog(r, cfg, "fname:   '%s'", d->fname);
    debugLog(r, cfg, "type:    '%s'", d->type);
    debugLog(r, cfg, "project: '%s'", d->prj);
    debugLog(r, cfg, "repo:    '%s'", d->repo);
    debugLog(r, cfg, "arch:    '%s'", d->arch);
    debugLog(r, cfg, "version: '%s'", d->vers);
    debugLog(r, cfg, "release: '%s'", d->rel);

    if (!cfg->query) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "No StatsDBDQuery configured!");
        return DECLINED;
    }

    ap_dbd_t *dbd = stats_dbd_acquire_fn(r);
    if (dbd == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "Error acquiring database connection");
        return DECLINED;
    }
    debugLog(r, cfg, "Successfully acquired database connection.");

    /* optionally, check for existence */
    if (cfg->select_query && cfg->insert_query) {
        statement = apr_hash_get(dbd->prepared, cfg->select_query, APR_HASH_KEY_STRING);
        if (statement == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "Could not get StatsDBDSelectQuery prepared statement");
            return DECLINED;
        }
        if (apr_dbd_pvselect(dbd->driver, r->pool, dbd->handle, &res, statement, 1, 
                    d->prj, d->repo, d->arch, d->fname, d->type, d->vers, d->rel, NULL) != 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                    "Error looking up %s in database", r->filename);
        }

        nrows = apr_dbd_num_tuples(dbd->driver, res);
        if (!nrows) {
            /* insert */
            statement = apr_hash_get(dbd->prepared, cfg->insert_query, APR_HASH_KEY_STRING);
            if (statement == NULL) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                        "Could not get StatsDBDInsertQuery prepared statement");
                return DECLINED;
            }
            if (apr_dbd_pvquery(dbd->driver, r->pool, dbd->handle, 
                        &nrows, statement,
                        d->prj, d->repo, d->arch, 
                        d->fname, d->type, d->vers, d->rel) != 0) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                        "Error inserting %s into database", r->filename);
            }
        }

    }

    /* update */
    statement = apr_hash_get(dbd->prepared, cfg->query, APR_HASH_KEY_STRING);
    if (statement == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "No StatsDBDQuery configured!");
        return DECLINED;
    }
    if (apr_dbd_pvquery(dbd->driver, r->pool, dbd->handle, 
                &nrows, statement, 
                d->prj, d->repo, d->arch, 
                d->fname, d->type, d->vers, d->rel) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                "Got error with update query for %s", r->filename);
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

    /* to be used only in server context */
    AP_INIT_TAKE1("StatsDBDQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, query), 
                  RSRC_CONF,
                  "the SQL query string to update the statistics database"),
    AP_INIT_TAKE1("StatsDBDSelectQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, select_query), 
                  RSRC_CONF,
                  "optional SQL query string to check for existence objects"),
    AP_INIT_TAKE1("StatsDBDInsertQuery", stats_dbd_prepare, 
                      (void *)APR_OFFSETOF(stats_dir_conf, insert_query), 
                  RSRC_CONF,
                  "optional SQL query string to create non-existant objects"),
    { NULL }
};

static void stats_register_hooks(apr_pool_t *p)
{
    ap_hook_post_config    (stats_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(stats_logger, NULL, NULL, APR_HOOK_MIDDLE);
}


module AP_MODULE_DECLARE_DATA stats_module =
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
