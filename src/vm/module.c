#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/mem.h"
#include "vm/stdlib.h"
#include "vm/vm.h"

#define WC_MODULE_UNKNOWN (-1)
#define WC_MODULE_MAX_BYTES (1024u * 1024u)

static const char TEST_SOURCE[] = "foreign class Echo {\n"
                                  "  construct new(seed) {}\n"
                                  "  foreign value\n"
                                  "  foreign static ping(x)\n"
                                  "  foreign static reenter()\n"
                                  "}\n";

static const char NET_SOURCE[] =
    "foreign class Req {\n"
    "  foreign rawHeaderCount\n"
    "  foreign rawHeaderName(index)\n"
    "  foreign rawHeaderValue(index)\n"
    "  foreign rawQueryCount\n"
    "  foreign rawQueryKey(index)\n"
    "  foreign rawQueryValue(index)\n"
    "  foreign method\n"
    "  foreign path\n"
    "  foreign body\n"
    "  foreign httpVersion\n"
    "  headers {\n"
    "    var result = {}\n"
    "    for (i in 0...rawHeaderCount) {\n"
    "      var key = rawHeaderName(i)\n"
    "      var value = rawHeaderValue(i)\n"
    "      if (result.containsKey(key)) {\n"
    "        result[key] = result[key] + (key == \"cookie\" ? \"; \" : \", \") + value\n"
    "      } else {\n"
    "        result[key] = value\n"
    "      }\n"
    "    }\n"
    "    return result\n"
    "  }\n"
    "  query {\n"
    "    var result = {}\n"
    "    for (i in 0...rawQueryCount) result[rawQueryKey(i)] = rawQueryValue(i)\n"
    "    return result\n"
    "  }\n"
    "}\n"
    "foreign class Res {\n"
    "  foreign send(status, body)\n"
    "  foreign header(name, value)\n"
    "  foreign sent\n"
    "  foreign sseStart(contentType)\n"
    "  foreign sseSend(event, data)\n"
    "  foreign sseClose()\n"
    "}\n"
    "foreign class Server {\n"
    "  foreign static serve(handler)\n"
    "  foreign static stop()\n"
    "  foreign static running\n"
    "}\n";

static const char FS_SOURCE[] = "class Fs {\n"
                                "  foreign static read(path)\n"
                                "  foreign static write(path, contents)\n"
                                "  foreign static exists(path)\n"
                                "  foreign static list(path)\n"
                                "  foreign static isDir(path)\n"
                                "}\n";
#ifdef WC_ENABLE_HTTP_CLIENT
static const char HTTP_SOURCE[] = "class Http {\n"
                                  "  foreign static request(method, url, headers, body)\n"
                                  "  foreign static streamLines(method, url, headers, body)\n"
                                  "  foreign static setTimeout(ms)\n"
                                  "}\n";
#endif
static const char SYS_SOURCE[] = "class Sys {\n"
                                 "  foreign static env(name)\n"
                                 "  foreign static args\n"
                                 "  foreign static clock\n"
                                 "  foreign static sleep(ms)\n"
                                 "  foreign static exit()\n"
                                 "}\n";

static int module_capability(const char *name) {
    if (strcmp(name, "net") == 0) {
        return WC_CAP_NET;
    }
    if (strcmp(name, "fs") == 0) {
        return WC_CAP_FS;
    }
#ifdef WC_ENABLE_HTTP_CLIENT
    if (strcmp(name, "http") == 0) {
        return WC_CAP_NET;
    }
#endif
    if (strcmp(name, "sys") == 0) {
        return WC_CAP_SYS;
    }
    if (strcmp(name, "test") == 0 || strcmp(name, "json") == 0 || strcmp(name, "router") == 0) {
        return 0;
    }
    return WC_MODULE_UNKNOWN;
}

static const char *builtin_source(const char *name) {
    if (strcmp(name, "test") == 0) {
        return TEST_SOURCE;
    }
    if (strcmp(name, "net") == 0) {
        return NET_SOURCE;
    }
    if (strcmp(name, "fs") == 0) {
        return FS_SOURCE;
    }
    if (strcmp(name, "sys") == 0) {
        return SYS_SOURCE;
    }
#ifdef WC_ENABLE_HTTP_CLIENT
    if (strcmp(name, "http") == 0) {
        return HTTP_SOURCE;
    }
#endif
    for (size_t i = 0; i < wc_stdlib_module_count; i++) {
        if (strcmp(name, wc_stdlib_modules[i].name) == 0) {
            return (const char *)wc_stdlib_modules[i].data;
        }
    }
    return NULL;
}

static void module_complete(WrenVM *wren, const char *name, WrenLoadModuleResult result) {
    (void)wren;
    (void)name;
    wc_mem_free(result.userData);
}

static const char *resolve_module(WrenVM *wren, const char *importer, const char *name) {
    (void)wren;
    (void)importer;
    return name;
}

/* A loadable module name is a single path segment: no separators, no parent
 * references, no leading dot. This keeps the flat module_path stub from
 * becoming a traversal primitive; the rooted loader (GAPS G5) replaces it. */
static bool module_name_safe(const char *name) {
    if (name == NULL || name[0] == '\0' || name[0] == '.') {
        return false;
    }
    for (const char *p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    if (strcmp(name, "..") == 0) {
        return false;
    }
    return true;
}

static WrenLoadModuleResult load_module(WrenVM *wren, const char *name) {
    wc_vm *vm = wrenGetUserData(wren);
    WrenLoadModuleResult result = {NULL, NULL, NULL};

    int capability = module_capability(name);
    if (capability == WC_MODULE_UNKNOWN) {
        if (vm->module_path != NULL && module_name_safe(name)) {
            char path[512];
            int written = snprintf(path, sizeof path, "%s/%s.wren", vm->module_path, name);
            if (written > 0 && (size_t)written < sizeof path) {
                FILE *file = fopen(path, "rb");
                if (file != NULL) {
                    fseek(file, 0, SEEK_END);
                    long length = ftell(file);
                    fseek(file, 0, SEEK_SET);
                    if (length >= 0 && length <= (long)WC_MODULE_MAX_BYTES) {
                        char *source = wc_mem_malloc((size_t)length + 1);
                        if (source != NULL) {
                            size_t got = fread(source, 1, (size_t)length, file);
                            if (got == (size_t)length && ferror(file) == 0) {
                                source[got] = '\0';
                                result.source = source;
                                result.userData = source;
                                result.onComplete = module_complete;
                                fclose(file);
                                return result;
                            }
                            wc_mem_free(source);
                        }
                    }
                    fclose(file);
                }
            }
        }
        vm->denied = true;
        return result;
    }

    if (capability != 0 && (vm->capabilities & (uint32_t)capability) == 0) {
        vm->denied = true;
        return result;
    }

    result.source = builtin_source(name);
    return result;
}

void wc_vm_install_modules(WrenConfiguration *config) {
    config->resolveModuleFn = resolve_module;
    config->loadModuleFn = load_module;
}
