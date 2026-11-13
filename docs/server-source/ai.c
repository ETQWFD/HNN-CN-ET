/*
 * ai.c — HNN CN ET Server (Termux / Linux / Windows)
 * Copyright (c) 2026 ETC · MIT License
 * 纯 C 单文件：菜单(1首页/2市场/3远程/4改端口/7密钥开关/8强制密钥/0退出) + OpenAI 兼容 API
 * 推理: 调用 llm_demo (MNN, 与手机版同款模型)；下载: 系统 curl 走 ModelScope 官方 API
 * 编译: gcc/clang 各平台直接编；Windows 编 DLL 核心 + exe 启动器
 */
#ifdef _WIN32
#include <winsock2.h>   /* 必须最先包含：pthread.h/winpthreads 会拉进 windows.h */
#endif
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
#include <net/if.h>
#include <sys/ioctl.h>
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
#include <ws2tcpip.h>
#include <iphlpapi.h>
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

#define VERSION "1.3.5"
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
/* 递归创建多级目录（Windows _mkdir 只建一级，故逐级创建；已存在的忽略错误） */
static void mkpath(const char *path) {
    char tmp[MAXPATH];
    strncpy(tmp, path, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i]; tmp[i] = 0;
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            tmp[i] = c;
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}
static char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
#ifdef _WIN32
    const char *t = strrchr(p, '\\');
    if (!s || (t && t > s)) s = t;
#endif
    return (char *)(s ? s + 1 : p);
}

/* 递归删除目录（用于清理 llm_demo 独立工作目录；lstat 不跟随符号链接，防误删链接目标） */
static void rmrf(const char *path) {
    DIR *d = opendir(path);
    if (!d) { remove(path); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sub[MAXPATH];
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        struct stat st;
#ifdef _WIN32
        if (!stat(sub, &st) && S_ISDIR(st.st_mode)) rmrf(sub);
        else remove(sub);
#else
        if (!lstat(sub, &st) && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) rmrf(sub);
        else remove(sub);
#endif
    }
    closedir(d);
    rmdir(path);
}

/* 解析可执行文件真实路径（替代 argv[0]，避免 PATH 启动/相对路径时
   拿不到程序目录导致 cwd bug）。buf 返回可执行文件全路径。 */
static void self_exe_path(char *buf, size_t n) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, buf, (DWORD)n);
#else
    ssize_t r = readlink("/proc/self/exe", buf, n - 1);
    if (r > 0) buf[r] = 0;
    else snprintf(buf, n, ".");
#endif
}
/* 可执行文件所在目录（不含末级 / 文件名） */
static void self_exe_dir(char *buf, size_t n) {
    self_exe_path(buf, n);
    char *s = strrchr(buf, '/');
#ifdef _WIN32
    char *b = strrchr(buf, '\\');
    if (!s || (b && b > s)) s = b;
#endif
    if (s) *s = 0; else snprintf(buf, n, ".");
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

#ifdef _WIN32
/* Windows: 用 CreateProcess 直接启动并捕获 stdout/stderr，不经 cmd.exe，
   避免路径里的括号、&、%、引号等被 shell 解释导致参数错乱 */
static char *capture_exec_in(const char *cmdline, const char *wkdir) {
    HANDLE rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa; sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return strdup("管道创建失败");
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    char *cl = _strdup(cmdline);
    STARTUPINFOA si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    /* llm_demo 内部 tmp_path 相对路径需可写 cwd，用独立工作目录作为子进程工作目录 */
    char wk[MAXPATH];
    snprintf(wk, sizeof wk, "%s", wkdir && wkdir[0] ? wkdir : ".");
    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, TRUE, 0, NULL, wk, &si, &pi);
    free(cl); CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return strdup("无法启动 llm_demo"); }
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) { CloseHandle(rd); return strdup("内存不足"); }
    DWORD got;
    while (ReadFile(rd, buf + len, (DWORD)(cap - len - 1), &got, NULL) && got > 0) {
        len += got;
        if (cap - len < 8192) { cap *= 2; buf = realloc(buf, cap); }
        buf[len] = 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    buf[len] = 0;
    return buf;
}
#endif

/* 推理互斥锁与序号：llm_demo 每次调用使用 HOME 下独立工作子目录，
   并发 API 请求串行执行，避免同时写同一 prompt 文件/tmp 目录导致 double free */
static pthread_mutex_t llm_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned llm_seq = 0;

/* ---------- llm_demo 输出解析（流式状态机） ----------
   llm_demo 的输出结构是固定的：
     config path is /abs/config.json
     The device supports: ...
     main, 264, cost time: ...
     prompt file is xxx
     <答案正文，逐字输出>
     #################################
     prompt tokens num = ...
   旧实现按“前缀黑名单”过滤日志行，一旦没有任何一行通过过滤（例如模型没加载成功、
   或日志措辞变化），就会回退成“整段原始输出”，于是把第一行
   `config path is config.json` 当成回答返回（用户看到的复读）。
   这里改成状态机：只在 `prompt file is` 之后开始取答案，遇到 25 个以上连续 '#'
   的分隔线结束。既杜绝日志混进答案，又能在生成过程中逐字回调做真流式。 */
typedef void (*emit_fn)(void *ctx, const char *text, size_t len);

typedef struct {
    int state;              /* 0=等待答案开始 1=答案中 2=结束 */
    char line[600];
    size_t linelen;
    int at_line_start;
    int hash_run;
    int hold_hash;
    char obuf[96];
    size_t olen;
    char tail[400];         /* 最后一条日志行，失败时用于诊断 */
    char errs[400];         /* 关键报错行（Failed/Can't/Unable/Error），失败时优先展示 */
    int errn;
    emit_fn emit;
    void *ctx;
    int saw;
} OutParser;

static void outp_flush(OutParser *p) {
    if (p->olen && p->emit) { p->emit(p->ctx, p->obuf, p->olen); p->olen = 0; }
}
static void outp_put(OutParser *p, char c) {
    p->obuf[p->olen++] = c;
    if (p->olen >= sizeof p->obuf - 1 || c == '\n') outp_flush(p);
}
static void pars_init(OutParser *p, emit_fn emit, void *ctx) {
    memset(p, 0, sizeof *p);
    p->state = 0; p->at_line_start = 1; p->emit = emit; p->ctx = ctx; p->errn = 0;
}
static void pars_feed(OutParser *p, const char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        if (p->state == 2) return;
        if (p->state == 0) {
            if (c == '\n') {
                p->line[p->linelen] = 0;
                if (p->line[0]) {
                    snprintf(p->tail, sizeof p->tail, "%s", p->line);
                    if (p->errn < 3 && (strstr(p->line, "Failed") || strstr(p->line, "Can't") ||
                        strstr(p->line, "Unable") || strstr(p->line, "Error") || strstr(p->line, "cannot"))) {
                        if (p->errs[0]) strncat(p->errs, " | ", sizeof p->errs - strlen(p->errs) - 1);
                        strncat(p->errs, p->line, sizeof p->errs - strlen(p->errs) - 1);
                        p->errn++;
                    }
                }
                if (!strncmp(p->line, "prompt file is", 14)) {
                    p->state = 1; p->at_line_start = 1;
                    p->hash_run = 0; p->hold_hash = 0;
                }
                p->linelen = 0;
            } else if (p->linelen < sizeof p->line - 1) {
                p->line[p->linelen++] = c;
            }
            continue;
        }
        /* state 1：答案区。行首连续 '#' 可能是结束分隔线，先扣住判断 */
        if (p->hold_hash) {
            if (c == '#') { p->hash_run++; continue; }
            if (p->hash_run >= 25) {                 /* 确认是分隔线 → 答案结束 */
                p->state = 2; p->hold_hash = 0; p->hash_run = 0;
                outp_flush(p);
                return;
            }
            for (int k = 0; k < p->hash_run; k++) outp_put(p, '#');  /* 是 Markdown 标题，回吐 */
            p->hold_hash = 0; p->hash_run = 0;
        }
        if (p->at_line_start && c == '#') { p->hold_hash = 1; p->hash_run = 1; continue; }
        outp_put(p, c);
        p->at_line_start = (c == '\n');
    }
}
static void pars_finish(OutParser *p) {
    if (p->hold_hash) {
        for (int k = 0; k < p->hash_run; k++) outp_put(p, '#');
        p->hold_hash = 0; p->hash_run = 0;
    }
    outp_flush(p);
    p->saw = (p->state == 1 || p->state == 2);
}

