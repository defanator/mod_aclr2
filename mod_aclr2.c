/* 
 * Original mod_aclr module for Apache 1.3.x
 *   Copyright (c) Dmitry MikSir (http://miksir.maker.ru)
 *
 * Porting to Apache 2.x
 *   Copyright (c) 2011-2012 Andrey Belov
 *   Copyright (c) 2011-2012 Nginx, Inc. (http://nginx.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_filter.h"
#include "apr_file_info.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "apr_strings.h"
#define APR_WANT_STRFUNC
#include "apr_want.h"

module AP_MODULE_DECLARE_DATA aclr_module;

#define ACLR_VERSION "0.01"

#define ACLR_ENABLED 1
#define ACLR_DISABLED 0
#define UNSET -1

typedef struct {
    int state;
    int redirect_outside_of_docroot;
    apr_off_t fsize;
} aclr_dir_config;

const char *xa_int_name = "X-Accel-Internal";
const char *xa_ver_name = "X-Accel-Version";
const char *xa_redir_name = "X-Accel-Redirect";


/* utilities */

#ifdef DEBUG
static int debuglevel = 0;

#define aclr_debug(level, s, fmt, args...)      \
    if (level <= debuglevel)                    \
        _aclr_debug(s, fmt, args)

static void
_aclr_debug(server_rec *s, const char *fmt, ...)
{
    char errstr[MAX_STRING_LEN];
    va_list args;

    va_start(args, fmt);
    apr_vsnprintf(errstr, sizeof(errstr), fmt, args);
    va_end(args);
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "[%" APR_PID_T_FMT "] %s",
                 getpid(), errstr);
    return;
}

#else
#define aclr_debug(level, s, fmt, ...)
#endif


/* runtime */

static int
aclr_handler(request_rec *r)
{
    int	rc;
    const char *idhead;
    char *real_uri;
    const char *docroot;
    size_t docroot_len;
    ap_filter_t *f, *nextf;
    char iredirect[MAX_STRING_LEN];

    const char *server_name = ap_get_server_name(r);
    aclr_dir_config *cfg = (aclr_dir_config *)ap_get_module_config
                           (r->per_dir_config, &aclr_module);

    if (cfg->state != ACLR_ENABLED) {
        return DECLINED;
    }

    if (!ap_is_initial_req(r)) {
        return DECLINED;
    }

    idhead = apr_table_get(r->headers_in, xa_int_name);

    if (idhead == NULL) {
        return DECLINED;
    }

    docroot = ap_document_root(r);
    docroot_len = strlen(docroot);

    /* obtain real URI from filename - for rewrited URIs */
    if (strncmp(r->filename, docroot, docroot_len) != 0) {
        if (cfg->redirect_outside_of_docroot != ACLR_ENABLED) {

            aclr_debug(2, r->server, "file \"%s\" is outside of "
                       "DocumentRoot \"%s\": %s%s",
                       r->filename, docroot, server_name, r->uri);

            return DECLINED;
        }
        real_uri = r->uri;
    }
    else {
        real_uri = r->filename;
        real_uri += docroot_len;
    }

/*  if ((idh1 = strstr(idhead, "%host%"))) {
        *idh1 = '\0';
        idh1 += 6;
        snprintf(iredirect, sizeof(iredirect), "%s%s%s%s",
                 idhead, server_name, idh1, real_uri);
    } else {
        snprintf(iredirect, sizeof(iredirect), "%s%s", idhead, real_uri);
    } */

    snprintf(iredirect, sizeof(iredirect), "%s%s", idhead, real_uri);

    aclr_debug(3, r->server, "trying to process request: %s%s -> %s",
               server_name, r->uri, iredirect);

    aclr_debug(3, r->server, "r->filename: \"%s\"", r->filename);
    aclr_debug(3, r->server, "r->uri     : \"%s\"", r->uri);
    aclr_debug(3, r->server, "docroot    : \"%s\"", docroot);
    aclr_debug(3, r->server, "real_uri   : \"%s\"", real_uri);

    if ((rc = ap_discard_request_body(r)) != OK) {
        return rc;
    }

    apr_table_set(r->headers_out, xa_ver_name, ACLR_VERSION);

    if ((r->method_number != M_GET) || (r->header_only)) {
        aclr_debug(2, r->server, "request method is not GET: %s%s",
                   server_name, r->uri);

        return DECLINED;
    }

    if (r->finfo.filetype != APR_REG) {
        aclr_debug(2, r->server, "request file not found "
                   "or is not regular file: %s%s",
                   server_name, r->uri);

        return DECLINED;
    }

    if (cfg->fsize != UNSET && r->finfo.size < cfg->fsize) {
	aclr_debug(2, r->server, "file size %lu < minsize %lu: %s%s",
                   r->finfo.size, cfg->fsize, server_name, r->uri);

	return DECLINED;
    }

    f = r->output_filters;
    do {
        nextf = f->next;
        if (strcmp(f->frec->name, "includes") == 0) {
            aclr_debug(2, r->server, "request uses INCLUDES filter: %s%s",
                       server_name, r->uri);

	    return DECLINED;
	}
	f = nextf;
    } while (f && f != r->proto_output_filters);

    apr_table_set(r->headers_out, xa_redir_name, iredirect);

    /* for CustomLog %b/%B correct logging */
    r->bytes_sent = r->finfo.size;

    r->header_only = 0;
    ap_update_mtime(r, r->finfo.mtime);

    aclr_debug(1, r->server, "request %s%s redirected to %s",
               server_name, r->uri, iredirect);

    return OK;
}


