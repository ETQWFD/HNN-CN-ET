package main

// HNN CN ET Server · Go 重写版
// 单文件、零第三方依赖，保留 C 版 v1.3.5 的接口与目录约定，
// 并补上真正的流式输出、完整多轮上下文、深度思考（reasoning_content）。
//
// 构建：go build -trimpath -ldflags "-s -w" -o ai .

import (
	"bufio"
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

const version = "1.3.6"

var (
	exeDir, homeDir string
	modelRoot       string
	modelRootSet    bool
	confPath        string
	llmDemo         string

	apiKey    string
	authOn    = false
	forceAuth = false
	apiPort   = 8080
	remoteURL string

	activeModel    string
	activeModelDir string

	inferMu sync.Mutex // MNN 单实例，串行推理
)

func initPaths() {
	exe, err := os.Executable()
	if err == nil {
		exeDir = filepath.Dir(exe)
	} else {
		exeDir, _ = os.Getwd()
	}
	homeDir, _ = os.UserHomeDir()
	if homeDir == "" {
		homeDir = "."
	}
	modelRoot = filepath.Join(homeDir, ".hnn", "models")
	if v := os.Getenv("HNN_MODEL_DIR"); v != "" {
		modelRoot = v
	}
	_ = os.MkdirAll(modelRoot, 0755)
	confPath = filepath.Join(exeDir, "ai.conf")

	cands := []string{
		filepath.Join(homeDir, ".hnn", "bin", exeName("llm_demo")),
		filepath.Join(exeDir, exeName("llm_demo")),
		filepath.Join(exeDir, "bin", exeName("llm_demo")),
	}
	for _, c := range cands {
		if st, err := os.Stat(c); err == nil && !st.IsDir() {
			llmDemo = c
			break
		}
	}
	if llmDemo == "" {
		llmDemo = cands[0]
	}
}

func exeName(base string) string {
	if isWindows() {
		return base + ".exe"
	}
	return base
}

func isWindows() bool { return os.PathSeparator == '\\' }

// ---------------- 配置 ----------------

func loadConf() {
	f, err := os.Open(confPath)
	if err == nil {
		defer f.Close()
		sc := bufio.NewScanner(f)
		for sc.Scan() {
			line := strings.TrimSpace(sc.Text())
			switch {
			case strings.HasPrefix(line, "port="):
				if v, e := strconv.Atoi(strings.TrimPrefix(line, "port=")); e == nil && v > 0 && v < 65536 {
					apiPort = v
				}
			case strings.HasPrefix(line, "auth="):
				authOn = strings.TrimPrefix(line, "auth=") != "0"
			case strings.HasPrefix(line, "force="):
				forceAuth = strings.TrimPrefix(line, "force=") != "0"
			case strings.HasPrefix(line, "key="):
				k := strings.TrimSpace(strings.TrimPrefix(line, "key="))
				if len(k) >= 6 {
					apiKey = k
				}
			case strings.HasPrefix(line, "model="):
				v := strings.TrimSpace(strings.TrimPrefix(line, "model="))
				if v != "" && v != activeModel {
					activeModel = v
					activeModelDir = ""
				}
			case strings.HasPrefix(line, "remote="):
				remoteURL = strings.TrimSpace(strings.TrimPrefix(line, "remote="))
			}
		}
	}
	if apiKey == "" {
		apiKey = defaultKey()
	}
}

func defaultKey() string {
	chars := "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	b := make([]byte, 16)
	_, _ = rand.Read(b)
	var sb strings.Builder
	sb.WriteString("etc-")
	sb.WriteByte(byte('A' + b[0]%26))
	sb.WriteByte(byte('0' + b[1]%10))
	for i := 2; i < 14; i++ {
		sb.WriteByte(chars[int(b[i])%len(chars)])
	}
	return sb.String()
}

func saveConf() {
	var sb strings.Builder
	fmt.Fprintf(&sb, "port=%d\nauth=%d\nforce=%d\nkey=%s\n", apiPort, b2i(authOn), b2i(forceAuth), apiKey)
	if activeModel != "" {
		fmt.Fprintf(&sb, "model=%s\n", activeModel)
	}
	if remoteURL != "" {
		fmt.Fprintf(&sb, "remote=%s\n", remoteURL)
	}
	_ = os.WriteFile(confPath, []byte(sb.String()), 0644)
}

func b2i(b bool) int {
	if b {
		return 1
	}
	return 0
}

// ---------------- system.ini ----------------

// 与 C 版一致：找 ./system.ini → exe 目录 → ~/.hnn/system.ini，
// 只取第一处 system="..." / system=“...”。
func loadSystemPrompt() string {
	cands := []string{
		filepath.Join(".", "system.ini"),
		filepath.Join(exeDir, "system.ini"),
		filepath.Join(homeDir, ".hnn", "system.ini"),
	}
	for _, c := range cands {
		f, err := os.Open(c)
		if err != nil {
			continue
		}
		sc := bufio.NewScanner(f)
		for sc.Scan() {
			if v := parseSystemLine(sc.Text()); v != "" {
				f.Close()
				return v
			}
		}
		f.Close()
	}
	return ""
}

func parseSystemLine(line string) string {
	s := strings.TrimSpace(line)
	if len(s) < 6 || !strings.EqualFold(s[:6], "system") {
		return ""
	}
	s = strings.TrimSpace(s[6:])
	if !strings.HasPrefix(s, "=") {
		return ""
	}
	s = strings.TrimSpace(s[1:])
	if strings.HasPrefix(s, `"`) {
		s = s[1:]
		if i := strings.Index(s, `"`); i >= 0 {
			return s[:i]
		}
	}
	if strings.HasPrefix(s, "\u201c") { // 中文左引号
		s = strings.TrimPrefix(s, "\u201c")
		if i := strings.Index(s, "\u201d"); i >= 0 {
			return s[:i]
		}
	}
	return ""
}

// ---------------- 模型 ----------------

type Model struct {
	Name string
	Dir  string
}

func scanModels() []Model {
	entries, err := os.ReadDir(modelRoot)
	if err != nil {
		return nil
	}
	var out []Model
	for _, e := range entries {
		if !e.IsDir() || strings.HasPrefix(e.Name(), ".") {
			continue
		}
		p := filepath.Join(modelRoot, e.Name())
		if d := resolveModelDir(p); d != "" {
			out = append(out, Model{Name: e.Name(), Dir: d})
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

// 允许模型目录里面再套一层（下载工具常见的 nested 结构）
func resolveModelDir(dir string) string {
	if hasModelMarker(dir) {
		return dir
	}
	// 递归向下最多 3 层，兼容下载器多套目录
	var walk func(string, int) string
	walk = func(d string, depth int) string {
		if depth > 3 {
			return ""
		}
		subs, err := os.ReadDir(d)
		if err != nil {
			return ""
		}
		best, bestScore := "", 0
		for _, sub := range subs {
			if !sub.IsDir() || strings.HasPrefix(sub.Name(), ".") {
				continue
			}
			p := filepath.Join(d, sub.Name())
			if hasModelMarker(p) {
				score := markerScore(p)
				if score > bestScore {
					best, bestScore = p, score
				}
			}
			if best == "" {
				if inner := walk(p, depth+1); inner != "" {
					best = inner
				}
			}
		}
		return best
	}
	return walk(dir, 0)
}

func hasModelMarker(dir string) bool {
	for _, f := range []string{"config.json", "llm_config.json", "tokenizer.txt", "llm.mnn", "llm.mnn.weight"} {
		if fileExists(filepath.Join(dir, f)) {
			return true
		}
	}
	return false
}

func markerScore(dir string) int {
	n := 0
	for _, f := range []string{"config.json", "llm_config.json", "tokenizer.txt", "llm.mnn", "llm.mnn.weight", "embeddings_bf16.bin"} {
		if fileExists(filepath.Join(dir, f)) {
			n++
		}
	}
	return n
}

func fileExists(p string) bool {
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}

// 返回缺失的必需文件；空表示完整
func checkModel(dir string) []string {
	need := []string{"config.json", "llm_config.json", "tokenizer.txt", "llm.mnn", "llm.mnn.weight", "embeddings_bf16.bin"}
	var missing []string
	for _, n := range need {
		if !fileExists(filepath.Join(dir, n)) {
			missing = append(missing, n)
		}
	}
	return missing
}

func findModelByName(name string) (Model, bool) {
	name = strings.TrimSpace(name)
	for _, m := range scanModels() {
		if strings.EqualFold(m.Name, name) {
			return m, true
		}
	}
	// 也允许直接给路径
	if d := resolveModelDir(name); d != "" {
		return Model{Name: filepath.Base(name), Dir: d}, true
	}
	return Model{}, false
}

func ensureActiveModel() (string, error) {
	if activeModelDir != "" && fileExists(filepath.Join(activeModelDir, "config.json")) {
		return activeModelDir, nil
	}
	if activeModel == "" {
		ms := scanModels()
		if len(ms) > 0 {
			activeModel, activeModelDir = ms[0].Name, ms[0].Dir
			return activeModelDir, nil
		}
		return "", fmt.Errorf("未选择模型（%s 下没有模型）", modelRoot)
	}
	m, ok := findModelByName(activeModel)
	if !ok {
		for _, cand := range scanModels() {
			if len(checkModel(cand.Dir)) == 0 {
				activeModel, activeModelDir = cand.Name, cand.Dir
				return cand.Dir, nil
			}
		}
		return "", fmt.Errorf("找不到模型 %s", activeModel)
	}
	activeModel = m.Name
	activeModelDir = m.Dir
	return m.Dir, nil
}

// ---------------- 推理 ----------------

func buildPrompt(messages []chatMessage, sysIni string) string {
	var sb strings.Builder
	sys := sysIni
	var sysParts []string
	for _, m := range messages {
		if m.Role == "system" || m.Role == "developer" {
			if strings.TrimSpace(m.Text()) != "" {
				sysParts = append(sysParts, m.Text())
			}
		}
	}
	sysAll := strings.Join(sysParts, "\n")
	if strings.TrimSpace(sys) != "" {
		sb.WriteString("系统：")
		sb.WriteString(sys)
		sb.WriteString("\n\n")
	}
	if strings.TrimSpace(sysAll) != "" {
		sb.WriteString("系统：")
		sb.WriteString(sysAll)
		sb.WriteString("\n\n")
	}
	// 完整多轮历史（C 版只取最后一条用户消息，这里补齐）
	for _, m := range messages {
		t := strings.TrimSpace(m.Text())
		if t == "" {
			continue
		}
		switch m.Role {
		case "user":
			sb.WriteString("用户：")
		case "assistant":
			sb.WriteString("助手：")
		case "system", "developer":
			continue
		default:
			sb.WriteString("用户：")
		}
		sb.WriteString(t)
		sb.WriteString("\n")
	}
	sb.WriteString("助手：")
	return sb.String()
}

// 启动 llm_demo，返回 stdout 读取器
func startInference(ctx context.Context, modelDir, prompt string, maxTokens int) (*exec.Cmd, io.ReadCloser, error) {
	if !fileExists(llmDemo) {
		return nil, nil, fmt.Errorf("找不到 llm_demo（应为 %s）", llmDemo)
	}
	tmp, err := os.CreateTemp("", "hnn-prompt-*.txt")
	if err != nil {
		return nil, nil, err
	}
	path := tmp.Name()
	if _, err := tmp.WriteString(prompt); err != nil {
		tmp.Close()
		os.Remove(path)
		return nil, nil, err
	}
	tmp.Close()
	cmd := exec.CommandContext(ctx, llmDemo, filepath.Join(modelDir, "config.json"), path, strconv.Itoa(maxTokens))
	cmd.Dir = modelDir
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		os.Remove(path)
		return nil, nil, err
	}
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		os.Remove(path)
		return nil, nil, err
	}
	go func() {
		defer os.Remove(path)
		sc := bufio.NewScanner(stderr)
		for sc.Scan() {
			line := strings.TrimSpace(sc.Text())
			if line != "" {
				fmt.Fprintf(os.Stderr, "[llm] %s\n", line)
			}
		}
	}()
	return cmd, stdout, nil
}

// 跳过 llm_demo 的启动日志，定位到正文，并按行流式返回
func bodyReader(ctx context.Context, r io.Reader, cmd *exec.Cmd) (*bufio.Reader, error) {
	br := bufio.NewReaderSize(r, 64*1024)
	ch := make(chan error, 1)
	timer := time.AfterFunc(45*time.Second, func() {
		if cmd != nil && cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
	})
	defer timer.Stop()
	go func() {
		for {
			line, err := br.ReadString('\n')
			if err != nil {
				ch <- err
				return
			}
			if strings.Contains(line, "prompt file is") {
				ch <- nil
				return
			}
		}
	}()
	select {
	case err := <-ch:
		if err != nil {
			return nil, fmt.Errorf("llm_demo 未输出正文（日志格式变化或启动失败）：%v", err)
		}
		return br, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// 把文本流拆成 reasoning / content 两路
type thinkSplitter struct {
	state  int // 0 未定 1 思考 2 正文
	buf    string
	inited bool
}

func (s *thinkSplitter) feed(chunk string) (reason, content string) {
	s.buf += chunk
	for {
		switch s.state {
		case 0:
			if i := strings.Index(s.buf, "<think>"); i >= 0 {
				head := s.buf[:i]
				s.buf = s.buf[i+len("<think>"):]
				s.state = 1
				s.inited = true
				if strings.TrimSpace(head) != "" {
					content += head
				}
				continue
			}
			if len(s.buf) > 64 {
				content += s.buf
				s.buf = ""
				s.state = 2
				s.inited = true
				return
			}
			return
		case 1:
			if i := strings.Index(s.buf, "</think>"); i >= 0 {
				reason += s.buf[:i]
				s.buf = s.buf[i+len("</think>"):]
				s.state = 2
				reason = strings.TrimPrefix(reason, "\n")
				continue
			}
			// 保留可能是半个结束标签的尾巴
			hold := 0
			for k := 1; k < len("</think>") && k <= len(s.buf); k++ {
				if strings.HasSuffix(s.buf, "</think>"[:k]) {
					hold = k
				}
			}
			if len(s.buf) > hold {
				reason += s.buf[:len(s.buf)-hold]
				s.buf = s.buf[len(s.buf)-hold:]
			}
			return
		case 2:
			content += s.buf
			s.buf = ""
			return
		}
	}
}

// flush 收尾：把残留缓冲吐出来（没闭合的 think 也归到思考）
func (s *thinkSplitter) flush() (reason, content string) {
	if s.buf == "" {
		return
	}
	switch s.state {
	case 1:
		reason = s.buf
	case 2:
		content = s.buf
	default:
		content = s.buf
	}
	s.buf = ""
	return
}

// ---------------- HTTP ----------------

type sseWriter struct {
	w    http.ResponseWriter
	f    http.Flusher
	mu   sync.Mutex
	done chan struct{}
	once sync.Once
}

func (s *sseWriter) stopHeartbeat() {
	s.once.Do(func() { close(s.done) })
}

func (s *sseWriter) write(v any) {
	b, _ := json.Marshal(v)
	s.raw("data: " + string(b) + "\n\n")
}

func (s *sseWriter) raw(text string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	_, _ = io.WriteString(s.w, text)
	s.f.Flush()
}

func (s *sseWriter) heartbeat(ctx context.Context) {
	t := time.NewTicker(5 * time.Second)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-s.done:
			return
		case <-t.C:
			s.raw(": keep-alive\n\n")
		}
	}
}

func chunk(id, model string, created int64, delta map[string]any, finish any, obj string) map[string]any {
	if obj == "text_completion" {
		text := ""
		if v, ok := delta["text"].(string); ok {
			text = v
		}
		return map[string]any{
			"id": id, "object": obj, "created": created, "model": model,
			"choices": []map[string]any{{"index": 0, "text": text, "finish_reason": finish}},
		}
	}
	return map[string]any{
		"id": id, "object": obj, "created": created, "model": model,
		"choices": []map[string]any{{"index": 0, "delta": delta, "finish_reason": finish}},
	}
}

type chatMessage struct {
	Role    string          `json:"role"`
	Content json.RawMessage `json:"content"`
}

func (m chatMessage) Text() string {
	raw := m.Content
	if len(raw) == 0 {
		return ""
	}
	var s string
	if json.Unmarshal(raw, &s) == nil {
		return s
	}
	var parts []struct {
		Type string `json:"type"`
		Text string `json:"text"`
	}
	if json.Unmarshal(raw, &parts) == nil {
		var b strings.Builder
		for _, p := range parts {
			if p.Type == "text" || p.Type == "" {
				b.WriteString(p.Text)
			}
		}
		return b.String()
	}
	return ""
}

type chatRequest struct {
	Model         string          `json:"model"`
	Messages      []chatMessage   `json:"messages"`
	Prompt        json.RawMessage `json:"prompt"`
	Input         json.RawMessage `json:"input"`
	Stream        bool            `json:"stream"`
	StreamOptions *struct {
		IncludeUsage bool `json:"include_usage"`
	} `json:"stream_options"`
	MaxTokens           int             `json:"max_tokens"`
	MaxCompletionTokens int             `json:"max_completion_tokens"`
	Stop                json.RawMessage `json:"stop"`
	Thinking            json.RawMessage `json:"thinking"`
	EnableThinking      *bool           `json:"enable_thinking"`
	ReasoningEffort     string          `json:"reasoning_effort"`
	ChatTemplateKwargs  map[string]any  `json:"chat_template_kwargs"`
}

func (r *chatRequest) wantsThinking() bool {
	if len(r.Thinking) > 0 {
		var b bool
		if json.Unmarshal(r.Thinking, &b) == nil && b {
			return true
		}
		var obj map[string]any
		if json.Unmarshal(r.Thinking, &obj) == nil {
			if t, _ := obj["type"].(string); strings.EqualFold(t, "enabled") || strings.EqualFold(t, "true") {
				return true
			}
		}
	}
	if r.EnableThinking != nil && *r.EnableThinking {
		return true
	}
	if strings.EqualFold(r.ReasoningEffort, "low") || strings.EqualFold(r.ReasoningEffort, "medium") || strings.EqualFold(r.ReasoningEffort, "high") {
		return true
	}
	if r.ChatTemplateKwargs != nil {
		if v, ok := r.ChatTemplateKwargs["enable_thinking"].(bool); ok && v {
			return true
		}
	}
	return false
}

func (r *chatRequest) maxTok() int {
	mt := r.MaxTokens
	if mt == 0 {
		mt = r.MaxCompletionTokens
	}
	if mt <= 0 {
		mt = 512
	}
	if mt > 2048 {
		mt = 2048
	}
	return mt
}

func (r *chatRequest) stops() []string {
	if len(r.Stop) == 0 {
		return nil
	}
	var one string
	if json.Unmarshal(r.Stop, &one) == nil {
		return []string{one}
	}
	var many []string
	if json.Unmarshal(r.Stop, &many) == nil {
		return many
	}
	return nil
}

func (r *chatRequest) singlePrompt() string {
	for _, raw := range []json.RawMessage{r.Prompt, r.Input} {
		if len(raw) == 0 {
			continue
		}
		var s string
		if json.Unmarshal(raw, &s) == nil {
			return s
		}
	}
	return ""
}

func effectiveAuth() bool { return authOn || forceAuth }

func authOK(r *http.Request) bool {
	if !effectiveAuth() || apiKey == "" {
		return true
	}
	// 本机回环免密，方便本地 harness / 调试
	host, _, _ := net.SplitHostPort(r.RemoteAddr)
	if host == "127.0.0.1" || host == "::1" || host == "localhost" {
		return true
	}
	var got string
	for _, name := range []string{"Authorization", "X-Api-Key", "Api-Key", "api-key", "Token", "X-Token"} {
		if v := r.Header.Get(name); v != "" {
			got = v
			break
		}
	}
	got = strings.TrimSpace(got)
	for _, prefix := range []string{"bearer ", "token ", "apikey ", "api-key "} {
		if len(got) >= len(prefix) && strings.EqualFold(got[:len(prefix)], prefix) {
			got = strings.TrimSpace(got[len(prefix):])
			break
		}
	}
	if got == "" {
		got = strings.TrimSpace(r.URL.Query().Get("key"))
	}
	return subtle.ConstantTimeCompare([]byte(got), []byte(apiKey)) == 1
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func errJSON(w http.ResponseWriter, code int, msg, typ string) {
	writeJSON(w, code, map[string]any{"error": map[string]any{"message": msg, "type": typ}})
}

func modelsJSON() map[string]any {
	ms := scanModels()
	data := make([]map[string]any, 0, len(ms)+1)
	seen := map[string]bool{}
	for _, m := range ms {
		seen[m.Name] = true
		data = append(data, map[string]any{
			"id": m.Name, "object": "model", "created": 0, "owned_by": "ETC",
		})
	}
	if activeModel != "" && !seen[activeModel] {
		data = append(data, map[string]any{"id": activeModel, "object": "model", "created": 0, "owned_by": "ETC"})
	}
	return map[string]any{"object": "list", "data": data}
}

func estimateTokens(s string) int {
	n := 0
	for _, r := range s {
		if r < 128 {
			n++
		} else {
			n++
		}
	}
	n = len([]rune(s))
	// 英文按 4 字符 1 token 折算，中文按 1 字 1 token
	ascii := 0
	for _, r := range s {
		if r < 128 {
			ascii++
		}
	}
	other := n - ascii
	return other + (ascii+3)/4
}

// 模型自带 <think> 能力（Qwen3 / R1 等）时不要额外跑两遍，直接单遍输出并拆标签
func nativeThinking(dir string) bool {
	b, err := os.ReadFile(filepath.Join(dir, "config.json"))
	if err == nil {
		t := strings.ToLower(string(b))
		if strings.Contains(t, `"enable_thinking"`) && (strings.Contains(t, `"enable_thinking": true`) || strings.Contains(t, `"enable_thinking":true`)) {
			return true
		}
		if strings.Contains(t, `"enablethinking"`) && (strings.Contains(t, `"enablethinking": true`) || strings.Contains(t, `"enablethinking":true`)) {
			return true
		}
	}
	name := strings.ToLower(filepath.Base(dir))
	for _, k := range []string{"qwen3", "deepseek-r1", "reasoner", "glm-4.5", "glm-4.6", "qwq"} {
		if strings.Contains(name, k) {
			return true
		}
	}
	return false
}

func handleChat(w http.ResponseWriter, r *http.Request, isChat bool) {
	var req chatRequest
	body, err := io.ReadAll(io.LimitReader(r.Body, 16<<20))
	if err != nil {
		errJSON(w, 400, "读取请求体失败", "invalid_request_error")
		return
	}
	if err := json.Unmarshal(body, &req); err != nil {
		errJSON(w, 400, "JSON 解析失败："+err.Error(), "invalid_request_error")
		return
	}
	mdir, err := ensureActiveModel()
	if err != nil {
		errJSON(w, 503, "模型未就绪："+err.Error(), "server_error")
		return
	}
	if miss := checkModel(mdir); len(miss) > 0 {
		errJSON(w, 500, "模型文件不完整，缺少："+strings.Join(miss, "、"), "server_error")
		return
	}

	modelName := req.Model
	if modelName == "" {
		modelName = activeModel
	}

	var userText string
	if isChat && len(req.Messages) > 0 {
		for _, m := range req.Messages {
			if m.Role == "user" && strings.TrimSpace(m.Text()) != "" {
				userText = m.Text()
			}
		}
	}
	if userText == "" {
		userText = req.singlePrompt()
	}
	if strings.TrimSpace(userText) == "" {
		errJSON(w, 400, "缺少 messages[].content 或 prompt", "invalid_request_error")
		return
	}

	basePrompt := ""
	if isChat && len(req.Messages) > 0 {
		basePrompt = buildPrompt(req.Messages, loadSystemPrompt())
	} else {
		sys := loadSystemPrompt()
		var sb strings.Builder
		if sys != "" {
			sb.WriteString("系统：" + sys + "\n\n")
		}
		sb.WriteString("用户：" + userText + "\n助手：")
		basePrompt = sb.String()
	}

	id := "chatcmpl-" + randomID()
	created := time.Now().Unix()
	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()
	stops := req.stops()
	mt := req.maxTok()
	think := req.wantsThinking()
	contentKey := "content"
	chunkObj := "chat.completion.chunk"
	if !isChat {
		contentKey = "text"
		chunkObj = "text_completion"
	}

	var sse *sseWriter
	if req.Stream {
		fl, ok := w.(http.Flusher)
		if !ok {
			errJSON(w, 500, "当前连接不支持流式", "server_error")
			return
		}
		w.Header().Set("Content-Type", "text/event-stream; charset=utf-8")
		w.Header().Set("Cache-Control", "no-cache, no-transform")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("X-Accel-Buffering", "no")
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.WriteHeader(200)
		sse = &sseWriter{w: w, f: fl, done: make(chan struct{})}
		go sse.heartbeat(ctx)
		role := map[string]any{"role": "assistant", contentKey: ""}
		if think && isChat {
			role["reasoning_content"] = ""
		}
		if !isChat {
			role = map[string]any{contentKey: ""}
		}
		sse.write(chunk(id, modelName, created, role, nil, chunkObj))
	}

	var fullReason, fullContent strings.Builder

	// 第一段：思考。模型没有原生 think 能力时，用“先分析、不答”单独跑一遍，
	// 这样任何模型都能先产出思考过程，再给方案；不会把提示词本身当成答案复述。
	if think && !nativeThinking(mdir) {
		thinkPrompt := basePrompt + "\n用户：请分析这个问题，只写分析过程，不要给结论：" + strings.TrimSpace(userText) + "\n助手："
		err = runStage(ctx, mdir, thinkPrompt, minInt(mt, 384), sse, id, modelName, created, stops, "reasoning", contentKey, &fullReason)
		if err != nil && fullReason.Len() == 0 {
			fmt.Fprintf(os.Stderr, "[think] 第一段失败，降级为单轮回答：%v\n", err)
		}
		if fullReason.Len() > 0 {
			basePrompt = basePrompt + "\n助手：" + strings.TrimSpace(fullReason.String()) + "\n"
			userTail := "\n用户：根据上面的分析，直接给出最终答案（不要重复分析过程）。\n助手："
			basePrompt += strings.TrimPrefix(userTail, "\n")
		}
	}

	err = runStage(ctx, mdir, basePrompt, mt, sse, id, modelName, created, stops, "content", contentKey, &fullContent)
	if err != nil && fullContent.Len() == 0 {
		errJSON(w, 500, "模型推理失败："+err.Error(), "server_error")
		return
	}

	if sse != nil {
		sse.write(chunk(id, modelName, created, map[string]any{}, "stop", chunkObj))
		if req.StreamOptions != nil && req.StreamOptions.IncludeUsage {
			pt := estimateTokens(basePrompt)
			ct := estimateTokens(fullContent.String()) + estimateTokens(fullReason.String())
			sse.write(map[string]any{
				"id": id, "object": "chat.completion.chunk", "created": created, "model": modelName,
				"choices": []map[string]any{},
				"usage":   map[string]any{"prompt_tokens": pt, "completion_tokens": ct, "total_tokens": pt + ct},
			})
		}
		sse.stopHeartbeat()
		sse.raw("data: [DONE]\n\n")
		return
	}

	pt := estimateTokens(basePrompt)
	ct := estimateTokens(fullContent.String()) + estimateTokens(fullReason.String())
	if !isChat {
		ct = estimateTokens(fullContent.String())
	}
	usage := map[string]any{"prompt_tokens": pt, "completion_tokens": ct, "total_tokens": pt + ct}
	if !isChat {
		writeJSON(w, 200, map[string]any{
			"id": id, "object": "text_completion", "created": created, "model": modelName,
			"choices": []map[string]any{{"index": 0, "text": strings.TrimSpace(fullContent.String()), "finish_reason": "stop"}},
			"usage":   usage,
		})
		return
	}
	msg := map[string]any{"role": "assistant", "content": strings.TrimSpace(fullContent.String())}
	if strings.TrimSpace(fullReason.String()) != "" {
		msg["reasoning_content"] = strings.TrimSpace(fullReason.String())
	}
	writeJSON(w, 200, map[string]any{
		"id": id, "object": "chat.completion", "created": created, "model": modelName,
		"choices": []map[string]any{{"index": 0, "message": msg, "finish_reason": "stop"}},
		"usage":   usage,
	})
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// runStage 跑一次 llm_demo，把输出按 kind 写进 SSE / 缓冲区。
// kind=reasoning 时原样当思考；kind=content 时若模型自带 <think> 标签也能拆出来。
func runStage(ctx context.Context, mdir, prompt string, maxTok int, sse *sseWriter, id string, model string, created int64, stops []string, kind string, contentKey string, out *strings.Builder) error {
	inferMu.Lock()
	cmd, stdout, err := startInference(ctx, mdir, prompt, maxTok)
	if err != nil {
		inferMu.Unlock()
		return err
	}
	defer func() {
		_ = stdout.Close() // 先关管道，避免子进程写满缓冲后 Wait 卡死
		done := make(chan struct{})
		go func() { _ = cmd.Wait(); close(done) }()
		select {
		case <-done:
		case <-time.After(3 * time.Second):
			if cmd.Process != nil {
				_ = cmd.Process.Kill()
			}
			<-done
		}
		inferMu.Unlock()
	}()

	br, err := bodyReader(ctx, stdout, cmd)
	if err != nil {
		return err
	}
	obj := "chat.completion.chunk"
	if contentKey == "text" {
		obj = "text_completion"
	}
	split := &thinkSplitter{}
	for {
		line, rerr := br.ReadString('\n')
		if rerr != nil {
			break
		}
		t := strings.TrimSpace(line)
		if strings.HasPrefix(t, "#####") || strings.HasPrefix(t, "prompt tokens num") || strings.HasPrefix(t, "decode tokens num") {
			break
		}
		if stopped(line, stops) {
			break
		}
		if kind == "reasoning" {
			out.WriteString(line)
			if sse != nil {
				sse.write(chunk(id, model, created, map[string]any{"reasoning_content": strings.TrimPrefix(line, "\n")}, nil, obj))
			}
			continue
		}
		reason, content := split.feed(line)
		if reason != "" {
			reason = strings.TrimPrefix(reason, "\n")
			if reason != "" && sse != nil {
				sse.write(chunk(id, model, created, map[string]any{"reasoning_content": reason}, nil, obj))
			}
		}
		if content != "" {
			out.WriteString(content)
			if sse != nil {
				sse.write(chunk(id, model, created, map[string]any{contentKey: content}, nil, obj))
			}
		}
	}
	rf, cf := split.flush()
	if rf != "" && sse != nil {
		sse.write(chunk(id, model, created, map[string]any{"reasoning_content": strings.TrimPrefix(rf, "\n")}, nil, obj))
	}
	if cf != "" {
		out.WriteString(cf)
		if sse != nil {
			sse.write(chunk(id, model, created, map[string]any{contentKey: cf}, nil, obj))
		}
	}
	return nil
}

func answerReason(s string) {}

func stopped(line string, stops []string) bool {
	for _, s := range stops {
		if len([]rune(s)) < 2 || strings.TrimSpace(s) == "" {
			continue // 忽略 "\n\n"、单字符这类会误伤正文的 stop
		}
		if strings.Contains(line, s) {
			return true
		}
	}
	return false
}

func randomID() string {
	b := make([]byte, 12)
	_, _ = rand.Read(b)
	return fmt.Sprintf("%x", b)
}

func handler(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodOptions {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Headers", "Authorization, Content-Type, x-api-key")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.WriteHeader(204)
		return
	}
	path := strings.TrimSuffix(r.URL.Path, "/")
	if path == "" {
		path = "/"
	}
	if path == "/health" || path == "/v1/health" || path == "/" {
		writeJSON(w, 200, map[string]any{
			"status": "ok", "version": version, "models": len(scanModels()),
			"auth": authOn || forceAuth, "instance": "ETC", "engine": "go",
		})
		return
	}
	if path == "/v1/models" || path == "/models" {
		// 模型列表不校验密钥：harness 常先无 key 探测可用性
		writeJSON(w, 200, modelsJSON())
		return
	}
	if strings.HasPrefix(path, "/v1/models/") || strings.HasPrefix(path, "/models/") {
		id := strings.TrimPrefix(path, "/v1/models/")
		id = strings.TrimPrefix(id, "/models/")
		if m, ok := findModelByName(id); ok {
			writeJSON(w, 200, map[string]any{"id": m.Name, "object": "model", "created": 0, "owned_by": "ETC"})
		} else {
			errJSON(w, 404, "model not found", "invalid_request_error")
		}
		return
	}
	if r.Method == http.MethodPost && (path == "/v1/chat/completions" || path == "/chat/completions") {
		if !authOK(r) {
			errJSON(w, 401, "invalid api key", "authentication_error")
			return
		}
		handleChat(w, r, true)
		return
	}
	if r.Method == http.MethodPost && (path == "/v1/completions" || path == "/completions") {
		if !authOK(r) {
			errJSON(w, 401, "invalid api key", "authentication_error")
			return
		}
		handleChat(w, r, false)
		return
	}
	errJSON(w, 404, "not found", "invalid_request_error")
}

func startHTTP(ctx context.Context) error {
	mux := http.NewServeMux()
	if remoteURL != "" {
		u, err := url.Parse(remoteURL)
		if err != nil {
			return fmt.Errorf("远程地址无效：%v", err)
		}
		proxy := httputil.NewSingleHostReverseProxy(u)
		mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
			writeJSON(w, 200, map[string]any{"status": "ok", "version": version, "mode": "remote",
				"remote": remoteURL, "auth": effectiveAuth(), "engine": "go"})
		})
		mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
			r.Host = u.Host
			proxy.ServeHTTP(w, r)
		})
	} else {
		mux.HandleFunc("/", handler)
	}
	srv := &http.Server{
		Addr:              fmt.Sprintf(":%d", apiPort),
		Handler:           mux,
		ReadHeaderTimeout: 60 * time.Second,
		IdleTimeout:       120 * time.Second,
		WriteTimeout:      0,
	}
	go func() {
		<-ctx.Done()
		sh, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		_ = srv.Shutdown(sh)
	}()
	fmt.Printf("\n  ✅ 服务已启动：监听 0.0.0.0:%d（本机 http://127.0.0.1:%d）\n", apiPort, apiPort)
	for _, ip := range lanIPs() {
		fmt.Printf("     局域网 http://%s:%d/v1\n", ip, apiPort)
	}
	if remoteURL != "" {
		fmt.Printf("     远程模式：转发到 %s（/health 仍由本机响应）\n", remoteURL)
	}
	fmt.Printf("  📍 模型：%s\n", activeModel)
	fmt.Printf("  🔑 密钥：%s%s\n", apiKey, map[bool]string{true: "（强制）", false: map[bool]string{true: "", false: "（未启用）"}[authOn || forceAuth]}[forceAuth])
	fmt.Printf("  接口：/v1/chat/completions · /v1/completions · /v1/models · /health\n")
	fmt.Printf("  深度思考：请求体加 \"thinking\": true，返回 reasoning_content 流\n")
	fmt.Printf("  Ctrl+C 停止服务\n\n")
	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		return err
	}
	return nil
}

