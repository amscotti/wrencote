#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wrencote/wrencote.h"

typedef struct options {
    const char *script;
    const char *listen;
    const char *root;
    const char *tls_cert;
    const char *tls_key;
    size_t max_request;
    size_t vm_heap;
    uint32_t capabilities;
    int log_level;
    bool sandbox;
    bool no_listen;
    int app_argc;
    char **app_argv;
} options;

/* Reads the script before the sandbox is applied so it can live anywhere the
 * operator can read, then wc_host_load_source runs it sandboxed. */
static char *read_script_source(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    char *source = malloc((size_t)length + 1);
    if (source == NULL) {
        fclose(file);
        return NULL;
    }
    size_t got = fread(source, 1, (size_t)length, file);
    fclose(file);
    source[got] = '\0';
    return source;
}

static void usage(void) {
    printf("wrencote %s\n"
           "usage:\n"
           "  wrencote run <script.wren> [--listen HOST:PORT] [--allow http,fs,sys]\n"
           "                 [--root DIR] [--tls-cert FILE --tls-key FILE]\n"
           "                 [--max-request BYTES] [--vm-heap BYTES]\n"
           "                 [--log-level 0-3] [--no-listen] [-- <script args>]\n"
           "  wrencote check <script.wren> [--allow ...] [--root DIR]\n"
           "  wrencote --version | --help\n",
           WC_VERSION_STRING);
}

static void log_sink(void *user, int level, const char *line, size_t len) {
    int limit = *(int *)user;
    if (level > limit) {
        return;
    }
    fprintf(stderr, "%.*s", (int)len, line);
    if (len == 0 || line[len - 1] != '\n') {
        fputc('\n', stderr);
    }
}

static bool parse_size(const char *text, size_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *out = (size_t)value;
    return true;
}

static bool add_capabilities(options *opt, const char *list) {
    char buffer[256];
    if (strlen(list) >= sizeof buffer) {
        return false;
    }
    strcpy(buffer, list);
    for (char *token = strtok(buffer, ","); token != NULL; token = strtok(NULL, ",")) {
        if (strcmp(token, "http") == 0 || strcmp(token, "net") == 0) {
            opt->capabilities |= WC_CAP_NET;
        } else if (strcmp(token, "fs") == 0) {
            opt->capabilities |= WC_CAP_FS;
        } else if (strcmp(token, "sys") == 0) {
            opt->capabilities |= WC_CAP_SYS;
        } else if (strcmp(token, "json") != 0 && strcmp(token, "router") != 0) {
            return false;
        }
    }
    return true;
}

static bool parse_options(int argc, char **argv, options *opt) {
    memset(opt, 0, sizeof *opt);
    opt->listen = "127.0.0.1:8080";
    opt->vm_heap = 8u * 1024u * 1024u;
    opt->max_request = 1024u * 1024u;
    opt->log_level = 2;
    opt->sandbox = true;
    opt->script = argv[2];
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            opt->app_argc = argc - i - 1;
            opt->app_argv = &argv[i + 1];
            break;
        } else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            opt->listen = argv[++i];
        } else if (strcmp(argv[i], "--allow") == 0 && i + 1 < argc) {
            if (!add_capabilities(opt, argv[++i])) {
                return false;
            }
        } else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            opt->root = argv[++i];
        } else if (strcmp(argv[i], "--tls-cert") == 0 && i + 1 < argc) {
            opt->tls_cert = argv[++i];
        } else if (strcmp(argv[i], "--tls-key") == 0 && i + 1 < argc) {
            opt->tls_key = argv[++i];
        } else if (strcmp(argv[i], "--max-request") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &opt->max_request)) {
                return false;
            }
        } else if (strcmp(argv[i], "--vm-heap") == 0 && i + 1 < argc) {
            if (!parse_size(argv[++i], &opt->vm_heap)) {
                return false;
            }
        } else if (strcmp(argv[i], "--no-listen") == 0) {
            opt->no_listen = true;
        } else if (strcmp(argv[i], "--no-sandbox") == 0) {
            opt->sandbox = false;
        } else if (strcmp(argv[i], "--sandbox") == 0) {
            opt->sandbox = true;
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            size_t level = 0;
            if (!parse_size(argv[++i], &level) || level > 3) {
                return false;
            }
            opt->log_level = (int)level;
        } else {
            return false;
        }
    }
    if ((opt->capabilities & WC_CAP_FS) != 0 && opt->root == NULL) {
        return false;
    }
    if ((opt->tls_cert == NULL) != (opt->tls_key == NULL)) {
        return false;
    }
    return true;
}

