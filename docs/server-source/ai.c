/*
 * ai.c — HNN CN ET Server (Termux / Linux / Windows)
 * Copyright (c) 2026 ETC · MIT License
 * 纯 C 单文件：菜单(1首页/2市场/3远程/4改端口/7密钥开关/8强制密钥/0退出) + OpenAI 兼容 API
 * 推理: 调用 llm_demo (MNN, 与手机版同款模型)；下载: 系统 curl 走 ModelScope 官方 API
 * 编译: gcc/clang 各平台直接编；Windows 编 DLL 核心 + exe 启动器
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <strings.h>
#else
#include <io.h>
#endif
#include <stdarg.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#else
#include "win_dirent.h"
#endif
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <direct.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define close closesocket
#define usleep(us) Sleep((us) / 1000)
#define mkdir(p, m) _mkdir(p)
#endif

#ifdef HNN_CORE_DLL
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

#define VERSION "1.3.4"
#define MAXBUF 65536
#define MAXPATH 1024

static char MODEL_DIR[MAXPATH];   /* 模型根目录 */
static char CONF_PATH[MAXPATH];
static char LLM_DEMO_PATH[MAXPATH];
static char API_KEY[64];
static int AUTH_ON = 1;
static int FORCE_AUTH = 0;              /* 1: 无论 AUTH_ON 如何，所有请求必须带 Bearer */
static int API_PORT = 8080;
static char REMOTE_URL[512];
static char ACTIVE_MODEL_DIR[MAXPATH] = ""; /* 菜单1选定模型后的绝对目录，请求直接复用 */
static char ACTIVE_MODEL[256] = "";          /* 活动模型基目录名（横幅/状态展示） */
static char SYSTEM_PROMPT[2048] = "";       /* system.ini 自定义系统提示词 */
static char EXE_DIR[MAXPATH] = ".";          /* 可执行文件所在目录（用于找 system.ini） */
static char HOME_DIR[MAXPATH] = ".";         /* 用户主目录 */

static void cprint(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); fflush(stdout);
}
static void trim(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0;
    while (*s == ' ') memmove(s, s+1, strlen(s));
}
static char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
#ifdef _WIN32
    const char *t = strrchr(p, '\\');
    if (!s || (t && t > s)) s = t;
#endif
    return (char *)(s ? s + 1 : p);
}

/* ---------- 配置 ---------- */
static void default_key(void) {
    /* 大写 A-Z + 小写 a-z + 数字 0-9 混合字符集；前缀 etc-，其后 16 位随机 */
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char digits[] = "0123456789";
    int n = (int)sizeof(chars) - 1;
    sprintf(API_KEY, "etc-");
    for (int i = 4; i < 20; i++) API_KEY[i] = chars[rand() % n];
    /* 强制第1位随机字符=大写字母、第2位=数字，保证不会出现纯数字/纯字母 */
    API_KEY[4] = upper[rand() % 26];
    API_KEY[5] = digits[rand() % 10];
    API_KEY[20] = 0;
}
static void load_conf(void) {
    FILE *f = fopen(CONF_PATH, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            trim(line);
            if (!strncmp(line, "port=", 5)) API_PORT = atoi(line + 5);
            else if (!strncmp(line, "auth=", 5)) AUTH_ON = atoi(line + 5);
            else if (!strncmp(line, "force=", 6)) FORCE_AUTH = atoi(line + 6);
            else if (!strncmp(line, "key=", 4) && strlen(line + 4) >= 6) strncpy(API_KEY, line + 4, 63);
        }
        fclose(f);
    }
    if (!API_KEY[0]) default_key();
}
static void save_conf(void) {
    FILE *f = fopen(CONF_PATH, "w");
    if (f) {
        fprintf(f, "port=%d\n", API_PORT);
        fprintf(f, "auth=%d\n", AUTH_ON);
        fprintf(f, "force=%d\n", FORCE_AUTH);
        fprintf(f, "key=%s\n", API_KEY);
        fclose(f);
    }
}