/* configs */

static void *
aclr_create_dir_config(apr_pool_t *p, char *dummy)
{
    aclr_dir_config *cfg;
    cfg = (aclr_dir_config *)apr_palloc(p, sizeof(aclr_dir_config));
    cfg->state = UNSET;
    cfg->redirect_outside_of_docroot = UNSET;
    cfg->fsize = UNSET;
    return (void *)cfg;
}


/* commands */

static const char *
set_aclr_state(cmd_parms *cmd, void *config, int flag)
{
    aclr_dir_config *cfg = (aclr_dir_config *)config;
    cfg->state = (flag ? ACLR_ENABLED : ACLR_DISABLED);
    return NULL;
}

static const char *
set_aclr_outside_of_docroot(cmd_parms *cmd, void *config, int flag)
{
    const char *err = ap_check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;
    aclr_dir_config *cfg = (aclr_dir_config *)config;
    cfg->redirect_outside_of_docroot = (flag ? ACLR_ENABLED : ACLR_DISABLED);
    return NULL;
}

static const char *
set_redirect_min_size(cmd_parms *cmd, void *config, const char *size_str)
{
    aclr_dir_config *cfg = (aclr_dir_config *)config;
    char *endptr;
    long size;

    size = strtol(size_str, &endptr, 10);
    while (apr_isspace(*endptr)) endptr++;
    if (*endptr == '\0' || *endptr == 'b' || *endptr == 'B') {
        ;
    }
    else if (*endptr == 'k' || *endptr == 'K') {
        size *= 1024;
    }
    else if (*endptr == 'm' || *endptr == 'M') {
        size *= 1048576;
    }
    else {
        return apr_pstrcat(cmd->pool, "Invalid size in AccelRedirectSize: ",
                           size_str, NULL);
    }

    cfg->fsize = (size >= 0) ? size : UNSET;
    return NULL;
}

#ifdef DEBUG
static const char *
set_debug_level(cmd_parms *cmd, void *config, const char *arg)
{
    const char *err = ap_check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;
    debuglevel = strtol(arg, NULL, 10);
    return NULL;
}
#endif


/* module info */

static const command_rec aclr_cmds[] =
{
    AP_INIT_FLAG("AccelRedirectSet", set_aclr_state,
        NULL, ACCESS_CONF|RSRC_CONF,
	"Turn X-ACLR-Redirect support On or Off (default Off)"),

    AP_INIT_TAKE1("AccelRedirectSize", set_redirect_min_size,
        NULL, ACCESS_CONF|RSRC_CONF,
	"Minimum size of file for redirect in bytes"),

    AP_INIT_FLAG("AccelRedirectOutsideDocRoot", set_aclr_outside_of_docroot,
        NULL, RSRC_CONF,
	"Allow redirect to outside of DocumentRoot (default Off)"),

#ifdef DEBUG
    AP_INIT_TAKE1("AccelRedirectDebug", set_debug_level,
        NULL, RSRC_CONF,
	"Debug level (0=off, 1=min, 2=mid, 3=max)"),
#endif

    { NULL }
};

static void register_hooks(apr_pool_t *p)
{
    ap_hook_handler(aclr_handler, NULL, NULL, APR_HOOK_REALLY_LAST - 1);
}

module AP_MODULE_DECLARE_DATA aclr_module =
{
    STANDARD20_MODULE_STUFF,
    aclr_create_dir_config,     /* create per-directory config structures */
    NULL,                       /* merge per-directory config structures  */
    NULL,                       /* create per-server config structures    */
    NULL,                       /* merge per-server config structures     */
    aclr_cmds,                  /* command handlers                       */
    register_hooks              /* register hooks                         */
};

