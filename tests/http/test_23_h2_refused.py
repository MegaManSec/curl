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
import contextlib
import datetime
import ipaddress
import logging
import socket
import ssl
import struct
import threading
import time

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from testenv import CurlClient, Env

log = logging.getLogger(__name__)

_OK_HEADERS = b'\x88'                     # HPACK static table ':status: 200'
_REFUSED_STREAM = (7).to_bytes(4, 'big')  # RST_STREAM error code


def _frame(ftype, flags, sid, payload=b''):
    return len(payload).to_bytes(3, 'big') + bytes([ftype, flags]) + \
        sid.to_bytes(4, 'big') + payload


def _read_frame(sock, buf):
    while len(buf) < 9 or len(buf) < 9 + int.from_bytes(buf[:3], 'big'):
        data = sock.recv(65536)
        if not data:
            return None, None, None, None, buf
        buf += data
    length = int.from_bytes(buf[:3], 'big')
    ftype, flags = buf[3], buf[4]
    sid = int.from_bytes(buf[5:9], 'big') & 0x7fffffff
    payload = buf[9:9 + length]
    return ftype, flags, sid, payload, buf[9 + length:]


def _make_server_ctx(tmp_path):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, '127.0.0.1')])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (
        x509.CertificateBuilder()
        .subject_name(name).issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(minutes=30))
        .add_extension(
            x509.SubjectAlternativeName(
                [x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]),
            critical=False)
        .sign(key, hashes.SHA256())
    )
    cert_file = tmp_path / 'server.pem'
    key_file = tmp_path / 'server-key.pem'
    cert_file.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_file.write_bytes(key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption()))
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(cert_file), str(key_file))
    ctx.set_alpn_protocols(['h2'])
    return ctx


class RefusedStreamOrigin:
    """A frame-level HTTP/2 origin (TLS + ALPN, so retries stay on h2) used
    to check that a REFUSED_STREAM on one connection cannot authorize
    retrying a POST a second time on a later, unrelated connection.

    Connection 1 serves the bodyless warm-up request normally, then
    refuses the POST with RST_STREAM(REFUSED_STREAM). Connection 2 (the
    retry curl is expected to make) receives the POST body in full and
    then has its TCP connection aborted before any response byte, so the
    only sanctioned retry has already happened. Any POST body received on
    a further connection is a second, unwanted application of the same
    request; such a connection gets a normal 200 response so the exit
    code differs too.
    """

    def __init__(self, tmp_path):
        self.applied = 0
        self._ctx = _make_server_ctx(tmp_path)
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(('127.0.0.1', 0))
        self._srv.settimeout(10)
        self._srv.listen(5)
        self.port = self._srv.getsockname()[1]
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def join(self, timeout=10):
        self._thread.join(timeout=timeout)
        with contextlib.suppress(OSError):
            self._srv.close()

    def _run(self):
        n = 0
        with contextlib.suppress(OSError):
            while n < 4:
                conn, _ = self._srv.accept()
                try:
                    tconn = self._ctx.wrap_socket(conn, server_side=True)
                except ssl.SSLError:
                    continue
                n += 1
                with tconn:
                    self._serve(tconn, n)

    def _serve(self, conn, n):
        buf = b''
        preface = conn.recv(24)
        if len(preface) < 24:
            return
        conn.sendall(_frame(4, 0, 0))  # our (empty) SETTINGS
        while True:
            ftype, flags, sid, payload, buf = _read_frame(conn, buf)
            if ftype is None:
                return
            if ftype == 4 and not flags & 1:
                conn.sendall(_frame(4, 1, 0))  # SETTINGS ack
            elif ftype == 1 and flags & 1:
                # a HEADERS frame with END_STREAM and no DATA: the
                # bodyless warm-up GET
                conn.sendall(_frame(1, 5, sid, _OK_HEADERS))
            elif ftype == 0 and flags & 1:
                # DATA with END_STREAM: the POST body arrived in full
                if n == 1:
                    conn.sendall(_frame(3, 0, sid, _REFUSED_STREAM))
                    return
                self.applied += 1
                if n == 2:
                    # applied, then the connection dies before a response
                    # byte goes out
                    time.sleep(0.1)
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                    struct.pack('ii', 1, 0))
                    return
                conn.sendall(_frame(1, 5, sid, _OK_HEADERS))
                return


class TestRefusedStream:

    # A REFUSED_STREAM on a reused connection must not authorize a second,
    # later retry of the same POST on an unrelated connection. See the
    # "REFUSED_STREAM, retrying a fresh connect" retry branch in
    # Curl_retry_request().
    @pytest.mark.skipif(condition=not Env.have_h2_curl(), reason="curl without h2")
    def test_23_01_refused_stream_stale_flag(self, env: Env, tmp_path):
        origin = RefusedStreamOrigin(tmp_path)
        origin.start()
        curl = CurlClient(env=env, timeout=env.test_timeout)
        url_warmup = f'https://127.0.0.1:{origin.port}/warmup'
        url_post = f'https://127.0.0.1:{origin.port}/pay'
        r = curl.run_direct(args=[
            '--http2', '--insecure', '-v',
            url_warmup,
            '--next', '--http2', '--insecure',
            '-d', 'nonce=abc123&amount=100',
            url_post,
        ])
        origin.join(timeout=10)
        assert origin.applied == 1, \
            f'POST body applied {origin.applied} times, expected 1\n{r.dump_logs()}'
        assert r.exit_code == 56, \
            f'unexpected exit code {r.exit_code}\n{r.dump_logs()}'
