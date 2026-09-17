#***************************************************************************
#                                  _   _ ____  _
#  Project                     ___| | | |  _ \| |
#                             / __| | | | |_) | |
#                            | (__| |_| |  _ <| |___
#                             \___|\___/|_| \_\_____|
#
# Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution. The terms
# are also available at https://curl.se/docs/copyright.html.
#
# You may opt to use, copy, modify, merge, publish, distribute and/or sell
# copies of the Software, and permit persons to whom the Software is
# furnished to do so, under the terms of the COPYING file.
#
# This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
# KIND, either express or implied.
#
# SPDX-License-Identifier: curl
#
###########################################################################
#
import base64
import hashlib
import logging
import os
import re
import shutil
import socket
import subprocess
import threading
import time
from datetime import datetime, timedelta, timezone
from typing import Dict

import pytest
from testenv import CurlClient, Env, LocalClient
from testenv.ports import alloc_ports_and_do

log = logging.getLogger(__name__)


class WsServer:

    def __init__(self, name, env, cmd):
        self.name = name
        self.env = env
        self.run_dir = os.path.join(env.gen_dir, self.name)
        self.err_file = os.path.join(self.run_dir, 'stderr')
        self._rmrf(self.run_dir)
        self._mkpath(self.run_dir)
        self.cmd = cmd
        self.wsproc = None
        self.cerr = None
        self.port = 0

    def check_alive(self, env, port, timeout=Env.SERVER_TIMEOUT):
        curl = CurlClient(env=env)
        url = f'http://localhost:{port}/'
        end = datetime.now(timezone.utc) + timedelta(seconds=timeout)
        while datetime.now(timezone.utc) < end:
            r = curl.http_download(urls=[url])
            if r.exit_code == 0:
                return True
            time.sleep(.1)
        return False

    def _mkpath(self, path):
        if not os.path.exists(path):
            os.makedirs(path)

    def _rmrf(self, path):
        if os.path.exists(path):
            shutil.rmtree(path)

    def startup(self):

        def startup(ports: Dict[str, int]) -> bool:
            self.port = ports[self.name]
            wargs = [self.cmd, '--port', str(self.port)]
            log.info(f'start_ {wargs}')
            self.wsproc = subprocess.Popen(args=wargs,
                                           cwd=self.run_dir,
                                           stderr=self.cerr,
                                           stdout=self.cerr)
            if self.check_alive(self.env, self.port):
                self.env.update_ports(ports)
                return True
            log.error(f'not alive {wargs}')
            self.wsproc.terminate()
            self.wsproc = None
            return False

        self.cerr = open(self.err_file, 'w')  # noqa: SIM115
        port_spec = {
            self.name: socket.SOCK_STREAM
        }
        assert alloc_ports_and_do(port_spec, startup,
                                  self.env.gen_root, max_tries=3)
        assert self.wsproc

    def shutdown(self):
        self.wsproc.terminate()
        self.cerr.close()


@pytest.mark.skipif(condition=not Env.curl_has_protocol('ws'),
                    reason='curl lacks ws protocol support')