#ifdef _WIN32
/* Windows 流式执行：CreateProcess 起 llm_demo，stdout 边读边喂状态机 */
static int exec_stream_in(const char *cmdline, const char *wkdir, OutParser *p) {
    HANDLE rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa; sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    char *cl = _strdup(cmdline);
    STARTUPINFOA si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    char wk[MAXPATH];
    snprintf(wk, sizeof wk, "%s", wkdir && wkdir[0] ? wkdir : ".");
    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, TRUE, 0, NULL, wk, &si, &pi);
    free(cl); CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return 0; }
    char buf[4096]; DWORD got;
    while (ReadFile(rd, buf, sizeof buf, &got, NULL) && got > 0)
        pars_feed(p, buf, (size_t)got);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return 1;
}
#endif

/* ---------- 子进程推理（MNN llm_demo 一次性进程 + 流式回调） ----------
   推理串行化：并发请求共用同一 llm_demo 与相对 tmp 目录会互相覆盖，
   历史上导致 double free。这里依旧全局加锁，但每次使用独立工作子目录。
   返回 1=拿到答案，0=进程跑通但没识别到答案，-1=启动失败。 */
static int run_llm_stream(const char *model_dir, const char *prompt, int max_tokens,
                          emit_fn emit, void *ctx, char *diag, size_t diagsz) {
    pthread_mutex_lock(&llm_mutex);
    char wdir[MAXPATH];
    llm_seq++;
    snprintf(wdir, sizeof wdir, "%s/.ai_work/%u", HOME_DIR[0] ? HOME_DIR : ".", llm_seq);
    mkpath(wdir);
    char pf[MAXPATH];
    snprintf(pf, sizeof pf, "%s/prompt.txt", wdir);
    FILE *p = fopen(pf, "w");
    if (!p) {
        pthread_mutex_unlock(&llm_mutex);
        if (diag) snprintf(diag, diagsz, "无法写入 prompt 文件");
        return -1;
    }
    fputs(prompt, p); fclose(p);
    char cfg[MAXPATH];
    snprintf(cfg, sizeof cfg, "%s/config.json", model_dir);

    OutParser *pa = malloc(sizeof *pa);
    if (!pa) { rmrf(wdir); pthread_mutex_unlock(&llm_mutex); return -1; }
    pars_init(pa, emit, ctx);
    int ok = 0;
#ifdef _WIN32
    char cmdline[4096];
    snprintf(cmdline, sizeof cmdline, "\"%s\" \"%s\" \"%s\" %d",
             LLM_DEMO_PATH, cfg, pf, max_tokens);
    ok = exec_stream_in(cmdline, wdir, pa);
#else
    /* 不再 chdir（chdir 是进程全局状态，会干扰并发连接线程）；
       用 sh -c 让子进程自己进隔离目录，llm_demo 的相对 tmp 路径就落在可写目录里 */
    char cmd[MAXPATH * 2 + 320];
    snprintf(cmd, sizeof cmd, "cd '%s' && '%s' '%s' '%s' %d 2>/dev/null",
             wdir, LLM_DEMO_PATH, cfg, pf, max_tokens);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, fp)) > 0) pars_feed(pa, buf, n);
        pclose(fp);
        ok = 1;
    }
#endif
    pars_finish(pa);
    int saw = pa->saw;
    if (diag) snprintf(diag, diagsz, "%s", pa->errs[0] ? pa->errs : pa->tail);
    free(pa);
    rmrf(wdir);
    pthread_mutex_unlock(&llm_mutex);
    if (saw) return 1;
    return ok ? 0 : -1;
}

/* 累加器：非流式请求用 */
typedef struct { char *buf; size_t len, cap; } Acc;
static void acc_emit(void *ctx, const char *t, size_t n) {
    Acc *a = (Acc *)ctx;
    if (a->len + n + 1 >= a->cap) {
        size_t nc = a->cap ? a->cap : 1024;
        while (nc < a->len + n + 2) nc *= 2;
        a->buf = realloc(a->buf, nc); a->cap = nc;
    }
    memcpy(a->buf + a->len, t, n);
    a->len += n;
    a->buf[a->len] = 0;
}
static void trim_tail(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
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

/* ---------- 模型完整性检查 ----------
   llm_demo 启动会依次读 config.json / llm_config.json / tokenizer.txt /
   llm.mnn / llm.mnn.weight / embeddings_bf16.bin。任何一项缺失或 0 字节，
   llm_demo 只打印 "config path is ..." 就退出，旧版把它当成"未返回内容"，
   用户完全看不出缺哪个文件。这里先自查，直接告诉用户缺什么。 */
static const char *MODEL_REQUIRED[] = {
    "config.json", "llm_config.json", "tokenizer.txt", "llm.mnn", "llm.mnn.weight", "embeddings_bf16.bin"
};
static int model_check(const char *dir, char *missing, size_t ms) {
    int n = 0;
    missing[0] = 0;
    for (int i = 0; MODEL_REQUIRED[i]; i++) {
        char p[MAXPATH]; snprintf(p, sizeof p, "%s/%s", dir, MODEL_REQUIRED[i]);
        struct stat st;
        if (stat(p, &st) != 0 || st.st_size == 0) {
            n++;
            if (strlen(missing) + strlen(MODEL_REQUIRED[i]) + 3 < ms) {
                if (missing[0]) strncat(missing, "、", ms - strlen(missing) - 1);
                strncat(missing, MODEL_REQUIRED[i], ms - strlen(missing) - 1);
            }
        }
    }
    return n;
}
/* 下载器有时会把仓库再套一层目录（X/X/config.json），
   若当前目录缺 llm.mnn 而唯一子目录里有，则自动下沉一层。 */
static void model_descend(char *dir, size_t sz) {
    char miss[256];
    if (model_check(dir, miss, sizeof miss) == 0) return;
    char sub[MAXPATH]; sub[0] = 0; int cnt = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[MAXPATH]; snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st)) continue;
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
        if (!S_ISDIR(st.st_mode)) continue;
        char m2[256];
        if (model_check(full, m2, sizeof m2) < model_check(dir, miss, sizeof miss)) {
            snprintf(sub, sizeof sub, "%s", full);
            cnt++;
        }
    }
    closedir(d);
    if (cnt == 1 && sub[0]) snprintf(dir, sz, "%s", sub);
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
    char hdr[1200];
    int len = (int)strlen(body);
    snprintf(hdr, sizeof hdr,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type, x-api-key, anthropic-version\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: close\r\n\r\n",
        status, ctype, len);
    send(cli, hdr, (int)strlen(hdr), 0);
    if (len > 0) send(cli, body, len, 0);
}
/* 请求头取值：名称不区分大小写（HTTP 规范要求），值去首尾空白。返回 malloc 串 */
static char *hdr_get(const char *req, const char *name) {
    size_t nl = strlen(name);
    const char *p = strstr(req, "\r\n");
    if (!p) return NULL;
    p += 2;
    while (*p) {
        if (*p == '\r' || *p == '\n') break;
        const char *e = strstr(p, "\r\n");
        if (!e) e = p + strlen(p);
        const char *c = p;
        while (c < e && *c != ':') c++;
        if (c < e && (size_t)(c - p) == nl) {
            int same = 1;
            for (size_t i = 0; i < nl; i++) {
                char a = p[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { same = 0; break; }
            }
            if (same) {
                const char *v = c + 1;
                while (v < e && (*v == ' ' || *v == '\t')) v++;
                const char *ve = e;
                while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
                char *out = malloc((size_t)(ve - v) + 1);
                memcpy(out, v, (size_t)(ve - v)); out[ve - v] = 0;
                return out;
            }
        }
        p = (*e == '\r') ? e + 2 : e;
        while (*p == '\r' || *p == '\n') {
            if (*p == '\n' && p[-1] == '\n') { p++; break; }
            p++;
            if (*p == '\n') break;
        }
    }
    return NULL;
}
static int ci_contains(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b || !a) break;
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}
/* 鉴权：AUTH_ON 与 FORCE_AUTH 都关时全部放行；否则 /v1/models 与 chat 一视同仁。
   兼容 Authorization: Bearer（大小写、多空格）与 x-api-key；
   旧实现用 strstr("Authorization: Bearer ") 精确匹配，客户端发小写头或
   多余空格时会误判 401——这是“开了密钥就直接不能用”的根因之一。 */