static wc_status make_host(options *opt, wc_host **out) {
    wc_host_config config;
    wc_host_config_init(&config);
    config.capabilities = opt->capabilities;
    config.fs_root = opt->root;
    config.max_body_bytes = opt->max_request;
    config.max_response_bytes = opt->max_request;
    config.vm_heap_bytes = opt->vm_heap;
    config.log = log_sink;
    config.log_user = &opt->log_level;
    return wc_host_new(&config, out);
}

/* Accepts HOST:PORT and [v6-host]:PORT. */
static bool split_listen(const char *listen, char *host, size_t host_size, uint16_t *port) {
    const char *port_text = NULL;
    size_t host_len = 0;
    if (listen[0] == '[') {
        const char *close = strchr(listen, ']');
        if (close == NULL || close[1] != ':') {
            return false;
        }
        host_len = (size_t)(close - listen) - 1;
        port_text = close + 2;
        if (host_len == 0 || host_len >= host_size) {
            return false;
        }
        memcpy(host, listen + 1, host_len);
    } else {
        const char *colon = strrchr(listen, ':');
        if (colon == NULL) {
            return false;
        }
        host_len = (size_t)(colon - listen);
        port_text = colon + 1;
        if (host_len >= host_size) {
            return false;
        }
        memcpy(host, listen, host_len);
    }
    host[host_len] = '\0';
    size_t value = 0;
    if (!parse_size(port_text, &value) || value > 65535) {
        return false;
    }
    *port = (uint16_t)value;
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("wrencote %s\n", WC_VERSION_STRING);
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return 0;
    }
    bool check_only = strcmp(argv[1], "check") == 0;
    if (!check_only && strcmp(argv[1], "run") != 0) {
        usage();
        return 2;
    }
    if (argc < 3) {
        usage();
        return 2;
    }
    options opt;
    if (!parse_options(argc, argv, &opt)) {
        fprintf(stderr, "wrencote: invalid option\n");
        return 2;
    }
    wc_host *host = NULL;
    wc_status status = make_host(&opt, &host);
    if (status != WC_OK) {
        fprintf(stderr, "wrencote: host setup failed (%d)\n", (int)status);
        return 1;
    }
    if (check_only) {
        status = wc_host_check(host, opt.script);
        wc_host_free(host);
        return status == WC_OK ? 0 : 1;
    }
    char host_text[128];
    uint16_t port = 0;
    if (!opt.no_listen && !split_listen(opt.listen, host_text, sizeof host_text, &port)) {
        fprintf(stderr, "wrencote: --listen must be HOST:PORT\n");
        wc_host_free(host);
        return 2;
    }
    if (wc_host_set_args(host, opt.app_argc, (const char *const *)opt.app_argv) != WC_OK) {
        wc_host_free(host);
        return 1;
    }
    if (!opt.no_listen) {
        wc_status served = opt.tls_cert != NULL
                               ? wc_host_serve_tls(host, host_text, port, opt.tls_cert, opt.tls_key)
                               : wc_host_serve(host, host_text, port);
        if (served != WC_OK) {
            fprintf(stderr, "wrencote: could not listen on %s\n", opt.listen);
            wc_host_free(host);
            return 1;
        }
    }
    char *script_source = NULL;
    if (opt.sandbox) {
        /* Read the entry script before the sandbox exists so it may live
         * anywhere readable; its top level then runs under the sandbox. */
        script_source = read_script_source(opt.script);
        if (script_source == NULL) {
            fprintf(stderr, "wrencote: could not read %s\n", opt.script);
            wc_host_free(host);
            return 1;
        }
    }
    status = opt.sandbox ? wc_host_enable_sandbox(host)
                         : WC_OK; /* sandbox before any script code runs */
    if (status != WC_OK) {
        fprintf(stderr, "wrencote: sandbox setup failed (%d)\n", (int)status);
        free(script_source);
        wc_host_free(host);
        return 1;
    }
    status = script_source != NULL ? wc_host_load_source(host, "main", script_source)
                                   : wc_host_load(host, opt.script);
    free(script_source);
    if (status != WC_OK) {
        fprintf(stderr, "wrencote: %s\n", wc_host_last_error(host));
        wc_host_free(host);
        return 1;
    }
    if (opt.no_listen) {
        wc_host_free(host);
        return 0;
    }
    if (!opt.no_listen) {
        fprintf(stderr, "wrencote: listening on %s%s\n", opt.tls_cert != NULL ? "https://" : "",
                opt.listen);
    }
    status = wc_host_run(host);
    wc_host_free(host);
    return status == WC_OK ? 0 : 1;
}
