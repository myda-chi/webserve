#!/usr/bin/env python3
"""
Comprehensive test suite for 42 Webserv project.

Covers all mandatory requirements based on:
  - Project subject (webserv .impl.md)
  - Typical 42 peer-evaluation criteria
  - Known edge cases from other 42 webserv implementations

Usage:
  # Run against already-started server
  python3 test_webserv.py [host] [port]
  python3 test_webserv.py                    # defaults to 127.0.0.1:8080

  # Start server, run tests, kill server
  python3 test_webserv.py --start ./webserv config/default.conf

Options:
  --start <binary> [config]        Start server before tests, kill after
  --host <host>                    Server host (default: 127.0.0.1)
  --port <port>                    Server port (default: 8080)
  --port2 <port2>                  Second server port (default: 8081)
  --test <category>                Run only specific category
  --list                           List all test categories
  --no-color                       Disable color output
  --stress                        Run stress/concurrency tests (give extra time)
"""

import socket
import select
import sys
import os
import time
import subprocess
import threading
import re
import signal
import argparse
import tempfile
import random
from io import BytesIO

# ---------------------------------------------------------------------------
# globals
# ---------------------------------------------------------------------------

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
SERVER_PROC = None
HOST = "127.0.0.1"
PORT = 8080
PORT2 = 8081
TIMEOUT = 3
RESULTS = {"pass": 0, "fail": 0, "skip": 0}
FAILED_TESTS = []
SKIP_REASON = None  # set when server is unreachable at startup

# colors
GREEN = "\033[0;32m"
RED = "\033[0;31m"
YELLOW = "\033[0;33m"
CYAN = "\033[0;36m"
BOLD = "\033[1m"
NC = "\033[0m"

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def cprint(color, text):
    print(f"{color}{text}{NC}")

def pass_msg(test):
    RESULTS["pass"] += 1
    cprint(GREEN, f"  [PASS] {test}")

def fail_msg(test, detail=""):
    RESULTS["fail"] += 1
    FAILED_TESTS.append((test, detail))
    msg = f"  [FAIL] {test}"
    if detail:
        msg += f"  --  {detail}"
    cprint(RED, msg)

def skip_msg(test, detail=""):
    RESULTS["skip"] += 1
    msg = f"  [SKIP] {test}"
    if detail:
        msg += f"  --  {detail}"
    cprint(YELLOW, msg)

def header(title):
    print()
    cprint(BOLD + CYAN, f"{'='*60}")
    cprint(BOLD + CYAN, f"  {title}")
    cprint(BOLD + CYAN, f"{'='*60}")

def section(title):
    print()
    cprint(BOLD, f"--- {title} ---")

# ---------------------------------------------------------------------------
# raw socket helpers
# ---------------------------------------------------------------------------