static int auth_ok(const char *req) {
    if (!AUTH_ON && !FORCE_AUTH) return 1;
    char *auth = hdr_get(req, "authorization");
    char *xk = hdr_get(req, "x-api-key");
    char tok[128] = "";
    if (auth) {
        const char *p = auth;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncasecmp(p, "bearer", 6)) { p += 6; while (*p == ' ' || *p == '\t') p++; }
        snprintf(tok, sizeof tok, "%s", p);
    } else if (xk) {
        snprintf(tok, sizeof tok, "%s", xk);
    }
    free(auth); free(xk);
    return tok[0] && !strcmp(tok, API_KEY);
}
static int check_auth(int cli, const char *req) {
    if (auth_ok(req)) return 1;
    http_send(cli, "401 Unauthorized", "application/json",
        "{\"error\":{\"message\":\"invalid api key: send 'Authorization: Bearer <key>' or 'x-api-key: <key>'\","
        "\"type\":\"authentication_error\",\"code\":\"invalid_api_key\"}}");
    return 0;
}
/* ---------- 请求体读取（动态扩容，支持 Content-Length 与 chunked） ----------
   旧实现只 recv 一次：长对话（DSH 会带完整历史 + 长系统提示）会被截断，
   表现为 400/空回复/乱答。 */
#define MAX_REQ_BODY (8 * 1024 * 1024)
static int grow_recv(int cli, char **buf, size_t *len, size_t *cap) {
    if (*len + 1 >= *cap) {
        size_t nc = *cap ? *cap * 2 : 16384;
        if (nc > MAX_REQ_BODY + 65536) return -1;
        char *nb = realloc(*buf, nc);
        if (!nb) return -1;
        *buf = nb; *cap = nc;
    }
    ssize_t n = recv(cli, *buf + *len, (int)(*cap - *len - 1), 0);
    if (n <= 0) return -1;
    *len += (size_t)n;
    (*buf)[*len] = 0;
    return 0;
}
static int read_request(int cli, char **out) {
    size_t cap = 16384, len = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;
    buf[0] = 0;
    char *he = NULL;
    while (!he) {
        if (grow_recv(cli, &buf, &len, &cap) != 0) {
            if (len == 0) { free(buf); return -1; }
            break;
        }
        he = strstr(buf, "\r\n\r\n");
    }
    if (!he) { free(buf); return -1; }
    size_t hlen = (size_t)(he - buf) + 4;
    char *te = hdr_get(buf, "transfer-encoding");
    char *cl = hdr_get(buf, "content-length");
    size_t body_len = 0;
    if (!te && cl) {
        long v = atol(cl);
        if (v > 0 && (size_t)v < MAX_REQ_BODY) body_len = (size_t)v;
    }
    free(te); free(cl);
    if (body_len) {
        while (len < hlen + body_len) {
            if (grow_recv(cli, &buf, &len, &cap) != 0) break;
        }
    }
    /* 注意：没有 Content-Length 的 GET/HEAD 不能继续 recv 等对端关闭，
       否则请求线程会挂到客户端超时断开，随后 send 触发 SIGPIPE 直接杀死进程。 */
    /* chunked 解码 */
    const char *p = buf + hlen;
    int chunked = 0;
    {
        char *te2 = hdr_get(buf, "transfer-encoding");
        chunked = te2 && ci_contains(te2, "chunked");
        free(te2);
    }
    if (chunked) {
        char *b = malloc(len - hlen + 2);
        size_t bl = 0;
        const char *q = p;
        while (q < buf + len) {
            unsigned long sz = 0;
            while (*q && *q != '\r') {
                char c = *q;
                if (c >= '0' && c <= '9') sz = sz * 16 + (unsigned long)(c - '0');
                else if (c >= 'a' && c <= 'f') sz = sz * 16 + (unsigned long)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') sz = sz * 16 + (unsigned long)(c - 'A' + 10);
                q++;
            }
            while (*q == '\r' || *q == '\n') q++;
            if (sz == 0) break;
            if (q + sz > buf + len) break;
            memcpy(b + bl, q, sz); bl += sz;
            q += sz;
            while (*q == '\r' || *q == '\n') q++;
        }
        b[bl] = 0;
        /* 必须把解码结果放回头部之后、保留请求行与头字段：
           路由要靠 method/path 分派，鉴权要靠 Authorization 头。 */
        if (hlen + bl + 1 > cap) {
            char *nb = realloc(buf, hlen + bl + 1);
            if (!nb) { free(b); free(buf); return -1; }
            buf = nb;
        }
        memcpy(buf + hlen, b, bl);
        buf[hlen + bl] = 0;
        free(b);
        *out = buf;
        return 0;
    }
    *out = buf;
    return 0;
}
/* ---------- JSON 小工具 ---------- */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}
/* 解析 JSON 字符串字面量（含转义与 \uXXXX / 代理对），pp 指向起始引号 */
static char *json_parse_string(const char **pp) {
    const char *p = *pp;
    if (*p != '"') return NULL;
    p++;
    size_t cap = 128, len = 0;
    char *s = malloc(cap + 4);
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\') {
            p++;
            switch (*p) {
                case 'n': c = '\n'; p++; break;
                case 't': c = '\t'; p++; break;
                case 'r': c = '\r'; p++; break;
                case 'b': c = '\b'; p++; break;
                case 'f': c = '\f'; p++; break;
                case '/': c = '/'; p++; break;
                case '"': c = '"'; p++; break;
                case '\\': c = '\\'; p++; break;
                case 'u': {
                    p++;
                    unsigned v = 0; int k;
                    for (k = 0; k < 4 && p[k]; k++) {
                        char h = p[k]; v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                    }
                    p += k;
                    if (v >= 0xD800 && v <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo = 0; int k2;
                        for (k2 = 0; k2 < 4 && p[k2]; k2++) {
                            char h = p[k2]; lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                        }
                        p += k2;
                        unsigned cp = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                        unsigned char mb[4];
                        mb[0] = (unsigned char)(0xF0 | (cp >> 18));
                        mb[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                        mb[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                        mb[3] = (unsigned char)(0x80 | (cp & 0x3F));
                        if (len + 5 >= cap) { cap *= 2; s = realloc(s, cap + 4); }
                        for (int q = 0; q < 4; q++) s[len++] = (char)mb[q];
                        continue;
                    }
                    if (v < 0x80) c = (char)v;
                    else {
                        unsigned char mb[3]; int m = 0;
                        if (v < 0x800) {
                            mb[m++] = (unsigned char)(0xC0 | (v >> 6));
                            mb[m++] = (unsigned char)(0x80 | (v & 0x3F));
                        } else {
                            mb[m++] = (unsigned char)(0xE0 | (v >> 12));
                            mb[m++] = (unsigned char)(0x80 | ((v >> 6) & 0x3F));
                            mb[m++] = (unsigned char)(0x80 | (v & 0x3F));
                        }
                        if (len + 4 >= cap) { cap *= 2; s = realloc(s, cap + 4); }
                        for (int q = 0; q < m; q++) s[len++] = (char)mb[q];
                        continue;
                    }
                    break;
                }
                default: c = *p; if (*p) p++; break;
            }
        } else {
            p++;
        }
        if (len + 2 >= cap) { cap *= 2; s = realloc(s, cap + 4); }
        s[len++] = c;
    }
    if (*p == '"') p++;
    s[len] = 0;
    *pp = p;
    return s;
}
/* 跳过一个 JSON 值（字符串/对象/数组/标量） */
static const char *json_skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') {
        const char *q = p + 1;
        while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
        return *q ? q + 1 : q;
    }
    if (*p == '{' || *p == '[') {
        char open = *p, close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                const char *q = p + 1;
                while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
                p = *q ? q + 1 : q;
                continue;
            }
            if (*p == open) depth++;
            else if (*p == close) { depth--; if (depth == 0) return p + 1; }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
