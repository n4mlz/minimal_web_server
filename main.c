#include "libsyscall.h"

static u64 c_strlen(const char* s) {
  u64 n = 0;
  while (s[n]) n++;
  return n;
}
static int c_starts_with(const char* s, const char* pfx) {
  for (u64 i = 0; pfx[i]; i++) if (s[i] != pfx[i]) return 0;
  return 1;
}
static void* c_memset(void* p, int v, u64 n) {
  u8* b = (u8*)p;
  for (u64 i = 0; i < n; i++) b[i] = (u8)v;
  return p;
}
static void* c_memcpy(void* dst, const void* src, u64 n) {
  u8* d = (u8*)dst;
  const u8* s = (const u8*)src;
  for (u64 i = 0; i < n; i++) d[i] = s[i];
  return dst;
}
static int c_is_printable_ascii(char ch) {
  return (ch >= 0x20 && ch <= 0x7e);
}

static void write_all(int fd, const char* s, u64 n) {
  while (n) {
    s64 r = sys_sendto(fd, s, n, MSG_NOSIGNAL, NULL, 0);
    if (r <= 0) return;
    s += (u64)r;
    n -= (u64)r;
  }
}
static void write_str(int fd, const char* s) {
  write_all(fd, s, c_strlen(s));
}

static u64 u64_to_dec(char out[32], u64 x) {
  char tmp[32];
  u64 n = 0;
  if (x == 0) { out[0] = '0'; return 1; }
  while (x) {
    tmp[n++] = (char)('0' + (x % 10));
    x /= 10;
  }
  for (u64 i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
  return n;
}


#define REQBUF_SZ 4096
#define PATH_MAX  512
#define SEG_MAX   255

static int is_safe_seg(const char* s, u64 n) {
  // Conservative: printable ASCII excluding: '%' '\' and control, and no ':' (avoid weird forms)
  // Also reject "." and ".." explicitly outside.
  if (n == 0 || n > SEG_MAX) return 0;
  for (u64 i = 0; i < n; i++) {
    char c = s[i];
    if (!c_is_printable_ascii(c)) return 0;
    if (c == '\\' || c == '%' || c == ':' ) return 0;
    // Disallow NUL is implicit (string slices)
  }
  return 1;
}

static const char* guess_ct(const char* path, u64 n) {
  // Minimal content-type mapping
  // Find last '.'
  s64 dot = -1;
  for (u64 i = 0; i < n; i++) if (path[i] == '.') dot = (s64)i;
  if (dot < 0) return "application/octet-stream";
  const char* ext = path + dot + 1;
  u64 elen = n - (u64)dot - 1;

  if (elen == 3 && ext[0]=='h' && ext[1]=='t' && ext[2]=='m') return "text/html; charset=utf-8";
  if (elen == 4 && ext[0]=='h' && ext[1]=='t' && ext[2]=='m' && ext[3]=='l') return "text/html; charset=utf-8";
  if (elen == 3 && ext[0]=='c' && ext[1]=='s' && ext[2]=='s') return "text/css; charset=utf-8";
  if (elen == 2 && ext[0]=='j' && ext[1]=='s') return "application/javascript; charset=utf-8";
  if (elen == 4 && ext[0]=='j' && ext[1]=='s' && ext[2]=='o' && ext[3]=='n') return "application/json; charset=utf-8";
  if (elen == 3 && ext[0]=='p' && ext[1]=='n' && ext[2]=='g') return "image/png";
  if (elen == 3 && ext[0]=='s' && ext[1]=='v' && ext[2]=='g') return "image/svg+xml";
  if (elen == 3 && ext[0]=='t' && ext[1]=='x' && ext[2]=='t') return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

static void send_status(int cfd, const char* status, const char* body) {
  write_str(cfd, "HTTP/1.1 ");
  write_str(cfd, status);
  write_str(cfd, "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ");
  char lenbuf[32];
  u64 blen = c_strlen(body);
  u64 l = u64_to_dec(lenbuf, blen);
  write_all(cfd, lenbuf, l);
  write_str(cfd, "\r\nConnection: close\r\n\r\n");
  write_all(cfd, body, blen);
}

static void send_file(int cfd, int fd, u64 size, const char* ct) {
  write_str(cfd, "HTTP/1.1 200 OK\r\nContent-Length: ");
  char lenbuf[32];
  u64 l = u64_to_dec(lenbuf, size);
  write_all(cfd, lenbuf, l);
  write_str(cfd, "\r\nContent-Type: ");
  write_str(cfd, ct);
  write_str(cfd, "\r\nConnection: close\r\n\r\n");

  char buf[8192];
  u64 left = size;
  while (left) {
    u64 want = left < (u64)sizeof(buf) ? left : (u64)sizeof(buf);
    s64 r = sys_read(fd, buf, want);
    if (r <= 0) break;
    write_all(cfd, buf, (u64)r);
    left -= (u64)r;
  }
}

// Correct stat/open logic for final segment (kept separate to avoid fstat syscall dependency)
static int open_file_stat_open(int dirfd, const char* name, int* out_fd, u64* out_size) {
  struct stat st;
  s64 r = sys_newfstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW);
  if (r < 0) return -1;
  // regular file? S_IFREG = 0100000
  if ((st.st_mode & 0170000) != 0100000) return -1;
  if (st.st_size < 0) return -1;

  s64 fd = sys_openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0);
  if (fd < 0) return -1;

  *out_fd = (int)fd;
  *out_size = (u64)st.st_size;
  return 0;
}
static int open_file_from_stat(int dirfd, const char* name, const struct stat* st, int* out_fd, u64* out_size) {
  if ((st->st_mode & 0170000) != 0100000) return -1;
  if (st->st_size < 0) return -1;

  s64 fd = sys_openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0);
  if (fd < 0) return -1;

  *out_fd = (int)fd;
  *out_size = (u64)st->st_size;
  return 0;
}

