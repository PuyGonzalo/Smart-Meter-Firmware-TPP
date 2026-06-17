#!/usr/bin/env python3
"""
session_replay.py — Replay firmware's registration + periodic session FSM in Python.

Mirrors the exact sequence and timeouts from atcom_registration.c and
atcom_session.c (DEBUG_FAST_TIMEOUTS values). Talks directly to the Quectel
BG95 over serial, so you can iterate timeout values and observe what breaks
under marginal signal.

Flow:
    AT sync → setup PDP context → open UDP socket → REGISTRATION
    (REGISTER_REQUEST → REGISTER_RESPONSE → ACK → drain) → PERIODIC SESSION
    (SESSION_START_REQUEST → HANDSHAKE → READ/WRITE → ACK), repeat N sessions.

Usage:
    pip install pyserial
    python session_replay.py /dev/ttyUSB2 -v --sessions 3
    python session_replay.py /dev/ttyUSB2 --skip-registration \\
        --device-id <hex32> --mac <hex32>          # reuse known creds
    python session_replay.py /dev/ttyUSB2 --timeout-send 30   # iterate timeouts

The script assumes the BG95 is powered on and the SIM is attached. It does
NOT configure bands/APN/iotopmode — do that one-shot manually beforehand.
"""

import argparse
import re
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

import serial


# ============================================================================
# Protocol constants — match firmware (atcom_internal.h, at_parser.h)
# ============================================================================

ENV_VERSION = 1
DEV_ID_BYTES = 16
MAC_BYTES = 16
# Envelope structure: [header(30)] + [payload(N)] + [MAC(16)]
# Header: version(1) + msg_type(1) + device_id(16) + seq(4) + timestamp(8) = 30 bytes
# MAC is appended AT THE END, after the payload (not at byte 30 — that's where
# the MAC lands only when payload_size == 0).
ENV_PRE_PAYLOAD_BYTES = 1 + 1 + DEV_ID_BYTES + 4 + 8  # 30

MSG_TYPE_HANDSHAKE = 0x00
MSG_TYPE_HANDSHAKE_RESPONSE = 0x01
MSG_TYPE_REGISTER_REQUEST = 0x02
MSG_TYPE_REGISTER_RESPONSE = 0x03
MSG_TYPE_READ_REQUEST = 0x0A
MSG_TYPE_READ_RESPONSE = 0x0B
MSG_TYPE_WRITE_REQUEST = 0x14
MSG_TYPE_WRITE_RESPONSE = 0x15
MSG_TYPE_ACK = 0xFF
MSG_TYPE_SESSION_START_REQUEST = 0xF0

MSG_STATUS_OK = 0x00

MSG_NAMES = {
    0x00: "HANDSHAKE",
    0x01: "HANDSHAKE_RESPONSE",
    0x02: "REGISTER_REQUEST",
    0x03: "REGISTER_RESPONSE",
    0x0A: "READ_REQUEST",
    0x0B: "READ_RESPONSE",
    0x14: "WRITE_REQUEST",
    0x15: "WRITE_RESPONSE",
    0xFF: "ACK",
    0xF0: "SESSION_START_REQUEST",
}


# ============================================================================
# Tunable timeouts (DEBUG_FAST_TIMEOUTS values from atcom_internal.h)
# ============================================================================

@dataclass
class Timeouts:
    """All timeouts in seconds. Defaults match firmware DEBUG mode."""
    qiact_query: float = 2.0       # AT+QIACT?
    qiact_activate: float = 150.0  # AT+QIACT=n
    qiopen: float = 150.0          # AT+QIOPEN (used as upper bound for QISTATE polling)
    cgpaddr: float = 3.0           # AT+CGPADDR
    send: float = 5.0              # SEND OK wait after QISENDEX
    wait_response: float = 3.0     # inter-poll delay in WAIT_SEND / POLL_WAIT
    data_rdy: float = 5.0          # QIRD,N,0 response (buffer length query)
    data_request: float = 2.0      # QIRD,N (data read)
    poll_resend: float = 30.0      # resend last msg if no HES response
    generic_retry: float = 0.5     # short retry delay on AT cmd send failure
    drain_window: float = 6.0      # total window to drain HES confirm ACK
    drain_retry: float = 0.5       # inter-poll delay during drain
    restart_base: float = 0.2      # backoff base after handle_failure
    restart_max: float = 3.0       # backoff cap
    max_failures: int = 20         # hard reset threshold


# ============================================================================
# Stats collection
# ============================================================================

@dataclass
class Stats:
    sends: int = 0
    send_ok: int = 0
    send_timeout: int = 0
    resends: int = 0
    qird_ok: int = 0
    qird_timeout: int = 0
    failures: int = 0
    send_latencies_ms: list = field(default_factory=list)
    hes_response_latencies_ms: list = field(default_factory=list)
    # Cold-start timing breakdown (one entry per cold start)
    cold_start_reattach_ms: list = field(default_factory=list)
    cold_start_qiact_ms: list = field(default_factory=list)
    cold_start_qiopen_ms: list = field(default_factory=list)
    cold_start_total_ms: list = field(default_factory=list)

    def summarize(self) -> str:
        def stats_str(name, samples):
            if not samples:
                return f"  {name}: no samples"
            mn, mx = min(samples), max(samples)
            avg = sum(samples) / len(samples)
            return f"  {name}: n={len(samples)}  min={mn}ms  avg={avg:.0f}ms  max={mx}ms"

        return (
            "\n========== STATS ==========\n"
            f"  QISENDEX sends:    {self.sends}\n"
            f"  SEND OK received:  {self.send_ok}\n"
            f"  SEND OK timeouts:  {self.send_timeout}\n"
            f"  Resends (HES mudo): {self.resends}\n"
            f"  QIRD ok:           {self.qird_ok}\n"
            f"  QIRD timeouts:     {self.qird_timeout}\n"
            f"  handle_failure() count: {self.failures}\n"
            + stats_str("SEND OK latency  ", self.send_latencies_ms) + "\n"
            + stats_str("HES reply latency", self.hes_response_latencies_ms) + "\n"
            "  --- COLD START ---\n"
            + stats_str("Reattach (CFUN cycle)", self.cold_start_reattach_ms) + "\n"
            + stats_str("QIACT (PDP)          ", self.cold_start_qiact_ms) + "\n"
            + stats_str("QIOPEN (UDP socket)  ", self.cold_start_qiopen_ms) + "\n"
            + stats_str("TOTAL cold start     ", self.cold_start_total_ms) + "\n"
            "===========================\n"
        )