/* ---------- system.ini 自定义系统提示词 ---------- */
/* 解析一行：system 后跟可选空白再 =，取成对引号内内容（支持直引号与中文引号） */
static void parse_system_line(char *line, char *out, size_t outsz) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "system", 6)) return;
    p += 6;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    /* 直双引号 "..." */
    if (*p == '"') {
        p++;
        size_t j = 0;
        while (*p && *p != '"' && j + 1 < outsz) out[j++] = *p++;
        out[j] = 0;
        return;
    }
    /* 中文引号 “...” (UTF-8: e2 80 9c / e2 80 9d) */
    if ((unsigned char)p[0] == 0xe2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x9c) {
        p += 3;
        size_t j = 0;
        while (*p && j + 1 < outsz) {
            if ((unsigned char)p[0] == 0xe2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x9d) break;
            out[j++] = *p++;
        }
        out[j] = 0;
        return;
    }
}
/* 每次本地推理前重新读取，改完下次即生效；找不到/解析不到就置空，绝不报错 */
static void load_system_prompt(void) {
    SYSTEM_PROMPT[0] = 0;
    const char *cands[3]; int nc = 0;
    cands[nc++] = "./system.ini";
    char p2[MAXPATH]; snprintf(p2, sizeof p2, "%s/system.ini", EXE_DIR); cands[nc++] = p2;
#ifdef _WIN32
    char p3[MAXPATH]; snprintf(p3, sizeof p3, "%s\\.hnn\\system.ini", HOME_DIR);
#else
    char p3[MAXPATH]; snprintf(p3, sizeof p3, "%s/.hnn/system.ini", HOME_DIR);
#endif
    cands[nc++] = p3;
    for (int i = 0; i < nc; i++) {
        FILE *f = fopen(cands[i], "r");
        if (!f) continue;
        char line[4096];
        while (fgets(line, sizeof line, f)) {
            char val[2048];
            parse_system_line(line, val, sizeof val);
            if (val[0]) {
                snprintf(SYSTEM_PROMPT, sizeof SYSTEM_PROMPT, "%s", val);
                fclose(f);
                return; /* 取到即返回 */
            }
        }
        fclose(f);
    }
}

/* ---------- 子进程推理（MNN llm_demo 一次性模式） ---------- */
static char *run_llm(const char *model_dir, const char *prompt, int max_tokens) {
    char pf[MAXPATH]; snprintf(pf, sizeof pf, "%s/.ai_prompt.txt", MODEL_DIR);
    FILE *p = fopen(pf, "w");
    if (!p) return strdup("写入 prompt 失败");
    fputs(prompt, p); fclose(p);
    char cmd[MAXPATH * 2 + 256];
#ifndef _WIN32
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s/config.json\" \"%s\" %d 2>/dev/null",
             LLM_DEMO_PATH, model_dir, pf, max_tokens);
#else
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s/config.json\" \"%s\" %d 2>NUL",
             LLM_DEMO_PATH, model_dir, pf, max_tokens);