static int open_under_dist_safe(int dist_dirfd, const char* url_path, u64 n, int* out_fd, u64* out_size, const char** out_ct) {
  char pathbuf[PATH_MAX];
  if (n == 0 || n >= PATH_MAX) return -1;
  c_memcpy(pathbuf, url_path, n);
  pathbuf[n] = 0;

  // Strip query/fragment
  for (u64 i = 0; i < n; i++) {
    if (pathbuf[i] == '?' || pathbuf[i] == '#') { pathbuf[i] = 0; n = i; break; }
  }
  if (n == 0 || pathbuf[0] != '/') return -1;

  // Resolve "/" or trailing "/" -> index.html
  char full[PATH_MAX];
  u64 fn = 0;
  c_memset(full, 0, sizeof(full));
  if (n == 1) {
    const char* idx = "/index.html";
    u64 il = c_strlen(idx);
    if (il >= PATH_MAX) return -1;
    c_memcpy(full, idx, il);
    fn = il;
  } else if (pathbuf[n - 1] == '/') {
    const char* idx = "index.html";
    u64 il = c_strlen(idx);
    if (n + il >= PATH_MAX) return -1;
    c_memcpy(full, pathbuf, n);
    c_memcpy(full + n, idx, il);
    fn = n + il;
  } else {
    c_memcpy(full, pathbuf, n);
    fn = n;
  }
  int cur = dist_dirfd;

  // Walk segments
  u64 i = 1;
  while (i < fn) {
    if (full[i] == '/') return -1; // reject '//' and empty seg
    u64 seg_start = i;
    while (i < fn && full[i] != '/') i++;
    u64 seg_len = i - seg_start;

    if (seg_len == 0) return -1;
    if (seg_len == 1 && full[seg_start] == '.') return -1;
    if (seg_len == 2 && full[seg_start] == '.' && full[seg_start+1] == '.') return -1;
    if (!is_safe_seg(full + seg_start, seg_len)) return -1;

    char seg[SEG_MAX + 1];
    c_memcpy(seg, full + seg_start, seg_len);
    seg[seg_len] = 0;
    int is_last = (i == fn);
    if (!is_last) {
      s64 nfd = sys_openat(cur, seg, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC, 0);
      if (nfd < 0) return -1;
      if (cur != dist_dirfd) sys_close(cur);
      cur = (int)nfd;
      i++; // skip '/'
      continue;
    } else {
      struct stat st;
      s64 sr = sys_newfstatat(cur, seg, &st, AT_SYMLINK_NOFOLLOW);
      if (sr < 0) { if (cur != dist_dirfd) sys_close(cur); return -1; }

      u64 size = 0;
      int fd = -1;
      if ((st.st_mode & 0170000) == 0100000) {
        if (open_file_from_stat(cur, seg, &st, &fd, &size) < 0) {
          if (cur != dist_dirfd) sys_close(cur);
          return -1;
        }
        if (cur != dist_dirfd) sys_close(cur);

        *out_fd = fd;
        *out_size = size;
        *out_ct = guess_ct(full, fn);
        return 0;
      } else if ((st.st_mode & 0170000) == 0040000) {
        s64 dfd = sys_openat(cur, seg, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC, 0);
        if (dfd < 0) {
          if (cur != dist_dirfd) sys_close(cur);
          return -1;
        }
        if (cur != dist_dirfd) sys_close(cur);

        if (open_file_stat_open((int)dfd, "index.html", &fd, &size) < 0) {
          sys_close((int)dfd);
          return -1;
        }
        sys_close((int)dfd);

        char full_idx[PATH_MAX];
        if (fn + 11 >= PATH_MAX) return -1;
        c_memcpy(full_idx, full, fn);
        full_idx[fn] = '/';
        c_memcpy(full_idx + fn + 1, "index.html", 10);

        *out_fd = fd;
        *out_size = size;
        *out_ct = guess_ct(full_idx, fn + 11);
        return 0;
      }

      if (cur != dist_dirfd) sys_close(cur);
      return -1;
    }
  }
  return -1;
}