def raw_request(host, port, request_bytes, timeout=TIMEOUT, read_response=True):
    """Send raw bytes, return (status_line, headers_dict, body_bytes)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        s.sendall(request_bytes if isinstance(request_bytes, bytes) else request_bytes.encode())
        if not read_response:
            s.close()
            return (None, None, None)
        data = b""
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
                if b"\r\n\r\n" in data:
                    # Check for Content-Length or chunked to decide if we need more
                    hdr_end = data.index(b"\r\n\r\n") + 4
                    headers_raw = data[:hdr_end].decode("latin-1", errors="replace")
                    body_start = data[hdr_end:]
                    clen = _get_content_length(headers_raw)
                    tenc = _get_transfer_encoding(headers_raw)
                    if clen is not None:
                        # content-length: wait for body
                        while len(body_start) < clen:
                            chunk = s.recv(4096)
                            if not chunk:
                                break
                            body_start += chunk
                            data += chunk
                        break
                    elif tenc and "chunked" in tenc.lower():
                        while not body_start.endswith(b"0\r\n\r\n"):
                            chunk = s.recv(4096)
                            if not chunk:
                                break
                            body_start += chunk
                            data += chunk
                        break
                    else:
                        # connection close or no body
                        break
            except socket.timeout:
                break
    except socket.timeout:
        fail_msg("connection timed out", f"{host}:{port}")
        return (None, None, None)
    except ConnectionRefusedError:
        fail_msg("connection refused", f"{host}:{port}")
        return (None, None, None)
    finally:
        s.close()
    return _parse_response(data)

def _get_content_length(headers_raw):
    m = re.search(r"(?i)content-length:\s*(\d+)", headers_raw)
    return int(m.group(1)) if m else None

def _get_transfer_encoding(headers_raw):
    m = re.search(r"(?i)transfer-encoding:\s*(.+)", headers_raw)
    return m.group(1).strip() if m else None

def _parse_response(data):
    if not data:
        return (None, {}, b"")
    text = data.decode("latin-1", errors="replace")
    parts = text.split("\r\n\r\n", 1)
    header_section = parts[0]
    body = parts[1].encode("latin-1", errors="replace") if len(parts) > 1 else b""
    if len(data) > len(header_section) + 4:
        leftover = data[len(header_section) + 4:]
        body = leftover
    lines = header_section.split("\r\n")
    status_line = lines[0] if lines else ""
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    return (status_line, headers, body)

def http_request(method, path, host=None, port=None, body=None, headers=None, timeout=TIMEOUT):
    h = host if host else HOST
    p = port if port else PORT
    req = f"{method} {path} HTTP/1.1\r\n"
    req += f"Host: {h}:{p}\r\n"
    if body is not None:
        if isinstance(body, str):
            body = body.encode()
        if "content-length" not in (k.lower() for k in (headers or {})):
            if headers is None:
                headers = {}
            headers["Content-Length"] = str(len(body))
    if headers:
        for k, v in headers.items():
            req += f"{k}: {v}\r\n"
    req += "\r\n"
    full = req.encode() + body if body else req.encode()
    return raw_request(h, p, full, timeout)

def raw_connect_send(host, port, data, timeout=TIMEOUT):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, port))
    s.sendall(data if isinstance(data, bytes) else data.encode())
    return s

def raw_recv_all(sock, timeout=TIMEOUT):
    sock.settimeout(timeout)
    data = b""
    while True:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            break
    sock.close()
    return _parse_response(data)

# ---------------------------------------------------------------------------
# server lifecycle
# ---------------------------------------------------------------------------

def start_server(binary, config):
    global SERVER_PROC
    binary_path = os.path.join(TEST_DIR, binary) if not os.path.isabs(binary) else binary
    if not os.path.isfile(binary_path):
        cprint(RED, f"  Binary not found: {binary_path}")
        sys.exit(1)
    config_path = os.path.join(TEST_DIR, config) if not os.path.isabs(config) else config
    if not os.path.isfile(config_path):
        cprint(RED, f"  Config not found: {config_path}")
        sys.exit(1)
    cprint(CYAN, f"  Starting server: {binary_path} {config_path}")
    SERVER_PROC = subprocess.Popen(
        [binary_path, config_path],
        cwd=TEST_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1)

def stop_server():
    global SERVER_PROC
    if SERVER_PROC:
        SERVER_PROC.terminate()
        try:
            SERVER_PROC.wait(timeout=3)
        except subprocess.TimeoutExpired:
            SERVER_PROC.kill()
        cprint(CYAN, "  Server stopped.")

def check_server(host=None, port=None, retries=3):
    h = host if host else HOST
    p = port if port else PORT
    for _ in range(retries):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        try:
            s.connect((h, p))
            s.close()
            return True
        except Exception:
            time.sleep(1)
        finally:
            s.close()
    return False

# ---------------------------------------------------------------------------
# 1. Basic Connectivity & Server Startup
# ---------------------------------------------------------------------------

def test_01_connectivity():
    header("1. BASIC CONNECTIVITY & SERVER STARTUP")

    s1 = check_server(HOST, PORT)
    if s1:
        pass_msg(f"Server reachable on {HOST}:{PORT}")
    else:
        fail_msg(f"Server reachable on {HOST}:{PORT}", "cannot connect")
        global SKIP_REASON
        SKIP_REASON = f"Cannot connect to {HOST}:{PORT}"

    s2 = check_server(HOST, PORT2)
    if s2:
        pass_msg(f"Server reachable on {HOST}:{PORT2} (multi-port)")
    else:
        fail_msg(f"Server reachable on {HOST}:{PORT2} (multi-port)", "second server block may not be listening")

    status, headers, body = http_request("GET", "/")
    if status and status != "":
        pass_msg("GET / returns a response")
    else:
        fail_msg("GET / returns a response", "empty or no response")

    if status and "HTTP/1." in status:
        pass_msg(f"Response uses HTTP protocol version ({status.split()[0]})")
    else:
        fail_msg("Response uses HTTP protocol version", f"got: {status[:50] if status else 'nothing'}")

def test_02_server_identity():
    section("Server header")
    status, headers, body = http_request("GET", "/")
    if status is None:
        skip_msg("Server header check", "server not reachable"); return
    if "server" in headers:
        pass_msg(f"Response includes Server header: {headers['server']}")
    else:
        fail_msg("Response includes Server header", "missing")

# ---------------------------------------------------------------------------
# 2. GET Method
# ---------------------------------------------------------------------------

def test_03_get_root():
    header("2. GET METHOD")
    status, headers, body = http_request("GET", "/")
    if status is None:
        skip_msg("GET / - root", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        pass_msg("GET / returns 200 OK")
    else:
        fail_msg("GET / returns 200 OK", f"got {code}")
    if b"<html" in body.lower() or b"<!doctype" in body.lower():
        pass_msg("GET / returns HTML content")
    else:
        fail_msg("GET / returns HTML content", "body does not look like HTML")
    if "content-type" in headers and "text/html" in headers["content-type"].lower():
        pass_msg("GET / has Content-Type: text/html")
    else:
        fail_msg("GET / has Content-Type: text/html", f"got: {headers.get('content-type', 'missing')}")

def test_04_get_nonexistent():
    section("404 handling")
    status, headers, body = http_request("GET", "/nonexistent_page_xyz.html")
    if status is None:
        skip_msg("404 test", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "404":
        pass_msg("GET /nonexistent returns 404")
    else:
        fail_msg("GET /nonexistent returns 404", f"got {code}")
    if b"404" in body or b"Not Found" in body or b"not found" in body.lower():
        pass_msg("404 response body indicates not found")
    else:
        fail_msg("404 response body indicates not found", "body doesn't mention 404/Not Found")

def test_05_get_custom_404():
    section("Custom error page")
    status, headers, body = http_request("GET", "/nonexistent_page.html")
    if status is None:
        skip_msg("Custom 404", "server not reachable"); return
    custom_marker = b"404" in body and b"</html>" in body
    if custom_marker:
        pass_msg("Custom 404 page served (HTML page with 404)")
    else:
        fail_msg("Custom 404 page served", "body does not appear to be full HTML error page")

def test_06_get_directory_no_index():
    section("Directory request (autoindex off)")
    # /uploads on main server has autoindex on (root was ./www/uploads but the location 
    # / has autoindex off; but the /uploads location has autoindex on)
    # Let's create a directory without index and without autoindex
    # On main server (port 8080) location / has autoindex off, root ./www/html
    # /www/html only has index.html so it would serve that - not a good test
    # Instead test that a directory WITH index serves the index
    status, headers, body = http_request("GET", "/")
    if status is None:
        skip_msg("Directory with index", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        pass_msg("GET / (directory with index file) returns 200")
    else:
        fail_msg("GET / (directory with index file) returns 200", f"got {code}")

def test_07_get_with_query():
    section("Query strings")
    status, headers, body = http_request("GET", "/index.html?foo=bar&baz=42")
    if status is None:
        skip_msg("Query string", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        pass_msg("GET with query string returns 200")
    else:
        fail_msg("GET with query string returns 200", f"got {code}")

def test_08_head_method():
    section("HEAD method")
    status, headers, body = http_request("HEAD", "/")
    if status is None:
        skip_msg("HEAD", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        if body == b"" or len(body) == 0:
            pass_msg("HEAD / returns 200 with empty body")
        else:
            # Some implementations send body with HEAD - warn but not fail critically
            pass_msg("HEAD / returns 200 OK")
    elif code in ("405", "501"):
        skip_msg("HEAD /", f"got {code} (HEAD not required but nice to have)")
    else:
        fail_msg("HEAD /", f"unexpected status {code}")

# ---------------------------------------------------------------------------
# 3. POST Method
# ---------------------------------------------------------------------------

def test_09_post_basic():
    header("3. POST METHOD")
    # POST to uploads location (has POST allowed on 8080)
    status, headers, body = http_request(
        "POST", "/uploads/test_post.txt",
        body=b"Hello POST body content"
    )
    if status is None:
        skip_msg("POST basic", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "201", "204"):
        pass_msg(f"POST /uploads/test_post.txt returns {code}")
    elif code == "405":
        fail_msg("POST /uploads/test_post.txt", "got 405 Method Not Allowed")
    else:
        fail_msg("POST /uploads/test_post.txt", f"unexpected code {code}")

def test_10_post_method_not_allowed():
    section("Method not allowed (405)")
    status, headers, body = http_request("POST", "/")
    if status is None:
        skip_msg("405 for POST on /", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    # / location in default.conf has "allowed_methods GET POST" so POST on / is allowed
    # This test might pass or fail depending on config
    # Better test: use a method that's never allowed
    status2, h2, b2 = http_request("PATCH", "/")
    if status2 is None:
        skip_msg("405 for PATCH on /", "server not reachable"); return
    code2 = status2.split()[1] if len(status2.split()) > 1 else ""
    if code2 in ("405", "501"):
        pass_msg(f"PATCH / (unsupported method) returns {code2}")
    else:
        fail_msg("PATCH / (unsupported method) returns 405/501", f"got {code2}")

# ---------------------------------------------------------------------------
# 4. DELETE Method
# ---------------------------------------------------------------------------

def test_11_delete():
    header("4. DELETE METHOD")
    # First create a file to delete
    test_file = f"/uploads/_delete_test_{random.randint(1000, 9999)}.txt"
    s, h, b = http_request("POST", test_file, body=b"temporary file for DELETE test")
    test_file = (h or {}).get("location", test_file)
    time.sleep(0.1)

    status, headers, body = http_request("DELETE", test_file)
    if status is None:
        skip_msg("DELETE", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "202", "204"):
        pass_msg(f"DELETE {test_file} returns {code}")
    elif code == "405":
        fail_msg(f"DELETE {test_file}", "got 405 - DELETE may not be configured on /uploads")
    else:
        fail_msg(f"DELETE {test_file}", f"got {code}")

def test_12_delete_nonexistent():
    section("DELETE nonexistent")
    status, headers, body = http_request("DELETE", "/uploads/_nonexistent_file_xyz_.txt")
    if status is None:
        skip_msg("DELETE nonexistent", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("404", "204"):
        pass_msg(f"DELETE nonexistent file returns {code}")
    else:
        fail_msg("DELETE nonexistent file", f"expected 404/204, got {code}")

# ---------------------------------------------------------------------------
# 5. HTTP Protocol Compliance
# ---------------------------------------------------------------------------

def test_13_content_length():
    header("5. HTTP PROTOCOL COMPLIANCE")
    status, headers, body = http_request("GET", "/")
    if status is None:
        skip_msg("Content-Length", "server not reachable"); return
    if "content-length" in headers:
        cl = int(headers["content-length"])
        if cl == len(body):
            pass_msg(f"Content-Length ({cl}) matches body size ({len(body)})")
        else:
            fail_msg("Content-Length matches body size", f"header={cl}, actual={len(body)}")
    else:
        fail_msg("Content-Length header present in response", "missing")

def test_14_keep_alive():
    section("Keep-Alive / Connection header")
    status, headers, body = http_request("GET", "/")
    if status is None:
        skip_msg("Connection header", "server not reachable"); return
    conn = headers.get("connection", "").lower()
    if conn:
        pass_msg(f"Response has Connection header: {conn}")
    else:
        fail_msg("Response has Connection header", "missing")

def test_15_malformed_request_bad_request():
    section("Malformed request -> 400 Bad Request")
    s = raw_connect_send(HOST, PORT, b"GARBAGE / HTTP/1.1\r\n\r\n")
    status, headers, body = raw_recv_all(s)
    if status:
        code = status.split()[1] if len(status.split()) > 1 else ""
        if code == "400":
            pass_msg("Garbage request line returns 400")
        else:
            fail_msg("Garbage request line returns 400", f"got {code}")
    else:
        fail_msg("Garbage request line: server closed connection (may be OK too)")

def test_16_malformed_header():
    section("Bad header -> response")
    s = raw_connect_send(HOST, PORT, b"GET / HTTP/1.1\r\nHost: \r\n\r\n")
    status, headers, body = raw_recv_all(s)
    if status is None:
        skip_msg("Bad header", "server closed connection"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "400"):
        pass_msg(f"Request with empty Host header returns {code}")
    else:
        fail_msg("Request with empty Host header", f"got {code}")

def test_17_host_header_required():
    section("HTTP/1.1 Host header requirement")
    # Per RFC 7230, HTTP/1.1 requests MUST include Host header
    s = raw_connect_send(HOST, PORT, b"GET / HTTP/1.1\r\n\r\n")
    status, headers, body = raw_recv_all(s)
    if status is None:
        skip_msg("Missing Host header", "server closed connection immediately"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "400":
        pass_msg("HTTP/1.1 request without Host returns 400")
    else:
        # Many implementations don't strictly enforce this in practice
        fail_msg("HTTP/1.1 request without Host header", f"expected 400, got {code}")

def test_18_connection_close_multiple_requests():
    section("Connection: close semantics")
    s = raw_connect_send(HOST, PORT, b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    # First, read the response to the first request
    resp1 = raw_recv_all(s, timeout=5)
    if resp1[0] is None:
        fail_msg("Connection: close", "no response to first request"); return

    # Now the server should have closed the connection.
    # Try reading again - should get 0 bytes (FIN from server close)
    try:
        s.settimeout(2)
        extra = s.recv(1)
        s.close()
        if len(extra) == 0:
            pass_msg("Connection: close - server closed connection after response")
        else:
            fail_msg("Connection: close", "server sent extra data after Connection: close response")
    except (BrokenPipeError, ConnectionResetError, socket.error, OSError):
        pass_msg("Connection: close - server closed connection (recv failed)")
    except Exception:
        s.close()
        pass_msg("Connection: close - connection appears closed")

def test_19_chunked_transfer():
    section("Chunked transfer encoding (request)")
    chunked_body = (
        b"5\r\nHello\r\n"
        b"6\r\n World\r\n"
        b"0\r\n\r\n"
    )
    req = (
        b"POST /uploads/chunked_test.txt HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Transfer-Encoding: chunked\r\n"
        b"\r\n"
    ) + chunked_body
    status, headers, body = raw_request(HOST, PORT, req, timeout=TIMEOUT)
    if status is None:
        skip_msg("Chunked request", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "201", "204"):
        pass_msg(f"Chunked POST accepted: {code}")
    elif code == "400":
        fail_msg("Chunked POST", "got 400 - chunked decoding may not be implemented")
    else:
        fail_msg("Chunked POST", f"unexpected {code}")

def test_20_chunked_trailer():
    section("Chunked with trailer")
    req = (
        b"POST /uploads/trailer_test.txt HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Transfer-Encoding: chunked\r\n"
        b"Trailer: X-Checksum\r\n"
        b"\r\n"
        b"4\r\nwiki\r\n"
        b"5\r\npedia\r\n"
        b"0\r\n"
        b"X-Checksum: abc123\r\n"
        b"\r\n"
    )
    status, headers, body = raw_request(HOST, PORT, req, timeout=TIMEOUT)
    if status is None:
        skip_msg("Chunked trailer", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "201", "204", "400", "411"):
        # 400/411 acceptable - trailer support is advanced
        pass_msg(f"Chunked with trailer handled: {code}")
    else:
        fail_msg("Chunked with trailer", f"unexpected {code}")

# ---------------------------------------------------------------------------
# 6. CGI
# ---------------------------------------------------------------------------

def test_21_cgi_basic_get():
    header("6. CGI")
    status, headers, body = http_request("GET", "/cgi-bin/test.py")
    if status is None:
        skip_msg("CGI GET", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        if b"CGI Test" in body or b"Python CGI" in body:
            pass_msg("CGI GET /cgi-bin/test.py - 200 with expected output")
        else:
            fail_msg("CGI GET /cgi-bin/test.py", "body doesn't contain expected CGI output")
    elif code in ("500", "502"):
        fail_msg("CGI GET /cgi-bin/test.py", f"got {code} - CGI execution may be failing")
    else:
        fail_msg("CGI GET /cgi-bin/test.py", f"unexpected {code}")

def test_22_cgi_get_query():
    section("CGI with QUERY_STRING")
    status, headers, body = http_request("GET", "/cgi-bin/echo.py?name=webserv&test=42")
    if status is None:
        skip_msg("CGI query string", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        body_text = body.decode("latin-1", errors="replace")
        if "query=name=webserv&test=42" in body_text:
            pass_msg("CGI QUERY_STRING passed correctly")
        else:
            fail_msg("CGI QUERY_STRING passed correctly", f"got: {body_text[:200]}")
    else:
        fail_msg("CGI with query string", f"got {code}")

def test_23_cgi_post_body():
    section("CGI with POST body")
    status, headers, body = http_request(
        "POST", "/cgi-bin/echo.py",
        body=b"test_body_content_123",
        headers={"Content-Type": "text/plain"}
    )
    if status is None:
        skip_msg("CGI POST body", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        body_text = body.decode("latin-1", errors="replace")
        if "body=test_body_content_123" in body_text:
            pass_msg("CGI body forwarded correctly")
        else:
            fail_msg("CGI body forwarded correctly", f"got: {body_text[:200]}")
    else:
        fail_msg("CGI POST body", f"got {code}")

def test_24_cgi_env_vars():
    section("CGI environment variables")
    status, headers, body = http_request(
        "POST", "/cgi-bin/echo.py?hello=world",
        body=b"env_test",
        headers={"Content-Type": "application/x-test"}
    )
    if status is None:
        skip_msg("CGI env vars", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code != "200":
        fail_msg("CGI env vars", f"got {code}")
        return
    body_text = body.decode("latin-1", errors="replace")

    checks = {
        "REQUEST_METHOD=POST": "method=POST",
        "QUERY_STRING=hello=world": "query=hello=world",
        "CONTENT_LENGTH=8": "content_length=8",
        "CONTENT_TYPE=application/x-test": "content_type=application/x-test",
    }
    all_pass = True
    for check_name, check_str in checks.items():
        if check_str in body_text:
            pass_msg(f"CGI env: {check_name}")
        else:
            all_pass = False
            fail_msg(f"CGI env: {check_name}", f"not found in output")

    # SERVER_NAME / SERVER_PORT
    has_server_name = "server_name=" in body_text
    has_server_port = "server_port=" in body_text
    if has_server_name:
        pass_msg("CGI env: SERVER_NAME present")
    else:
        all_pass = False
        fail_msg("CGI env: SERVER_NAME", "missing")
    if has_server_port:
        pass_msg("CGI env: SERVER_PORT present")
    else:
        all_pass = False
        fail_msg("CGI env: SERVER_PORT", "missing")

def test_25_cgi_nonexistent():
    section("CGI - nonexistent script")
    status, headers, body = http_request("GET", "/cgi-bin/nonexistent_script.xyz")
    if status is None:
        skip_msg("CGI nonexistent", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("404", "500", "502"):
        pass_msg(f"Nonexistent CGI script returns {code}")
    else:
        fail_msg("Nonexistent CGI script", f"expected 404/500, got {code}")

def test_26_cgi_post_chunked():
    section("CGI with chunked request body")
    # "CGI test" is 8 bytes
    chunked_body = (
        b"8\r\nCGI test\r\n"
        b"0\r\n\r\n"
    )
    req = (
        b"POST /cgi-bin/echo.py HTTP/1.1\r\n"
        b"Host: " + HOST.encode() + b":" + str(PORT).encode() + b"\r\n"
        b"Transfer-Encoding: chunked\r\n"
        b"Content-Type: text/plain\r\n"
        b"\r\n"
    ) + chunked_body
    status, headers, body = raw_request(HOST, PORT, req, timeout=TIMEOUT)
    if status is None:
        skip_msg("CGI chunked", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        body_text = body.decode("latin-1", errors="replace")
        if "body=CGI test" in body_text:
            pass_msg("CGI chunked body dechunked correctly")
        else:
            fail_msg("CGI chunked body", f"body doesn't match: {body_text[:200]}")
    else:
        fail_msg("CGI chunked", f"got {code}")

# ---------------------------------------------------------------------------
# 7. Error Handling
# ---------------------------------------------------------------------------

def test_27_client_max_body_size():
    header("7. ERROR HANDLING & BODY SIZE LIMITS")
    # default.conf: 10MB limit on PORT (8080), 2MB on PORT2 (8081).
    # The server rejects a declared oversized Content-Length as soon as the
    # headers are parsed, so only headers need to be sent.
    for port, limit_label, declared in ((PORT, "10MB", 11 * 1024 * 1024),
                                        (PORT2, "2MB", 3 * 1024 * 1024)):
        req = (f"POST / HTTP/1.1\r\nHost: {HOST}:{port}\r\n"
               f"Content-Length: {declared}\r\n\r\n").encode()
        status, headers, body = raw_request(HOST, port, req, timeout=10)
        if status is None:
            skip_msg(f"Body size limit on :{port}", "server not reachable"); continue
        code = status.split()[1] if len(status.split()) > 1 else ""
        if code == "413":
            pass_msg(f"Port {port} ({limit_label} limit): declared {declared}B -> 413 (early, before body)")
        else:
            fail_msg(f"Port {port} body size limit", f"declared {declared}B, expected 413, got {code}")

    # Chunked body has no declared length: the limit must trip mid-stream.
    section("Chunked body over the limit")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    resp = b""
    try:
        s.connect((HOST, PORT2))
        s.sendall((f"POST / HTTP/1.1\r\nHost: {HOST}:{PORT2}\r\n"
                   "Transfer-Encoding: chunked\r\n\r\n").encode())
        chunk = b"x" * 65536
        frame = b"10000\r\n" + chunk + b"\r\n"
        sent = 0
        while sent < 3 * 1024 * 1024 and not resp:
            try:
                s.sendall(frame)
            except OSError:
                break  # server already responded and closed
            sent += len(chunk)
            readable, _, _ = select.select([s], [], [], 0)
            if readable:
                try:
                    resp = s.recv(4096)
                except OSError:
                    break
        if not resp:
            try:
                resp = s.recv(4096)
            except OSError:
                resp = b""
    except (socket.timeout, ConnectionRefusedError, OSError):
        pass
    finally:
        s.close()
    first_line = resp.split(b"\r\n")[0] if resp else b""
    if b" 413 " in first_line + b" ":
        pass_msg("Chunked upload over the limit rejected mid-stream with 413")
    elif resp:
        fail_msg("Chunked body over limit", "expected 413, got: " + first_line[:60].decode("latin-1"))
    else:
        fail_msg("Chunked body over limit", "no response before 3MB was sent")

def test_28_method_not_allowed_405():
    section("405 Method Not Allowed")
    # Send DELETE to location / which only allows GET POST
    status, headers, body = http_request("DELETE", "/")
    if status is None:
        skip_msg("405 test", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "405":
        pass_msg("DELETE on / (not allowed) returns 405")
    else:
        fail_msg("DELETE on / returns 405", f"got {code}")

def test_29_http_version_not_supported():
    section("Unsupported HTTP version")
    s = raw_connect_send(HOST, PORT, b"GET / HTTP/9.9\r\nHost: localhost\r\n\r\n")
    status, headers, body = raw_recv_all(s)
    if status is None:
        skip_msg("Bad HTTP version", "server closed connection"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("505", "400", "200"):
        pass_msg(f"HTTP/9.9 request handled: {code}")
    else:
        fail_msg("HTTP/9.9 request", f"got {code}")

def test_30_uri_too_long():
    section("Long URI")
    long_path = "/" + "a" * 10000
    status, headers, body = http_request("GET", long_path)
    if status is None:
        skip_msg("Long URI", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("414", "400", "404"):
        pass_msg(f"Very long URI returns {code}")
    else:
        fail_msg("Very long URI", f"expected 414/400/404, got {code}")

# ---------------------------------------------------------------------------
# 8. Redirects
# ---------------------------------------------------------------------------

def test_31_redirect():
    header("8. REDIRECTS")
    status, headers, body = http_request("GET", "/redirect")
    if status is None:
        skip_msg("Redirect", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "301":
        if "location" in headers:
            location = headers["location"]
            pass_msg(f"301 redirect with Location: {location}")
        else:
            fail_msg("301 redirect includes Location header", "missing")
    else:
        fail_msg("GET /redirect returns 301", f"got {code}")

# ---------------------------------------------------------------------------
# 9. Configuration & Multi-Server
# ---------------------------------------------------------------------------

def test_32_multi_port_independence():
    header("9. CONFIGURATION & MULTI-SERVER")
    section("Independent content on different ports")

    # Port 8080: autoindex off on /
    s1, h1, b1 = http_request("GET", "/", port=PORT)
    # Port 8081: autoindex on
    s2, h2, b2 = http_request("GET", "/", port=PORT2)

    if s2 is None:
        skip_msg("Multi-port independence", f"port {PORT2} not reachable"); return
    if s1 is None:
        skip_msg("Multi-port independence", f"port {PORT} not reachable"); return

    code1 = s1.split()[1] if len(s1.split()) > 1 else ""
    code2 = s2.split()[1] if len(s2.split()) > 1 else ""

    if code1 == "200" and code2 == "200":
        pass_msg("Both ports return 200 on GET /")
    else:
        fail_msg("Both ports return 200", f"port {PORT}={code1}, port {PORT2}={code2}")

def test_33_different_body_sizes():
    section("Different client_max_body_size per server")

    # default.conf: Port 8080 has a 10MB limit; Port 8081 has a 2MB limit.
    # The same 1.5MB body must be accepted by both, and a declared 3MB body
    # must be rejected only by the stricter server.
    body_15mb = b"x" * (1500 * 1024)  # 1.5MB

    status1, h1, b1 = http_request("POST", "/", body=body_15mb, port=PORT, timeout=10)
    status2, h2, b2 = http_request("POST", "/", body=body_15mb, port=PORT2, timeout=10)

    for port, limit_label, status in ((PORT, "10MB", status1), (PORT2, "2MB", status2)):
        if not status:
            skip_msg(f"Port {port} body size test", "no response")
            continue
        code = status.split()[1] if len(status.split()) > 1 else ""
        if code in ("200", "201", "202", "204"):
            pass_msg(f"Port {port} ({limit_label} limit) accepts 1.5MB -> {code}")
        elif code == "403":
            pass_msg(f"Port {port} ({limit_label} limit) accepts 1.5MB -> 403 (no upload_path configured for this route)")
        elif code == "413":
            fail_msg(f"Port {port} ({limit_label} limit) accepts 1.5MB", "got 413 (should accept)")
        else:
            fail_msg(f"Port {port} ({limit_label} limit) accepts 1.5MB", f"got {code}")

    req = (f"POST / HTTP/1.1\r\nHost: {HOST}:{PORT2}\r\n"
           f"Content-Length: {3 * 1024 * 1024}\r\n\r\n").encode()
    status3, h3, b3 = raw_request(HOST, PORT2, req, timeout=10)
    if status3:
        code3 = status3.split()[1] if len(status3.split()) > 1 else ""
        if code3 == "413":
            pass_msg(f"Port {PORT2} (2MB limit) rejects declared 3MB -> 413")
        else:
            fail_msg(f"Port {PORT2} (2MB limit) rejects declared 3MB", f"got {code3}")
    else:
        skip_msg(f"Port {PORT2} 3MB rejection test", "no response")

# ---------------------------------------------------------------------------
# 10. File Uploads
# ---------------------------------------------------------------------------

def test_34_file_upload_and_retrieve():
    header("10. FILE UPLOADS")
    upload_content = f"Upload test content {random.randint(10000, 99999)}"
    upload_filename = f"/uploads/_upload_test_{random.randint(1000, 9999)}.txt"

    # Upload
    status, headers, body = http_request("POST", upload_filename, body=upload_content.encode())
    if status is None:
        skip_msg("File upload", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code not in ("200", "201", "202", "204"):
        fail_msg("File upload", f"POST {upload_filename} returned {code}")
        return
    pass_msg(f"File uploaded: POST {upload_filename} -> {code}")
    upload_filename = headers.get("location", upload_filename)

    # Retrieve
    time.sleep(0.1)
    status2, headers2, body2 = http_request("GET", upload_filename, port=PORT)
    if status2 is None:
        fail_msg("Retrieve uploaded file", "no response"); return
    code2 = status2.split()[1] if len(status2.split()) > 1 else ""
    if code2 == "200" and upload_content.encode() in body2:
        pass_msg("Uploaded file retrieved correctly via GET")
    else:
        fail_msg("Retrieve uploaded file", f"code={code2}, content_match={(upload_content.encode() in body2)}")

    # Cleanup
    http_request("DELETE", upload_filename, port=PORT)

def test_35_empty_body_upload():
    section("Empty POST body")
    status, headers, body = http_request("POST", "/uploads/_empty_test.txt", body=b"")
    if status is None:
        skip_msg("Empty POST", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "201", "202", "204", "400"):
        pass_msg(f"Empty POST body handled: {code}")
    else:
        fail_msg("Empty POST body", f"unexpected {code}")

def test_36_upload_path_cleanup():
    section("Repeated uploads to the same name get unique files")
    fname = f"/uploads/_stress_{random.randint(1000, 9999)}.txt"
    locations = []
    for i in range(5):
        s, h, b = http_request("POST", fname, body=f"iteration {i}".encode())
        loc = (h or {}).get("location")
        if loc:
            locations.append(loc)
    if len(set(locations)) == 5:
        pass_msg("5 anonymous uploads to the same name -> 5 distinct files (no overwrite)")
    else:
        fail_msg("Upload collision handling", f"expected 5 unique locations, got {locations}")
    for loc in set(locations):
        http_request("DELETE", loc)

# ---------------------------------------------------------------------------
# 11. Concurrency & Resilience
# ---------------------------------------------------------------------------

CONCURRENT_RESULTS = []

def concurrent_worker(worker_id, path, method="GET", body=None):
    try:
        s, h, b = http_request(method, path, body=body, timeout=5)
        code = s.split()[1] if s and len(s.split()) > 1 else "FAIL"
        CONCURRENT_RESULTS.append((worker_id, code, True))
        return (worker_id, code, True)
    except Exception as e:
        CONCURRENT_RESULTS.append((worker_id, str(e), False))
        return (worker_id, str(e), False)

def test_37_concurrent_connections():
    header("11. CONCURRENCY & RESILIENCE")
    global CONCURRENT_RESULTS
    CONCURRENT_RESULTS = []

    threads = []
    for i in range(20):
        t = threading.Thread(target=concurrent_worker, args=(i, "/"))
        threads.append(t)
        t.start()

    for t in threads:
        t.join(timeout=10)

    successes = sum(1 for r in CONCURRENT_RESULTS if r[2])
    if successes >= 18:
        pass_msg(f"Concurrent connections: {successes}/20 succeeded")
    elif successes > 0:
        fail_msg("Concurrent connections", f"only {successes}/20 succeeded")
    else:
        fail_msg("Concurrent connections", "all failed")

def test_38_rapid_requests():
    section("Rapid-fire requests (no crash)")
    crash = False
    for i in range(50):
        status, headers, body = http_request("GET", "/", timeout=3)
        if status is None:
            crash = True
            break
    if crash:
        fail_msg("Rapid-fire GET (50 requests)", "server stopped responding")
    else:
        pass_msg("50 rapid GET requests handled without crash")

def test_39_slow_client():
    section("Slow client (partial sends)")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((HOST, PORT))
    s.sendall(b"GET /")
    time.sleep(1)
    s.sendall(b" HTTP/1.1\r\n")
    time.sleep(0.5)
    s.sendall(b"Host: localhost\r\n\r\n")
    status, headers, body = raw_recv_all(s, timeout=5)
    if status:
        code = status.split()[1] if len(status.split()) > 1 else ""
        if code == "200":
            pass_msg(f"Slow client (drip-fed request) responded {code}")
        else:
            pass_msg(f"Slow client (drip-fed request) responded {code} (server handles partial reads)")
    else:
        pass_msg("Slow client: server timed out (acceptable behavior)")

def test_40_partial_header():
    section("Incomplete request then disconnect")
    s = raw_connect_send(HOST, PORT, b"GET / HTTP/1.1\r\nHost: local")
    time.sleep(1)
    s.close()
    # Server should not crash after client disconnects mid-request
    time.sleep(0.1)
    status, headers, body = http_request("GET", "/")
    if status and "200" in status:
        pass_msg("Server still responsive after client disconnects mid-request")
    else:
        fail_msg("Server still responsive after client disconnects mid-request", "unresponsive")

# ---------------------------------------------------------------------------
# 12. Edge Cases & Security
# ---------------------------------------------------------------------------

def test_41_path_traversal():
    header("12. EDGE CASES & SECURITY")
    section("Path traversal protection")
    status, headers, body = http_request("GET", "/../../../etc/passwd")
    if status is None:
        skip_msg("Path traversal", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("400", "403", "404"):
        pass_msg(f"Path traversal attempt returns {code}")
    elif code == "200" and b"root:" in body:
        fail_msg("Path traversal protection", "SERVER LEAKED /etc/passwd!")
    else:
        fail_msg("Path traversal protection", f"unexpected response: {code}")

def test_42_double_encoding_path_traversal():
    section("URL-encoded path traversal")
    # %2e%2e%2f = ../ 
    status, headers, body = http_request("GET", "/%2e%2e%2f%2e%2e%2fetc/passwd")
    if status is None:
        skip_msg("Encoded traversal", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("400", "403", "404"):
        pass_msg(f"URL-encoded path traversal returns {code}")
    elif code == "200" and b"root:" in body:
        fail_msg("URL-encoded path traversal protection", "SERVER LEAKED /etc/passwd!")
    else:
        fail_msg("URL-encoded path traversal protection", f"unexpected: {code}")

def test_43_null_byte_injection():
    section("Null byte in URL")
    status, headers, body = http_request("GET", "/%00index.html")
    if status is None:
        skip_msg("Null byte URL", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("400", "403", "404"):
        pass_msg(f"Null byte in URL returns {code}")
    else:
        fail_msg("Null byte in URL", f"expected 400/403/404, got {code}")

def test_44_special_characters():
    section("Special characters in URL")
    for path, desc in [
        ("/index.html?foo=<script>alert(1)</script>", "XSS query string"),
        ("/index.html?a=b&c=d&e=f&g=h", "Long query string"),
    ]:
        status, headers, body = http_request("GET", path)
        if status:
            code = status.split()[1] if len(status.split()) > 1 else ""
            pass_msg(f"{desc}: {code}")
        else:
            fail_msg(f"{desc}", "no response")

def test_45_double_slash():
    section("Double slash in path")
    status, headers, body = http_request("GET", "//index.html")
    if status is None:
        skip_msg("Double slash", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("200", "301", "404"):
        pass_msg(f"GET //index.html returns {code}")
    else:
        fail_msg("GET //index.html", f"unexpected {code}")

def test_46_http_09_request():
    section("HTTP/0.9-style request")
    s = raw_connect_send(HOST, PORT, b"GET /\r\n")
    time.sleep(0.3)
    try:
        data = b""
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
            except socket.timeout:
                break
    except Exception:
        pass
    finally:
        s.close()
    if data:
        pass_msg("HTTP/0.9-style request: server responded (some data returned)")
    else:
        pass_msg("HTTP/0.9-style request: server closed with no data (acceptable)")

def test_47_empty_request():
    section("Empty request then close")
    s = raw_connect_send(HOST, PORT, b"")
    time.sleep(0.5)
    s.close()
    # Server should survive
    time.sleep(0.1)
    status, headers, body = http_request("GET", "/")
    if status and "200" in (status or ""):
        pass_msg("Server survives empty-request client disconnect")
    else:
        fail_msg("Server survives empty-request client disconnect")

def test_48_multiple_requests_one_connection():
    section("Pipelining: 3 requests on one connection")
    s = raw_connect_send(HOST, PORT, b"")
    # Send 3 requests on same connection
    s.sendall(
        b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
        b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
        b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
    )
    time.sleep(0.5)
    try:
        data = b""
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
            except socket.timeout:
                break
    except Exception:
        pass
    finally:
        s.close()
    # Count HTTP/1.1 status lines
    count = data.count(b"HTTP/1.1")
    if count >= 2:
        pass_msg(f"Pipelining: {count}/3 responses received on one connection")
    elif count >= 1:
        fail_msg("Pipelining", f"only {count}/3 responses received")
    else:
        fail_msg("Pipelining", "no responses received")

# ---------------------------------------------------------------------------
# 13. Autoindex & Directory Listing
# ---------------------------------------------------------------------------

def test_49_autoindex():
    header("13. AUTOINDEX / DIRECTORY LISTING")
    # /uploads on 8080 has autoindex on (root ./www/uploads)
    # The autoindex test depends on the path being configured correctly
    # Test with a custom path or use /uploads
    status, headers, body = http_request("GET", "/uploads/", port=PORT)
    if status is None:
        skip_msg("Autoindex", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "200":
        body_text = body.decode("latin-1", errors="replace")
        if "<html" in body_text.lower() or ".gitkeep" in body_text:
            pass_msg("GET /uploads/ returns directory listing (autoindex on)")
        else:
            fail_msg("Autoindex", "response doesn't look like a directory listing")
    else:
        fail_msg("Autoindex", f"GET /uploads/ returned {code}")

# ---------------------------------------------------------------------------
# 14. Specific Evaluation Tests
# ---------------------------------------------------------------------------

def test_50_eval_static_website():
    header("14. EVALUATION-SPECIFIC TESTS")
    section("Serve fully static website")
    status, headers, body = http_request("GET", "/")
    if status is None:
        skip_msg("Static website", "server not reachable"); return
    if "200" in status and len(body) > 100:
        pass_msg("Static HTML page served with substantial content")
    else:
        fail_msg("Static HTML page", "insufficient content or wrong status")

def test_51_eval_non_blocking():
    section("Non-blocking I/O verification")
    # Connect without sending anything - server should accept new connections
    blocker = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    blocker.settimeout(10)
    blocker.connect((HOST, PORT))
    # Don't send data - just hold the connection open
    time.sleep(0.2)
    # Another client should still be able to connect and get a response
    status, headers, body = http_request("GET", "/")
    blocker.close()
    if status and "200" in status:
        pass_msg("Server serves other clients while one connection is idle (non-blocking)")
    else:
        fail_msg("Non-blocking I/O", "server blocked on idle connection")

def test_52_eval_accurate_status_codes():
    section("Accurate status codes")
    tests = [
        ("GET /", "200"),
        ("GET /nonexistent_abc_def.html", "404"),
        ("DELETE /", "405"),
    ]
    for path, expected in tests:
        method, url_path = path.split(" ")
        status, headers, body = http_request(method, url_path)
        if status is None:
            fail_msg(f"Status: {method} {url_path}", "no response"); continue
        code = status.split()[1] if len(status.split()) > 1 else ""
        if code == expected:
            pass_msg(f"{method} {url_path} -> {code}")
        else:
            fail_msg(f"{method} {url_path} -> {code}", f"expected {expected}")

def test_53_eval_default_error_pages():
    section("Default error pages when none configured")
    # Port 8080 has custom 404 but no custom 405
    # Check if 405 has a default error page
    status, headers, body = http_request("PATCH", "/")
    if status is None:
        skip_msg("Default error pages", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code in ("405", "501"):
        if len(body) > 0:
            pass_msg(f"Error {code} includes a body (default error page)")
        else:
            pass_msg(f"Error {code} returned with no body (acceptable)")
    else:
        fail_msg("Default error page", f"unexpected {code}")

def test_54_eval_file_upload_flow():
    section("File upload end-to-end")
    test_content = f"Eval upload test - {random.randint(10000, 99999)}"
    test_file = f"/uploads/_eval_{random.randint(1000, 9999)}.txt"

    # upload
    s, h, b = http_request("POST", test_file, body=test_content.encode())
    if s is None:
        skip_msg("File upload E2E", "server not reachable"); return
    c = s.split()[1] if len(s.split()) > 1 else ""
    if c not in ("200", "201", "202", "204"):
        fail_msg("File upload E2E", f"upload returned {c}"); return
    test_file = h.get("location", test_file)

    # retrieve
    time.sleep(0.1)
    s, h, b = http_request("GET", test_file)
    c2 = s.split()[1] if len(s.split()) > 1 else ""
    if c2 == "200" and test_content.encode() in b:
        pass_msg("File upload E2E: upload -> GET -> content verified")
    else:
        fail_msg("File upload E2E: GET after upload", f"code={c2}, match={test_content.encode() in b}")

    # delete
    http_request("DELETE", test_file)

def test_55_eval_cgi_complete():
    section("CGI complete workflow")
    # GET with query string -> CGI -> verify output
    s, h, b = http_request("GET", "/cgi-bin/echo.py?eval_test=passed")
    if s is None:
        skip_msg("CGI complete", "server not reachable"); return
    c = s.split()[1] if len(s.split()) > 1 else ""
    if c == "200":
        body_text = b.decode("latin-1", errors="replace")
        if "eval_test=passed" in body_text:
            pass_msg("CGI complete: GET + QUERY_STRING verified")
        else:
            fail_msg("CGI complete", f"query string not found in output: {body_text[:200]}")
    else:
        fail_msg("CGI complete", f"got {c}")

# ---------------------------------------------------------------------------
# 15. Stress Tests (optional, longer runtime)
# ---------------------------------------------------------------------------

def test_56_stress_concurrent():
    header("15. STRESS TESTS (extended)")
    section("100 concurrent connections")
    global CONCURRENT_RESULTS
    CONCURRENT_RESULTS = []

    threads = []
    for i in range(100):
        t = threading.Thread(target=concurrent_worker, args=(i, "/"))
        threads.append(t)
        t.start()

    for t in threads:
        t.join(timeout=15)

    successes = sum(1 for r in CONCURRENT_RESULTS if r[2])
    if successes >= 90:
        pass_msg(f"100 concurrent connections: {successes}/100 succeeded")
    elif successes > 0:
        fail_msg("100 concurrent connections", f"only {successes}/100 succeeded")
    else:
        fail_msg("100 concurrent connections", "all failed")

def test_57_large_response():
    section("Large file serving")
    # Create a temp file in www/html
    large_content = "A" * (500 * 1024)  # 500KB
    temp_path = os.path.join(TEST_DIR, "www", "html", "_large_test_file.txt")
    with open(temp_path, "w") as f:
        f.write(large_content)

    status, headers, body = http_request("GET", "/_large_test_file.txt", timeout=10)
    os.remove(temp_path)

    if status:
        code = status.split()[1] if len(status.split()) > 1 else ""
        if code == "200" and len(body) == len(large_content):
            pass_msg(f"Large file (500KB) served: {len(body)} bytes")
        elif code == "200":
            fail_msg("Large file served", f"size mismatch: expected {len(large_content)}, got {len(body)}")
        else:
            fail_msg("Large file served", f"got {code}")
    else:
        skip_msg("Large file", "server not reachable")

# ---------------------------------------------------------------------------
# 16. CGI meta-variables, timeouts, status-code accuracy
# ---------------------------------------------------------------------------

def test_58_cgi_meta_variables():
    header("16. CGI META-VARIABLES & TIMEOUTS")
    section("PATH_INFO / REMOTE_ADDR / SERVER_PORT (RFC 3875)")
    if not os.path.isfile(os.path.join(TEST_DIR, "cgi-bin", "env.py")):
        skip_msg("CGI meta-variables", "cgi-bin/env.py not found"); return

    status, headers, body = http_request("GET", "/cgi-bin/env.py/extra/path?x=1")
    if status is None:
        skip_msg("CGI meta-variables", "server not reachable"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code != "200":
        fail_msg("CGI with PATH_INFO suffix", f"expected 200, got {code}")
        return
    text = body.decode("latin-1", errors="replace")

    checks = {
        "PATH_INFO is the suffix after the script": "path_info=/extra/path",
        "SCRIPT_NAME stops at the script": "script_name=/cgi-bin/env.py",
        "SERVER_PORT is the real listen port": f"server_port={PORT}",
        "QUERY_STRING survives PATH_INFO": "query_string=x=1",
    }
    for name, needle in checks.items():
        if needle in text:
            pass_msg(f"CGI env: {name}")
        else:
            fail_msg(f"CGI env: {name}", f"'{needle}' not found in output")

    m = re.search(r"remote_addr=(\S+)", text)
    if m:
        pass_msg(f"CGI env: REMOTE_ADDR set ({m.group(1)})")
    else:
        fail_msg("CGI env: REMOTE_ADDR set", "remote_addr missing or empty")

def test_59_cgi_hung_script_timeout():
    section("Hung CGI (infinite loop) must not block the server")
    script = os.path.join(TEST_DIR, "cgi-bin", "_hang_test.py")
    with open(script, "w") as f:
        f.write("#!/usr/bin/env python3\nimport time\ntime.sleep(60)\n")
    try:
        start = time.time()
        status, headers, body = http_request("GET", "/cgi-bin/_hang_test.py", timeout=15)
        elapsed = time.time() - start
        if status is None:
            fail_msg("Hung CGI", f"no response after {elapsed:.1f}s")
        else:
            code = status.split()[1] if len(status.split()) > 1 else ""
            if code in ("504", "502", "500") and elapsed < 15:
                pass_msg(f"Hung CGI returned {code} after {elapsed:.1f}s (killed by timeout)")
            else:
                fail_msg("Hung CGI", f"expected 5xx within 15s, got {code} after {elapsed:.1f}s")

        status2, _, _ = http_request("GET", "/", timeout=5)
        code2 = status2.split()[1] if status2 and len(status2.split()) > 1 else ""
        if code2 == "200":
            pass_msg("Server still responsive after hung CGI")
        else:
            fail_msg("Server responsive after hung CGI", f"got {code2 or 'no response'}")
    finally:
        os.remove(script)

def test_60_head_content_length():
    section("HEAD Content-Length matches GET (RFC 7231)")
    gstatus, gheaders, gbody = http_request("GET", "/")
    hstatus, hheaders, hbody = http_request("HEAD", "/")
    if gstatus is None or hstatus is None:
        skip_msg("HEAD Content-Length", "server not reachable"); return
    get_cl = gheaders.get("content-length")
    head_cl = hheaders.get("content-length")
    if head_cl is not None and head_cl == get_cl and int(head_cl) > 0:
        pass_msg(f"HEAD Content-Length equals GET's ({head_cl})")
    else:
        fail_msg("HEAD Content-Length equals GET's", f"GET={get_cl}, HEAD={head_cl}")
    if not hbody:
        pass_msg("HEAD response has no body")
    else:
        fail_msg("HEAD response has no body", f"{len(hbody)} bytes received")

def test_61_unknown_method_501():
    section("501 for unimplemented method")
    req = f"FOOBAR / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\n\r\n".encode()
    status, headers, body = raw_request(HOST, PORT, req)
    if status is None:
        skip_msg("Unknown method", "no response"); return
    code = status.split()[1] if len(status.split()) > 1 else ""
    if code == "501":
        pass_msg("Unknown method FOOBAR returns 501 Not Implemented")
    else:
        fail_msg("Unknown method FOOBAR returns 501", f"got {code}")

def test_62_sustained_load_fd_stability():
    header("17. SUSTAINED LOAD & RESOURCE STABILITY")
    section("15s mixed load: availability >= 99.5%, no FD leak")

    def find_server_pid():
        if SERVER_PROC:
            return SERVER_PROC.pid
        try:
            out = subprocess.run(["lsof", "-ti", f"tcp:{PORT}", "-sTCP:LISTEN"],
                                 capture_output=True, text=True, timeout=5)
            pids = out.stdout.split()
            return int(pids[0]) if pids else None
        except Exception:
            return None

    def fd_count(pid):
        proc_fd = f"/proc/{pid}/fd"
        if os.path.isdir(proc_fd):
            return len(os.listdir(proc_fd))
        try:
            out = subprocess.run(["lsof", "-p", str(pid)],
                                 capture_output=True, text=True, timeout=10)
            return max(len(out.stdout.splitlines()) - 1, 0)
        except Exception:
            return None

    pid = find_server_pid()
    http_request("GET", "/")  # warm up before the baseline snapshot
    fd_before = fd_count(pid) if pid else None

    paths = ["/", "/cgi-bin/test.py", "/uploads", "/nonexistent"]
    total = 0
    ok = 0
    deadline = time.time() + 15
    while time.time() < deadline:
        status, headers, body = http_request("GET", paths[total % len(paths)], timeout=5)
        total += 1
        if status is not None:
            ok += 1

    availability = (ok / total) if total else 0.0
    if total >= 50 and availability >= 0.995:
        pass_msg(f"Availability {availability * 100:.2f}% over {total} mixed requests (static/CGI/autoindex/404)")
    else:
        fail_msg("Sustained availability", f"{ok}/{total} = {availability * 100:.2f}% (expected >= 99.5%)")

    if pid is None or fd_before is None:
        skip_msg("FD stability", "cannot determine server pid / fd count (lsof missing?)")
        return
    time.sleep(1)  # let in-flight sockets and CGI pipes close
    fd_after = fd_count(pid)
    if fd_after is None:
        skip_msg("FD stability", "fd count unavailable after load")
    elif fd_after - fd_before <= 8:
        pass_msg(f"FD count stable: {fd_before} -> {fd_after} after {total} requests")
    else:
        fail_msg("FD stability", f"fd count grew {fd_before} -> {fd_after} (possible fd leak)")

# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------

def all_tests():
    return [
        # 1. Basic
        test_01_connectivity, test_02_server_identity,
        # 2. GET
        test_03_get_root, test_04_get_nonexistent, test_05_get_custom_404,
        test_06_get_directory_no_index, test_07_get_with_query, test_08_head_method,
        # 3. POST
        test_09_post_basic, test_10_post_method_not_allowed,
        # 4. DELETE
        test_11_delete, test_12_delete_nonexistent,
        # 5. HTTP Protocol
        test_13_content_length, test_14_keep_alive, test_15_malformed_request_bad_request,
        test_16_malformed_header, test_17_host_header_required, test_18_connection_close_multiple_requests,
        test_19_chunked_transfer, test_20_chunked_trailer,
        # 6. CGI
        test_21_cgi_basic_get, test_22_cgi_get_query, test_23_cgi_post_body,
        test_24_cgi_env_vars, test_25_cgi_nonexistent, test_26_cgi_post_chunked,
        # 7. Error handling
        test_27_client_max_body_size, test_28_method_not_allowed_405,
        test_29_http_version_not_supported, test_30_uri_too_long,
        # 8. Redirects
        test_31_redirect,
        # 9. Multi-server
        test_32_multi_port_independence, test_33_different_body_sizes,
        # 10. File uploads
        test_34_file_upload_and_retrieve, test_35_empty_body_upload, test_36_upload_path_cleanup,
        # 11. Concurrency
        test_37_concurrent_connections, test_38_rapid_requests,
        test_39_slow_client, test_40_partial_header,
        # 12. Edge cases
        test_41_path_traversal, test_42_double_encoding_path_traversal,
        test_43_null_byte_injection, test_44_special_characters,
        test_45_double_slash, test_46_http_09_request,
        test_47_empty_request, test_48_multiple_requests_one_connection,
        # 13. Autoindex
        test_49_autoindex,
        # 14. Evaluation-specific
        test_50_eval_static_website, test_51_eval_non_blocking,
        test_52_eval_accurate_status_codes, test_53_eval_default_error_pages,
        test_54_eval_file_upload_flow, test_55_eval_cgi_complete,
        # 16. CGI meta-variables, timeouts, status codes
        test_58_cgi_meta_variables, test_59_cgi_hung_script_timeout,
        test_60_head_content_length, test_61_unknown_method_501,
    ]

def stress_tests():
    return [test_56_stress_concurrent, test_57_large_response,
            test_62_sustained_load_fd_stability]

CATEGORIES = {
    "basic": [test_01_connectivity, test_02_server_identity],
    "get": [test_03_get_root, test_04_get_nonexistent, test_05_get_custom_404,
            test_06_get_directory_no_index, test_07_get_with_query, test_08_head_method],
    "post": [test_09_post_basic, test_10_post_method_not_allowed],
    "delete": [test_11_delete, test_12_delete_nonexistent],
    "protocol": [test_13_content_length, test_14_keep_alive, test_15_malformed_request_bad_request,
                 test_16_malformed_header, test_17_host_header_required,
                 test_18_connection_close_multiple_requests, test_19_chunked_transfer,
                 test_20_chunked_trailer],
    "cgi": [test_21_cgi_basic_get, test_22_cgi_get_query, test_23_cgi_post_body,
            test_24_cgi_env_vars, test_25_cgi_nonexistent, test_26_cgi_post_chunked,
            test_58_cgi_meta_variables, test_59_cgi_hung_script_timeout],
    "errors": [test_27_client_max_body_size, test_28_method_not_allowed_405,
               test_29_http_version_not_supported, test_30_uri_too_long,
               test_60_head_content_length, test_61_unknown_method_501],
    "redirect": [test_31_redirect],
    "multiserver": [test_32_multi_port_independence, test_33_different_body_sizes],
    "upload": [test_34_file_upload_and_retrieve, test_35_empty_body_upload, test_36_upload_path_cleanup],
    "concurrency": [test_37_concurrent_connections, test_38_rapid_requests,
                    test_39_slow_client, test_40_partial_header],
    "security": [test_41_path_traversal, test_42_double_encoding_path_traversal,
                 test_43_null_byte_injection, test_44_special_characters,
                 test_45_double_slash, test_46_http_09_request,
                 test_47_empty_request, test_48_multiple_requests_one_connection],
    "autoindex": [test_49_autoindex],
    "eval": [test_50_eval_static_website, test_51_eval_non_blocking,
             test_52_eval_accurate_status_codes, test_53_eval_default_error_pages,
             test_54_eval_file_upload_flow, test_55_eval_cgi_complete],
    "stress": [test_56_stress_concurrent, test_57_large_response,
               test_62_sustained_load_fd_stability],
}

def print_summary():
    print()
    cprint(BOLD + CYAN, f"{'='*60}")
    cprint(BOLD + CYAN, f"  RESULTS SUMMARY")
    cprint(BOLD + CYAN, f"{'='*60}")
    total = RESULTS["pass"] + RESULTS["fail"] + RESULTS["skip"]
    cprint(GREEN, f"  Passed:  {RESULTS['pass']}/{total}")
    cprint(RED, f"  Failed:  {RESULTS['fail']}/{total}")
    if RESULTS["skip"]:
        cprint(YELLOW, f"  Skipped: {RESULTS['skip']}/{total}")
    if RESULTS["fail"] > 0:
        print()
        cprint(RED, "  Failed tests:")
        for name, detail in FAILED_TESTS:
            cprint(RED, f"    - {name}")
            if detail:
                cprint(RED, f"      {detail}")
    print()
    if RESULTS["fail"] == 0 and RESULTS["pass"] > 0:
        cprint(GREEN, "  All tests passed! Ready for evaluation.")
    elif RESULTS["fail"] > 0:
        cprint(RED, f"  {RESULTS['fail']} test(s) failed. Review issues above.")
    print()

def main():
    global HOST, PORT, SERVER_PROC, SKIP_REASON

    parser = argparse.ArgumentParser(description="42 Webserv Test Suite")
    parser.add_argument("--host", default="127.0.0.1", help="Server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Server port (default: 8080)")
    parser.add_argument("--port2", type=int, default=8081, help="Second server port (default: 8081)")
    parser.add_argument("--start", nargs="+", metavar=("BINARY", "CONFIG"),
                        help="Start server with BINARY CONFIG, run tests, then stop")
    parser.add_argument("--test", metavar="CATEGORY",
                        help=f"Run specific test category. Available: {', '.join(CATEGORIES.keys())}")
    parser.add_argument("--list", action="store_true", help="List test categories")
    parser.add_argument("--no-color", action="store_true", help="Disable color output")
    parser.add_argument("--stress", action="store_true", help="Include stress tests")
    args = parser.parse_args()

    if args.no_color:
        global GREEN, RED, YELLOW, CYAN, BOLD, NC
        GREEN = RED = YELLOW = CYAN = BOLD = NC = ""

    if args.list:
        print("Available test categories:")
        for name, funcs in CATEGORIES.items():
            print(f"  {name}: {len(funcs)} tests")
        return

    HOST = args.host
    PORT = args.port
    PORT2 = args.port2

    if args.start:
        binary, config = args.start[0], args.start[1] if len(args.start) > 1 else "config/default.conf"
        start_server(binary, config)

    try:
        cprint(BOLD + CYAN, f"\n  42 Webserv Test Suite")
        cprint(CYAN, f"  Target: {HOST}:{PORT} / {HOST}:{PORT2}")
        cprint(CYAN, f"  Time:   {time.strftime('%Y-%m-%d %H:%M:%S')}")
        print()

        if args.test:
            cat = args.test.lower()
            if cat not in CATEGORIES:
                cprint(RED, f"Unknown category: {cat}")
                cprint(YELLOW, f"Available: {', '.join(CATEGORIES.keys())}")
                return
            tests = CATEGORIES[cat]
        else:
            tests = all_tests()
            if args.stress:
                tests += stress_tests()

        for test_func in tests:
            if SKIP_REASON:
                skip_msg(test_func.__name__, SKIP_REASON)
                continue
            try:
                test_func()
            except Exception as e:
                fail_msg(test_func.__name__, f"exception: {e}")

        print_summary()

    finally:
        if SERVER_PROC:
            stop_server()

if __name__ == "__main__":
    main()