/* JSON 转义（含控制字符；旧版只处理引号/反斜杠/换行，
   模型输出带 tab 等控制字符时会让客户端解析失败 → 表现为 terminated） */
static char *json_escape(const char *s) {
    size_t cap = strlen(s) * 2 + 16, len = 0;
    char *o = malloc(cap);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char add[8]; int n = 2;
        add[0] = '\\';
        if (c == '"') add[1] = '"';
        else if (c == '\\') add[1] = '\\';
        else if (c == '\n') add[1] = 'n';
        else if (c == '\r') add[1] = 'r';
        else if (c == '\t') add[1] = 't';
        else if (c == '\b') add[1] = 'b';
        else if (c == '\f') add[1] = 'f';
        else if (c < 0x20) {
            add[1] = 'u'; add[2] = '0'; add[3] = '0';
            add[4] = (char)('0' + ((c >> 4) & 0xF));
            add[5] = (char)('0' + (c & 0xF));
            n = 6;
        } else { o[len++] = (char)c; continue; }
        if (len + 8 >= cap) { cap *= 2; o = realloc(o, cap); }
        memcpy(o + len, add, (size_t)n); len += (size_t)n;
    }
    o[len] = 0;
    return o;
}
static int json_bool(const char *body, const char *key, int def) {
    char k[64]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(body, k);
    if (!p) return def;
    p += strlen(k);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (!strncmp(p, "true", 4)) return 1;
    if (!strncmp(p, "false", 5)) return 0;
    return def;
}
/* 按字节粗估 token 数（中文 1 字≈1 token，英文 4 字符≈1 token），仅用于 usage 展示 */
static int est_tokens(const char *s) {
    int ascii = 0, other = 0;
    for (const char *p = s; *p; p++) {
        if ((unsigned char)*p < 0x80) ascii++;
        else if (((unsigned char)*p & 0xC0) != 0x80) other++;
    }
    return ascii / 4 + other + 1;
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
#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "curl -sL -m 30 \"%s/v1/models\"", REMOTE_URL);
#else
    snprintf(cmd, sizeof cmd, "curl -sL -m 30 '%s/v1/models'", REMOTE_URL);
#endif
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
/* ---------- 多轮 messages 解析（旧版只取第一个 "content"，把完整对话历史丢了） ---------- */
#define MAX_MSGS 24
typedef struct { char role[16]; char *content; } Msg;

static void sb_add(char **b, size_t *len, size_t *cap, const char *s, size_t maxn) {
    size_t n = strlen(s);
    if (n > maxn) n = maxn;
    if (*len + n + 2 >= *cap) {
        size_t nc = *cap ? *cap : 1024;
        while (nc < *len + n + 2) nc *= 2;
        *b = realloc(*b, nc); *cap = nc;
    }
    memcpy(*b + *len, s, n); *len += n; (*b)[*len] = 0;
}
static int parse_messages(const char *body, Msg *msgs, int maxm) {
    const char *p = strstr(body, "\"messages\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    int n = 0;
    while (*p && n < maxm) {
        p = skip_ws(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') { p = json_skip_value(p); continue; }
        p++;
        char role[16] = "";
        char *content = NULL;
        while (*p) {
            p = skip_ws(p);
            if (*p == '}') { p++; break; }
            if (*p == ',') { p++; continue; }
            if (*p != '"') { p = json_skip_value(p); continue; }
            char *key = json_parse_string(&p);
            if (!key) break;
            p = skip_ws(p);
            if (*p == ':') p++;
            p = skip_ws(p);
            if (!strcmp(key, "role")) {
                if (*p == '"') {
                    char *v = json_parse_string(&p);
                    if (v) { snprintf(role, sizeof role, "%s", v); free(v); }
                } else p = json_skip_value(p);
            } else if (!strcmp(key, "content")) {
                if (*p == '"') {
                    free(content); content = json_parse_string(&p);
                } else if (*p == '[') {
                    /* 多段内容（多模态）：只拼 text 字段 */
                    size_t clen = 0, ccap = 0;
                    char *cb = NULL;
                    const char *q = p + 1;
                    while (*q) {
                        q = skip_ws(q);
                        if (*q == ']') { q++; break; }
                        if (*q == ',') { q++; continue; }
                        if (*q != '{') { q = json_skip_value(q); continue; }
                        const char *obj_end = json_skip_value(q);
                        const char *t = strstr(q, "\"text\"");
                        if (t && t < obj_end) {
                            const char *tp = t + 6;
                            tp = skip_ws(tp);
                            if (*tp == ':') tp++;
                            tp = skip_ws(tp);
                            if (*tp == '"') {
                                char *tv = json_parse_string(&tp);
                                if (tv) {
                                    if (clen) sb_add(&cb, &clen, &ccap, "\n", 1);
                                    sb_add(&cb, &clen, &ccap, tv, 6000);
                                    free(tv);
                                }
                            }
                        }
                        q = obj_end;
                    }
                    p = q;
                    free(content); content = cb ? cb : strdup("");
                } else p = json_skip_value(p);
            } else {
                p = json_skip_value(p);
            }
            free(key);
        }
        if (role[0]) {
            snprintf(msgs[n].role, sizeof msgs[n].role, "%s", role);
            msgs[n].content = content ? content : strdup("");
            n++;
        } else {
            free(content);
        }
        p = skip_ws(p);
        if (*p == ',') p++;
    }
    return n;
}
/* 组 prompt：system.ini 人设 + 请求内 system + 最后一条用户消息。
   为什么不用完整历史：llm_demo 会把整个 prompt 当作“一条用户消息”套模板，
   把多轮记录塞进去时 0.5B 级小模型会照着剧本把历史逐条复述（实测），
   反而答非所问。只保留最后一条问题，答案最干净、首字也更快。 */
static char *build_prompt(const char *sysini, Msg *msgs, int n) {
    size_t cap = 1024, len = 0;
    char *b = malloc(cap); b[0] = 0;
    char *sys = NULL; size_t syslen = 0, syscap = 0;
    for (int i = 0; i < n; i++)
        if (!strcmp(msgs[i].role, "system") || !strcmp(msgs[i].role, "developer")) {
            sb_add(&sys, &syslen, &syscap, msgs[i].content, 2000);
            sb_add(&sys, &syslen, &syscap, "\n", 1);
        }
    if (sysini && sysini[0]) {
        sb_add(&b, &len, &cap, "系统：", 4);
        sb_add(&b, &len, &cap, sysini, 1200);
        sb_add(&b, &len, &cap, "\n\n", 2);
    }
    if (sys && sys[0]) {
        sb_add(&b, &len, &cap, "系统：", 4);
        sb_add(&b, &len, &cap, sys, 2000);
        sb_add(&b, &len, &cap, "\n\n", 2);
    }
    free(sys);
    /* 取最后一条用户消息；没有用户消息则取最后一条非 system 消息 */
    const char *q = NULL;
    for (int i = n - 1; i >= 0; i--)
        if (!strcmp(msgs[i].role, "user")) { q = msgs[i].content; break; }
    if (!q)
        for (int i = n - 1; i >= 0; i--)
            if (strcmp(msgs[i].role, "system") && strcmp(msgs[i].role, "developer")) { q = msgs[i].content; break; }
    if (!q || !q[0]) { free(b); return NULL; }
    sb_add(&b, &len, &cap, "用户：", 7);
    sb_add(&b, &len, &cap, q, 6000);
    sb_add(&b, &len, &cap, "\n助手：", 8);
    return b;
}
static char *extract_stop(const char *body) {
    const char *p = strstr(body, "\"stop\"");
    if (!p) return NULL;
    p += 6;
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p == '"') return json_parse_string(&p);
    if (*p == '[') {
        p++;
        p = skip_ws(p);
        if (*p == '"') return json_parse_string(&p);
    }
    return NULL;
}

/* ---------- SSE 真流式 ----------
   llm_demo 是“一次性进程”，但它的 stdout 是逐字打印的；状态机把答案区逐字回调，
   这里立刻包成 OpenAI chat.completion.chunk 推给客户端。
   同时每 5 秒发一条 SSE 注释作为心跳：客户端（DSH 等）默认超时很短，
   模型加载/首字延迟期间没有字节到达就会被判定 terminated。 */
typedef struct {
    int cli;
    char id[64];
    const char *model;
    long long created;
    pthread_mutex_t *wlock;
    char *stop;
    size_t stoplen;
    char *pend;
    size_t plen, pcap;
    int stopped;
    size_t chars;
} SseCtx;

static void sse_raw(SseCtx *c, const char *s, size_t n) {
    if (c->wlock) pthread_mutex_lock(c->wlock);
    send(c->cli, s, (int)n, 0);
    if (c->wlock) pthread_mutex_unlock(c->wlock);
}
static void sse_chunk(SseCtx *c, const char *content, size_t clen, int finish, const char *reason) {
    char *esc = content ? json_escape(content) : NULL;
    size_t need = (esc ? strlen(esc) : 0) + 512;
    char *buf = malloc(need);
    if (content && esc) {
        snprintf(buf, need,
            "data: {\"id\":\"%s\",\"object\":\"chat.completion.chunk\",\"created\":%lld,\"model\":\"%s\","
            "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"%s\"},\"finish_reason\":%s}]}\n\n",
            c->id, c->created, c->model, esc,
            finish ? "\"stop\"" : "null");
    } else if (finish) {
        snprintf(buf, need,
            "data: {\"id\":\"%s\",\"object\":\"chat.completion.chunk\",\"created\":%lld,\"model\":\"%s\","
            "\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n",
            c->id, c->created, c->model);
    } else {
        snprintf(buf, need,
            "data: {\"id\":\"%s\",\"object\":\"chat.completion.chunk\",\"created\":%lld,\"model\":\"%s\","
            "\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"\"},\"finish_reason\":null}]}\n\n",
            c->id, c->created, c->model);
    }
    sse_raw(c, buf, strlen(buf));
    free(buf); free(esc);
}
static void sse_put(SseCtx *c, const char *t, size_t n) {
    if (c->stopped || n == 0) return;
    if (c->plen + n + 2 > c->pcap) {
        size_t nc = c->pcap ? c->pcap : 256;
        while (nc < c->plen + n + 2) nc *= 2;
        c->pend = realloc(c->pend, nc); c->pcap = nc;
    }
    memcpy(c->pend + c->plen, t, n);
    c->plen += n;
    c->pend[c->plen] = 0;
    if (c->stoplen) {
        char *pos = strstr(c->pend, c->stop);
        if (pos) {
            size_t keep = (size_t)(pos - c->pend);
            if (keep) { sse_chunk(c, c->pend, keep, 0, NULL); c->chars += keep; }
            c->stopped = 1; c->plen = 0; c->pend[0] = 0;
            return;
        }
        if (c->plen > c->stoplen) {
            size_t emitn = c->plen - c->stoplen;
            sse_chunk(c, c->pend, emitn, 0, NULL);
            c->chars += emitn;
            memmove(c->pend, c->pend + emitn, c->plen - emitn);
            c->plen -= emitn;
            c->pend[c->plen] = 0;
        }
    } else {
        /* 末尾的换行/空格先扣住不发：答案结束后这些是 llm_demo 的空行，
           多发一个只含 "\n" 的 chunk 会让客户端多出空段落 */
        size_t n_emit = c->plen;
        while (n_emit > 0 && (c->pend[n_emit-1] == '\n' || c->pend[n_emit-1] == '\r' ||
                              c->pend[n_emit-1] == ' ' || c->pend[n_emit-1] == '\t')) n_emit--;
        if (n_emit) {
            sse_chunk(c, c->pend, n_emit, 0, NULL);
            c->chars += n_emit;
            memmove(c->pend, c->pend + n_emit, c->plen - n_emit);
            c->plen -= n_emit;
            c->pend[c->plen] = 0;
        }
    }
}
static void sse_emit_cb(void *ctx, const char *t, size_t n) { sse_put((SseCtx *)ctx, t, n); }

typedef struct { int cli; volatile int stop; pthread_mutex_t *wlock; } Hb;
static void *hb_main(void *a) {
    Hb *h = (Hb *)a;
    while (!h->stop) {
        usleep(5000000);
        if (h->stop) break;
        if (h->wlock) pthread_mutex_lock(h->wlock);
        send(h->cli, ": keep-alive\n\n", 14, 0);
        if (h->wlock) pthread_mutex_unlock(h->wlock);
    }
    return NULL;
}

static char *models_one_json(const char *id) {
    scan_models();
    char *j = malloc(512);
    long long created = (long long)time(NULL);
    for (int i = 0; i < g_model_count; i++) {
        if (!strcasecmp(g_models[i].name, id)) {
            snprintf(j, 512, "{\"id\":\"%s\",\"object\":\"model\",\"created\":%lld,\"owned_by\":\"ETC\"}",
                     g_models[i].name, created);
            return j;
        }
    }
    free(j);
    return NULL;
}

static void handle_client(int cli) {
    char *req = NULL;
    if (read_request(cli, &req) != 0) { close(cli); return; }
    char method[16] = "", path[256] = "";
    if (sscanf(req, "%15s %255s", method, path) < 2) {
        http_send(cli, "400 Bad Request", "application/json", "{\"error\":\"bad request\"}");
        free(req); close(cli); return;
    }
    char *qm = strchr(path, '?');
    if (qm) *qm = 0;
    if (!strcmp(method, "OPTIONS")) {
        http_send(cli, "200 OK", "text/plain", "");
        free(req); close(cli); return;
    }
    if (!strcmp(method, "GET") || !strcmp(method, "HEAD")) {
        if (!strcmp(path, "/")) {
            http_send(cli, "200 OK", "text/plain; charset=utf-8",
                      "HNN CN ET Server is running.\nPOST /v1/chat/completions\nGET /v1/models\n");
            free(req); close(cli); return;
        }
        if (!strcmp(path, "/health") || !strcmp(path, "/v1/health")) {
            char body[512];
            scan_models();
            snprintf(body, sizeof body,
                "{\"status\":\"ok\",\"version\":\"%s\",\"models\":%d,\"auth\":%s,\"instance\":\"ETC\"}",
                VERSION, g_model_count, (AUTH_ON || FORCE_AUTH) ? "true" : "false");
            http_send(cli, "200 OK", "application/json", body);
            free(req); close(cli); return;
        }
        if (!strcmp(path, "/v1/models") || !strcmp(path, "/models")) {
            if (!check_auth(cli, req)) { free(req); close(cli); return; }
            char *body = REMOTE_URL[0] ? remote_get_models() : build_models_json();
            http_send(cli, "200 OK", "application/json", body);
            free(body); free(req); close(cli); return;
        }
        if (!strncmp(path, "/v1/models/", 11) || !strncmp(path, "/models/", 8)) {
            if (!check_auth(cli, req)) { free(req); close(cli); return; }
            const char *id = path + (!strncmp(path, "/v1/models/", 11) ? 11 : 8);
            char *body = models_one_json(id);
            if (body) { http_send(cli, "200 OK", "application/json", body); free(body); }
            else http_send(cli, "404 Not Found", "application/json", "{\"error\":{\"message\":\"model not found\"}}");
            free(req); close(cli); return;
        }
        http_send(cli, "404 Not Found", "application/json", "{\"error\":{\"message\":\"not found\"}}");
        free(req); close(cli); return;
    }
    if (!strcmp(method, "POST")) {
        int is_chat = !strcmp(path, "/v1/chat/completions") || !strcmp(path, "/chat/completions");
        int is_comp = !strcmp(path, "/v1/completions") || !strcmp(path, "/completions");
        if (!is_chat && !is_comp) {
            http_send(cli, "404 Not Found", "application/json", "{\"error\":{\"message\":\"not found\"}}");
            free(req); close(cli); return;
        }
        char *he = strstr(req, "\r\n\r\n");
        if (!he) {
            http_send(cli, "400 Bad Request", "application/json", "{\"error\":\"bad request\"}");
            free(req); close(cli); return;
        }
        char *body = he + 4;
        if (!check_auth(cli, req)) { free(req); close(cli); return; }

        int stream = is_chat && json_bool(body, "stream", 0);
        int inc_usage = json_bool(body, "include_usage", 0);
        Msg msgs[MAX_MSGS];
        int nm = parse_messages(body, msgs, MAX_MSGS);
        char *prompt = NULL;
        if (nm > 0) {
            load_system_prompt();
            prompt = build_prompt(SYSTEM_PROMPT, msgs, nm);
        }
        for (int i = 0; i < nm; i++) free(msgs[i].content);
        if (!prompt) {
            /* 非标准/兼容路径：prompt、input、content 单字段 */
            char *one = extract_field(body, "prompt", 1);
            if (!one) one = extract_field(body, "input", 1);
            if (!one) one = extract_field(body, "content", 1);
            if (!one) {
                http_send(cli, "400 Bad Request", "application/json",
                    "{\"error\":{\"message\":\"缺少 messages[].content 或 prompt\",\"type\":\"invalid_request_error\"}}");
                free(req); close(cli); return;
            }
            char *m2[1];
            (void)m2;
            load_system_prompt();
            size_t cap = strlen(one) + strlen(SYSTEM_PROMPT) + 32;
            prompt = malloc(cap);
            if (SYSTEM_PROMPT[0]) snprintf(prompt, cap, "系统：%s\n\n用户：%s\n助手：", SYSTEM_PROMPT, one);
            else snprintf(prompt, cap, "用户：%s\n助手：", one);
            free(one);
        }
        char *model = extract_field(body, "model", 1);
        char *stop = extract_stop(body);
        int mt = 512;
        char *mtf = extract_field(body, "max_tokens", 0);
        if (mtf) { mt = atoi(mtf); free(mtf); }
        if (mt < 1) mt = 1;
        if (mt > 2048) mt = 2048;

        char mdir[MAXPATH]; mdir[0] = 0;
        int resolved = 0;
        const char *used = "HNN CN ET";
        if (REMOTE_URL[0]) {
            /* 远程转发模式：不做本地推理 */
            char *ans = remote_call(body);
            char *esc = json_escape(ans);
            size_t rc = strlen(esc) + 1024;
            char *resp = malloc(rc);
            snprintf(resp, rc,
                "{\"id\":\"chatcmpl-%ld\",\"object\":\"chat.completion\",\"created\":%lld,\"model\":\"%s\","
                "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}]}",
                (long)time(NULL), (long long)time(NULL), model ? model : "remote", esc);
            http_send(cli, "200 OK", "application/json", resp);
            free(resp); free(esc); free(ans);
            free(prompt); free(model); free(stop); free(req); close(cli); return;
        }
        if (model && model[0] && find_model(model, mdir)) {
            resolved = 1;
            if (strcmp(mdir, ACTIVE_MODEL_DIR)) {
                snprintf(ACTIVE_MODEL_DIR, sizeof ACTIVE_MODEL_DIR, "%s", mdir);
                snprintf(ACTIVE_MODEL, sizeof ACTIVE_MODEL, "%s", base_name(mdir));
            }
        }
        if (!resolved && ACTIVE_MODEL_DIR[0]) {
            snprintf(mdir, sizeof mdir, "%s", ACTIVE_MODEL_DIR);
            resolved = 1;
        }
        if (!resolved && find_first_model(mdir)) {
            snprintf(ACTIVE_MODEL_DIR, sizeof ACTIVE_MODEL_DIR, "%s", mdir);
            snprintf(ACTIVE_MODEL, sizeof ACTIVE_MODEL, "%s", base_name(mdir));
            resolved = 1;
        }
        if (!resolved) {
            http_send(cli, "404 Not Found", "application/json",
                "{\"error\":{\"message\":\"未找到已下载模型，请先在菜单 1 选择或下载模型\",\"type\":\"model_not_found\"}}");
            free(prompt); free(model); free(stop); free(req); close(cli); return;
        }
        /* 目录可能被下载器多套了一层；缺文件时先自动下沉，再给出精确缺失清单 */
        model_descend(mdir, sizeof mdir);
        snprintf(ACTIVE_MODEL_DIR, sizeof ACTIVE_MODEL_DIR, "%s", mdir);
        {
            char miss[256];
            if (model_check(mdir, miss, sizeof miss) > 0) {
                char emsg[600], *esc;
                snprintf(emsg, sizeof emsg,
                    "模型不完整：%s 下缺少 %s。请到菜单 2 重新下载该模型（需包含 llm.mnn、llm.mnn.weight、tokenizer.txt、embeddings_bf16.bin 等全部文件）",
                    base_name(mdir), miss);
                esc = json_escape(emsg);
                size_t rc2 = strlen(esc) + 256;
                char *resp2 = malloc(rc2);
                snprintf(resp2, rc2, "{\"error\":{\"message\":\"%s\",\"type\":\"model_incomplete\"}}", esc);
                http_send(cli, "500 Internal Server Error", "application/json", resp2);
                free(resp2); free(esc); free(prompt); free(model); free(stop); free(req); close(cli); return;
            }
        }
        used = base_name(mdir);
        char cfgchk[MAXPATH];
        snprintf(cfgchk, sizeof cfgchk, "%s/config.json", mdir);
        struct stat st;
        if (stat(cfgchk, &st)) {
            http_send(cli, "500 Internal Server Error", "application/json",
                "{\"error\":{\"message\":\"模型目录缺少 config.json\",\"type\":\"model_error\"}}");
            free(prompt); free(model); free(stop); free(req); close(cli); return;
        }

        long long created = (long long)time(NULL);
        char rid[64];
        snprintf(rid, sizeof rid, "chatcmpl-%lld-%d", created, rand() % 10000);

        if (stream) {
            char hdr[512];
            snprintf(hdr, sizeof hdr,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream; charset=utf-8\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n"
                "X-Accel-Buffering: no\r\n"
                "Access-Control-Allow-Origin: *\r\n\r\n");
            send(cli, hdr, (int)strlen(hdr), 0);
            pthread_mutex_t wlock = PTHREAD_MUTEX_INITIALIZER;
            SseCtx sc;
            memset(&sc, 0, sizeof sc);
            sc.cli = cli; sc.model = used; sc.created = created; sc.wlock = &wlock;
            sc.stop = stop; sc.stoplen = stop ? strlen(stop) : 0;
            snprintf(sc.id, sizeof sc.id, "%s", rid);
            sse_chunk(&sc, NULL, 0, 0, NULL);            /* 立刻给 role，客户端马上有响应 */
            Hb hb; hb.cli = cli; hb.stop = 0; hb.wlock = &wlock;
            pthread_t ht; pthread_create(&ht, NULL, hb_main, &hb);
            char diag[400] = "";
            int r = run_llm_stream(mdir, prompt, mt, sse_emit_cb, &sc, diag, sizeof diag);
            hb.stop = 1;
            pthread_join(ht, NULL);
            if (r > 0) {
                if (sc.plen) {
                    sse_chunk(&sc, sc.pend, sc.plen, 0, NULL);
                    sc.chars += sc.plen; sc.plen = 0; sc.pend[0] = 0;
                }
                sse_chunk(&sc, NULL, 0, 1, "stop");
                if (inc_usage) {
                    int pt = est_tokens(prompt), ct = (int)(sc.chars / 2) + 1;
                    char ub[512];
                    snprintf(ub, sizeof ub,
                        "data: {\"id\":\"%s\",\"object\":\"chat.completion.chunk\",\"created\":%lld,\"model\":\"%s\","
                        "\"choices\":[],\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d,\"total_tokens\":%d}}\n\n",
                        sc.id, created, used, pt, ct, pt + ct);
                    sse_raw(&sc, ub, strlen(ub));
                }
                sse_raw(&sc, "data: [DONE]\n\n", 13);
            } else {
                char msg[700];
                snprintf(msg, sizeof msg,
                    "模型推理失败（llm_demo 未返回内容）。请确认模型目录包含完整权重%s%s%s",
                    diag[0] ? "。最后日志：" : "", diag, diag[0] ? "" : "");
                sse_put(&sc, msg, strlen(msg));
                if (sc.plen) { sse_chunk(&sc, sc.pend, sc.plen, 0, NULL); sc.plen = 0; }
                sse_chunk(&sc, NULL, 0, 1, "stop");
                sse_raw(&sc, "data: [DONE]\n\n", 13);
            }
            free(sc.pend);
            pthread_mutex_destroy(&wlock);
        } else {
            Acc acc; memset(&acc, 0, sizeof acc);
            char diag[400] = "";
            int r = run_llm_stream(mdir, prompt, mt, acc_emit, &acc, diag, sizeof diag);
            char *ans = acc.buf ? acc.buf : strdup("");
            trim_tail(ans);
            if (r <= 0) {
                char msg[700];
                snprintf(msg, sizeof msg,
                    "模型推理失败：llm_demo 没有返回内容。请检查模型目录是否完整%s%s%s",
                    diag[0] ? "（最后日志：" : "", diag, diag[0] ? "）" : "");
                char *esc = json_escape(msg);
                size_t rc = strlen(esc) + 256;
                char *resp = malloc(rc);
                snprintf(resp, rc,
                    "{\"error\":{\"message\":\"%s\",\"type\":\"model_error\",\"model\":\"%s\"}}", esc, used);
                http_send(cli, "500 Internal Server Error", "application/json", resp);
                free(resp); free(esc); free(ans); free(prompt); free(model); free(stop); free(req);
                close(cli); return;
            }
            char *esc = json_escape(ans);
            int pt = est_tokens(prompt), ct = est_tokens(ans);
            size_t rc = strlen(esc) + 1024;
            char *resp = malloc(rc);
            if (is_comp) {
                snprintf(resp, rc,
                    "{\"id\":\"cmpl-%lld\",\"object\":\"text_completion\",\"created\":%lld,\"model\":\"%s\","
                    "\"choices\":[{\"index\":0,\"text\":\"%s\",\"finish_reason\":\"stop\",\"logprobs\":null}]}",
                    created, created, used, esc);
            } else {
                snprintf(resp, rc,
                    "{\"id\":\"%s\",\"object\":\"chat.completion\",\"created\":%lld,\"model\":\"%s\","
                    "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}],"
                    "\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d,\"total_tokens\":%d}}",
                    rid, created, used, esc, pt, ct, pt + ct);
            }
            http_send(cli, "200 OK", "application/json", resp);
            free(resp); free(esc); free(ans);
        }
        free(prompt); free(model); free(stop); free(req);
        close(cli); return;
    }
    http_send(cli, "405 Method Not Allowed", "application/json", "{\"error\":\"method not allowed\"}");
    free(req);
    close(cli);
}

static void *conn_thread(void *arg) {
    int cli = (int)(intptr_t)arg;
    handle_client(cli);
    return NULL;
}
static volatile int running = 1;
#define MAX_LAN 8
/* 枚举本机所有处于 Up 状态、非回环的 IPv4 地址，不依赖外网连接，
   无外网/纯局域网也能正确返回，多网卡可返回多个。返回地址数量。 */
static int get_lan_ips(char ips[][16], int max) {
    int n = 0;
#ifndef _WIN32
    /* SIOCGIFCONF 枚举，Linux 与 Android(API21+) 通用，不依赖 getifaddrs(API24) */
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    char buf[1024];
    struct ifconf ifc;
    ifc.ifc_len = sizeof buf;
    ifc.ifc_buf = buf;
    if (ioctl(s, SIOCGIFCONF, &ifc) < 0) { close(s); return 0; }
    int count = ifc.ifc_len / (int)sizeof(struct ifreq);
    struct ifreq *reqs = (struct ifreq *)buf;
    for (int i = 0; i < count && n < max; i++) {
        struct ifreq *r = &reqs[i];
        struct ifreq fl;
        memset(&fl, 0, sizeof fl);
        strncpy(fl.ifr_name, r->ifr_name, IFNAMSIZ - 1);
        if (ioctl(s, SIOCGIFFLAGS, &fl) < 0) continue;
        if (!(fl.ifr_flags & IFF_UP) || (fl.ifr_flags & IFF_LOOPBACK)) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)&r->ifr_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ips[n], 16);
        if (!strncmp(ips[n], "127.", 4) || !strncmp(ips[n], "169.254.", 8)) continue;
        n++;
    }
    close(s);
#else
    ULONG sz = 0;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, NULL, NULL, &sz) != ERROR_BUFFER_OVERFLOW) return 0;
    IP_ADAPTER_ADDRESSES *aa = (IP_ADAPTER_ADDRESSES *)malloc(sz);
    if (!aa) return 0;
    if (GetAdaptersAddresses(AF_INET, flags, NULL, aa, &sz) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES a = aa; a && n < max; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u && n < max; u = u->Next) {
                struct sockaddr *sa = u->Address.lpSockaddr;
                if (!sa || sa->sa_family != AF_INET) continue;
                struct sockaddr_in *sin = (struct sockaddr_in *)sa;
                inet_ntop(AF_INET, &sin->sin_addr, ips[n], 16);
                if (!strncmp(ips[n], "127.", 4) || !strncmp(ips[n], "169.254.", 8)) continue;
                n++;
            }
        }
    }
    free(aa);
