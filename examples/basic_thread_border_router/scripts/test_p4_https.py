#!/usr/bin/env python3
"""Loopback HTTPS tests; synthetic Local password, certificate and verifier only."""
import base64
import hashlib
import hmac
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys

FIXTURE = os.environ.get('P4_TLS_FIXTURE', '/tmp/p488-generated')
BINARY = '/tmp/gateway-p4-https-fixture'

class Server:
    def __init__(self, **env):
        self.env = dict(os.environ, **env)
    def __enter__(self):
        self.proc = subprocess.Popen([BINARY, FIXTURE+'.der', FIXTURE+'.key'], stdout=subprocess.PIPE, text=True, env=self.env)
        line = self.proc.stdout.readline().strip()
        if not line.startswith('PORT '):
            self.proc.wait(timeout=2)
            raise AssertionError('TLS fixture failed to start: '+line)
        self.port = int(line.split()[1])
        self.connections = []
        return self
    def __exit__(self, *_):
        for c in self.connections:
            c.close()
        self.proc.terminate()
        self.proc.wait(timeout=15)
    def connect(self, wrong=False):
        # Trust fixture provisioned out of band. Exact actual peer DER equality
        # is checked before any SCRAM bytes; manufacturer-chain qualification is
        # the separate existing G2.2 suite, not replaced by this test pin.
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        c = ctx.wrap_socket(socket.create_connection(('127.0.0.1', self.port)), server_hostname='synthetic')
        expected = Path(FIXTURE+'.der').read_bytes()
        if wrong:
            expected = bytes([expected[0] ^ 1]) + expected[1:]
        if not hmac.compare_digest(c.getpeercert(binary_form=True), expected):
            c.close()
            raise ValueError('server identity rejected before authentication')
        c.settimeout(12)
        self.connections.append(c)
        return c

def request(c, body, extra=b'', method=b'POST', path=b'/un/p4/auth', content=b'application/octet-stream', raw=None):
    if raw is None:
        raw = method+b' '+path+b' HTTP/1.1\r\nHost: synthetic\r\nContent-Type: '+content+b'\r\nContent-Length: '+str(len(body)).encode()+b'\r\n'+extra+b'\r\n'+body
    c.sendall(raw)
    data=b''
    while b'\r\n\r\n' not in data:
        more=c.recv(1)
        assert more, 'unexpected TLS EOF'
        data+=more
    header, body=data.split(b'\r\n\r\n',1)
    lines=header.split(b'\r\n')
    status=int(lines[0].split()[1])
    fields=[line.split(b': ',1) for line in lines[1:]]
    headers={k.lower():v for k,v in fields}
    length=int(headers[b'content-length'])
    while len(body)<length:
        more=c.recv(length-len(body));assert more;body+=more
    assert headers[b'cache-control']==b'no-store'
    if status!=200:
        assert not body and b'p4-access-token' not in headers
    assert sum(k.lower()==b'p4-access-token' for k,v in fields)<=1
    return status, headers, body

FIRST=b'n,,n=u,r=synthetic-client-nonce'
START=b'\x3e\x01\x01'+struct.pack('>HH',len(FIRST),0)+FIRST

def start(c):
    status, headers, body=request(c,START)
    assert status==200 and body[:2]==b'\x3e\x01' and b'p4-access-token' not in headers
    tx=body[2:18];n=struct.unpack('>H',body[18:20])[0]
    assert len(body)==20+n
    return tx,body[20:]

def proof(tx,server_first,password=b'localpassword'):
    fields=dict(p.split(b'=',1) for p in server_first.split(b','))
    salted=hashlib.pbkdf2_hmac('sha256',password,base64.b64decode(fields[b's']),int(fields[b'i']))
    key=hmac.digest(salted,b'Client Key','sha256')
    final=b'c=biws,r='+fields[b'r']
    message=FIRST[3:]+b','+server_first+b','+final
    signature=hmac.digest(hashlib.sha256(key).digest(),message,'sha256')
    client_proof=bytes(a^b for a,b in zip(key,signature))
    final+=b',p='+base64.b64encode(client_proof)
    expected=b'v='+base64.b64encode(hmac.digest(hmac.digest(salted,b'Server Key','sha256'),message,'sha256'))
    return b'\x3f\x01'+tx+struct.pack('>H',len(final))+final,expected

def complete(c,tx,sf):
    payload,expected=proof(tx,sf)
    status,headers,body=request(c,payload)
    assert status==200 and body[:2]==b'\x3f\x01' and body[2:18]==tx and body[20:]==expected
    assert struct.unpack('>H',body[18:20])[0]==len(expected)
    token=headers[b'p4-access-token']
    assert len(token)==43 and b'=' not in token
    decoded=base64.urlsafe_b64decode(token+b'=')
    assert len(decoded)==32 and base64.urlsafe_b64encode(decoded).rstrip(b'=')==token
    return token,payload