#endif
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("无法启动 llm_demo");
    /* 循环读取直到 EOF，缓冲动态增长，不再用固定 MAXBUF 截断长回答 */
    size_t cap = 65536, len = 0;
    char *raw = malloc(cap);
    if (!raw) { pclose(fp); return strdup("内存不足"); }
    size_t n;
    while ((n = fread(raw + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (cap - len < 8192) {
            cap *= 2;
            char *nr = realloc(raw, cap);
            if (!nr) break;
            raw = nr;
        }
        raw[len] = 0;
    }
    pclose(fp);
    raw[len] = 0;
    /* 过滤 llm_demo 日志行，取答案（手动按行切分，避免 strtok 静态状态跨线程竞争） */
    static const char *skip[] = {"config path", "The device", "main,", "prompt file", "#",
        "prompt tokens", "vision time", "vision speed", "audio ", "pixels", "MP/s", "decode tokens",
        "prefill time", "decode time", "sample time", "prefill speed", "decode speed",
        "sample speed", NULL};
    size_t outcap = 256;
    char *out = malloc(outcap); out[0] = 0;
    char *start = raw;
    while (start && *start) {
        char *nl = strchr(start, '\n');
        if (nl) *nl = 0;
        char *ls = start; while (*ls == ' ') ls++;
        int sk = 0;
        for (int i = 0; skip[i]; i++)
            if (!strncmp(ls, skip[i], strlen(skip[i]))) { sk = 1; break; }
        if (!sk && *ls) {
            size_t need = strlen(out) + strlen(ls) + 2;
            if (need + 1 > outcap) {
                while (outcap < need + 1) outcap *= 2;
                out = realloc(out, outcap);
            }
            strcat(out, ls); strcat(out, "\n");
        }
        if (!nl) break;
        start = nl + 1;
    }
    if (!out[0]) {
        strncpy(out, raw, 399); out[399] = 0;
    }
    free(raw);
    return out;
}

/* ---------- HTTP 转发（远程模式） ---------- */
static char *remote_call(const char *body) {
    /* 请求体写入临时文件，curl 用 --data @"file" 读取，避免 sh/cmd 引号差异 */
    char bp[MAXPATH]; snprintf(bp, sizeof bp, "%s/.ai_remote_body.json", MODEL_DIR);
    FILE *bf = fopen(bp, "w");
    if (bf) { fputs(body, bf); fclose(bf); }
    char cmd[2048];
#ifdef _WIN32
    snprintf(cmd, sizeof cmd,
             "curl -sL -m 600 -X POST \"%s/v1/chat/completions\" -H \"Content-Type: application/json\" --data @\"%s\"",
             REMOTE_URL, bp);
#else
    snprintf(cmd, sizeof cmd,
             "curl -sL -m 600 -X POST \"%s/v1/chat/completions\" -H 'Content-Type: application/json' --data '@%s'",
             REMOTE_URL, bp);
#endif
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("转发失败");
    char *buf = malloc(MAXBUF); size_t n = fread(buf, 1, MAXBUF - 1, fp); buf[n] = 0;
    pclose(fp);
    return buf;
}

/* ---------- API 处理 ---------- */
static char *extract_field(const char *body, const char *name, int quoted) {
    char key[64]; snprintf(key, sizeof key, "\"%s\"", name);
    const char *p = strstr(body, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p && (*p == ' ' || *p == ':')) p++;
    if (quoted && *p == '"') {
        p++; const char *e = strchr(p, '"');
        if (!e) return NULL;
        char *v = malloc(e - p + 1); memcpy(v, p, e - p); v[e - p] = 0; return v;
    }
    const char *e = strchr(p, ',');
    if (!e) e = p + strlen(p);
    char *v = malloc(e - p + 1); memcpy(v, p, e - p); v[e - p] = 0; return v;
}
/* ---------- 模型递归扫描（含 config.json 的目录） ---------- */
#define MAX_MODELS 64
typedef struct {
    char dir[MAXPATH];   /* 含 config.json 的模型目录绝对路径 */
    char name[256];      /* 基目录名 */
} ModelEntry;
static ModelEntry g_models[MAX_MODELS];
static int g_model_count = 0;
static int g_models_scanned = 0;

static void scan_models_rec(const char *base, int depth) {
    if (depth > 6 || g_model_count >= MAX_MODELS) return;
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (!strcmp(e->d_name, "blobs")) continue;          /* HF 缓存指针目录 */
        if (!strncmp(e->d_name, "models--", 7)) continue;     /* HF modelers 缓存 */
        char full[MAXPATH]; snprintf(full, sizeof full, "%s/%s", base, e->d_name);
        struct stat st;
        if (stat(full, &st)) continue;
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
        if (!S_ISDIR(st.st_mode)) continue;
        char cfg[MAXPATH]; snprintf(cfg, sizeof cfg, "%s/config.json", full);
        if (!stat(cfg, &st)) {
            if (g_model_count < MAX_MODELS) {
                snprintf(g_models[g_model_count].dir, MAXPATH, "%s", full);
                snprintf(g_models[g_model_count].name, 256, "%s", base_name(full));
                g_model_count++;
            }
            continue; /* 模型目录内部不再下钻 */
        }
        scan_models_rec(full, depth + 1);
    }
    closedir(d);
}
static void scan_models(void) {
    g_model_count = 0;
    scan_models_rec(MODEL_DIR, 0);
    g_models_scanned = 1;
}
/* 归一化：小写 + 去掉 - _ 空格 / */
static void normalize_name(const char *in, char *out, size_t outsz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outsz; i++) {
        char c = in[i];
        if (c == '-' || c == '_' || c == ' ' || c == '/') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = 0;
}
/* 忽略大小写子串包含 */
static int contains_ci(const char *hay, const char *needle) {
    if (!*needle) return 0;
    char h[1024], n[256]; size_t i;
    for (i = 0; hay[i] && i < sizeof h - 1; i++) h[i] = (char)((hay[i] >= 'A' && hay[i] <= 'Z') ? hay[i] - 'A' + 'a' : hay[i]);
    h[i] = 0;
    for (i = 0; needle[i] && i < sizeof n - 1; i++) n[i] = (char)((needle[i] >= 'A' && needle[i] <= 'Z') ? needle[i] - 'A' + 'a' : needle[i]);
    n[i] = 0;
    return strstr(h, n) != NULL;
}

/* 递归解析模型名 → 模型目录。匹配优先级：
   ① 基目录名/末段忽略大小写完全相等  ② 归一化后相等
   ③ 忽略大小写子串包含                ④ 权重文件名(llm.mnn 等)反查所属目录 */
static int find_model(const char *name, char *out) {
    if (!name || !name[0]) return 0;
    if (!g_models_scanned) scan_models();
    if (g_model_count == 0) return 0;
    const char *tail = strrchr(name, '/'); tail = tail ? tail + 1 : name;
    /* ① 完全相等（忽略大小写） */
    for (int i = 0; i < g_model_count; i++)
        if (!strcasecmp(g_models[i].name, tail)) { snprintf(out, MAXPATH, "%s", g_models[i].dir); return 1; }
    /* ② 归一化后相等 */
    char nname[256]; normalize_name(name, nname, sizeof nname);
    for (int i = 0; i < g_model_count; i++) {
        char nm[256]; normalize_name(g_models[i].name, nm, sizeof nm);
        if (!strcmp(nm, nname)) { snprintf(out, MAXPATH, "%s", g_models[i].dir); return 1; }
    }
    /* ③ 忽略大小写子串 */
    for (int i = 0; i < g_model_count; i++)
        if (contains_ci(g_models[i].name, tail)) { snprintf(out, MAXPATH, "%s", g_models[i].dir); return 1; }
    /* ④ 权重文件名兜底（llm.mnn、model.mnn 等 mnn/weight 文件反查所属目录） */
    if (strstr(name, ".mnn") || strstr(name, ".weight")) {
        const char *bn = base_name(name);
        for (int i = 0; i < g_model_count; i++) {
            char p[MAXPATH]; snprintf(p, sizeof p, "%s/%s", g_models[i].dir, bn);
            struct stat st;
            if (!stat(p, &st)) { snprintf(out, MAXPATH, "%s", g_models[i].dir); return 1; }
        }
    }
    return 0;
}