static void handle_conn(int cfd, int dist_dirfd) {
  char req[REQBUF_SZ];
  c_memset(req, 0, sizeof(req));

  s64 r = sys_read(cfd, req, sizeof(req) - 1);
  if (r <= 0) return;

  // Parse request line: METHOD SP PATH SP HTTP/...
  // Accept only GET/HEAD.
  char* p = req;

  // Find first space
  u64 i = 0;
  while (i < (u64)r && req[i] != ' ' && req[i] != '\r' && req[i] != '\n') i++;
  if (i == 0 || i >= (u64)r || req[i] != ' ') { send_status(cfd, "400 Bad Request", "bad request\n"); return; }

  int is_head = 0;
  if (i == 3 && c_starts_with(p, "GET")) is_head = 0;
  else if (i == 4 && c_starts_with(p, "HEAD")) is_head = 1;
  else { send_status(cfd, "405 Method Not Allowed", "method not allowed\n"); return; }

  u64 mlen = i;
  (void)mlen;

  // Path starts after space
  u64 ps = i + 1;
  u64 pe = ps;
  while (pe < (u64)r && req[pe] != ' ' && req[pe] != '\r' && req[pe] != '\n') pe++;
  if (pe == ps) { send_status(cfd, "400 Bad Request", "bad path\n"); return; }

  const char* path = req + ps;
  u64 path_len = pe - ps;

  // Reject absolute-form and weird beginnings
  if (path_len >= 7 && c_starts_with(path, "http://")) { send_status(cfd, "400 Bad Request", "bad request\n"); return; }
  if (path_len >= 8 && c_starts_with(path, "https://")) { send_status(cfd, "400 Bad Request", "bad request\n"); return; }

  int fd = -1;
  u64 size = 0;
  const char* ct = "application/octet-stream";
  if (open_under_dist_safe(dist_dirfd, path, path_len, &fd, &size, &ct) < 0) {
    send_status(cfd, "404 Not Found", "not found\n");
    return;
  }

  if (is_head) {
    write_str(cfd, "HTTP/1.1 200 OK\r\nContent-Length: ");
    char lenbuf[32];
    u64 l = u64_to_dec(lenbuf, size);
    write_all(cfd, lenbuf, l);
    write_str(cfd, "\r\nContent-Type: ");
    write_str(cfd, ct);
    write_str(cfd, "\r\nConnection: close\r\n\r\n");
    sys_close(fd);
    return;
  }

  send_file(cfd, fd, size, ct);
  sys_close(fd);
}

__attribute__((used)) static void server_main(void) {
  // Open /dist directory (required).
  int dist_dirfd = (int)sys_openat(AT_FDCWD, "/dist", O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC, 0);
  if (dist_dirfd < 0) sys_exit_group(1);

  int sfd = (int)sys_socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sfd < 0) sys_exit_group(2);

  int one = 1;
  (void)sys_setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, (u32)sizeof(one));

  struct sockaddr_in addr;
  c_memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);
  addr.sin_addr.s_addr = 0; // 0.0.0.0

  if (sys_bind(sfd, (const struct sockaddr*)&addr, (u32)sizeof(addr)) < 0) sys_exit_group(3);
  if (sys_listen(sfd, 128) < 0) sys_exit_group(4);

  for (;;) {
    struct sockaddr sa;
    u32 sl = (u32)sizeof(sa);
    int cfd = (int)sys_accept4(sfd, &sa, &sl, SOCK_CLOEXEC);
    if (cfd < 0) continue;
    handle_conn(cfd, dist_dirfd);
    sys_close(cfd);
  }
}

// Provide our own _start (no libc, no crt).
// Ensure 16-byte stack alignment before calling C code.
__attribute__((naked, noreturn)) void _start(void) {
  __asm__ volatile(
    "xor %rbp, %rbp\n"
    "andq $-16, %rsp\n"
    "call server_main\n"
    "mov $0, %edi\n"
    "mov $231, %eax\n"
    "syscall\n"
    "hlt\n"
  );
}
