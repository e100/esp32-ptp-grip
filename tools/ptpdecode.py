#!/usr/bin/env python3
"""Decode PTP/IP traffic (TCP port 15740) from a pcapng capture.

Usage: tools/ptpdecode.py capture.pcapng [max data bytes shown, default 64]

Prints one line per PTP/IP packet: time, direction, the host-side TCP port
(command and event channels differ), and the decoded fields: operations with
parameters, responses, data phases (hex) and events. Handles Ethernet, Linux
cooked and raw-IP captures. No dependencies beyond Python 3.
"""
import struct, sys
from collections import defaultdict

PORT = 15740

def blocks(f):
    data = f.read()
    off = 0
    endian = '<'
    while off + 12 <= len(data):
        btype, blen = struct.unpack_from(endian + 'II', data, off)
        if btype == 0x0A0D0D0A:  # section header: check byte order
            bom = struct.unpack_from('<I', data, off + 8)[0]
            endian = '<' if bom == 0x1A2B3C4D else '>'
            btype, blen = struct.unpack_from(endian + 'II', data, off)
        yield btype, data[off + 8: off + blen - 4], endian
        off += blen

def packets(path):
    linktypes = []
    tsres = []
    with open(path, 'rb') as f:
        for btype, body, e in blocks(f):
            if btype == 0x00000001:  # IDB
                lt = struct.unpack_from(e + 'H', body, 0)[0]
                linktypes.append(lt)
                # options: if_tsresol (9)
                res = 6
                o = 8
                while o + 4 <= len(body):
                    code, ln = struct.unpack_from(e + 'HH', body, o)
                    if code == 0:
                        break
                    if code == 9:
                        res = body[o + 4]
                    o += 4 + ((ln + 3) & ~3)
                tsres.append(res)
            elif btype == 0x00000006:  # EPB
                ifid, th, tl, caplen, origlen = struct.unpack_from(e + 'IIIII', body, 0)
                ts = ((th << 32) | tl) / (10 ** tsres[ifid] if tsres[ifid] < 128 else 2 ** (tsres[ifid] & 127))
                yield ts, linktypes[ifid], body[20:20 + caplen]

def ip_payload(lt, frame):
    if lt == 1:  # Ethernet
        et = struct.unpack_from('>H', frame, 12)[0]
        off = 14
        while et == 0x8100:
            et = struct.unpack_from('>H', frame, off + 2)[0]
            off += 4
        if et != 0x0800:
            return None
        return frame[off:]
    if lt == 113:  # Linux SLL
        if struct.unpack_from('>H', frame, 14)[0] != 0x0800:
            return None
        return frame[16:]
    if lt == 276:  # SLL2
        if struct.unpack_from('>H', frame, 0)[0] != 0x0800:
            return None
        return frame[20:]
    if lt in (101, 228):  # raw IPv4
        return frame
    if lt == 0:  # BSD loopback
        return frame[4:]
    return None

def tcp_segments(path):
    for ts, lt, frame in packets(path):
        ip = ip_payload(lt, frame)
        if ip is None or len(ip) < 20 or (ip[0] >> 4) != 4 or ip[9] != 6:
            continue
        ihl = (ip[0] & 15) * 4
        tot = struct.unpack_from('>H', ip, 2)[0]
        src = '.'.join(map(str, ip[12:16]))
        dst = '.'.join(map(str, ip[16:20]))
        tcp = ip[ihl:tot]
        sp, dp, seq = struct.unpack_from('>HHI', tcp, 0)
        doff = (tcp[12] >> 4) * 4
        flags = tcp[13]
        payload = tcp[doff:]
        if PORT not in (sp, dp):
            continue
        yield ts, (src, sp, dst, dp), seq, flags, payload

OPS = {0x1001:'GetDeviceInfo',0x1002:'OpenSession',0x1003:'CloseSession',0x1004:'GetStorageIDs',
       0x1005:'GetStorageInfo',0x1007:'GetObjectHandles',0x1008:'GetObjectInfo',0x1009:'GetObject',
       0x100A:'GetThumb',0x100E:'InitiateCapture',0x1014:'GetDevicePropDesc',0x1015:'GetDevicePropValue',
       0x1016:'SetDevicePropValue',0x101B:'GetPartialObject',0x90C7:'Nikon CheckEvent',0x90C8:'Nikon DeviceReady',
       0x90C0:'Nikon Capture',0x90C1:'Nikon AfDrive',0x90C2:'Nikon ChangeCameraMode',0x9201:'Nikon StartLiveView',
       0x9202:'Nikon EndLiveView',0x9203:'Nikon GetLiveViewImg',0x9204:'Nikon MfDrive',0x9205:'Nikon ChangeAfArea',
       0x9206:'Nikon AfDriveCancel',0x9207:'Nikon InitiateCaptureRecInMedia',0x9209:'Nikon GetVendorStorageIDs',
       0x920A:'Nikon StartMovieRecInCard',0x920B:'Nikon EndMovieRec',0x920C:'Nikon TerminateCapture',
       0x941C:'Nikon GetEventEx',0x9426:'Nikon ChangeAELock',0x9428:'Nikon GetLiveViewImageEx',
       0x9435:'Nikon ChangeApplicationMode',0x9439:'Nikon GetVendorCodes',
       0x943A:'Nikon GetDevicePropDescEx',0x943B:'Nikon GetDevicePropValueEx',
       0x943C:'Nikon SetDevicePropValueEx',0x90CC:'Nikon GetPictCtrlData',0x952B:'Nikon GetPairingCode',
       0x935A:'Nikon ConfirmPairing',0x9400:'Nikon GetPartialObjectHiSpeed'}