class TestWebsockets:

    @pytest.fixture(autouse=True, scope='class')
    def ws_echo(self, env):
        cmd = os.path.join(env.project_dir,
                           'tests/http/testenv/ws_echo_server.py')
        server = WsServer('ws_echo', env, cmd)
        server.startup()
        yield server
        server.shutdown()

    @pytest.fixture(autouse=True, scope='class')
    def ws_4frames(self, env):
        cmd = os.path.join(env.project_dir,
                           'tests/http/testenv/ws_4frames_server.py')
        server = WsServer('ws_4frames', env, cmd)
        server.startup()
        yield server
        server.shutdown()

    def test_20_01_basic(self, env: Env, ws_echo):
        curl = CurlClient(env=env)
        url = f'http://localhost:{ws_echo.port}/'
        r = curl.http_download(urls=[url])
        r.check_response(http_status=426)

    def test_20_02_pingpong_small(self, env: Env, ws_echo):
        payload = 125 * "x"
        client = LocalClient(env=env, name='cli_ws_pingpong')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        r = client.run(args=[url, payload])
        r.check_exit_code(0)

    # the python websocket server does not like 'large' control frames
    def test_20_03_pingpong_too_large(self, env: Env, ws_echo):
        payload = 127 * "x"
        client = LocalClient(env=env, name='cli_ws_pingpong')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        r = client.run(args=[url, payload])
        r.check_exit_code(100)  # CURLE_TOO_LARGE

    @pytest.mark.parametrize("model", [
        pytest.param(1, id='multi_perform'),
        pytest.param(2, id='curl_ws_send+recv'),
    ])
    def test_20_04_data_small(self, env: Env, ws_echo, model):
        client = LocalClient(env=env, name='cli_ws_data')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        r = client.run(args=[f'-{model}', '-m', str(1), '-M', str(10), url])
        r.check_exit_code(0)

    @pytest.mark.parametrize("model", [
        pytest.param(1, id='multi_perform'),
        pytest.param(2, id='curl_ws_send+recv'),
    ])
    def test_20_05_data_med(self, env: Env, ws_echo, model):
        client = LocalClient(env=env, name='cli_ws_data')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        r = client.run(args=[f'-{model}', '-m', str(120), '-M', str(130), url])
        r.check_exit_code(0)

    @pytest.mark.parametrize("model", [
        pytest.param(1, id='multi_perform'),
        pytest.param(2, id='curl_ws_send+recv'),
    ])
    def test_20_06_data_large(self, env: Env, ws_echo, model):
        client = LocalClient(env=env, name='cli_ws_data')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        r = client.run(args=[f'-{model}', '-m', str(65535 - 5), '-M', str(65535 + 5), url])
        r.check_exit_code(0)

    @pytest.mark.parametrize("model", [
        pytest.param(1, id='multi_perform'),
        pytest.param(2, id='curl_ws_send+recv'),
    ])
    def test_20_07_data_large_small_recv(self, env: Env, ws_echo, model):
        run_env = os.environ.copy()
        run_env['CURL_WS_CHUNK_SIZE'] = '1024'
        client = LocalClient(env=env, name='cli_ws_data', run_env=run_env)
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        r = client.run(args=[f'-{model}', '-m', str(65535 - 5), '-M', str(65535 + 5), url])
        r.check_exit_code(0)

    # Send large frames and simulate send blocking on 8192 bytes chunks
    # Simulates error reported in #15865
    @pytest.mark.parametrize("model", [
        pytest.param(1, id='multi_perform'),
        pytest.param(2, id='curl_ws_send+recv'),
    ])
    def test_20_08_data_very_large(self, env: Env, ws_echo, model):
        run_env = os.environ.copy()
        run_env['CURL_WS_CHUNK_EAGAIN'] = '8192'
        client = LocalClient(env=env, name='cli_ws_data', run_env=run_env)
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        count = 10
        large = 20000
        r = client.run(args=[f'-{model}', '-c', str(count), '-m', str(large), url])
        r.check_exit_code(0)

    @pytest.mark.parametrize("model", [
        pytest.param(1, id='multi_perform'),
        pytest.param(2, id='curl_ws_send+recv'),
    ])
    def test_20_09_data_empty(self, env: Env, ws_echo, model):
        client = LocalClient(env=env, name='cli_ws_data')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_echo.port}/'
        count = 10
        large = 0
        r = client.run(args=[f'-{model}', '-c', str(count), '-m', str(large), url])
        r.check_exit_code(0)

    # use ws:// URL with HTTP proxy, check that it tunnels automatically
    def test_20_10_proxy_http(self, env: Env, httpd, ws_echo):
        curl = CurlClient(env=env)
        url = f'ws://127.0.0.1:{ws_echo.port}/'
        xargs = curl.get_proxy_args(proxys=False)
        xargs.extend([
            '--max-time', '2'
        ])
        r = curl.http_download(urls=[url], alpn_proto='http/1.1', with_stats=True,
                               extra_args=xargs)
        # The CONNECT through the proxy fails as it does not allow it
        r.check_exit_code(7)  # CURLE_COULDNT_CONNECT
        assert r.stats[0]['http_connect'] == 403, f'{r}'

    def test_20_11_crazy_pings(self, env: Env):
        st = {}
        send_rounds = 1

        def srv():
            try:
                with socket.socket() as s:
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    s.bind(("127.0.0.1", 0))
                    s.listen(1)
                    st["p"] = s.getsockname()[1]

                    c, _ = s.accept()
                    c.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
                    c.settimeout(Env.SERVER_TIMEOUT)
                    req = b""
                    while b"\r\n\r\n" not in req:
                        req += c.recv(4096)

                    k = re.search(rb"(?im)^Sec-WebSocket-Key:\s*(\S+)", req).group(1)
                    a = base64.b64encode(
                        hashlib.sha1(k + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest()
                    ).decode()
                    c.sendall(
                        (
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            f"Sec-WebSocket-Accept: {a}\r\n\r\n"
                        ).encode()
                    )

                    f = b"\x89\x00" * 65536  # PING frames, many
                    try:
                        for _ in range(send_rounds):
                            c.sendall(f)
                        f = b"\x88\x00"  # CLOSE frame
                        c.sendall(f)
                    except OSError:
                        # Client may close/reset while we intentionally flood frames.
                        # Send errors are expected here, ignore them.
                        pass
                    time.sleep(1)
                    c.close()
            except OSError as e:
                st["err"] = e

        run_env = os.environ.copy()
        if 'CURL_DEBUG' in run_env:
            del run_env['CURL_DEBUG']
        curl = CurlClient(env=env, run_env=run_env)
        send_rounds = 2
        threading.Thread(target=srv, daemon=True).start()
        while "p" not in st and "err" not in st:
            time.sleep(0.01)
        assert "err" not in st, f'ws-ping server failed to start: {st["err"]}'

        url = f'ws://127.0.0.1:{st["p"]}/'
        r = curl.http_download(urls=[url], alpn_proto='http/1.1', with_stats=True,
                               with_profile=True)
        assert r.exit_code in [55, 56], f'{r.dump_logs()}'  # SEND/RECV_ERROR
        assert r.profile, f'{r}'
        rss1 = r.profile.stats['rss-max'] / (1024 * 1024)

        st.clear()
        send_rounds = 10
        threading.Thread(target=srv, daemon=True).start()
        while "p" not in st and "err" not in st:
            time.sleep(0.01)
        assert "err" not in st, f'ws-ping server failed to start: {st["err"]}'

        url = f'ws://127.0.0.1:{st["p"]}/'
        r = curl.http_download(urls=[url], alpn_proto='http/1.1', with_stats=True,
                               with_profile=True)
        assert r.exit_code in [55, 56], f'{r.dump_logs()}'  # SEND/RECV_ERROR
        assert r.profile, f'{r}'
        rss2 = r.profile.stats['rss-max'] / (1024 * 1024)
        assert (rss1 * 1.2) > rss2, 'bad memory increase'

    # test small frames delivery when pausing
    def test_20_12_pause_frames_small(self, env: Env, ws_4frames):
        payload = 127 * "x"
        client = LocalClient(env=env, name='cli_ws_pause')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_4frames.port}/small'
        r = client.run(args=[url, payload])
        r.check_exit_code(0)

    # test small frames delivery when pausing
    def test_20_13_pause_frames_large(self, env: Env, ws_4frames):
        payload = 127 * "x"
        client = LocalClient(env=env, name='cli_ws_pause')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_4frames.port}/large'
        r = client.run(args=[url, payload])
        r.check_exit_code(0)

    # test handling of write callback errors
    def test_20_14_write_err(self, env: Env, ws_4frames):
        payload = 127 * "x"
        client = LocalClient(env=env, name='cli_ws_write_err')
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        url = f'ws://localhost:{ws_4frames.port}/small'
        r = client.run(args=[url, payload])
        r.check_exit_code(0)

    # A peer PING arriving while a curl_ws_send() call has only partially
    # flushed its encoded frame must not desync ws->sendbuf_payload
    # accounting. Once the application resumes the partial send, the wire
    # must still show the single, intact frame it asked for, not a
    # frame header with fewer payload bytes behind it than declared.
    def test_20_15_ping_during_partial_send(self, env: Env):
        run_env = os.environ.copy()
        run_env['CURL_WS_CHUNK_EAGAIN'] = '3000'
        client = LocalClient(env=env, name='cli_ws_ping_desync',
                             run_env=run_env)
        if not client.exists():
            pytest.skip(f'example client not built: {client.name}')
        if not env.curl_is_debug():
            pytest.skip('CURL_WS_CHUNK_EAGAIN needs a debug build')

        st = {}

        def srv():
            try:
                with socket.socket() as s:
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    s.bind(("127.0.0.1", 0))
                    s.listen(1)
                    st["p"] = s.getsockname()[1]
                    c, _ = s.accept()
                    c.settimeout(Env.SERVER_TIMEOUT)
                    req = b""
                    while b"\r\n\r\n" not in req:
                        req += c.recv(4096)
                    k = re.search(rb"(?im)^Sec-WebSocket-Key:\s*(\S+)",
                                 req).group(1)
                    a = base64.b64encode(
                        hashlib.sha1(
                            k + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
                        ).digest()
                    ).decode()
                    c.sendall((
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        f"Sec-WebSocket-Accept: {a}\r\n\r\n"
                    ).encode())
                    # a PING (triggers an auto-PONG) followed by a small
                    # TEXT frame, so curl_ws_recv() has something to
                    # return once it is done auto-ponging.
                    c.sendall(b"\x89\x00")
                    c.sendall(b"\x81\x03hi!")
                    # read the client's entire outbound frame stream to EOF
                    c.settimeout(10)
                    buf = b""
                    try:
                        while True:
                            d = c.recv(65536)
                            if not d:
                                break
                            buf += d
                    except socket.timeout:
                        pass
                    st["stream"] = buf
                    c.close()
            except OSError as e:
                st["err"] = e

        threading.Thread(target=srv, daemon=True).start()
        while "p" not in st and "err" not in st:
            time.sleep(0.01)
        assert "err" not in st, f'server failed to start: {st.get("err")}'

        url = f'ws://127.0.0.1:{st["p"]}/'
        r = client.run(args=[url])
        r.check_exit_code(0)

        end = time.time() + 10
        while "stream" not in st and time.time() < end:
            time.sleep(0.05)
        assert "stream" in st, 'server never captured the client stream'

        # Parse the client's (masked) frame stream. Every frame's declared
        # payload length must be fully backed by actual bytes: a frame
        # left declaring more payload than ever followed it is exactly
        # the send-accounting desync this test guards against.
        stream = st["stream"]
        pos = 0
        text_total = 0
        text_payloads = []
        while pos + 2 <= len(stream):
            b1 = stream[pos + 1]
            opcode = stream[pos] & 0x0f
            plen = b1 & 0x7f
            hp = pos + 2
            if plen == 126:
                assert hp + 2 <= len(stream), 'truncated extended length'
                plen = int.from_bytes(stream[hp:hp + 2], 'big')
                hp += 2
            elif plen == 127:
                assert hp + 8 <= len(stream), 'truncated extended length'
                plen = int.from_bytes(stream[hp:hp + 8], 'big')
                hp += 8
            assert b1 & 0x80, 'client frames must be masked'
            assert hp + 4 <= len(stream), 'truncated mask key'
            mask = stream[hp:hp + 4]
            hp += 4
            avail = len(stream) - hp
            assert avail >= plen, (
                f'frame at offset {pos} declares {plen} payload bytes but '
                f'only {avail} are available before EOF: the send '
                f'accounting desynced')
            payload = bytes(stream[hp + i] ^ mask[i % 4] for i in range(plen))
            if opcode == 0x1:  # TEXT
                text_total += plen
                text_payloads.append(payload)
            pos = hp + plen

        assert text_total == 4000, (
            f'expected a single intact 4000 byte TEXT message, got '
            f'{text_total} bytes across {len(text_payloads)} frame(s)')
        expected = bytes((ord('0') + (i % 10)) for i in range(4000))
        assert b''.join(text_payloads) == expected, \
            'TEXT message content corrupted'