// ---------------- 菜单 ----------------

func banner() {
	fmt.Printf("\n  HNN CN ET Server v%s（Go 版）\n", version)
	fmt.Printf("  模型目录：%s\n", modelRoot)
	for _, ip := range lanIPs() {
		fmt.Printf("  局域网：http://%s:%d/v1\n", ip, apiPort)
	}
	fmt.Printf("  鉴权：%s\n", map[bool]string{true: "开（本地回环免密，模型列表公开）", false: "关（任何人可调用）"}[effectiveAuth()])
	fmt.Printf("  密钥：%s\n", apiKey)
}

func lanIPs() []string {
	var out []string
	ifs, err := net.Interfaces()
	if err != nil {
		return out
	}
	for _, i := range ifs {
		if i.Flags&net.FlagUp == 0 || i.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := i.Addrs()
		for _, a := range addrs {
			if ipn, ok := a.(*net.IPNet); ok && ipn.IP.To4() != nil {
				out = append(out, ipn.IP.String())
			}
		}
	}
	return out
}

func listModelsInteractive() {
	ms := scanModels()
	if len(ms) == 0 {
		fmt.Printf("  （%s 下还没有模型，先到菜单 2 下载）\n", modelRoot)
		return
	}
	for idx, m := range ms {
		miss := checkModel(m.Dir)
		status := "完整"
		if len(miss) > 0 {
			status = "缺 " + strings.Join(miss, ",")
		}
		fmt.Printf("  %2d. %-46s [%s]\n", idx+1, m.Name, status)
	}
}