# ============================================================================
# Envelope builder / parser (matches at_parser.c)
# ============================================================================

def build_envelope(
    msg_type: int,
    device_id: bytes,
    mac: bytes,
    seq: int,
    timestamp: int,
    payload: bytes = b"",
) -> bytes:
    """Build envelope: [header(30)] + [payload(N)] + [MAC(16)] (all big-endian).
    Matches firmware Parser_fBuild_Envelope_w_payload."""
    assert len(device_id) == DEV_ID_BYTES
    assert len(mac) == MAC_BYTES
    ts_hi = (timestamp >> 32) & 0xFFFFFFFF
    ts_lo = timestamp & 0xFFFFFFFF

    hdr = bytearray(ENV_PRE_PAYLOAD_BYTES)
    hdr[0] = ENV_VERSION
    hdr[1] = msg_type
    hdr[2:2 + DEV_ID_BYTES] = device_id
    struct.pack_into(">I", hdr, 18, seq & 0xFFFFFFFF)
    struct.pack_into(">I", hdr, 22, ts_hi)
    struct.pack_into(">I", hdr, 26, ts_lo)
    return bytes(hdr) + payload + mac


def parse_envelope(data: bytes) -> Optional[dict]:
    """Parse [header(30)] + [payload(N)] + [MAC(16)]. MAC is at the end."""
    if len(data) < ENV_PRE_PAYLOAD_BYTES + MAC_BYTES:
        return None
    payload_size = len(data) - ENV_PRE_PAYLOAD_BYTES - MAC_BYTES
    return {
        "version": data[0],
        "msg_type": data[1],
        "device_id": data[2:18],
        "seq": struct.unpack(">I", data[18:22])[0],
        "timestamp_hi": struct.unpack(">I", data[22:26])[0],
        "timestamp_lo": struct.unpack(">I", data[26:30])[0],
        "payload": data[ENV_PRE_PAYLOAD_BYTES:ENV_PRE_PAYLOAD_BYTES + payload_size],
        "mac": data[ENV_PRE_PAYLOAD_BYTES + payload_size:],
    }


# ============================================================================
# Minimal RLP encoder/decoder
# ============================================================================

def rlp_encode_byte(b: int) -> bytes:
    """Generic single-byte encoder (firmware-style for arbitrary byte)."""
    if b < 0x80:
        return bytes([b])
    return bytes([0x81, b])


def rlp_encode_uint8(v: int) -> bytes:
    """Canonical RLP uint8 — matches firmware rlp_encode_uint8.
    Zero is encoded as 0x80 (empty string), NOT 0x00."""
    if v == 0:
        return bytes([0x80])
    return rlp_encode_byte(v)


