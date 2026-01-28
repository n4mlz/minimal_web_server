# Builds a single static binary with no libc dependency (syscall-only).
# Runtime image does NOT include /dist content; downstream images can COPY into /dist.

FROM debian:stable-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends clang lld ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY libsyscall.h libsyscall.c main.c ./

# Build: freestanding, no libc, static, no PIE
RUN mkdir -p /out && \
    clang -O2 -ffreestanding -fno-builtin -mno-red-zone \
      -fno-stack-protector -fno-asynchronous-unwind-tables \
      -fno-unwind-tables -nostdlib -static -fno-pie \
      -Wl,-no-pie \
      -Wl,--gc-sections -Wl,-z,norelro -Wl,--build-id=none \
      -fdata-sections -ffunction-sections \
      -o /out/server main.c libsyscall.c && \
    mkdir -p /out/dist

FROM busybox:musl
COPY --from=build /out/server /server
COPY --from=build /out/dist /dist
EXPOSE 8080
ENTRYPOINT ["/server"]
