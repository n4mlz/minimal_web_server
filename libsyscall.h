#pragma once

// Minimal Linux x86_64 syscall layer (no libc).
// Returns negative errno on failure (e.g. -2 == -ENOENT).

typedef unsigned long  u64;
typedef long           s64;
typedef unsigned int   u32;
typedef int            s32;
typedef unsigned short u16;
typedef unsigned char  u8;

#define NULL ((void*)0)

enum {
  // file flags
  O_RDONLY    = 0,
  O_WRONLY    = 1,
  O_RDWR      = 2,
  O_CLOEXEC   = 02000000,
  O_DIRECTORY = 0200000,
  O_NOFOLLOW  = 0400000,
  O_PATH      = 010000000,

  // at flags
  AT_FDCWD        = -100,
  AT_SYMLINK_NOFOLLOW = 0x100,

  // socket / net
  AF_INET     = 2,
  SOCK_STREAM = 1,

  // setsockopt
  SOL_SOCKET  = 1,
  SO_REUSEADDR = 2,

  // accept4 flags
  SOCK_CLOEXEC = 02000000,

  // sendto flags
  MSG_NOSIGNAL = 0x4000,
};

struct sockaddr {
  u16 sa_family;
  char sa_data[14];
};

struct in_addr {
  u32 s_addr;
};

struct sockaddr_in {
  u16 sin_family;
  u16 sin_port;
  struct in_addr sin_addr;
  u8  sin_zero[8];
};

struct stat {
  u64 st_dev;
  u64 st_ino;
  u64 st_nlink;
  u32 st_mode;
  u32 st_uid;
  u32 st_gid;
  u32 __pad0;
  u64 st_rdev;
  s64 st_size;
  s64 st_blksize;
  s64 st_blocks;
  u64 st_atime;
  u64 st_atime_nsec;
  u64 st_mtime;
  u64 st_mtime_nsec;
  u64 st_ctime;
  u64 st_ctime_nsec;
  s64 __unused[3];
};

static inline u16 htons(u16 x) { return (u16)((x << 8) | (x >> 8)); }

// syscalls
s64 sys_read(int fd, void* buf, u64 count);
s64 sys_write(int fd, const void* buf, u64 count);
s64 sys_close(int fd);

s64 sys_openat(int dirfd, const char* path, int flags, int mode);
s64 sys_newfstatat(int dirfd, const char* path, struct stat* st, int flags);

s64 sys_socket(int domain, int type, int protocol);
s64 sys_setsockopt(int fd, int level, int optname, const void* optval, u32 optlen);
s64 sys_bind(int fd, const struct sockaddr* addr, u32 addrlen);
s64 sys_listen(int fd, int backlog);
s64 sys_accept4(int fd, struct sockaddr* addr, u32* addrlen, int flags);
s64 sys_sendto(int fd, const void* buf, u64 len, int flags, const void* addr, u32 addrlen);

__attribute__((noreturn)) void sys_exit_group(int code);