func menuLoop() {
	for {
		fmt.Printf("\n  请选择：\n")
		fmt.Printf("    1 = 选模型并启动服务\n")
		fmt.Printf("    2 = 模型市场（联网下载新模型）\n")
		fmt.Printf("    3 = 远程模式（转发到别的服务地址）\n")
		fmt.Printf("    4 = 改端口（当前 %d）\n", apiPort)
		fmt.Printf("    7 = 密钥开关（当前：%s）\n", onOff(authOn))
		fmt.Printf("    8 = 强制密钥（当前：%s）\n", onOff(forceAuth))
		fmt.Printf("    0 = 退出\n")
		fmt.Printf("  请输入数字并回车: ")
		line := readLine()
		switch strings.TrimSpace(line) {
		case "1":
			fmt.Printf("\n  ── 已下载好的模型 ──\n")
			listModelsInteractive()
			fmt.Printf("  输入模型名称或编号回车（直接回车=当前 %s）: ", activeModel)
			name := strings.TrimSpace(readLine())
			if name == "" {
				name = activeModel
			}
			var m Model
			var ok bool
			if n, err := strconv.Atoi(name); err == nil {
				ms := scanModels()
				if n >= 1 && n <= len(ms) {
					m, ok = ms[n-1], true
				}
			} else {
				m, ok = findModelByName(name)
			}
			if !ok {
				fmt.Printf("  未找到该模型\n")
				continue
			}
			if miss := checkModel(m.Dir); len(miss) > 0 {
				fmt.Printf("  ❌ 模型不完整，缺少：%s\n", strings.Join(miss, "、"))
				fmt.Printf("  请重新下载，或把缺失文件放到：%s\n", m.Dir)
				continue
			}
			activeModel, activeModelDir = m.Name, m.Dir
			saveConf()
			fmt.Printf("  正在加载模型，首次可能需要数十秒…\n")
			ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
			err := startHTTP(ctx)
			stop()
			if err != nil {
				fmt.Printf("  ❌ 启动失败：%v\n", err)
			}
			fmt.Printf("\n  服务已停止，返回主菜单\n")
		case "2":
			fmt.Printf("  输入模型名下载（如 Qwen2.5-0.5B-Instruct-MNN），回车开始：\n  > ")
			name := strings.TrimSpace(readLine())
			if name == "" {
				continue
			}
			if err := downloadModel(name); err != nil {
				fmt.Printf("  ❌ 下载失败：%v\n", err)
			} else {
				fmt.Printf("  ✅ 下载完成：%s\n", filepath.Join(modelRoot, name))
			}
		case "3":
			fmt.Printf("  当前远程地址：%s\n", remoteURL)
			fmt.Printf("  输入新的服务地址（如 http://127.0.0.1:18080，留空=关闭远程模式）：\n  > ")
			remoteURL = strings.TrimSpace(readLine())
			saveConf()
			fmt.Printf("  已保存，下次启动生效\n")
		case "4":
			fmt.Printf("  新端口（1-65535，当前 %d）：", apiPort)
			if v, err := strconv.Atoi(strings.TrimSpace(readLine())); err == nil && v > 0 && v < 65536 {
				apiPort = v
				saveConf()
			}
		case "7":
			authOn = !authOn
			if !authOn {
				forceAuth = false
			}
			saveConf()
			fmt.Printf("  密钥开关：%s（强制=%s）\n", onOff(authOn), onOff(forceAuth))
		case "8":
			forceAuth = !forceAuth
			saveConf()
			fmt.Printf("  强制密钥：%s\n", onOff(forceAuth))
		case "0":
			fmt.Printf("  已退出\n")
			return
		}
	}
}

