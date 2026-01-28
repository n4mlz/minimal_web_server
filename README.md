# Minimal Web Server

This repository provides a **minimal HTTP static file server** implemented in C with the following strict constraints:

- **Direct Linux syscalls only** (no libc, no POSIX wrappers)
- **Statically linked single binary**
- **no-PIE**
- **Designed to serve static files under `/dist` only**
- **Safe path resolution (no path traversal, no symlink escape)**

The result is a small, auditable binary suitable for low-level experiments, OS / ABI studies, or minimal container images.

## Design Goals

This project intentionally optimizes for _clarity and minimalism_, not feature completeness.

- Reduce the required syscall surface to the bare minimum
- Avoid libc and runtime dependencies entirely
- Make filesystem access rules explicit and verifiable
- Be easy to embed as a base image and extend via `FROM`

## Supported Features

- HTTP/1.1 `GET` and `HEAD`
- Static file serving from `/dist`
- Automatic `index.html` resolution for directories (with or without trailing `/`)
- Minimal `Content-Type` guessing
- Correct `Content-Length`
- Graceful handling of client disconnects (`MSG_NOSIGNAL` on send)

Not supported (by design):

- TLS / HTTPS
- CGI / dynamic content
- Directory listing
- Range requests
- Chunked encoding
- Concurrency / multiplexing

## Security Properties

The server enforces strict filesystem constraints:

- All paths are resolved **relative to `/dist`**
- `..`, `.`, empty segments, `%`, `\`, and non-printable characters are rejected
- Path resolution is done step-by-step using `openat`
- Intermediate components are opened with `O_PATH | O_DIRECTORY | O_NOFOLLOW`
- Final files are opened with `O_RDONLY | O_NOFOLLOW`
- Only regular files are served
- Path resolution is implemented by a single safe resolver (no legacy code paths)

This prevents:

- Path traversal (`../`)
- Symlink escape attacks
- Accidental access outside `/dist`

## Syscalls Used

The implementation intentionally limits itself to a small syscall set:

### Core I/O

- `read`
- `sendto` (`MSG_NOSIGNAL`)
- `close`

### Filesystem

- `openat`
- `newfstatat`

### Networking

- `socket`
- `setsockopt`
- `bind`
- `listen`
- `accept4`

### Process

- `exit_group`

No other syscalls are required.

## Build

The binary is built in a dedicated build stage and copied into a `scratch` image.

```sh
docker build -t minimal-static-server .
```

The resulting image contains:

- `/server` — the server binary
- `/dist` — an empty directory (mount point for content)

## Usage

### Base Image (this repository)

```sh
docker run -p 8080:8080 minimal-static-server
```

At this stage, `/dist` is empty, so all requests return `404`.

### Serving Files (recommended usage)

Create a downstream image:

```dockerfile
FROM ghcr.io/n4mlz/minimal_web_server:latest
COPY ./dist/ /dist/
```

Then build and run:

```sh
docker build -t my-static-site .
docker run -p 8080:8080 my-static-site
```

Files under `./dist` will now be served at `http://localhost:8080/`.

## Notes and Caveats

- The server is **single-threaded** and handles one connection at a time.
- There is no timeout handling; slow clients may block progress.
- The implementation is Linux **x86_64 only**.
- Because PIE is disabled and the binary is static, **ASLR is limited**.
- `_start` is a tiny asm stub to preserve **16-byte stack alignment** before calling C.
- If you build outside the Dockerfile, consider `-mno-red-zone` for robustness.
- The code favors explicitness over abstraction; expect low-level style.

This project is best viewed as a **reference implementation** or **teaching artifact**, not a production-ready web server.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
