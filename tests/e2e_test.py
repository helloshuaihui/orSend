#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
orSend 端到端集成测试

覆盖：
  1. 端口转发   listen 19001 -> 内网服务 19000
  2. 端口映射   listen 19002 -> [19000, 19000]（多目标 + 健康检查）
  3. 内网穿透   agent 注册 web 隧道 -> 公网 19003 -> 内网 19000
  4. 控制接口   status / forward.add / mapping.add / logs

用法:
  python tests/e2e_test.py [--exe build/bin/orsend.exe]
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXE = os.path.join(ROOT, "build", "bin", "orsend.exe" if os.name == "nt" else "orsend")

INTRANET_PORT = 19000
FORWARD_PORT = 19001
MAPPING_PORT = 19002
TUNNEL_PORT = 19003
CONTROL_PORT = 19010
AGENT_CONTROL_PORT = 19011
SERVER_CONTROL_PORT = 19012
SERVER_AGENT_PORT = 19004

BODY = b"<h1>orSend e2e ok</h1>"
LOG_LEVEL = os.environ.get("ORSEND_TEST_LOG_LEVEL", "info")
PASSED = []
FAILED = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path.startswith("/large"):
            body = b"0123456789abcdef" * (512 * 1024 // 16)  # 512 KB
        else:
            body = BODY
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class TestHTTPServer(ThreadingHTTPServer):
    """多线程 + 大 backlog。

    单线程 HTTPServer 的 accept 队列只有 5，30 个并发连接会被内核直接 RST，
    看起来像是穿透丢包，实际是测试用的内网服务自己撑不住。
    """

    daemon_threads = True
    request_queue_size = 128


def check(name, fn):
    try:
        fn()
        PASSED.append(name)
        print("  [PASS] %s" % name)
    except Exception as e:
        FAILED.append((name, str(e)))
        print("  [FAIL] %s -> %s" % (name, e))


def wait_port(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def http_get(port, path="/", timeout=5.0):
    """裸 socket 发 HTTP 请求。

    刻意不用 urllib：本机若配置了 HTTP_PROXY（CI/沙箱环境常见），urllib 会把
    127.0.0.1 的请求也交给代理，测到的就不是 orSend 自己的链路了。
    """
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    try:
        req = ("GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
               "Connection: close\r\n\r\n" % (path, port))
        s.sendall(req.encode("ascii"))
        buf = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    finally:
        s.close()
    head, sep, body = buf.partition(b"\r\n\r\n")
    if not sep:
        raise Exception("响应不完整: %r" % buf[:120])
    status_line = head.split(b"\r\n")[0].decode("latin-1")
    parts = status_line.split()
    if len(parts) < 2 or not parts[1].isdigit():
        raise Exception("状态行异常: %r" % status_line)
    return int(parts[1]), body


def control(port, cmd, args=None, timeout=5.0):
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.sendall((json.dumps({"id": 1, "cmd": cmd, "args": args or {}}) + "\n").encode("utf-8"))
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode("utf-8"))


def start(args, log_name):
    log_path = os.path.join(ROOT, "build", log_name)
    f = open(log_path, "wb")
    p = subprocess.Popen(args, stdout=f, stderr=subprocess.STDOUT)
    return p, f


def stop(proc, fh):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    if fh:
        fh.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=DEFAULT_EXE)
    args = ap.parse_args()

    exe = args.exe
    if not os.path.exists(exe):
        print("找不到可执行文件: %s" % exe)
        return 2

    print("orSend e2e test")
    print("  可执行文件: %s" % exe)

    httpd = TestHTTPServer(("127.0.0.1", INTRANET_PORT), Handler)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    print("  内网服务: 127.0.0.1:%d" % INTRANET_PORT)

    procs = []
    try:
        # ---------- 1. 端口转发 ----------
        print("\n[1] 端口转发")
        p1, f1 = start([exe, "forward", "--listen", "127.0.0.1:%d" % FORWARD_PORT,
                        "--target", "127.0.0.1:%d" % INTRANET_PORT,
                        "--control-port", str(CONTROL_PORT)], "test_forward.log")
        procs.append((p1, f1))
        check("转发端口已监听", lambda: wait_port(FORWARD_PORT) or (_ for _ in ()).throw(Exception("超时")))

        def fwd_ok():
            st, body = http_get(FORWARD_PORT)
            assert st == 200 and body == BODY, "状态 %s 内容 %r" % (st, body)
        check("转发返回内网内容", fwd_ok)

        def fwd_large():
            st, data = http_get(FORWARD_PORT, "/large", timeout=30)
            assert st == 200, "状态 %d" % st
            assert len(data) == 512 * 1024, "长度不符: %d" % len(data)
            assert data[-16:] == b"0123456789abcdef", data[-16:]
        check("转发传输 512KB 大报文", fwd_large)

        def fwd_concurrent():
            errors = []
            ok = []

            def worker(i):
                try:
                    st, body = http_get(FORWARD_PORT, timeout=20)
                    if st == 200 and body == BODY:
                        ok.append(i)
                    else:
                        errors.append((i, st, len(body)))
                except Exception as e:
                    errors.append(str(e))

            threads = [threading.Thread(target=worker, args=(i,)) for i in range(20)]
            for t in threads:
                t.start()
            for t in threads:
                t.join(timeout=30)
            assert not errors, "并发转发出错 %d/20: %r" % (len(errors), errors[:3])
            assert len(ok) == 20, "成功 %d/20" % len(ok)
        check("20 并发连接同时转发", fwd_concurrent)

        def fwd_status():
            r = control(CONTROL_PORT, "status")
            assert r["ok"], r
            assert r["data"]["totals"]["rules"] == 1, r["data"]["totals"]
        check("控制接口 status 可读", fwd_status)
        stop(p1, f1)

        # ---------- 2. 端口映射 ----------
        print("\n[2] 端口映射（多目标 + 健康检查）")
        p2, f2 = start([exe, "map", "--listen", "127.0.0.1:%d" % MAPPING_PORT,
                        "--targets", "127.0.0.1:%d,127.0.0.1:%d" % (INTRANET_PORT, INTRANET_PORT),
                        "--strategy", "roundrobin",
                        "--control-port", str(CONTROL_PORT)], "test_mapping.log")
        procs.append((p2, f2))
        check("映射端口已监听", lambda: wait_port(MAPPING_PORT) or (_ for _ in ()).throw(Exception("超时")))

        def map_ok():
            for _ in range(4):
                st, body = http_get(MAPPING_PORT)
                assert st == 200 and body == BODY, "状态 %s 内容 %r" % (st, body)
        check("映射轮询返回内网内容", map_ok)
        stop(p2, f2)

        # ---------- 3. 内网穿透 ----------
        print("\n[3] 内网穿透")
        p3, f3 = start([exe, "server", "--bind", "127.0.0.1:%d" % SERVER_AGENT_PORT,
                        "--token", "e2e-token", "--public-host", "127.0.0.1",
                        "--control-port", str(SERVER_CONTROL_PORT),
                        "--log-level", LOG_LEVEL], "test_server.log")
        procs.append((p3, f3))
        check("穿透服务端已监听", lambda: wait_port(SERVER_AGENT_PORT) or (_ for _ in ()).throw(Exception("超时")))

        p4, f4 = start([exe, "client", "--server", "127.0.0.1:%d" % SERVER_AGENT_PORT,
                        "--token", "e2e-token", "--client-id", "e2e-agent", "--name", "e2e 客户端",
                        "--tunnel", "web:%d:%d" % (INTRANET_PORT, TUNNEL_PORT),
                        "--control-port", str(AGENT_CONTROL_PORT),
                        "--log-level", LOG_LEVEL], "test_client.log")
        procs.append((p4, f4))
        check("公网端口已开放", lambda: wait_port(TUNNEL_PORT) or (_ for _ in ()).throw(Exception("超时")))

        def tunnel_ok():
            last = None
            for _ in range(20):
                try:
                    st, body = http_get(TUNNEL_PORT)
                    if st == 200 and body == BODY:
                        return
                    last = (st, body)
                except Exception as e:
                    last = e
                time.sleep(0.3)
            raise Exception("穿透访问失败: %r" % (last,))
        check("穿透返回内网内容", tunnel_ok)

        def server_status():
            r = control(SERVER_CONTROL_PORT, "status")
            d = r["data"]
            assert d["server"]["running"], d
            assert d["server"]["agentCount"] == 1, d["server"]
            assert d["server"]["tunnelCount"] == 1, d["server"]
            t = d["server"]["tunnels"][0]
            assert t["publicPort"] == TUNNEL_PORT, t
            assert t["bytesIn"] > 0 or t["bytesOut"] > 0, t
        check("服务端状态统计正确", server_status)

        def agent_status():
            r = control(AGENT_CONTROL_PORT, "status")
            c = r["data"]["client"]
            assert c["state"] == "ready", c
            assert c["tunnels"][0]["registered"], c
            assert c["tunnels"][0]["publicPort"] == TUNNEL_PORT, c
        check("客户端状态正确", agent_status)

        def logs_ok():
            r = control(SERVER_CONTROL_PORT, "logs", {"limit": 50})
            entries = r["data"]["entries"]
            assert len(entries) > 0, "日志为空"
            assert any("隧道已注册" in e["message"] for e in entries), "缺少注册日志"
        check("日志接口可读", logs_ok)

        # 动态规则
        def dynamic_rules():
            r = control(SERVER_CONTROL_PORT, "forward.add",
                        {"name": "dyn", "listen": "127.0.0.1:%d" % 19005,
                         "target": "127.0.0.1:%d" % INTRANET_PORT})
            assert r["ok"], r
            wait_port(19005)
            st, body = http_get(19005)
            assert body == BODY, body
            r = control(SERVER_CONTROL_PORT, "forward.remove", {"name": "dyn"})
            assert r["ok"], r
        check("运行时动态增删转发规则", dynamic_rules)

        # 大报文（验证部分写 / 背压）
        def large_payload():
            st, data = http_get(TUNNEL_PORT, "/large", timeout=30)
            assert st == 200, "状态 %d" % st
            assert len(data) == 512 * 1024, "长度不符: %d" % len(data)
            assert data[:16] == b"0123456789abcdef", data[:16]
            assert data[-16:] == b"0123456789abcdef", data[-16:]
        check("穿透传输 512KB 大报文", large_payload)

        # 并发：多路复用下 30 条 stream 同时传输
        def concurrent():
            errors = []
            bad = []
            results = []

            def worker(i):
                try:
                    st, body = http_get(TUNNEL_PORT, timeout=20)
                    if st == 200 and body == BODY:
                        results.append(i)
                    else:
                        bad.append((i, st, len(body), body[:40]))
                except Exception as e:
                    errors.append(str(e))

            threads = [threading.Thread(target=worker, args=(i,)) for i in range(30)]
            for t in threads:
                t.start()
            for t in threads:
                t.join(timeout=30)
            assert not errors, "并发请求出错: %r" % errors[:3]
            assert not bad, "内容不符 %d/30: %r" % (len(bad), bad[:3])
            assert len(results) == 30, "成功 %d/30" % len(results)
        check("30 并发连接同时穿透", concurrent)

        # ---------- 4. 断线重连 ----------
        # 放在最后：这一步会主动停掉正常客户端，属于破坏性用例
        print("\n[4] 认证失败与重连恢复")

        def bad_token():
            r = control(AGENT_CONTROL_PORT, "client.stop")
            assert r["ok"], r
            p5, f5 = start([exe, "client", "--server", "127.0.0.1:%d" % SERVER_AGENT_PORT,
                            "--token", "wrong-token", "--client-id", "bad-agent",
                            "--tunnel", "x:%d:0" % INTRANET_PORT,
                            "--control-port", str(AGENT_CONTROL_PORT)], "test_badtoken.log")
            procs.append((p5, f5))
            time.sleep(2.0)
            r = control(AGENT_CONTROL_PORT, "status")
            assert r["data"]["client"]["state"] == "idle", r["data"]["client"]
            stop(p5, f5)
        check("错误 token 被拒绝", bad_token)

        def reconnect():
            r = control(AGENT_CONTROL_PORT, "client.start")
            assert r["ok"], r
            assert wait_port(TUNNEL_PORT, timeout=15), "公网端口未恢复"
            deadline = time.time() + 15
            last = None
            while time.time() < deadline:
                try:
                    st, body = http_get(TUNNEL_PORT)
                    if st == 200 and body == BODY:
                        break
                    last = (st, body)
                except Exception as e:
                    last = e
                time.sleep(0.3)
            else:
                raise Exception("重连后穿透仍不可用: %r" % (last,))

            # 服务端应只保留一条 agent 会话，且隧道重新注册
            deadline = time.time() + 10
            while time.time() < deadline:
                d = control(SERVER_CONTROL_PORT, "status")["data"]
                if d["server"]["agentCount"] == 1 and d["server"]["tunnelCount"] == 1:
                    return
                time.sleep(0.3)
            raise Exception("服务端会话数异常: %r" % (d["server"],))
        check("客户端重连后隧道自动恢复", reconnect)

    finally:
        for p, f in procs:
            stop(p, f)
        httpd.shutdown()

    print("\n结果: %d 通过, %d 失败" % (len(PASSED), len(FAILED)))
    for name, err in FAILED:
        print("  - %s: %s" % (name, err))
    return 0 if not FAILED else 1


if __name__ == "__main__":
    sys.exit(main())