func onOff(b bool) string {
	if b {
		return "开"
	}
	return "关"
}

var stdin = bufio.NewReader(os.Stdin)

func readLine() string {
	s, _ := stdin.ReadString('\n')
	return strings.TrimRight(s, "\r\n")
}

// ---------------- 模型市场 ----------------

func downloadModel(name string) error {
	api := fmt.Sprintf("https://modelscope.cn/api/v1/models/MNN/%s/repo/files?Recursive=1", name)
	resp, err := http.Get(api)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
	if resp.StatusCode != 200 {
		return fmt.Errorf("获取仓库信息失败 HTTP %d（模型名是否存在？）", resp.StatusCode)
	}
	re := regexp.MustCompile(`"Path":"([^"]+)"`)
	var files []string
	for _, m := range re.FindAllStringSubmatch(string(body), -1) {
		if m[1] != "" && !strings.HasSuffix(m[1], "/") {
			files = append(files, m[1])
		}
	}
	if len(files) == 0 {
		return fmt.Errorf("仓库里没有可下载文件（模型名是否正确？）")
	}
	base := filepath.Join(modelRoot, name)
	client := &http.Client{Timeout: 0}
	for i, f := range files {
		dst := filepath.Join(base, filepath.FromSlash(f))
		if st, err := os.Stat(dst); err == nil && st.Size() > 0 {
			fmt.Printf("  [%d/%d] 已存在 %s\n", i+1, len(files), f)
			continue
		}
		_ = os.MkdirAll(filepath.Dir(dst), 0755)
		u := fmt.Sprintf("https://modelscope.cn/models/MNN/%s/resolve/master/%s", name, f)
		fmt.Printf("  [%d/%d] 下载 %s\n", i+1, len(files), f)
		if err := downloadFile(client, u, dst); err != nil {
			return fmt.Errorf("%s：%v", f, err)
		}
	}
	return nil
}