#endif
    return n;
}

static void on_sig(int s) { (void)s; running = 0; }
#ifndef _WIN32
/* 客户端中途断开时不能因为 send() 收到 SIGPIPE 就让整个服务进程退出 */
static void on_pipe(int s) { (void)s; }
#endif
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
    /* 直接枚举本机网卡 IPv4，不依赖外网；无外网/纯局域网也能拿到真实地址 */
    char lan_ips[MAX_LAN][16];
    int lan_n = get_lan_ips(lan_ips, MAX_LAN);
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
    if (lan_n > 0) {
        cprint("  🌐 局域网地址（同 WiFi/网段设备用以下任一地址）:\n");
        for (int i = 0; i < lan_n; i++)
            cprint("               http://%s:%d/v1/chat/completions\n", lan_ips[i], port);
    } else {
        cprint("  🌐 局域网   : 未检测到网卡地址，请确认已连接网络或网卡已启用\n");
    }
    cprint("  💡 想固定人设/规则：编辑当前目录 system.ini，在 system=\"...\" 引号内写提示语，\n");
    cprint("     下次对话自动生效，不用重启。\n");
    cprint("  按 Ctrl+C 停止服务\n");
    cprint("==========================================================\n");
    signal(SIGINT, on_sig);
#ifndef _WIN32
    signal(SIGPIPE, on_pipe);   /* 忽略断连写错误，服务常驻 */
