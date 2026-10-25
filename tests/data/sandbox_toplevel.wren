import "http" for Http

// Top-level code must run under the sandbox: outbound HTTP is denied by the
// seccomp allowlist, so this aborts instead of reaching the probe server.
Http.request("GET", "http://127.0.0.1:18096/", "", null)