func downloadFile(c *http.Client, u, dst string) error {
	req, _ := http.NewRequest(http.MethodGet, u, nil)
	resp, err := c.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	tmp := dst + ".part"
	f, err := os.Create(tmp)
	if err != nil {
		return err
	}
	n, err := io.Copy(f, resp.Body)
	f.Close()
	if err != nil {
		os.Remove(tmp)
		return err
	}
	if resp.ContentLength > 0 && n != resp.ContentLength {
		os.Remove(tmp)
		return fmt.Errorf("下载不完整 %d/%d 字节", n, resp.ContentLength)
	}
	return os.Rename(tmp, dst)
}

// ---------------- 入口 ----------------

func main() {
	initPaths()
	loadConf()

	serveNow := false
	modelFlag := ""
	for i := 1; i < len(os.Args); i++ {
		switch os.Args[i] {
		case "--port", "-p":
			if i+1 < len(os.Args) {
				if v, err := strconv.Atoi(os.Args[i+1]); err == nil && v > 0 && v < 65536 {
					apiPort = v
				}
				i++
			}
		case "--model", "-m":
			if i+1 < len(os.Args) {
				modelFlag = os.Args[i+1]
				i++
			}
		case "--models":
			if i+1 < len(os.Args) {
				modelRoot = os.Args[i+1]
				modelRootSet = true
				i++
			}
		case "--serve":
			serveNow = true
		case "--key":
			if i+1 < len(os.Args) {
				apiKey = os.Args[i+1]
				i++
			}
		case "--no-auth":
			authOn = false
		case "--no-remote":
			remoteURL = ""
		case "--version", "-v":
			fmt.Printf("HNN CN ET Server v%s (go)\n", version)
			return
		}
	}
	banner()

	if serveNow {
		if modelFlag != "" {
			m, ok := findModelByName(modelFlag)
			if !ok {
				fmt.Printf("  ❌ 找不到模型 %s\n", modelFlag)
				os.Exit(1)
			}
			activeModel, activeModelDir = m.Name, m.Dir
		}
		mdir, err := ensureActiveModel()
		if err != nil {
			fmt.Printf("  ❌ %v\n", err)
			os.Exit(1)
		}
		if miss := checkModel(mdir); len(miss) > 0 {
			fmt.Printf("  ❌ 模型文件不完整，缺少：%s\n", strings.Join(miss, "、"))
			os.Exit(1)
		}
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer stop()
		if err := startHTTP(ctx); err != nil {
			fmt.Printf("  ❌ 启动失败：%v\n", err)
			os.Exit(1)
		}
		return
	}

	menuLoop()
}