#endif
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
#ifdef _WIN32
    /* 控制台切 UTF-8，避免中文按系统 GBK 代码页显示成乱码（直接运行 exe 也生效） */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
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
    /* 记录可执行文件目录（用于查找 system.ini）——用 /proc/self/exe / GetModuleFileName
       解析真实路径，PATH 启动、软链接、任意 cwd 都不影响 */
    self_exe_dir(EXE_DIR, sizeof EXE_DIR);
    char self_exe[MAXPATH];
    self_exe_path(self_exe, sizeof self_exe);
    /* llm_demo 位置：按平台逐个探测候选，命中即写入 LLM_DEMO_PATH */
#ifdef _WIN32
    /* Windows: ① %USERPROFILE%\.hnn\bin\llm_demo.exe → ② 程序同目录 llm_demo.exe → ③ 程序同目录 bin\llm_demo.exe */
    snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\.hnn\\bin\\llm_demo.exe", home ? home : ".");
    if (access(LLM_DEMO_PATH, X_OK)) {
        char exe_dir[MAXPATH];
        self_exe_dir(exe_dir, sizeof exe_dir);
        snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\llm_demo.exe", exe_dir);
        if (access(LLM_DEMO_PATH, X_OK)) {
            snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\bin\\llm_demo.exe", exe_dir);
            if (access(LLM_DEMO_PATH, X_OK))
                snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s\\llm_demo.exe", exe_dir);
        }
    }