with Server() as s:
    try:
        s.connect(wrong=True)
        raise AssertionError('accepted wrong TLS identity')
    except ValueError:
        pass
    plain=socket.create_connection(('127.0.0.1',s.port));plain.settimeout(2)
    plain.sendall(b'POST /un/p4/auth HTTP/1.1\r\nHost: x\r\n\r\n')
    try: assert not plain.recv(256).startswith(b'HTTP/1.1 200')
    except ConnectionResetError: pass
    plain.close()
    a,b=s.connect(),s.connect()
    ta,sa=start(a);tb,sb=start(b)
    assert ta!=tb
    token_a,replay=complete(a,ta,sa)
    token_b,_=complete(b,tb,sb)
    assert token_a!=token_b
    a.close();b.close()
    c=s.connect()
    status,_,_=request(c,START,extra=b'Authorization: Bearer '+token_a+b'\r\n')
    assert status==200
    assert s.proc.stdout.readline().strip()=='BEARER_RESULT 200'
    assert request(c,replay)[0]==403
print('PASS actual TLS, two concurrent SCRAM clients, server-final correlation, token delivery, reconnect lookup, replay')

with Server() as s:
    a,b=s.connect(),s.connect();tx,sf=start(a);p,_=proof(tx,sf)
    assert request(b,p)[0]==403
    # Rejection on B does not affect A's pending transaction.
    complete(a,tx,sf)
    a.close()
    a=s.connect();tx,sf=start(a);p,_=proof(tx,sf);a.close()
    c=s.connect();assert request(c,p)[0]==403
print('PASS cross-client proof isolation and pending disconnect cleanup')

cases=[
 (dict(method=b'GET'),405),
 (dict(path=b'/un/gateway'),404),
 (dict(path=b'/un/gateway/events'),404),
 (dict(path=b'/un/gateway/diagnostics'),404),
 (dict(path=b'/un/gateway/session/start'),404),
 (dict(path=b'/un/p4/auth?q=1'),400),
 (dict(content=b'application/json'),415),
 (dict(extra=b'Origin: https://example.invalid\r\n'),403),
 (dict(extra=b'Content-Length: '+str(len(START)).encode()+b'\r\n'),400),
 (dict(extra=b'Transfer-Encoding: chunked\r\n'),400),
 (dict(extra=b'Content-Encoding: gzip\r\n'),400),
 (dict(extra=b' Folded: forbidden\r\n'),400),
 (dict(extra=b'X: '+b'x'*4096+b'\r\n'),400),
 (dict(raw=b'POST /un/p4/auth HTTP/1.1\r\nHost: x\r\nContent-Type: application/octet-stream\r\n\r\n'),400),
 (dict(raw=(b'POST /un/p4/auth HTTP/1.1\r\nHost: x\r\nContent-Type: application/octet-stream\r\nContent-Length: '+str(len(START)).encode()+b'\r\n\r\n'+START)*2),400),
 (dict(extra=b'Authorization: Bearer '+b'A'*43+b'\r\nauthorization: Bearer '+b'A'*43+b'\r\n'),400),
 (dict(extra=b'P4-Access-Token: '+b'A'*43+b'\r\n'),400),
 (dict(extra=b'Authorization: Bearer '+b'A'*42+b'B\r\n'),400),
 (dict(body=b'\x01'),400),
 (dict(body=b'x'*533),413),
 (dict(body=START[:2]+b'\x02'+START[3:]),400),
 (dict(body=START[:1]+b'\x02'+START[2:]),400),
 (dict(body=START+b'\0'),400),
]
with Server() as s:
    for kwargs,expected in cases:
        args={'body':START};args.update(kwargs)
        assert request(s.connect(),**args)[0]==expected,(kwargs,expected)
    a=s.connect();tx,sf=start(a);p,_=proof(tx,sf,b'wrong password')
    assert request(a,p)[0]==403
    unknown=s.connect()
    assert request(unknown,START,extra=b'Authorization: Bearer '+b'A'*43+b'\r\n')[0]==200
    assert s.proc.stdout.readline().strip()=='BEARER_RESULT 401'
print('PASS HTTP framing, header duplicates, method/path/type/bounds/scope/version, invalid proof, unknown bearer')

with Server() as s:
    a=s.connect()
    for _ in range(4): assert request(a,START)[0]==200
    assert request(a,START)[0]==429
    for _ in range(4):
        c=s.connect();assert request(c,START)[0]==200;c.close()
    assert request(s.connect(),START)[0]==429
print('PASS per-connection and global rolling-window rate limits survive reconnects')

with Server(P4_FIXTURE_TOKEN_RNG_FAIL='1') as s:
    a=s.connect();tx,sf=start(a);payload,_=proof(tx,sf)
    assert request(a,payload)[0]==503
with Server(P4_FIXTURE_NO_VERIFIER='1') as s:
    assert request(s.connect(),START)[0]==403
with Server() as s:
    tokens=[]
    for _ in range(4):
        c=s.connect();tx,sf=start(c);token,_=complete(c,tx,sf);tokens.append(token);c.close()
    c=s.connect();tx,sf=start(c);payload,_=proof(tx,sf)
    assert request(c,payload)[0]==503
print('PASS unavailable verifier, token RNG failure, full session/token table without eviction')