def rlp_encode_string(s) -> bytes:
    """Encode a string or bytes object as RLP."""
    if isinstance(s, str):
        data = s.encode("ascii")
    else:
        data = bytes(s)
    L = len(data)
    if L == 1 and data[0] < 0x80:
        return data
    if L < 56:
        return bytes([0x80 + L]) + data
    len_bytes = L.to_bytes((L.bit_length() + 7) // 8, "big")
    return bytes([0xB7 + len(len_bytes)]) + len_bytes + data


def rlp_encode_list(items: list[bytes]) -> bytes:
    payload = b"".join(items)
    L = len(payload)
    if L < 56:
        return bytes([0xC0 + L]) + payload
    len_bytes = L.to_bytes((L.bit_length() + 7) // 8, "big")
    return bytes([0xF7 + len(len_bytes)]) + len_bytes + payload


def rlp_handshake_response_payload() -> bytes:
    """list[ status(u8) = 0x00 ]"""
    return rlp_encode_list([rlp_encode_uint8(MSG_STATUS_OK)])


def rlp_register_request_payload(imei: str, ipv6: str) -> bytes:
    """list[ imei_str, ipv6_str ]"""
    return rlp_encode_list([rlp_encode_string(imei), rlp_encode_string(ipv6)])


def rlp_decode_register_response(payload: bytes) -> tuple[int, int]:
    """Decode list[flag(u8), next_wake_time(u64)]. Returns (flag, next_wake)."""
    if not payload:
        return (0xFF, 0)
    # Outer list header
    h = payload[0]
    if h < 0xC0:
        return (0xFF, 0)
    if h < 0xF8:
        list_data = payload[1:1 + (h - 0xC0)]
    else:
        n = h - 0xF7
        L = int.from_bytes(payload[1:1 + n], "big")
        list_data = payload[1 + n:1 + n + L]
    # Decode flag (single byte) — firmware encodes 0 as 0x80 (empty string)
    if not list_data:
        return (0xFF, 0)
    flag_byte = list_data[0]
    if flag_byte == 0x80:
        flag = 0
        rest = list_data[1:]
    elif flag_byte < 0x80:
        flag = flag_byte
        rest = list_data[1:]
    elif flag_byte == 0x81:
        flag = list_data[1]
        rest = list_data[2:]
    else:
        return (0xFF, 0)
    # Decode next_wake_time (u64)
    if not rest:
        return (flag, 0)
    h2 = rest[0]
    if h2 < 0x80:
        return (flag, h2)
    if h2 < 0xB8:
        L = h2 - 0x80
        return (flag, int.from_bytes(rest[1:1 + L], "big"))
    return (flag, 0)


# ============================================================================
# Serial / AT command wrapper
# ============================================================================

class ATModem:
    def __init__(self, port: str, baud: int, verbose: bool):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.verbose = verbose
        self.t0 = time.time()
        self.rx_buf = bytearray()
        time.sleep(0.3)
        self._drain()

    def _now(self) -> str:
        return f"[t+{time.time() - self.t0:6.2f}s]"

    def _log(self, prefix: str, line: str):
        if self.verbose:
            print(f"{self._now()} {prefix} {line}", flush=True)

    def _drain(self):
        n = self.ser.in_waiting
        if n:
            d = self.ser.read(n)
            if self.verbose:
                self._log("<<< (drain)", d.decode("ascii", "replace").strip())

    def write_cmd(self, cmd: str):
        full = cmd + "\r\n"
        self._log(">>>", cmd)
        self.ser.write(full.encode("ascii"))

    def wait_for_response(
        self,
        timeout: float,
        terminators: tuple = ("OK", "ERROR", "SEND OK", "SEND FAIL", "+CME ERROR"),
    ) -> Optional[bytes]:
        """Read until terminator on its own line or timeout."""
        deadline = time.time() + timeout
        buf = bytearray(self.rx_buf)
        self.rx_buf.clear()
        while time.time() < deadline:
            n = self.ser.in_waiting
            if n:
                buf.extend(self.ser.read(n))
                text = buf.decode("ascii", "replace")
                for term in terminators:
                    if re.search(rf"(^|\r\n|\n){re.escape(term)}\s*(\r\n|\n|$)", text):
                        self._log("<<<", text.strip())
                        return bytes(buf)
            else:
                time.sleep(0.01)
        if buf:
            self._log("<<< (TIMEOUT, partial)", buf.decode("ascii", "replace").strip())
            self.rx_buf = bytearray(buf)
        else:
            self._log("<<<", "(TIMEOUT, no data)")
        return None

    def wait_for_pattern(self, pattern: bytes, timeout: float) -> Optional[bytes]:
        """Read until regex pattern matches anywhere in buffer."""
        deadline = time.time() + timeout
        buf = bytearray(self.rx_buf)
        self.rx_buf.clear()
        compiled = re.compile(pattern)
        while time.time() < deadline:
            n = self.ser.in_waiting
            if n:
                buf.extend(self.ser.read(n))
                if compiled.search(bytes(buf)):
                    self._log("<<<", bytes(buf).decode("ascii", "replace").strip())
                    return bytes(buf)
            else:
                time.sleep(0.01)
        if buf:
            self._log("<<< (TIMEOUT awaiting pattern, partial)",
                      bytes(buf).decode("ascii", "replace").strip())
            self.rx_buf = buf
        else:
            self._log("<<<", "(TIMEOUT awaiting pattern, no data)")
        return None

    def send_at(self, cmd: str, timeout: float = 2.0) -> Optional[bytes]:
        self.write_cmd(cmd)
        return self.wait_for_response(timeout)

    def read_binary(self, expected_len: int, timeout: float) -> bytes:
        deadline = time.time() + timeout
        out = bytearray(self.rx_buf)
        self.rx_buf.clear()
        while len(out) < expected_len and time.time() < deadline:
            n = self.ser.in_waiting
            if n:
                out.extend(self.ser.read(n))
            else:
                time.sleep(0.005)
        return bytes(out[:expected_len])

    def close(self):
        self.ser.close()


# ============================================================================
# Modem helpers — IMEI, IPv6
# ============================================================================

def fetch_imei(modem: ATModem) -> str:
    """AT+CGSN → 15-digit IMEI."""
    resp = modem.send_at("AT+CGSN", 3.0)
    if not resp:
        raise RuntimeError("AT+CGSN no response")
    text = resp.decode("ascii", "replace")
    m = re.search(r"\b(\d{15})\b", text)
    if not m:
        raise RuntimeError(f"AT+CGSN: no IMEI in response: {text!r}")
    return m.group(1)


def fetch_ipv6(modem: ATModem, t: Timeouts, ctx: int = 1) -> str:
    """AT+CGPADDR=ctx → IP string."""
    resp = modem.send_at(f"AT+CGPADDR={ctx}", t.cgpaddr)
    if not resp:
        raise RuntimeError("AT+CGPADDR no response")
    text = resp.decode("ascii", "replace")
    m = re.search(rf"\+CGPADDR:\s*{ctx}\s*,\s*([0-9a-fA-F:.]+)", text)
    if not m:
        raise RuntimeError(f"AT+CGPADDR: no IP in response: {text!r}")
    return m.group(1)


# ============================================================================
# Modem readiness check — mirrors BG95_wait_until_ready() in bg95.c
# ============================================================================

# Defaults from bg95.h (BG95_READY_*_TIMEOUT_MS), in seconds.
BG95_READY_AT_TIMEOUT_S = 15.0   # AT respond — typical modem boot ~13s
BG95_READY_SIM_TIMEOUT_S = 20.0  # AT+CPIN? READY — Quectel recommends 20s
BG95_READY_NET_TIMEOUT_S = 60.0  # AT+CEREG? stat=1|5 — Quectel recommends 60s


class ReadyError(RuntimeError):
    """Raised when the modem fails any readiness phase. The phase attribute
    matches the bg95_ready_t enum value in bg95.h."""
    def __init__(self, phase: str, detail: str = ""):
        self.phase = phase
        super().__init__(f"{phase}: {detail}" if detail else phase)


def _poll_for_response(modem: ATModem, cmd: str, expected: str,
                       total_timeout_s: float, poll_interval_s: float) -> bool:
    """Send `cmd` repeatedly until `expected` appears in the response or
    `total_timeout_s` elapses. Mirrors _poll_for_response() in bg95.c."""
    deadline = time.time() + total_timeout_s
    while time.time() < deadline:
        resp = modem.send_at(cmd, min(poll_interval_s, 2.0))
        if resp and expected.encode("ascii") in resp:
            return True
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        time.sleep(min(poll_interval_s, remaining))
    return False


def wait_until_ready(modem: ATModem,
                     at_timeout_s: float = BG95_READY_AT_TIMEOUT_S,
                     sim_timeout_s: float = BG95_READY_SIM_TIMEOUT_S,
                     net_timeout_s: float = BG95_READY_NET_TIMEOUT_S) -> None:
    """Verify the modem is fully ready before network operations.

    Mirrors BG95_wait_until_ready() in bg95.c (TCP/IP Application Note v1.4
    fig 1):
      1. AT responds OK              — modem alive
      2. AT+CPIN? = READY            — SIM ready
      3. AT+CEREG? stat=1|5          — network attached (home or roaming)

    Raises ReadyError with phase = AT_TIMEOUT | SIM_TIMEOUT | NET_TIMEOUT.
    """
    print(f"\n{modem._now()} === Modem readiness check ===", flush=True)

    # Phase 1: modem alive
    t0 = time.time()
    if not _poll_for_response(modem, "AT", "OK", at_timeout_s, 0.5):
        raise ReadyError("AT_TIMEOUT", f"no OK in {at_timeout_s}s")
    print(f"{modem._now()} [1/3] AT alive ({(time.time() - t0) * 1000:.0f}ms)",
          flush=True)

    # Phase 2: SIM ready
    t0 = time.time()
    if not _poll_for_response(modem, "AT+CPIN?", "READY", sim_timeout_s, 1.0):
        raise ReadyError("SIM_TIMEOUT", f"CPIN not READY in {sim_timeout_s}s")
    print(f"{modem._now()} [2/3] SIM READY ({(time.time() - t0) * 1000:.0f}ms)",
          flush=True)

    # Phase 3: PS network attached. CEREG returns "+CEREG: <n>,<stat>".
    # stat=1 (home) or stat=5 (roaming) are OK. Poll every 2s, same as
    # firmware (Quectel recommends for the 60s attach window).
    t0 = time.time()
    deadline = time.time() + net_timeout_s
    cereg_re = re.compile(r"\+CEREG:\s*\d+\s*,\s*(\d+)")
    while time.time() < deadline:
        resp = modem.send_at("AT+CEREG?", 1.0)
        if resp:
            m = cereg_re.search(resp.decode("ascii", "replace"))
            if m and m.group(1) in ("1", "5"):
                stat_name = "home" if m.group(1) == "1" else "roaming"
                print(f"{modem._now()} [3/3] CEREG stat={m.group(1)} "
                      f"({stat_name}) ({(time.time() - t0) * 1000:.0f}ms)",
                      flush=True)
                return
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        time.sleep(min(2.0, remaining))
    raise ReadyError("NET_TIMEOUT",
                     f"CEREG never reached stat=1|5 in {net_timeout_s}s")


# ============================================================================
# UDP context + QIOPEN setup
# ============================================================================

def setup_pdp_and_udp(modem: ATModem, t: Timeouts, host: str, port: int,
                       ctx: int = 1, conn_id: int = 0):
    print(f"\n{modem._now()} === Setup PDP context + UDP socket ===", flush=True)

    resp = modem.send_at("AT+QIACT?", t.qiact_query)
    needs_activate = True
    if resp:
        text = resp.decode("ascii", "replace")
        if re.search(rf"\+QIACT:\s*{ctx},\s*1", text):
            print(f"{modem._now()} PDP context {ctx} already active", flush=True)
            needs_activate = False

    if needs_activate:
        resp = modem.send_at(f"AT+QIACT={ctx}", t.qiact_activate)
        if not resp or b"OK" not in resp:
            raise RuntimeError("QIACT failed")

    # Close any stale socket
    modem.send_at(f"AT+QICLOSE={conn_id}", 5.0)

    # Open new UDP socket; poll QISTATE for state=2
    open_cmd = f'AT+QIOPEN={ctx},{conn_id},"UDP","{host}",{port},0,0'
    resp = modem.send_at(open_cmd, 5.0)
    if not resp:
        raise RuntimeError("QIOPEN command timeout (no OK)")

    deadline = time.time() + min(t.qiopen, 30.0)
    while time.time() < deadline:
        time.sleep(0.5)
        st = modem.send_at(f"AT+QISTATE=1,{conn_id}", 3.0)
        if st:
            stx = st.decode("ascii", "replace")
            ms = re.search(
                rf'\+QISTATE:\s*{conn_id},"UDP",[^\n]*?,(\d+),(\d+),(\d+),',
                stx,
            )
            if ms and int(ms.group(3)) == 2:
                print(f"{modem._now()} UDP socket {conn_id} → {host}:{port} OPEN", flush=True)
                return
    raise RuntimeError("QIOPEN socket never reached state=2")


def simulate_cold_start(modem: ATModem, t: Timeouts, stats: Stats,
                         host: str, port: int, ctx: int = 1, conn_id: int = 0):
    """Force a full cold-start cycle: close socket → deactivate PDP → RF off →
    RF on → wait reattach → reactivate PDP → reopen socket.
    Measures each step. This is what the firmware does every periodic session."""
    print(f"\n{modem._now()} === COLD START SIMULATION ===", flush=True)

    t_total = time.time()
    # 1. Close socket + deactivate PDP
    modem.send_at(f"AT+QICLOSE={conn_id}", 10.0)
    modem.send_at(f"AT+QIDEACT={ctx}", 40.0)

    # 2. RF off (CFUN=4 = airplane mode, faster than CFUN=0 + cleaner reattach)
    modem.send_at("AT+CFUN=4", 15.0)
    time.sleep(1.0)

    # 3. RF on — start measuring reattach
    t_reattach = time.time()
    modem.send_at("AT+CFUN=1", 15.0)

    # 4. Poll CEREG until stat = 1 (home) or 5 (roaming)
    reattach_deadline = time.time() + 120.0
    attached = False
    while time.time() < reattach_deadline:
        time.sleep(2.0)
        resp = modem.send_at("AT+CEREG?", 3.0)
        if resp:
            text = resp.decode("ascii", "replace")
            m = re.search(r"\+CEREG:\s*\d+\s*,\s*(\d+)", text)
            if m and int(m.group(1)) in (1, 5):
                attached = True
                break
    reattach_ms = int((time.time() - t_reattach) * 1000)
    if not attached:
        raise RuntimeError(f"Reattach timeout after {reattach_ms/1000:.0f}s")
    stats.cold_start_reattach_ms.append(reattach_ms)
    print(f"{modem._now()}   ✓ reattached ({reattach_ms}ms)", flush=True)

    # 5. Reactivate PDP
    t_qiact = time.time()
    resp = modem.send_at(f"AT+QIACT={ctx}", t.qiact_activate)
    qiact_ms = int((time.time() - t_qiact) * 1000)
    if not resp or b"OK" not in resp:
        raise RuntimeError(f"QIACT failed after {qiact_ms}ms")
    stats.cold_start_qiact_ms.append(qiact_ms)
    print(f"{modem._now()}   ✓ PDP active ({qiact_ms}ms)", flush=True)

    # 6. Reopen UDP socket — reuse setup_pdp_and_udp's QIOPEN logic
    t_qiopen = time.time()
    modem.send_at(f"AT+QICLOSE={conn_id}", 5.0)
    open_cmd = f'AT+QIOPEN={ctx},{conn_id},"UDP","{host}",{port},0,0'
    modem.send_at(open_cmd, 5.0)
    deadline = time.time() + min(t.qiopen, 30.0)
    socket_open = False
    while time.time() < deadline:
        time.sleep(0.5)
        st = modem.send_at(f"AT+QISTATE=1,{conn_id}", 3.0)
        if st:
            stx = st.decode("ascii", "replace")
            ms = re.search(rf'\+QISTATE:\s*{conn_id},"UDP",[^\n]*?,(\d+),(\d+),(\d+),', stx)
            if ms and int(ms.group(3)) == 2:
                socket_open = True
                break
    qiopen_ms = int((time.time() - t_qiopen) * 1000)
    if not socket_open:
        raise RuntimeError(f"QIOPEN socket not opened after {qiopen_ms}ms")
    stats.cold_start_qiopen_ms.append(qiopen_ms)
    print(f"{modem._now()}   ✓ socket OPEN ({qiopen_ms}ms)", flush=True)

    total_ms = int((time.time() - t_total) * 1000)
    stats.cold_start_total_ms.append(total_ms)
    print(f"{modem._now()} === COLD START TOTAL: {total_ms}ms ===\n", flush=True)


# ============================================================================
# QISENDEX / QIRD wrappers
# ============================================================================

def qisendex(modem: ATModem, t: Timeouts, conn_id: int, data: bytes, stats: Stats) -> bool:
    """Send via AT+QISENDEX=<conn>,"<hex>". Matches firmware syntax."""
    hex_str = data.hex().upper()
    cmd = f'AT+QISENDEX={conn_id},"{hex_str}"'
    stats.sends += 1
    t_send = time.time()
    modem.write_cmd(cmd)
    resp = modem.wait_for_response(t.send, terminators=("SEND OK", "SEND FAIL", "ERROR"))
    if not resp:
        stats.send_timeout += 1
        print(f"{modem._now()} ✗ SEND timeout ({t.send}s)", flush=True)
        return False
    text = resp.decode("ascii", "replace")
    if "SEND OK" in text:
        lat = int((time.time() - t_send) * 1000)
        stats.send_ok += 1
        stats.send_latencies_ms.append(lat)
        print(f"{modem._now()} SEND OK ({lat}ms)", flush=True)
        return True
    print(f"{modem._now()} ✗ SEND failed: {text.strip()}", flush=True)
    return False


def qird_unread_len(modem: ATModem, t: Timeouts, conn_id: int, stats: Stats) -> int:
    cmd = f"AT+QIRD={conn_id},0"
    modem.write_cmd(cmd)
    resp = modem.wait_for_response(t.data_rdy, terminators=("OK", "ERROR"))
    if not resp:
        stats.qird_timeout += 1
        return -1
    text = resp.decode("ascii", "replace")
    m = re.search(r"\+QIRD:\s*(\d+),\s*(\d+),\s*(\d+)", text)
    if m:
        stats.qird_ok += 1
        return int(m.group(3))
    m2 = re.search(r"\+QIRD:\s*(\d+)\b", text)
    if m2:
        stats.qird_ok += 1
        return int(m2.group(1))
    return -1


def qird_read(modem: ATModem, t: Timeouts, conn_id: int, max_len: int = 512) -> Optional[bytes]:
    cmd = f"AT+QIRD={conn_id},{max_len}"
    modem.write_cmd(cmd)
    resp = modem.wait_for_response(t.data_request, terminators=("OK", "ERROR"))
    if not resp:
        return None
    m = re.search(rb"\+QIRD:\s*(\d+)\r\n", resp)
    if not m:
        return None
    declared = int(m.group(1))
    if declared == 0:
        return b""
    data_start = m.end()
    data = resp[data_start:data_start + declared]
    if len(data) < declared:
        data += modem.read_binary(declared - len(data), 1.0)
    return bytes(data)


# ============================================================================
# Registration FSM (mirrors atcom_registration.c)
# ============================================================================

@dataclass
class Credentials:
    device_id: bytes = bytes(DEV_ID_BYTES)
    mac: bytes = bytes(MAC_BYTES)


def run_registration(
    modem: ATModem, t: Timeouts, stats: Stats,
    imei: str, ipv6: str, conn_id: int,
) -> Credentials:
    """Full registration flow: REGISTER_REQUEST → REGISTER_RESPONSE → ACK → drain."""
    print(f"\n{modem._now()} === REGISTRATION ===", flush=True)
    print(f"{modem._now()} IMEI={imei}  IPv6={ipv6}", flush=True)

    failure_count = 0
    creds = Credentials()

    while failure_count < t.max_failures:
        # --- Build and send REGISTER_REQUEST ---
        payload = rlp_register_request_payload(imei, ipv6)
        env = build_envelope(
            MSG_TYPE_REGISTER_REQUEST,
            device_id=bytes(DEV_ID_BYTES),  # zeros — not yet registered
            mac=bytes(MAC_BYTES),
            seq=0, timestamp=0,
            payload=payload,
        )
        print(f"{modem._now()} → REGISTER_REQUEST seq=0 imei_len={len(imei)} ipv6_len={len(ipv6)} total={len(env)}", flush=True)

        if not qisendex(modem, t, conn_id, env, stats):
            failure_count += 1
            stats.failures += 1
            backoff = min(t.restart_max, t.restart_base * (1 << failure_count))
            print(f"{modem._now()} ⚠ reg failure #{failure_count} → backoff {backoff:.1f}s", flush=True)
            time.sleep(backoff)
            continue

        # --- Poll for REGISTER_RESPONSE ---
        time.sleep(t.wait_response)
        poll_start = time.time()
        got_response = None
        while time.time() - poll_start < t.poll_resend * 2:  # one resend max in registration
            n = qird_unread_len(modem, t, conn_id, stats)
            if n < 0:
                break
            if n > 0:
                data = qird_read(modem, t, conn_id)
                if data:
                    lat = int((time.time() - poll_start) * 1000)
                    stats.hes_response_latencies_ms.append(lat)
                    print(f"{modem._now()} ← HES sent {n} bytes ({lat}ms after request)", flush=True)
                    got_response = data
                break
            # Resend at POLL_RESEND_TIMEOUT
            elapsed = time.time() - poll_start
            if elapsed >= t.poll_resend:
                print(f"{modem._now()} ⏳ HES mudo {elapsed:.1f}s → resend REGISTER_REQUEST", flush=True)
                stats.resends += 1
                if not qisendex(modem, t, conn_id, env, stats):
                    failure_count += 1
                    stats.failures += 1
                    break
                poll_start = time.time()
            time.sleep(t.wait_response)

        if not got_response:
            failure_count += 1
            stats.failures += 1
            backoff = min(t.restart_max, t.restart_base * (1 << failure_count))
            print(f"{modem._now()} ⚠ no REGISTER_RESPONSE — failure #{failure_count} → backoff {backoff:.1f}s", flush=True)
            time.sleep(backoff)
            continue

        # --- Parse REGISTER_RESPONSE ---
        print(f"{modem._now()}   raw ({len(got_response)}B): {got_response.hex().upper()}", flush=True)
        env_rx = parse_envelope(got_response)
        if not env_rx or env_rx["msg_type"] != MSG_TYPE_REGISTER_RESPONSE:
            print(f"{modem._now()} ✗ unexpected msg_type: {env_rx['msg_type'] if env_rx else 'parse fail'}", flush=True)
            failure_count += 1
            continue

        creds.device_id = env_rx["device_id"]
        creds.mac = env_rx["mac"]
        flag, next_wake = rlp_decode_register_response(env_rx["payload"])
        print(f"{modem._now()} ← REGISTER_RESPONSE flag={flag} next_wake={next_wake} seq={env_rx['seq']}", flush=True)
        print(f"{modem._now()}   device_id={creds.device_id.hex()}", flush=True)
        print(f"{modem._now()}   mac={creds.mac.hex()}", flush=True)
        print(f"{modem._now()}   payload ({len(env_rx['payload'])}B): {env_rx['payload'].hex().upper()}", flush=True)
        if flag != MSG_STATUS_OK:
            print(f"{modem._now()} ⚠ HES flag={flag} (NOT OK) — continuing for diagnostics anyway", flush=True)

        # --- Send ACK with new credentials ---
        ack_env = build_envelope(
            MSG_TYPE_ACK,
            device_id=creds.device_id,
            mac=creds.mac,
            seq=env_rx["seq"] + 1, timestamp=0,
        )
        print(f"{modem._now()} → ACK seq={env_rx['seq'] + 1}", flush=True)
        if not qisendex(modem, t, conn_id, ack_env, stats):
            failure_count += 1
            continue

        # --- Drain HES confirm ACK (best-effort) ---
        drain_deadline = time.time() + t.drain_window
        time.sleep(t.wait_response)
        while time.time() < drain_deadline:
            n = qird_unread_len(modem, t, conn_id, stats)
            if n > 0:
                _ = qird_read(modem, t, conn_id)
                print(f"{modem._now()}   (drained HES confirm ACK: {n} bytes)", flush=True)
                break
            time.sleep(t.drain_retry)
        print(f"{modem._now()} ✓ REGISTRATION DONE", flush=True)
        return creds

    raise RuntimeError(f"Registration failed after {failure_count} attempts")


# ============================================================================
# Periodic session FSM (mirrors atcom_session.c)
# ============================================================================

@dataclass
class Session:
    seq: int = 0
    last_seq: int = 0
    last_msg_type: int = 0
    last_payload: bytes = b""
    can_resend: bool = False
    failure_count: int = 0
    needs_hard_reset: bool = False
    done: bool = False


def now_unix() -> int:
    return int(time.time())


def send_and_track(
    modem: ATModem, t: Timeouts, stats: Stats, session: Session,
    msg_type: int, payload: bytes, creds: Credentials, conn_id: int,
) -> bool:
    session.last_seq = session.seq
    env = build_envelope(msg_type, creds.device_id, creds.mac,
                          session.seq, now_unix(), payload)
    session.seq += 1
    print(f"{modem._now()} → {MSG_NAMES.get(msg_type, hex(msg_type))} seq={session.last_seq} len={len(env)}", flush=True)
    ok = qisendex(modem, t, conn_id, env, stats)
    if ok:
        session.last_msg_type = msg_type
        session.last_payload = payload
        session.can_resend = True
    return ok


def handle_session_failure(session: Session, t: Timeouts, stats: Stats, modem: ATModem) -> float:
    session.failure_count += 1
    stats.failures += 1
    if session.failure_count >= t.max_failures:
        session.needs_hard_reset = True
        print(f"{modem._now()} ⚠ MAX FAILURES ({session.failure_count}) — would hard reset", flush=True)
        return 0.0
    backoff = min(t.restart_max, t.restart_base * (1 << session.failure_count))
    print(f"{modem._now()} ⚠ failure #{session.failure_count} → backoff {backoff:.1f}s", flush=True)
    return backoff


def poll_loop(
    modem: ATModem, t: Timeouts, stats: Stats, session: Session,
    creds: Credentials, conn_id: int,
) -> Optional[bytes]:
    poll_start = time.time()
    last_send_t = poll_start

    while not session.needs_hard_reset:
        time.sleep(t.wait_response)
        n = qird_unread_len(modem, t, conn_id, stats)
        if n < 0:
            backoff = handle_session_failure(session, t, stats, modem)
            if session.needs_hard_reset:
                return None
            time.sleep(backoff)
            return None  # caller will rebuild

        if n > 0:
            lat = int((time.time() - last_send_t) * 1000)
            stats.hes_response_latencies_ms.append(lat)
            print(f"{modem._now()} ← HES response: {n} bytes ({lat}ms after send)", flush=True)
            return qird_read(modem, t, conn_id)

        elapsed = time.time() - poll_start
        if session.can_resend and elapsed >= t.poll_resend:
            print(f"{modem._now()} ⏳ HES mudo {elapsed:.1f}s → resend", flush=True)
            stats.resends += 1
            session.seq = session.last_seq
            poll_start = time.time()
            last_send_t = time.time()
            ok = send_and_track(modem, t, stats, session,
                                session.last_msg_type, session.last_payload,
                                creds, conn_id)
            if not ok:
                backoff = handle_session_failure(session, t, stats, modem)
                if session.needs_hard_reset:
                    return None
                time.sleep(backoff)
                return None


def run_session(modem: ATModem, t: Timeouts, stats: Stats,
                 creds: Credentials, conn_id: int = 0):
    """Periodic session: SESSION_START → handshake → ... → ACK."""
    session = Session()
    print(f"\n{modem._now()} === PERIODIC SESSION ===\n", flush=True)

    if not send_and_track(modem, t, stats, session,
                           MSG_TYPE_SESSION_START_REQUEST, b"", creds, conn_id):
        print(f"{modem._now()} ✗ initial SEND failed", flush=True)
        return

    while not session.needs_hard_reset and not session.done:
        env_bytes = poll_loop(modem, t, stats, session, creds, conn_id)
        if env_bytes is None:
            print(f"{modem._now()} ✗ session aborted", flush=True)
            return

        env = parse_envelope(env_bytes)
        if env is None:
            print(f"{modem._now()} ✗ {len(env_bytes)}B too short for envelope", flush=True)
            continue

        msg_type = env["msg_type"]
        name = MSG_NAMES.get(msg_type, f"0x{msg_type:02X}")
        print(f"{modem._now()} ← parsed {name} seq={env['seq']} payload_len={len(env['payload'])}", flush=True)

        if msg_type == MSG_TYPE_HANDSHAKE:
            send_and_track(modem, t, stats, session,
                            MSG_TYPE_HANDSHAKE_RESPONSE,
                            rlp_handshake_response_payload(), creds, conn_id)
        elif msg_type == MSG_TYPE_READ_REQUEST:
            print(f"{modem._now()}   (sending dummy READ_RESPONSE — empty list)", flush=True)
            send_and_track(modem, t, stats, session,
                            MSG_TYPE_READ_RESPONSE, bytes([0xC0]), creds, conn_id)
        elif msg_type == MSG_TYPE_WRITE_REQUEST:
            print(f"{modem._now()}   (sending dummy WRITE_RESPONSE)", flush=True)
            inner = rlp_encode_list([])
            payload = rlp_encode_list([rlp_encode_uint8(0x01), inner])
            send_and_track(modem, t, stats, session,
                            MSG_TYPE_WRITE_RESPONSE, payload, creds, conn_id)
        elif msg_type == MSG_TYPE_ACK:
            print(f"{modem._now()} ✓ ACK received — session complete", flush=True)
            session.done = True
        else:
            print(f"{modem._now()} ⚠ unknown msg_type 0x{msg_type:02X}", flush=True)


# ============================================================================
# CLI
# ============================================================================

def parse_hex(s: str, expected_len: int) -> bytes:
    s = s.strip().replace(" ", "").replace(":", "")
    b = bytes.fromhex(s)
    if len(b) != expected_len:
        raise ValueError(f"Expected {expected_len} bytes, got {len(b)}")
    return b


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("port", help="Serial port (e.g. /dev/ttyUSB2)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--host", default="mechardo3d.mooo.com")
    p.add_argument("--port-hes", type=int, default=6565)
    p.add_argument("--conn-id", type=int, default=0)

    # Skip registration: provide existing credentials
    p.add_argument("--skip-registration", action="store_true",
                   help="Skip registration; use --device-id and --mac")
    p.add_argument("--device-id", default=None, help="32 hex chars (16 bytes)")
    p.add_argument("--mac", default=None, help="32 hex chars (16 bytes)")

    # Timeouts
    p.add_argument("--timeout-send", type=float, default=5.0)
    p.add_argument("--timeout-data-rdy", type=float, default=5.0)
    p.add_argument("--timeout-data-request", type=float, default=2.0)
    p.add_argument("--wait-response", type=float, default=3.0)
    p.add_argument("--poll-resend", type=float, default=30.0)
    p.add_argument("--drain-window", type=float, default=6.0)
    p.add_argument("--restart-base", type=float, default=0.2)
    p.add_argument("--restart-max", type=float, default=3.0)
    p.add_argument("--max-failures", type=int, default=20)
    p.add_argument("--qiopen-timeout", type=float, default=30.0)
    p.add_argument("--qiact-timeout", type=float, default=150.0)

    # Readiness check (mirrors BG95_wait_until_ready() in bg95.c)
    p.add_argument("--ready-at-timeout", type=float, default=BG95_READY_AT_TIMEOUT_S,
                   help="Phase 1: AT response timeout (s)")
    p.add_argument("--ready-sim-timeout", type=float, default=BG95_READY_SIM_TIMEOUT_S,
                   help="Phase 2: AT+CPIN? READY timeout (s)")
    p.add_argument("--ready-net-timeout", type=float, default=BG95_READY_NET_TIMEOUT_S,
                   help="Phase 3: AT+CEREG? stat=1|5 timeout (s)")
    p.add_argument("--skip-ready-check", action="store_true",
                   help="Skip the readiness check (assume modem is already attached)")

    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--sessions", type=int, default=1)
    p.add_argument("--cold-start-between", action="store_true",
                   help="Force detach+reattach+QIACT+QIOPEN between sessions "
                        "(simulates what firmware does every periodic wake-up)")

    args = p.parse_args()

    t = Timeouts(
        qiact_query=2.0,
        qiact_activate=args.qiact_timeout,
        qiopen=args.qiopen_timeout,
        cgpaddr=3.0,
        send=args.timeout_send,
        wait_response=args.wait_response,
        data_rdy=args.timeout_data_rdy,
        data_request=args.timeout_data_request,
        poll_resend=args.poll_resend,
        generic_retry=0.5,
        drain_window=args.drain_window,
        drain_retry=0.5,
        restart_base=args.restart_base,
        restart_max=args.restart_max,
        max_failures=args.max_failures,
    )

    print(f"=== session_replay starting ===")
    print(f"  port:     {args.port} @ {args.baud}")
    print(f"  HES:      {args.host}:{args.port_hes}")
    print(f"  timeouts: send={t.send}s wait={t.wait_response}s resend={t.poll_resend}s data_rdy={t.data_rdy}s")
    print(f"  backoff:  base={t.restart_base}s cap={t.restart_max}s max_fail={t.max_failures}")
    print(f"  flow:     {'skip-registration (using CLI creds)' if args.skip_registration else 'registration + sessions'}")
    print()

    modem = ATModem(args.port, args.baud, args.verbose)
    stats = Stats()

    try:
        if args.skip_ready_check:
            if not modem.send_at("AT", 2.0):
                print("✗ Modem doesn't respond to AT — aborting", file=sys.stderr)
                return 1
        else:
            try:
                wait_until_ready(modem,
                                 at_timeout_s=args.ready_at_timeout,
                                 sim_timeout_s=args.ready_sim_timeout,
                                 net_timeout_s=args.ready_net_timeout)
            except ReadyError as e:
                print(f"✗ Modem not ready ({e.phase}): {e} — aborting",
                      file=sys.stderr)
                return 1

        setup_pdp_and_udp(modem, t, args.host, args.port_hes,
                          conn_id=args.conn_id)

        # Decide credentials
        if args.skip_registration:
            if not args.device_id or not args.mac:
                print("✗ --skip-registration requires --device-id and --mac", file=sys.stderr)
                return 1
            creds = Credentials(
                device_id=parse_hex(args.device_id, DEV_ID_BYTES),
                mac=parse_hex(args.mac, MAC_BYTES),
            )
            print(f"\nUsing supplied credentials:")
            print(f"  device_id={creds.device_id.hex()}")
            print(f"  mac={creds.mac.hex()}")
        else:
            imei = fetch_imei(modem)
            ipv6 = fetch_ipv6(modem, t)
            creds = run_registration(modem, t, stats, imei, ipv6, args.conn_id)
            print(f"\nSave for reuse: --device-id {creds.device_id.hex()} --mac {creds.mac.hex()}")

        for i in range(args.sessions):
            if args.sessions > 1:
                print(f"\n========== SESSION {i+1}/{args.sessions} ==========")
            if i > 0 and args.cold_start_between:
                simulate_cold_start(modem, t, stats, args.host, args.port_hes,
                                     conn_id=args.conn_id)
            run_session(modem, t, stats, creds, args.conn_id)
            if i + 1 < args.sessions and not args.cold_start_between:
                time.sleep(2.0)

    except KeyboardInterrupt:
        print("\n^C — aborting", file=sys.stderr)
    except Exception as e:
        print(f"\n✗ FATAL: {e}", file=sys.stderr)
    finally:
        print(stats.summarize())
        modem.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