#else
    /* Linux/Termux: ① ~/.hnn/bin/llm_demo → ② 程序同目录 llm_demo（真实 exe 路径） */
    snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s/.hnn/bin/llm_demo", home ? home : ".");
    if (access(LLM_DEMO_PATH, X_OK)) {
        char exe_dir[MAXPATH];
        self_exe_dir(exe_dir, sizeof exe_dir);
        snprintf(LLM_DEMO_PATH, sizeof LLM_DEMO_PATH, "%s/llm_demo", exe_dir);
    }
#endif
    load_conf();
    /* 命令行参数：ai --port 18080 可临时覆盖配置的端口（不写回 ai.conf） */
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--port") || !strcmp(argv[i], "-p")) {
            int cp = atoi(argv[i + 1]);
            if (cp >= 1 && cp <= 65535) API_PORT = cp;
        }
    }
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
            int picked = 0;
            /* 支持列表编号选择（输入纯数字且在范围内） */
            int all_digit = 1;
            for (char *q = name; *q; q++) if (*q < '0' || *q > '9') { all_digit = 0; break; }
            if (all_digit) {
                int idx = atoi(name);
                if (idx >= 1 && idx <= g_model_count) {
                    snprintf(mdir, sizeof mdir, "%s", g_models[idx - 1].dir); picked = 1;
                }
            }
            if (!picked && !find_model(name, mdir)) { cprint("  未找到该模型\n"); continue; }
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
#ifdef _WIN32
            snprintf(cmd, sizeof cmd,
                "curl -sL -m 60 \"https://modelscope.cn/api/v1/models/MNN/%s/repo/files?Recursive=1\"", name);