static int find_first_model(char *out) {
    if (!g_models_scanned) scan_models();
    if (g_model_count == 0) return 0;
    snprintf(out, MAXPATH, "%s", g_models[0].dir);
    return 1;
}

static void list_models(void) {
    scan_models(); /* 每次进菜单1重新扫描，市场新下载可即时列出 */
    if (g_model_count == 0) { cprint("  还没有已下载的模型，请先到市场下载。\n"); return; }
    for (int i = 0; i < g_model_count; i++) cprint("  %d. %s\n", i + 1, g_models[i].name);
}

/* ---------- HTTP server ---------- */
/* 统一响应发送：所有响应都带 CORS 头 */
static void http_send(int cli, const char *status, const char *ctype, const char *body) {
    char hdr[1024];
    int len = (int)strlen(body);
    snprintf(hdr, sizeof hdr,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type, x-api-key, anthropic-version\r\n"
        "Connection: close\r\n\r\n",
        status, ctype, len);
    send(cli, hdr, (int)strlen(hdr), 0);
    send(cli, body, len, 0);
}
/* 鉴权校验：FORCE_AUTH 或 AUTH_ON 任一开启则必须带正确 Bearer；否则放行 */
static int check_auth(int cli, const char *req) {
    if (!AUTH_ON && !FORCE_AUTH) return 1;
    const char *auth = strstr(req, "Authorization: Bearer ");
    if (!auth) {
        http_send(cli, "401 Unauthorized", "application/json",
                  "{\"error\":{\"message\":\"missing or invalid bearer token\",\"type\":\"authentication_error\"}}");
        return 0;
    }
    auth += strlen("Authorization: Bearer ");
    const char *e = strchr(auth, '\r'); if (!e) e = auth + strlen(auth);
    char key[64]; size_t k = (size_t)(e - auth); if (k > 63) k = 63;
    memcpy(key, auth, k); key[k] = 0;
    if (strcmp(key, API_KEY)) {
        http_send(cli, "401 Unauthorized", "application/json",
                  "{\"error\":{\"message\":\"invalid bearer token\",\"type\":\"authentication_error\"}}");
        return 0;
    }
    return 1;
}
/* 本地模型列表 → OpenAI 兼容 JSON，动态扩容（递归扫描结果） */
static char *build_models_json(void) {
    scan_models();
    size_t cap = 2048;
    char *j = malloc(cap);
    strcpy(j, "{\"object\":\"list\",\"data\":[");
    int first = 1;
    long long created = (long long)time(NULL);
    for (int i = 0; i < g_model_count; i++) {
        char item[640];
        snprintf(item, sizeof item,
            "%s{\"id\":\"%s\",\"object\":\"model\",\"created\":%lld,\"owned_by\":\"ETC\"}",
            first ? "" : ",", g_models[i].name, created);
        size_t need = strlen(j) + strlen(item) + 8;
        while (cap < need) { cap *= 2; j = realloc(j, cap); }
        strcat(j, item);
        first = 0;
    }
    strcat(j, "]}");
    return j;
}
/* 远程 GET /v1/models 转发，原样返回 */
static char *remote_get_models(void) {
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "curl -sL -m 30 '%s/v1/models'", REMOTE_URL);
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("{\"object\":\"list\",\"data\":[]}");
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap); size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (cap - len < 4096) { cap *= 2; buf = realloc(buf, cap); }
        buf[len] = 0;
    }
    pclose(fp);
    buf[len] = 0;
    return buf;
}
static void handle_client(int cli) {
    char req[MAXBUF]; ssize_t r = recv(cli, req, sizeof req - 1, 0);
    if (r <= 0) { close(cli); return; }
    req[r] = 0;
    char method[16] = "", path[256] = "";
    if (sscanf(req, "%15s %255s", method, path) < 2) {
        http_send(cli, "400 Bad Request", "application/json", "{\"error\":\"bad request\"}");
        close(cli); return;
    }
    /* OPTIONS 预检：任意路径都返回 200 + CORS */
    if (!strcmp(method, "OPTIONS")) {
        http_send(cli, "200 OK", "text/plain", "");
        close(cli); return;
    }
    /* GET /v1/models （兼容 /models） */
    if (!strcmp(method, "GET")) {
        if (!strcmp(path, "/v1/models") || !strcmp(path, "/models")) {
            if (!check_auth(cli, req)) { close(cli); return; }
            char *body = REMOTE_URL[0] ? remote_get_models() : build_models_json();
            http_send(cli, "200 OK", "application/json", body);
            free(body);
            close(cli); return;
        }
        http_send(cli, "404 Not Found", "application/json", "{\"error\":{\"message\":\"not found\"}}");
        close(cli); return;
    }
    /* POST /v1/chat/completions （兼容 /chat/completions） */
    if (!strcmp(method, "POST")) {
        if (strcmp(path, "/v1/chat/completions") && strcmp(path, "/chat/completions")) {
            http_send(cli, "404 Not Found", "application/json", "{\"error\":{\"message\":\"not found\"}}");
            close(cli); return;
        }
        char *hdr_end = strstr(req, "\r\n\r\n");
        if (!hdr_end) {
            http_send(cli, "400 Bad Request", "application/json", "{\"error\":\"bad request\"}");
            close(cli); return;
        }
        char *body = hdr_end + 4;
        if (!check_auth(cli, req)) { close(cli); return; }
        char *model = extract_field(body, "model", 1);
        char *content = extract_field(body, "content", 1);
        if (!content) {
            http_send(cli, "400 Bad Request", "application/json", "{\"error\":\"缺少 messages[].content\"}");
            close(cli); return;
        }
        char *ans;
        const char *used_model_name = "HNN CN ET";
        if (REMOTE_URL[0]) {
            ans = remote_call(body);
        } else {
            char mdir[MAXPATH]; mdir[0] = 0;
            int resolved = 0;
            /* 1) 请求里带 model 名 → 尝试递归解析 */
            if (model && model[0] && find_model(model, mdir)) {
                resolved = 1;
                /* 解析到不同目录则切换活动目录 */
                if (strcmp(mdir, ACTIVE_MODEL_DIR)) {
                    snprintf(ACTIVE_MODEL_DIR, sizeof ACTIVE_MODEL_DIR, "%s", mdir);
                    snprintf(ACTIVE_MODEL, sizeof ACTIVE_MODEL, "%s", base_name(mdir));
                }
            }
            /* 2) 解析不到：沿用当前活动目录（不静默 404） */
            if (!resolved && ACTIVE_MODEL_DIR[0]) {
                snprintf(mdir, sizeof mdir, "%s", ACTIVE_MODEL_DIR);
                resolved = 1;
            }
            /* 3) 还没有：回退第一个模型 */
            if (!resolved && find_first_model(mdir)) {
                snprintf(ACTIVE_MODEL_DIR, sizeof ACTIVE_MODEL_DIR, "%s", mdir);
                snprintf(ACTIVE_MODEL, sizeof ACTIVE_MODEL, "%s", base_name(mdir));
                resolved = 1;
            }
            if (!resolved) {
                http_send(cli, "404 Not Found", "application/json", "{\"error\":\"未找到已下载模型\"}");
                close(cli); return;
            }
            used_model_name = base_name(mdir);
            int mt = 512;
            char *mtf = extract_field(body, "max_tokens", 0);
            if (mtf) { mt = atoi(mtf); free(mtf); }
            /* 每次本地推理前重读 system.ini，拼上系统提示段（空行分隔） */
            load_system_prompt();
            char *final_prompt;
            if (SYSTEM_PROMPT[0]) {
                final_prompt = malloc(strlen(SYSTEM_PROMPT) + strlen(content) + 8);
                sprintf(final_prompt, "%s\n\n%s", SYSTEM_PROMPT, content);
            } else {
                final_prompt = strdup(content);
            }
            ans = run_llm(mdir, final_prompt, mt);
            free(final_prompt);
        }
        if (model) free(model);
        if (content) free(content);
        char *esc = strdup(ans);
        /* 简易 JSON 转义（双引号/反斜杠/换行） */
        size_t escap = strlen(esc) * 6 + 1;
        char *tmp = malloc(escap); char *d = tmp;
        for (char *s = esc; *s; s++) {
            if (*s == '"') { *d++ = '\\'; *d++ = '"'; }
            else if (*s == '\\') { *d++ = '\\'; *d++ = '\\'; }
            else if (*s == '\n') { *d++ = '\\'; *d++ = 'n'; }
            else if (*s == '\r') { *d++ = '\\'; *d++ = 'r'; }
            else *d++ = *s;
        }
        *d = 0;
        size_t rescap = strlen(tmp) + 320;
        char *resp = malloc(rescap);
        snprintf(resp, rescap,
            "{\"id\":\"chatcmpl\",\"object\":\"chat.completion\",\"model\":\"%s\","
            "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}]}",
            used_model_name, tmp);
        free(tmp); free(esc); free(ans);
        http_send(cli, "200 OK", "application/json", resp);
        free(resp);
        close(cli); return;
    }
    /* 其余方法 → 405 */
    http_send(cli, "405 Method Not Allowed", "application/json", "{\"error\":\"method not allowed\"}");
    close(cli);
}
static void *conn_thread(void *arg) {
    int cli = (int)(intptr_t)arg;
    handle_client(cli);
    return NULL;
}
static volatile int running = 1;
static void on_sig(int s) { (void)s; running = 0; }
static int start_api(int port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)port); sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&sa, sizeof sa) < 0) { cprint("  端口 %d 绑定失败\n", port); close(srv); return 0; }
    listen(srv, 8);
    char host[128] = "127.0.0.1";
    /* 探测局域网 IP（尽力而为） */
    FILE *f = popen("hostname -I 2>/dev/null || ip addr show 2>/dev/null | grep 'inet ' | grep -v 127.0.0.1 | head -1", "r");
    if (f) {
        char b[256]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; pclose(f);
        char *tok = strtok(b, " "); if (tok) { trim(tok); if (strncmp(tok, "169.254", 7) && strcmp(tok, "127.0.0.1")) strncpy(host, tok, 127); }
    }
    cprint("\n==========================================================\n");
    cprint("  HNN CN ET Server v%s  (Copyright 2026 ETC)\n", VERSION);
    cprint("  ✅ API 服务运行中（常驻，不会关闭）\n");
    cprint("  📛 模型名称 : %s\n", ACTIVE_MODEL[0] ? ACTIVE_MODEL : "(未指定，自动选第一个)");
    cprint("  🔑 API 密钥 : %s%s%s\n", API_KEY,
           FORCE_AUTH ? "（强制）" : (AUTH_ON ? "" : "（未启用）"),
           FORCE_AUTH ? "" : "");
    cprint("  🔐 密钥开关 : %s\n", AUTH_ON ? "开" : "关");
    cprint("  🔒 强制密钥 : %s\n", FORCE_AUTH ? "开（本机/局域网都要带密钥）" : "关");
    cprint("  🌐 本机地址 : http://127.0.0.1:%d/v1/chat/completions\n", port);
    cprint("  🌐 局域网   : http://%s:%d/v1/chat/completions\n", host, port);
    cprint("  💡 想固定人设/规则：编辑当前目录 system.ini，在 system=\"...\" 引号内写提示语，\n");
    cprint("     下次对话自动生效，不用重启。\n");
    cprint("  按 Ctrl+C 停止服务\n");
    cprint("==========================================================\n");
    signal(SIGINT, on_sig);
    while (running) {
        struct sockaddr_in ca; socklen_t cl = sizeof ca;
        int cli = accept(srv, (struct sockaddr *)&ca, &cl);
        if (cli < 0) { if (running) usleep(50000); continue; }
        pthread_t t; pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)cli);
        pthread_detach(t);
    }
    close(srv);
    return 1;
}