def opname(c):
    return OPS.get(c, '')

class Stream:
    def __init__(self):
        self.buf = b''
        self.next_seq = None
        self.pending = {}

    def add(self, seq, data):
        if not data:
            return
        if self.next_seq is None:
            self.next_seq = seq
        if seq < self.next_seq:  # retransmit / overlap
            cut = self.next_seq - seq
            if cut >= len(data):
                return
            data = data[cut:]
            seq = self.next_seq
        if seq > self.next_seq:
            self.pending[seq] = data
            return
        self.buf += data
        self.next_seq += len(data)
        while self.next_seq in self.pending:
            d = self.pending.pop(self.next_seq)
            self.buf += d
            self.next_seq += len(d)

    def take(self):
        out = []
        while len(self.buf) >= 8:
            ln, typ = struct.unpack_from('<II', self.buf, 0)
            if ln < 8 or ln > 64 * 1024 * 1024:
                out.append(('desync', self.buf[:64]))
                self.buf = b''
                break
            if len(self.buf) < ln:
                break
            out.append((typ, self.buf[8:ln]))
            self.buf = self.buf[ln:]
        return out

def main():
    path = sys.argv[1]
    hexlimit = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    streams = defaultdict(Stream)
    t0 = None
    camera_ports = {}
    data_acc = {}
    for ts, key, seq, flags, payload in tcp_segments(path):
        if t0 is None:
            t0 = ts
        src, sp, dst, dp = key
        st = streams[key]
        if flags & 0x02:  # SYN
            st.next_seq = seq + 1
            continue
        st.add(seq, payload)
        conn = dp if sp == PORT else sp  # host-side port identifies the connection
        direction = 'cam->host' if sp == PORT else 'host->cam'
        for typ, body in st.take():
            t = ts - t0
            pre = f'{t:9.3f} {direction} c{conn}'
            if typ == 'desync':
                print(pre, 'DESYNC', body.hex())
                continue
            if typ == 1:
                guid = body[:16].hex()
                name = body[16:-4].decode('utf-16le', 'replace').rstrip('\x00')
                ver = struct.unpack_from('<I', body, len(body) - 4)[0]
                print(pre, f'InitCommandRequest guid={guid} name={name!r} ver={ver:#x}')
            elif typ == 2:
                cid = struct.unpack_from('<I', body, 0)[0]
                print(pre, f'InitCommandAck conn={cid} rest={body[4:].hex()}')
            elif typ == 3:
                print(pre, f'InitEventRequest conn={struct.unpack_from("<I", body, 0)[0]}')
            elif typ == 4:
                print(pre, 'InitEventAck')
            elif typ == 5:
                print(pre, f'InitFail reason={struct.unpack_from("<I", body, 0)[0]:#x}')
            elif typ == 6:
                phase, op, tid = struct.unpack_from('<IHI', body, 0)
                params = [struct.unpack_from('<I', body, 10 + 4 * i)[0] for i in range((len(body) - 10) // 4)]
                print(pre, f'OP  {op:04X} {opname(op):<28} tid={tid} phase={phase} params=[{" ".join(f"{p:X}" for p in params)}]')
            elif typ == 7:
                rc, tid = struct.unpack_from('<HI', body, 0)
                params = [struct.unpack_from('<I', body, 6 + 4 * i)[0] for i in range((len(body) - 6) // 4)]
                print(pre, f'RSP {rc:04X} tid={tid} params=[{" ".join(f"{p:X}" for p in params)}]')
            elif typ == 8:
                code, tid = struct.unpack_from('<HI', body, 0)
                params = [struct.unpack_from('<I', body, 6 + 4 * i)[0] for i in range((len(body) - 6) // 4)]
                print(pre, f'EVT {code:04X} tid={tid} params=[{" ".join(f"{p:X}" for p in params)}]')
            elif typ == 9:
                tid, total = struct.unpack_from('<IQ', body, 0)
                data_acc[(key, tid)] = b''
                print(pre, f'StartData tid={tid} len={total}')
            elif typ in (0x0A, 0x0C):
                tid = struct.unpack_from('<I', body, 0)[0]
                data_acc[(key, tid)] = data_acc.get((key, tid), b'') + body[4:]
                if typ == 0x0C:
                    d = data_acc.pop((key, tid))
                    h = d[:hexlimit].hex(' ')
                    print(pre, f'EndData tid={tid} {len(d)}B: {h}{" ..." if len(d) > hexlimit else ""}')
            elif typ == 0x0D:
                print(pre, 'Probe')
            elif typ == 0x0E:
                print(pre, 'ProbeResponse')
            else:
                print(pre, f'type {typ:#x} {body[:32].hex()}')

main()