#else
            snprintf(cmd, sizeof cmd,
                "curl -sL -m 60 'https://modelscope.cn/api/v1/models/MNN/%s/repo/files?Recursive=1'", name);
#endif
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
                /* resolve CDN 直链，走对象存储加速，比 repo 接口少一次重定向 */
                snprintf(durl, sizeof durl, "https://modelscope.cn/models/MNN/%s/resolve/master/%s", name, path);
                snprintf(outp, sizeof outp, "%s/%s", mdir, path);
                /* 在 C 内递归建好父目录，避免依赖 shell 的 mkdir -p（Windows cmd 不支持） */
                char odir[MAXPATH];
                strncpy(odir, outp, sizeof odir - 1); odir[sizeof odir - 1] = 0;
                char *sl = strrchr(odir, '/');
#ifdef _WIN32
                char *bs = strrchr(odir, '\\');
                if (bs && (!sl || bs > sl)) sl = bs;
#endif
                if (sl) { *sl = 0; mkpath(odir); }
                char dcmd[4096];
                /* 显示进度条；弱网自动重试 3 次；引号按平台区分 */
#ifdef _WIN32
                snprintf(dcmd, sizeof dcmd, "curl -L --progress-bar --retry 3 --retry-delay 2 -m 1800 \"%s\" -o \"%s\"", durl, outp);
#else
                snprintf(dcmd, sizeof dcmd, "curl -L --progress-bar --retry 3 --retry-delay 2 -m 1800 '%s' -o '%s'", durl, outp);
#endif
                int rc = system(dcmd);
                if (rc != 0) cprint("  [警告] 该文件下载返回码 %d，可稍后重试或检查网络。\n", rc);
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
            cprint("  端口已改为 %d（已保存，重新进菜单1启动服务即生效）\n", API_PORT);
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
