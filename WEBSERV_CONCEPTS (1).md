# Webserv - Concepts Deep Dive

A comprehensive guide to all the concepts, techniques, and design decisions in this HTTP/1.1 server implementation.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architecture Overview](#2-architecture-overview)
3. [Non-Blocking I/O](#3-non-blocking-io)
4. [I/O Multiplexing with select()](#4-io-multiplexing-with-select)
5. [HTTP Request Parsing](#5-http-request-parsing)
6. [HTTP Response Building](#6-http-response-building)
7. [Configuration System](#7-configuration-system)
8. [Routing System](#8-routing-system)
9. [Static File Serving](#9-static-file-serving)
10. [File Upload & Multipart](#10-file-upload--multipart)
11. [CGI (Common Gateway Interface)](#11-cgi-common-gateway-interface)
12. [Connection Management](#12-connection-management)
13. [Error Handling Strategy](#13-error-handling-strategy)
14. [Security Considerations](#14-security-considerations)
15. [Design Patterns Used](#15-design-patterns-used)
16. [End-to-End Request Lifecycle](#16-end-to-end-request-lifecycle)
17. [Utility Systems](#17-utility-systems)

---

## 1. Project Overview

**Webserv** is an HTTP/1.1 web server written in C++98. It handles client requests asynchronously using non-blocking sockets and the `select()` system call for I/O multiplexing. It supports static file serving, CGI script execution, file uploads, and a NGINX-like configuration format.

### Key Constraints

| Constraint | Why It Matters |
|---|---|
| **C++98 only** | No `auto`, range-for, smart pointers, `std::array`, lambdas, move semantics |
| **One select()** | All I/O multiplexing goes through a single `select()` call (or equivalent) |
| **One epoll()/kqueue()** | Not used here; `select()` is the simplest cross-platform option |
| **Non-blocking I/O** | All sockets and pipes must be non-blocking |
| **No threads** | Single-threaded; all concurrency via async I/O |
| **Read config file** | Must parse a NGINX-inspired config on startup |
| **Allowed syscalls only** | No external libraries beyond standard POSIX calls |

---

## 2. Architecture Overview

### High-Level Component Diagram

```
┌──────────────────────────────────────────────────────┐
│                      main.cpp                        │
│   parse args → init Logger → parse Config → Server   │
└──────────────────────────┬───────────────────────────┘
                           │
                    ┌──────▼──────┐
                    │   Server     │  ← Event loop (select)
                    │              │     owns all sockets & clients
                    └──────┬───────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   ┌────▼─────┐    ┌───────▼───────┐   ┌──────▼──────┐
   │  Socket   │    │    Client     │   │ ServerConfig│
   │ (listen)  │    │  (per conn)   │   │  + Route    │
   └───────────┘    └───────┬───────┘   └─────────────┘
                            │
              ┌─────────────┼─────────────┐
              │             │             │
        ┌─────▼──────┐ ┌───▼────┐  ┌─────▼──────┐
        │HttpRequest │ │Response│  │RequestHandler│
        │  (parser)  │ │Builder │  │  (routing)   │
        └────────────┘ └────────┘  └──────┬───────┘
                                          │
                                   ┌──────▼──────┐
                                   │ CgiHandler  │
                                   │  (fork+exec)│
                                   └─────────────┘
```

### Component Responsibilities

| Component | Responsibility |
|---|---|
| **Server** | Main event loop: calls `select()`, accepts connections, dispatches reads/writes, manages timeouts |
| **Socket** | RAII wrapper around a TCP socket (create/bind/listen/accept/close) |
| **Client** | Represents one connected browser/client. Owns request/response objects, read/write buffers, CGI handler |
| **HttpRequest** | Parses raw bytes into method, URI, headers, body. Handles chunked decoding. |
| **HttpResponse** | Builds HTTP response: status line, headers, body. Serializes to bytes. |
| **ServerConfig** | Holds one `server { }` block's configuration. Matches URLs to routes. |
| **Route** | Holds one `location /path { }` block's configuration. Method allowlists, CGI, redirects. |
| **RequestHandler** | Routes requests to method handlers (GET/POST/DELETE). Serves files, handles uploads. |
| **CgiHandler** | Manages CGI process lifecycle: fork, pipe I/O, environment setup, output parsing. |
| **ConfigParser** | Lexes and parses the configuration file into `ServerConfig` + `Route` objects. |
| **Logger** | Singleton logger to file and/or console with timestamps and log levels. |
| **Utils** | Namespace of string, file, URL, MIME, encoding utility functions. |

---

## 3. Non-Blocking I/O

### Concept

In blocking I/O, a call to `recv()` or `send()` halts the entire program until data arrives or the operation completes. This means the server can only serve one client at a time. Non-blocking I/O allows the kernel to return immediately — either with available data or with a "would block" error — so the server can service other clients while waiting.

### Implementation

Every socket and pipe in this server is set to non-blocking mode using `fcntl()`:

```cpp
// Setting a file descriptor to non-blocking
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

This is done at three points:
1. **Listen sockets** — at creation time (`Socket::create()`)
2. **Client sockets** — at accept time (`Socket::accept()`)
3. **CGI pipes** — after fork, both stdin and stdout pipe ends (`CgiHandler::start()`)

### Handling EAGAIN / EWOULDBLOCK

When `recv()` or `send()` returns `-1` with `errno == EAGAIN` (or `EWOULDBLOCK`), it means "try again later — I can't do this right now." The server simply returns to the event loop and waits for `select()` to tell it the fd is ready again.

### Why SIGPIPE is Ignored

Writing to a socket that the client has already closed triggers `SIGPIPE`, which by default kills the process. The server ignores this signal:

```cpp
signal(SIGPIPE, SIG_IGN);
```

And uses `MSG_NOSIGNAL` on `send()` calls to prevent it per-call:

```cpp
send(fd, data, len, MSG_NOSIGNAL);
```

---

## 4. I/O Multiplexing with select()

### Concept

`select()` is a system call that monitors multiple file descriptors simultaneously, blocking until at least one becomes ready for reading, writing, or has an error. It's the core of the single-threaded async I/O model.

### The fd_set Data Structure

`select()` uses bit arrays called `fd_set` to track which fds to monitor:

```cpp
fd_set read_fds;    // fds we want to check for readability
fd_set write_fds;   // fds we want to check for writability
```

The server maintains two pairs:
- **Master sets** (`_masterReadFds`, `_masterWriteFds`): The permanent roster of all fds to monitor
- **Working sets** (`_readFds`, `_writeFds`): Copied from masters before each `select()` call, then modified by select

### The Event Loop

```cpp
while (_running) {
    FD_COPY(&_masterReadFds, &_readFds);   // Copy master → working
    FD_COPY(&_masterWriteFds, &_writeFds);

    int activity = select(_maxFd + 1, &_readFds, &_writeFds, NULL, &timeout);

    // 1. Accept new connections on listen sockets
    for each listen fd:
        if (FD_ISSET(listen_fd, &_readFds))
            acceptNewConnection();

    // 2. Process existing client connections
    for each client fd:
        // Check timeouts first
        if (FD_ISSET(client_fd, &_readFds) && no active CGI)
            handleClientRead();
        if (has active CGI)
            handle CGI pipe I/O;
        if (FD_ISSET(client_fd, &_writeFds) && no active CGI)
            handleClientWrite();
}
```

### Which FDs are Monitored?

| FD Type | Monitored For | When Added | When Removed |
|---|---|---|---|
| Listen sockets | Read (new connections) | `Server::init()` | Server shutdown |
| Client sockets | Read (incoming data) | `acceptNewConnection()` | Connection close / keep-alive write phase |
| Client sockets | Write (sending response) | `prepareResponse()` | Write complete / connection close |
| CGI stdin pipe | Write (sending body) | `startCgiIfNeeded()` | All body data written |
| CGI stdout pipe | Read (receiving output) | `startCgiIfNeeded()` | CGI process exits + pipe closed |

### The 1-Second Timeout

`select()` is called with a 1-second timeout (not infinite). This allows the server to periodically:
- Check for client idle timeouts (30s)
- Check for CGI execution timeouts (5s)
- Perform any cleanup needed

### select() vs epoll() vs kqueue()

| | select() | epoll() (Linux) | kqueue() (macOS/BSD) |
|---|---|---|---|
| **Complexity** | Simple | Moderate | Moderate |
| **Scalability** | Poor (O(n) scan) | Excellent (O(1) events) | Excellent (O(1) events) |
| **Max FDs** | 1024 (FD_SETSIZE) | Unlimited | Unlimited |
| **Portability** | All POSIX | Linux only | BSD/macOS only |

This implementation uses `select()` for simplicity and cross-platform support. For higher load, `epoll()` or `kqueue()` would be better.

---

## 5. HTTP Request Parsing

### The HTTP Request Format

```
METHOD /path?query HTTP/1.1\r\n
Header-Name: header-value\r\n
Host: example.com\r\n
\r\n
[optional-body]
```

### Parsing Strategy: Incremental Re-Parsing

The parser uses a **simplified approach**: as data arrives via `recv()`, it's appended to a raw buffer, and the entire buffer is re-parsed from scratch. This is not the most efficient approach (a finite-state machine streaming parser would be better), but it's simple and correct.

```cpp
void HttpRequest::appendData(const std::string& data) {
    _rawRequest += data;
    parse(_rawRequest);  // Re-parse the whole thing
}
```

### Parse Pipeline

```
raw bytes
    │
    ▼
┌─────────────────┐
│ 1. Find \r\n\r\n │  ← Headers/body boundary
└────────┬────────┘
         │
    ┌────▼────┐
    │ headers  │  body_text
    │  text    │
    └────┬─────┘
         │
    ┌────▼─────────┐
    │ 2. Split on  │  First line = request line
    │    first \n   │  Rest = header lines
    └────┬──────────┘
         │
    ┌────▼─────────┐
    │ 3. Parse line │  METHOD URI HTTP/1.1
    │    validate   │  - method: alpha uppercase
    │               │  - URI: must start with /
    │               │  - version: HTTP/1.0 or HTTP/1.1
    └────┬──────────┘
         │
    ┌────▼──────────┐
    │ 4. Parse each │  Host: localhost\r\n
    │    header line │  Content-Length: 42\r\n
    │    validate    │
    └────┬───────────┘
         │
    ┌────▼──────────┐
    │ 5. Parse body  │  Content-Length → read N bytes
    │               │  Transfer-Encoding: chunked → decode
    └───────────────┘
```

### Header Validation Rules

The parser enforces these HTTP/1.1 compliance checks:

| Check | Code If Failed |
|---|---|
| No space before colon (`Header : value` is invalid) | 400 |
| No duplicate `Host` header | 400 |
| No conflicting `Content-Length` + `Transfer-Encoding` | 400 |
| No CR or LF inside header values | 400 |
| `Host` header required for HTTP/1.1 | 400 |
| Method must be uppercase alpha-only token | 400 |
| URI must start with `/` | 400 |

### Handling Different Line Endings

The parser handles both `\r\n` (correct) and bare `\n` (common with telnet):

```cpp
size_t pos = _rawRequest.find("\r\n\r\n");  // Try CRLF first
if (pos == std::string::npos)
    pos = _rawRequest.find("\n\n");          // Fall back to bare LF
```

### Chunked Transfer Encoding

When `Transfer-Encoding: chunked` is set, the body is encoded as:

```
<hex-size>\r\n
<chunk-data>\r\n
<hex-size>\r\n
<chunk-data>\r\n
0\r\n
\r\n
```

The parser:
1. Reads a hex size line
2. Validates hex characters
3. Reads that many bytes of chunk data
4. Consumes the trailing `\r\n`
5. Repeats until size is `0`
6. Accumulates all chunk data into the decoded `_body`

After decoding, `_contentLength` is set to the decoded body size, and the chunked encoding is treated as resolved.

---

## 6. HTTP Response Building

### Response Format

```
HTTP/1.1 200 OK\r\n
Date: Mon, 08 Jun 2026 12:00:00 GMT\r\n
Server: webserv/1.0\r\n
Content-Type: text/html\r\n
Content-Length: 1234\r\n
Connection: close\r\n
\r\n
<html>...</html>
```

### Status Codes Supported

| Code | Reason Phrase | Used For |
|---|---|---|
| 200 | OK | Successful GET, successful POST |
| 201 | Created | Successful file upload |
| 204 | No Content | Successful DELETE |
| 301 | Moved Permanently | Redirects |
| 302 | Found | (Available for use) |
| 400 | Bad Request | Malformed request |
| 403 | Forbidden | Access denied |
| 404 | Not Found | File/route not found |
| 405 | Method Not Allowed | Method not in route allowlist |
| 408 | Request Timeout | Client idle timeout |
| 413 | Content Too Large | Body exceeds `client_max_body_size` |
| 500 | Internal Server Error | Generic server error |
| 501 | Not Implemented | PUT method |
| 502 | Bad Gateway | CGI returned error \\
| 504 | Gateway Timeout | CGI execution timeout |
| 505 | HTTP Version Not Supported | Non 1.0/1.1 versions |

### Response Headers Set Automatically

| Header | When Set |
|---|---|
| `Date` | Always (RFC 7231 format) |
| `Server` | Always (`webserv/1.0`) |
| `Content-Type` | When body exists (from MIME map or CGI) |
| `Content-Length` | When body exists |
| `Connection` | `close` by default; `keep-alive` per conditions |
| `Location` | On redirects |
| `Set-Cookie` | Via `setCookie()` helper |

### Custom Error Pages

Error pages are configured per-server:
```
error_page 404 /error_pages/404.html;
error_page 500 502 503 504 /error_pages/50x.html;
```

The server looks for custom error pages in this order:
1. `<error_page_path>` (as given)
2. `./www/<error_page_path>`
3. `<server_root>/<error_page_path>`
4. If none found: inline HTML fallback

---

## 7. Configuration System

### NGINX-Inspired Format

```
server {
    listen 127.0.0.1:8080;
    server_name localhost www.localhost;
    root ./www/html;
    index index.html index.htm;
    client_max_body_size 1048576;

    error_page 404 /error_pages/404.html;

    location / {
        allowed_methods GET POST DELETE;
        autoindex off;
    }

    location /uploads {
        allowed_methods POST;
        upload_path ./www/uploads;
    }

    location /cgi-bin {
        allowed_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }

    location /redirect {
        return 301 /new-location;
    }
}
```

### Parser Design

The parser is **line-oriented and deterministic** (no recursive descent needed since the grammar is simple):

1. Open config file → read line by line
2. Detect `server {` blocks → delegate to `parseServerBlock()`
3. Inside server blocks, detect `location /path {` blocks → delegate to `parseLocationBlock()`
4. Other lines → treat as directives → `parseDirective()`
5. Ignore `#` comments and blank lines
6. Strip trailing `;` from directive values

### Multi-Server & Socket Sharing

When multiple `server {}` blocks share the same `host:port`:
- Only **one** socket is created for that host:port
- Both configs are stored in a map keyed by listen fd
- The `_listenConfig` map is used at accept time to assign the correct `ServerConfig*` to each client

### Default Values

| Setting | Default |
|---|---|
| Host | `0.0.0.0` (all interfaces) |
| Port | `8080` |
| root | `.` |
| client_max_body_size | `1048576` (1 MB) |
| autoindex | `off` |
| redirect code | `301` |

---

## 8. Routing System

### Longest-Prefix Matching

When a request arrives for a path like `/cgi-bin/script.py`, the server needs to find the most specific matching `location` block.

```
location /           → matches /cgi-bin/script.py (prefix match)
location /cgi-bin    → matches /cgi-bin/script.py (prefix match, longer)
location /cgi-bin/   → matches /cgi-bin/script.py (prefix match, longest)
```

The algorithm:
```cpp
Route* ServerConfig::matchRoute(const std::string& path) {
    Route* best = NULL;
    size_t bestLen = 0;
    for each route in _routes:
        if (route.matches(path) && route.path.length() > bestLen):
            best = &route;
            bestLen = route.path.length();
    return best;  // May be NULL (no route matches)
}
```

### How Route::matches() Works

A route path `/foo` matches request path `/foo/bar` but **not** `/foobar`:

```
/foo matches /foo        ✓ (exact)
/foo matches /foo/       ✓ (prefix + /)
/foo matches /foo/bar    ✓ (prefix + /)
/foo matches /foobar     ✗ (prefix but no / boundary)
/     matches /anything  ✓ (catch-all)
```

### Route Resolution Order

When a request arrives, here's the decision order:

```
1. Match route (longest prefix)
2. If route has redirect → send redirect, STOP
3. If route has allowed_methods AND method not in list → 405, STOP
4. If body > client_max_body_size → 413, STOP
5. If CGI extension matches → start CGI, STOP
6. Dispatch to method handler (GET/POST/DELETE)
```

---

## 9. Static File Serving

### File Path Resolution

The server translates URL paths to filesystem paths:

```
URL: /images/photo.jpg
Server root: ./www/html
Filesystem path: ./www/html/images/photo.jpg
```

The full algorithm in `resolveFilePath()`:
1. Start with the route-specific root (or server root if route has none)
2. Strip the matched route path prefix from the request path
3. Join root + remaining path
4. URL-decode the result (e.g., `%20` → space)
5. **Security check**: reject paths containing `..` or null bytes
6. If the result is a directory:
   - Try route-specific index files first
   - Then try server index files
   - If autoindex is on, directory listing is generated

### MIME Type Detection

MIME types are determined by file extension using a hardcoded map:

| Extension | MIME Type |
|---|---|
| .html, .htm | text/html |
| .css | text/css |
| .js | application/javascript |
| .json | application/json |
| .png | image/png |
| .jpg, .jpeg | image/jpeg |
| .gif | image/gif |
| .ico | image/x-icon |
| .pdf | application/pdf |
| .txt | text/plain |
| (none) | application/octet-stream |

### Directory Listing (Autoindex)

When `autoindex on` is set for a route and a directory is requested without an index file, the server generates an HTML page listing the directory contents:

```html
<html><head><title>Index of /path/</title></head>
<body>
<h1>Index of /path/</h1>
<hr><ul>
<li><a href="file1.txt">file1.txt</a></li>
<li><a href="subdir/">subdir/</a></li>
</ul></body></html>
```

Implemented using `opendir()` / `readdir()` / `closedir()`.

---

## 10. File Upload & Multipart

### Simple Upload (raw body)

When a POST request sends data with `Content-Type` other than `multipart/form-data`, the entire body is written as a single file:

```cpp
// File is saved as: upload_path/filename_from_uri
std::string filePath = join(route.uploadPath, fileName);
writeFile(filePath, request.body());
```

The filename is extracted from the last segment of the URL path.

### Multipart Form Data Upload

When `Content-Type: multipart/form-data; boundary=----WebKitFormBoundary` is set, the body contains:

```
------WebKitFormBoundary\r\n
Content-Disposition: form-data; name="fieldname"; filename="photo.jpg"\r\n
Content-Type: image/jpeg\r\n
\r\n
<binary file data>
------WebKitFormBoundary--\r\n
```

The parser:
1. Extracts the **boundary** string from the `Content-Type` header
2. Scans the body for boundary markers
3. Finds the part with `filename=` in `Content-Disposition`
4. Extracts the file content between boundaries
5. Validates and sanitizes the filename

### Filename Sanitization

Only these characters are allowed in uploaded filenames: `[a-zA-Z0-9._-]`

This prevents:
- Path traversal attacks (`../../../etc/passwd`)
- Null byte injection
- Special character exploits

If no filename is given, a name like `upload_<timestamp>.dat` is generated.

---

## 11. CGI (Common Gateway Interface)

### Concept

CGI allows the web server to execute external programs (Python, PHP, Perl, etc.) to generate dynamic content. The server passes the HTTP request to the program via environment variables and stdin, and the program's stdout becomes the HTTP response.

### CGI Request Flow

```
Client ──▶ HTTP Request ──▶ webserv
                                │
                     ┌──────────▼──────────┐
                     │   CgiHandler::start()│
                     │   fork() + execve()  │
                     └──┬──────────────┬────┘
                        │              │
                   stdin pipe      stdout pipe
                        │              │
              ┌─────────▼──┐    ┌─────▼──────────┐
              │ CGI Script  │    │ webserv reads   │
              │ (python/php)│    │ CGI output +    │
              │ reads stdin │    │ parses headers  │
              └─────────────┘    └─────┬──────────┘
                                       │
                              ┌────────▼────────┐
                              │ HttpResponse     │
                              │ (status, headers,│
                              │  body merged)    │
                              └─────────────────┘
```

### The fork() + execve() Pattern

```cpp
pid_t pid = fork();

if (pid == 0) {
    // CHILD PROCESS
    dup2(input_pipe[0], STDIN_FILENO);   // Redirect stdin to pipe
    dup2(output_pipe[1], STDOUT_FILENO); // Redirect stdout to pipe
    close all unused pipe ends;

    chdir(script_directory.c_str());     // Run in script's directory
    execve(executable_path, argv, envp); // Replace process with CGI binary
    exit(1);  // Only reached if execve fails
} else {
    // PARENT PROCESS
    close(input_pipe[0]);   // Close read end of stdin pipe
    close(output_pipe[1]);  // Close write end of stdout pipe

    fcntl(input_pipe[1], F_SETFL, O_NONBLOCK);   // Set remaining ends non-blocking
    fcntl(output_pipe[0], F_SETFL, O_NONBLOCK);
}
```

### Pipe Setup (Visual)

```
 PARENT (webserv)                    CHILD (CGI script)
 ================                    ===================

  input_pipe[1] ──write──▶ input_pipe[0] ── dup2 ──▶ stdin (fd 0)
                          (body data flows this way)

 output_pipe[0] ◀──read── output_pipe[1] ◀── dup2 ──▶ stdout (fd 1)
                          (CGI output flows this way)
```

The parent closes the ends it doesn't need, and the child dup2s the ends it does need onto fd 0 and fd 1.

### CGI Environment Variables

The server sets these environment variables per the CGI/1.1 spec (RFC 3875):

| Variable | Source | Example |
|---|---|---|
| `GATEWAY_INTERFACE` | Hardcoded | `CGI/1.1` |
| `SERVER_SOFTWARE` | Hardcoded | `webserv/1.0` |
| `SERVER_PROTOCOL` | Request | `HTTP/1.1` |
| `SERVER_PORT` | Config | `8080` |
| `REQUEST_METHOD` | Request | `GET` or `POST` |
| `REQUEST_URI` | Request | `/cgi-bin/script.py?q=hello` |
| `SCRIPT_NAME` | URI parsing | `/cgi-bin/script.py` |
| `SCRIPT_FILENAME` | Filesystem | `/full/path/to/cgi-bin/script.py` |
| `QUERY_STRING` | URI parsing | `q=hello` |
| `PATH_INFO` | URI after script | `/extra/path` |
| `PATH_TRANSLATED` | PATH_INFO mapped to fs | `/docroot/extra/path` |
| `CONTENT_LENGTH` | Request header | `42` |
| `CONTENT_TYPE` | Request header | `application/x-www-form-urlencoded` |
| `SERVER_NAME` | Config | `localhost` |
| `REDIRECT_STATUS` | Hardcoded | `200` |
| `HTTP_*` | Each request header | `HTTP_HOST=localhost`, `HTTP_USER_AGENT=...` |

### Parsing CGI Output

CGI scripts can output their own headers:

```
Content-Type: text/html\r\n
Status: 200 OK\r\n
X-Custom-Header: value\r\n
\r\n
<html>...</html>
```

The parser:
1. Splits CGI output at the first `\r\n\r\n` (or `\n\n`)
2. Parses header lines
3. `Status:` header → sets response status code
4. `Content-Type:` header → sets response content type
5. All other headers → passed through to the HTTP response
6. Body → becomes the response body

### CGI Error Handling

| Condition | Response Code |
|---|---|
| Script file doesn't exist | 404 |
| CGI executable doesn't exist or isn't executable | 502 |
| Non-zero exit with empty output | 502 |
| Execution timeout (5 seconds) | 504 |
| `execve()` fails | 500 |

### CGI Timeout

The server enforces a 5-second CGI timeout. If the CGI process hasn't finished after 5 seconds, the server:
1. Sends `SIGKILL` to the child process
2. Calls `waitpid()` to reap the zombie
3. Closes the pipes
4. Returns a 504 Gateway Timeout response

---

## 12. Connection Management

### Keep-Alive (Persistent Connections)

HTTP/1.1 defaults to keep-alive. This means after sending a response, the server keeps the TCP connection open for the client to send another request.

The decision logic:
```
if (HTTP/1.1 && no "Connection: close"):
    keep-alive = true
elif (HTTP/1.0 && "Connection: keep-alive"):
    keep-alive = true
else:
    keep-alive = false
```

### Connection Lifecycle

```
                    ┌──────────────┐
          accept ──▶│  READING     │◀─────────────┐
                    │  (in read_fds)│               │
                    └───────┬──────┘               │
                            │ request complete      │
                    ┌───────▼──────┐               │
                    │ PROCESSING   │               │
                    │ (build resp) │               │
                    └───────┬──────┘               │
                            │ response ready        │
                    ┌───────▼──────┐               │
                    │  WRITING     │   keep-alive  │
                    │  (in write   │───and reset───┘
                    │   _fds)      │
                    └───────┬──────┘
                            │ not keep-alive
                    ┌───────▼──────┐
                    │    CLOSED    │
                    └──────────────┘
```

### Timeouts

| Timeout | Duration | What Happens |
|---|---|---|
| **Client idle** | 30 seconds | Connection closed, fd removed from sets |
| **CGI execution** | 5 seconds | SIGKILL sent, 504 returned |

The timeouts are checked during each iteration of the event loop (which runs every ~1 second due to the select timeout).

### Connection Reset on Keep-Alive

When a keep-alive connection finishes writing:
1. Clear the read buffer
2. Clear the write buffer
3. Reset the `HttpRequest` object
4. Reset the `HttpResponse` object
5. Remove client from write fd_set
6. Add client back to read fd_set (waiting for next request)

---

## 13. Error Handling Strategy

### Error Response Generation

When an error occurs, the server:
1. Creates an `HttpResponse` with the appropriate status code
2. Looks for a custom error page configured for that status code
3. If found (and file exists): serves the custom HTML file
4. If not found: generates inline fallback HTML

The fallback HTML looks like:
```html
<html><head><title>404 Not Found</title></head>
<body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>
```

### Exception Handling

The server uses exceptions sparingly (C++98 style):
- `ConfigParser` throws on fatal config errors
- `main.cpp` catches `std::exception` for fatal startup errors
- In the event loop, most errors are handled by setting status codes rather than throwing

### Resource Cleanup

When a client connection is closed:
1. CGI handler (if exists) is `delete`d
2. Client fd is removed from `_masterReadFds` and `_masterWriteFds`
3. Client fd is `close()`d
4. Client entry is removed from the `_clients` map

---

## 14. Security Considerations

### Path Traversal Prevention

Attackers can try to access files outside the web root using `..`:

```
GET /../../../etc/passwd HTTP/1.1
```

The server rejects any URL that:
- Contains `..` segments
- Contains null bytes (`%00` injection)
- After URL decoding, resolves outside the server root

### URL-Encoded Traversal

Attackers may try encoding `..` to bypass naive checks:

```
GET /%2e%2e/%2e%2e/etc/passwd HTTP/1.1
```

The server **decodes first, then checks** — so encoded traversal is detected.

### Filename Sanitization

Uploaded filenames are strictly sanitized to only allow `[a-zA-Z0-9._-]`. This prevents:
- Path traversal via filenames
- Null byte injection
- Shell metacharacters in filenames
- Unicode homograph attacks

### SO_REUSEADDR

`SO_REUSEADDR` is set on all listen sockets, allowing immediate rebinding after server restart (even if connections are in TIME_WAIT state).

---

## 15. Design Patterns Used

### Singleton — Logger

The `Logger` class ensures only one instance exists, providing global access to logging:

```cpp
Logger& Logger::getInstance() {
    static Logger instance;  // Actually uses dynamic init in this implementation
    _instance = &instance;
    return instance;
}
```

All log output goes through `Logger::log(INFO, "message")` style calls.

### RAII — Socket

The `Socket` class acquires a file descriptor in `create()` and automatically releases it in the destructor:

```cpp
Socket::~Socket() {
    close();
}
```

This prevents fd leaks even when exceptions are thrown.

### RAII — CGI Handler

`CgiHandler` closes its pipe file descriptors in the destructor. The CGI child process pointer is also cleaned up.

### Strategy — RequestHandler Method Dispatch

Different HTTP methods are handled by different private methods, dispatched via an if/else chain:

```cpp
if (method == "GET") handleGet();
else if (method == "POST") handlePost();
else if (method == "DELETE") handleDelete();
else if (method == "HEAD") handleHead();
else if (method == "PUT") handlePut();  // returns 501
```

### Observer-Like — select() Event Loop

The `select()` call acts as an event notifier. The server "subscribes" to events on file descriptors by adding them to fd_sets, and "reacts" when `select()` reports them as ready.

### Orthodox Canonical Form

All classes follow the C++98 canonical form:
- Default constructor
- Copy constructor
- Assignment operator
- Destructor

Even when not semantically needed (e.g., `Server`), to comply with the standard form.

---

## 16. End-to-End Request Lifecycle

Here's the complete path of a single HTTP request, step by step:

```
1. CLIENT CONNECTS
   TCP handshake completes. Server's listen socket becomes readable.

2. SERVER ACCEPTS
   server.acceptNewConnection()
   ├── accept() → new client fd (set O_NONBLOCK)
   ├── Look up ServerConfig* via _listenConfig[listen_fd]
   ├── Create Client object in _clients map
   └── Add client fd to _masterReadFds

3. CLIENT SENDS DATA
   select() reports client fd readable.

4. SERVER READS
   server.handleClientRead()
   └── client.read()
       ├── recv(fd, buf, 8192, 0)  → raw bytes
       ├── Append to _readBuffer
       └── request.appendData(data) → parse!

5. REQUEST PARSED
   request.isComplete() == true

6. REQUEST PROCESSED
   client.processRequest()
   ├── Validate request (isValid?)
   ├── Create RequestHandler
   ├── Create CgiHandler
   ├── Check if CGI needed:
   │   └── If YES: cgi.start() → fork + exec
   │       ├── Register CGI pipes in fd_sets
   │       └── Return (CGI handled async)
   │   └── If NO: requestHandler.handle()
   │       ├── matchRoute()
   │       ├── Method dispatch (GET/POST/DELETE)
   │       ├── File I/O (serve/upload/delete)
   │       └── Build HttpResponse
   └── prepareResponse()
       └── response.build() → serialize to _writeBuffer

7. CGI (IF APPLICABLE — async over multiple select loops)
   a. Server writes request body to CGI stdin pipe
   b. Server reads CGI stdout pipe → _output
   c. When pipes closed + process exited: finish()
      └── parseOutput() → populate HttpResponse

8. RESPONSE READY
   Remove client from _masterReadFds
   Add client to _masterWriteFds

9. RESPONSE SENT
   select() reports client fd writable.
   server.handleClientWrite()
   └── client.write()
       └── send(fd, _writeBuffer, MSG_NOSIGNAL)

10. AFTER SEND
    If keep-alive && write complete:
        Clear buffers, reset request/response
        Move client back to _masterReadFds
    Else:
        closeClientConnection()
```

---

## 17. Utility Systems

### Logging

The `Logger` singleton supports:
- **Log levels**: DEBUG, INFO, WARNING, ERROR, FATAL
- **Dual output**: Console (stdout/stderr) and/or file (`webserv.log` by default)
- **Timestamp format**: `[2026-06-08 12:00:00] [INFO] message here`

```cpp
Logger::getInstance().log(Logger::INFO, "Server started on port " + port);
```

### String Utilities (Utils namespace)

| Function | Purpose |
|---|---|
| `trim(str)` | Remove leading/trailing whitespace |
| `toLower(str)` / `toUpper(str)` | Case conversion |
| `split(str, delim)` | Split on character or string delimiter |
| `join(vec, delim)` | Join vector of strings |
| `startsWith(str, prefix)` | Prefix check |
| `endsWith(str, suffix)` | Suffix check |
| `toString(int)` / `toInt(str)` | Type conversion |

### URL Utilities

| Function | Purpose |
|---|---|
| `urlDecode(str)` | Decode `%XX` sequences and `+` → space |
| `urlEncode(str)` | Encode special characters |
| `normalizePath(str)` | Resolve `.` and `..` segments |
| `joinPath(a, b)` | Smart path joining (handles leading/trailing `/`) |

### File Utilities

| Function | Purpose |
|---|---|
| `fileExists(path)` | Check using `access(F_OK)` |
| `isDirectory(path)` | `stat()` + `S_ISDIR` |
| `isFile(path)` | `stat()` + `S_ISREG` |
| `isReadable(path)` | `access(R_OK)` |
| `readFile(path)` | Read entire file into string |
| `writeFile(path, content)` | Write string to file |
| `getFileExtension(path)` | Extract `.jpg` from path |
| `getFileName(path)` | Extract `photo.jpg` from `/dir/photo.jpg` |
| `listDirectory(path)` | `opendir` + `readdir` → vector of names |

### Encoding Utilities

| Function | Purpose |
|---|---|
| `base64Encode(str)` | Full custom Base64 implementation |
| `base64Decode(str)` | Full custom Base64 decoder |
| `hexEncode(str)` | Bytes to hex string |
| `hexDecode(str)` | Hex string to bytes |

### HTTP Date Formatting

```cpp
std::string getHttpDate();  // Returns: "Mon, 08 Jun 2026 12:00:00 GMT"
```

Uses RFC 7231 (HTTP-date) format via `strftime()`.

---

## Key Takeaways

1. **Non-blocking I/O is the foundation**: Every I/O operation (socket recv/send, pipe read/write) is non-blocking. The server never hangs waiting for one client.

2. **select() is the scheduler**: All I/O readiness detection flows through a single `select()` call. The server reactor pattern dispatches work based on which fds are ready.

3. **CGI is the most complex feature**: It spans process management (fork/exec/waitpid), IPC (pipes), environment setup, async I/O across multiple select iterations, output parsing, and timeout handling.

4. **Incremental parsing over streaming**: The HTTP parser re-parses from scratch on each chunk of data. This is simpler to implement and debug, at the cost of some efficiency.

5. **Defense in depth**: Path traversal is blocked via content checks (`..`, null bytes), and also URL-decoded first to prevent encoding-based bypass.

6. **C++98 constraints shape the design**: No smart pointers means explicit `new`/`delete` management (especially for CGI handlers). No `auto` means verbose iterator declarations. No move semantics means copies are real copies.
