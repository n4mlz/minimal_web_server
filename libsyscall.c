#include "libsyscall.h"

// Linux x86_64 syscall numbers
enum {
  __NR_read            = 0,
  __NR_write           = 1,
  __NR_close           = 3,
  __NR_socket          = 41,
  __NR_accept4         = 288,
  __NR_bind            = 49,
  __NR_listen          = 50,
  __NR_sendto          = 44,
  __NR_setsockopt      = 54,
  __NR_openat          = 257,
  __NR_newfstatat      = 262,
  __NR_exit_group      = 231,
};

static inline s64 syscall6(
    s64 n,
    s64 a1, s64 a2, s64 a3,
    s64 a4, s64 a5, s64 a6
) {
    s64 ret;

    register s64 r10 __asm__("r10") = a4;
    register s64 r8  __asm__("r8")  = a5;
    register s64 r9  __asm__("r9")  = a6;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline s64 syscall4(s64 n, s64 a1, s64 a2, s64 a3, s64 a4) {
  return syscall6(n, a1, a2, a3, a4, 0, 0);
}
static inline s64 syscall3(s64 n, s64 a1, s64 a2, s64 a3) {
  return syscall6(n, a1, a2, a3, 0, 0, 0);
}

s64 sys_read(int fd, void* buf, u64 count) {
  return syscall3(__NR_read, fd, (s64)buf, (s64)count);
}
s64 sys_write(int fd, const void* buf, u64 count) {
  return syscall3(__NR_write, fd, (s64)buf, (s64)count);
}
s64 sys_close(int fd) {
  return syscall3(__NR_close, fd, 0, 0);
}

s64 sys_openat(int dirfd, const char* path, int flags, int mode) {
  return syscall6(__NR_openat, dirfd, (s64)path, flags, mode, 0, 0);
}
s64 sys_newfstatat(int dirfd, const char* path, struct stat* st, int flags) {
  return syscall4(__NR_newfstatat, dirfd, (s64)path, (s64)st, flags);
}

s64 sys_socket(int domain, int type, int protocol) {
  return syscall3(__NR_socket, domain, type, protocol);
}
s64 sys_setsockopt(int fd, int level, int optname, const void* optval, u32 optlen) {
  return syscall6(__NR_setsockopt, fd, level, optname, (s64)optval, optlen, 0);
}
s64 sys_bind(int fd, const struct sockaddr* addr, u32 addrlen) {
  return syscall3(__NR_bind, fd, (s64)addr, addrlen);
}
s64 sys_listen(int fd, int backlog) {
  return syscall3(__NR_listen, fd, backlog, 0);
}
s64 sys_accept4(int fd, struct sockaddr* addr, u32* addrlen, int flags) {
  return syscall6(__NR_accept4, fd, (s64)addr, (s64)addrlen, flags, 0, 0);
}
s64 sys_sendto(int fd, const void* buf, u64 len, int flags, const void* addr, u32 addrlen) {
  return syscall6(__NR_sendto, fd, (s64)buf, (s64)len, flags, (s64)addr, addrlen);
}

__attribute__((noreturn)) void sys_exit_group(int code) {
  (void)syscall3(__NR_exit_group, code, 0, 0);
  for (;;) { }
}