/* ---------- 市场（ModelScope 官方 API） ---------- */
static void market(void) {
    char cmd[2048];
    snprintf(cmd, sizeof cmd,
        "curl -sL -m 60 'https://modelscope.cn/api/v1/models/MNN/Qwen2.5-0.5B-Instruct-MNN/repo/files?Recursive=1'");
    cprint("  该功能需联网拉取模型清单，正在初始化…\n");
    cprint("  与手机版完全一致的模型市场：请直接输入模型名（如 Qwen3-4B-MNN）或退出\n");
    cprint("  下载：输入 模型名 后回车（走 ModelScope 官方 API 直链）\n");
    fflush(stdout);
}

/* ---------- 主菜单 ---------- */
static void banner(void) {
    cprint("\n==========================================================\n");
    cprint("  HNN CN ET · 模型服务端  (Copyright 2026 ETC)\n");
    cprint("  与手机版完全相同的模型市场 · MNN 本地推理 · OpenAI 兼容 API\n");
    cprint("----------------------------------------------------------\n");
    cprint("  新手三步：\n");
    cprint("   1) 菜单 1 选一个已下载好的模型，启动服务；\n");
    cprint("   2) 启动后屏幕上会显示 API 地址、密钥、模型名，复制下来；\n");
    cprint("   3) 填到第三方 AI 客户端的 Base URL / API Key / 模型名，就能聊天。\n");
    cprint("==========================================================\n");
}
DLLEXPORT int ai_server_main(int argc, char **argv) {
    srand((unsigned)time(NULL));
    /* 路径：优先程序同目录（Termux $PREFIX/bin 时模型放 ~/.hnn/models） */
#ifdef _WIN32
    const char *home = getenv("HNN_HOME") ? getenv("HNN_HOME") : getenv("USERPROFILE");
#else
    const char *home = getenv("HNN_HOME") ? getenv("HNN_HOME") : getenv("HOME");
#endif
    snprintf(MODEL_DIR, sizeof MODEL_DIR, "%s/.hnn/models", home ? home : ".");
    snprintf(HOME_DIR, sizeof HOME_DIR, "%s", home ? home : ".");
#ifdef _WIN32
    _mkdir(MODEL_DIR);
#else
    mkdir(MODEL_DIR, 0755);
#endif
    char bin[MAXPATH];
    snprintf(bin, sizeof bin, "%s/.hnn/bin", home ? home : ".");
#ifdef _WIN32
    _mkdir(bin);
#else
    mkdir(bin, 0755);
#endif
    snprintf(CONF_PATH, sizeof CONF_PATH, "%s/.hnn/ai.conf", home ? home : ".");
    /* 记录可执行文件目录（用于查找 system.ini） */
    snprintf(EXE_DIR, sizeof EXE_DIR, "%s", argv[0][0] ? argv[0] : ".");
    {
        char *es = strrchr(EXE_DIR, '/');
#ifdef _WIN32
        char *eb = strrchr(EXE_DIR, '\\');
        if (!es || (eb && eb > es)) es = eb;
#endif
        if (es) *es = 0; else strcpy(EXE_DIR, ".");
    }
    /* llm_demo 位置：按平台逐个探测候选，命中即写入 LLM_DEMO_PATH */
#ifdef _WIN32
    /* Windows: ① %USERPROFILE%\.hnn\bin\llm_demo.exe → ② 程序同目录 llm_demo.exe → ③ 程序同目录 bin\llm_demo.exe */
    snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\.hnn\\bin\\llm_demo.exe", home ? home : ".");
    if (access(LLM_DEMO_PATH, X_OK)) {
        char exe_dir[MAXPATH];
        snprintf(exe_dir, sizeof exe_dir, "%s", argv[0][0] ? argv[0] : ".");
        char *slash = strrchr(exe_dir, '/');
        char *bs = strrchr(exe_dir, '\\');
        if (!slash || (bs && bs > slash)) slash = bs;
        if (slash) *slash = 0; else strcpy(exe_dir, ".");
        snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\llm_demo.exe", exe_dir);
        if (access(LLM_DEMO_PATH, X_OK)) {
            snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\bin\\llm_demo.exe", exe_dir);
            if (access(LLM_DEMO_PATH, X_OK))
                snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\llm_demo.exe", exe_dir);
        }
    }
#else
    /* Linux/Termux: ① ~/.hnn/bin/llm_demo → ② 程序同目录 llm_demo */
    snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s/.hnn/bin/llm_demo", home ? home : ".");
    if (access(LLM_DEMO_PATH, X_OK)) {
        snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s/llm_demo", argv[0][0] ? argv[0] : ".");
        char *slash = strrchr(LLM_DEMO_PATH, '/');
        if (slash) *slash = 0; else strcpy(LLM_DEMO_PATH, ".");
    }
#endif
    load_conf();
    banner();
    while (1) {
        cprint("\n  请选择：\n");
        cprint("    1 = 选模型并启动服务（已下载的模型都在这）\n");
        cprint("    2 = 模型市场（联网下载新模型）\n");
        cprint("    3 = 远程模式（转发到别的服务地址）\n");
        cprint("    4 = 改端口（当前 %d）\n", API_PORT);
        cprint("    7 = 密钥开关（当前：%s；关了后调用不用带密钥）\n", AUTH_ON ? "开" : "关");
        cprint("    8 = 强制密钥（当前：%s；开了后不管 7 怎么设都必须带密钥）\n", FORCE_AUTH ? "开" : "关");
        cprint("    0 = 退出\n");
        cprint("  请输入数字并回车: ");
        char line[64]; fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        trim(line);
        if (!strcmp(line, "1")) {
            cprint("  💡 想固定人设/规则：编辑当前目录 system.ini，在 system=\"...\" 引号内写提示语，下次对话自动生效，不用重启。\n");
            cprint("\n  ── 已下载好的模型 ──\n");
            list_models();
            cprint("  输入模型名称回车: ");
            char name[256];
            if (!fgets(name, sizeof name, stdin)) break;
            trim(name);
            if (!name[0]) continue;
            char mdir[MAXPATH];
            if (!find_model(name, mdir)) { cprint("  未找到该模型\n"); continue; }
            snprintf(ACTIVE_MODEL, sizeof ACTIVE_MODEL, "%s", base_name(mdir));
            snprintf(ACTIVE_MODEL_DIR, sizeof ACTIVE_MODEL_DIR, "%s", mdir); /* 缓存绝对路径，请求直接复用 */
            cprint("  正在加载模型，首次加载可能需要数十秒…\n");
            start_api(API_PORT);
            running = 1;
            cprint("\n  服务已停止，返回主菜单\n");
        } else if (!strcmp(line, "2")) {
            market();
            cprint("  输入模型名下载（如 Qwen2.5-0.5B-Instruct-MNN），回车开始：\n");
            cprint("  > ");
            char name[256]; fflush(stdout);
            if (!fgets(name, sizeof name, stdin)) break;
            trim(name);
            if (!name[0]) continue;
            char mdir[MAXPATH]; snprintf(mdir, sizeof mdir, "%s/%s", MODEL_DIR, name);
            mkdir(mdir, 0755);
            char cmd[4096];
            snprintf(cmd, sizeof cmd,
                "curl -sL -m 60 'https://modelscope.cn/api/v1/models/MNN/%s/repo/files?Recursive=1'", name);
            FILE *f = popen(cmd, "r");
            char jb[MAXBUF]; size_t n = fread(jb, 1, sizeof jb - 1, f); jb[n] = 0; pclose(f);
            /* 解析 Path 字段并下载每个文件 */
            int count = 0;
            char *p = jb;
            while ((p = strstr(p, "\"Path\":"))) {
                p += 7; while (*p && (*p == ' ' || *p == '"')) p++;
                char path[512]; char *e = strchr(p, '"');
                if (!e) break;
                size_t k = (size_t)(e - p); if (k > 511) k = 511;
                memcpy(path, p, k); path[k] = 0;
                if (strstr(path, ".gitattributes") || strstr(path, "README")) { p = e; continue; }
                cprint("  -> %s\n", path);
                char durl[2048]; char outp[MAXPATH];
                snprintf(durl, sizeof durl, "https://modelscope.cn/api/v1/models/MNN/%s/repo?FilePath=%s", name, path);
                snprintf(outp, sizeof outp, "%s/%s", mdir, path);
                char dcmd[4096];
                snprintf(dcmd, sizeof dcmd, "mkdir -p '%s' && curl -sL -m 1800 '%s' -o '%s'", mdir, durl, outp);
                system(dcmd);
                count++;
                p = e;
            }
            cprint("  完成：%s 已下载（%d 个文件），可到首页选择启动。\n", name, count);
        } else if (!strcmp(line, "3")) {
            cprint("  本机与局域网设备可直接用 http://<本机IP>:%d 访问，无需额外设置。\n", API_PORT);
            cprint("  如需外网访问，请自行使用 frpc 等内网穿透工具；本服务端不内置穿透。\n");
            cprint("  输入手机服务版 API 地址（如 http://192.168.1.5:8080 或穿透地址）: ");
            char url[512]; fflush(stdout);
            if (!fgets(url, sizeof url, stdin)) break;
            trim(url);
            if (strncmp(url, "http", 4)) { cprint("  地址需以 http:// 开头\n"); continue; }
            strncpy(REMOTE_URL, url, sizeof REMOTE_URL - 1);
            ACTIVE_MODEL_DIR[0] = 0; /* 远程模式不使用本地模型路径 */
            snprintf(ACTIVE_MODEL, sizeof ACTIVE_MODEL, "%s (远程)", REMOTE_URL);
            cprint("  已设置远程转发：%s（启动服务后转发调用）\n", REMOTE_URL);
            start_api(API_PORT);
            running = 1;
        } else if (!strcmp(line, "4")) {
            cprint("  输入新端口（如 18080）: ");
            char pv[32]; fflush(stdout);
            if (!fgets(pv, sizeof pv, stdin)) break;
            trim(pv);
            int np = atoi(pv);
            if (np < 1 || np > 65535) { cprint("  无效端口\n"); continue; }
            API_PORT = np;
            save_conf();
            cprint("  端口已改为 %d（已保存，下次启动服务生效）\n", API_PORT);
        } else if (!strcmp(line, "7")) {
            AUTH_ON = !AUTH_ON;
            save_conf();
            cprint("  密钥开关已切换为：%s（已写入配置）\n",
                   AUTH_ON ? "开（API 调用需带 Bearer 密钥）" : "关（API 调用无需密钥）");
        } else if (!strcmp(line, "8")) {
            FORCE_AUTH = !FORCE_AUTH;
            save_conf();
            cprint("  强制密钥已切换为：%s（已写入配置）\n",
                   FORCE_AUTH ? "开（无论 7 如何，所有请求都必须带正确密钥）" : "关（按 7 的设置执行）");
        } else if (!strcmp(line, "0")) {
            cprint("  再见。\n");
            break;
        } else {
            cprint("  无效输入\n");
        }
    }
    return 0;
}

#ifndef HNN_CORE_DLL
int main(int argc, char **argv) { return ai_server_main(argc, argv); }
#endif
