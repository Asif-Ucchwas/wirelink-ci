"""WireLink-CI link tests: sender on this machine, receiver on the peer machine.

Environment variables:
  WL_TX_IF    local interface used to send                         (required)
  WL_RX_MAC   MAC address of the receiving interface               (required)
  WL_RX_IF    interface name on the receiving machine              (default: WL_TX_IF)
  WL_RX_HOST  ssh target of the receiving machine; empty = run the receiver locally
  WL_RX_DIR   repo directory on the receiving machine              (default: ~/projects/wirelink-ci)
  WL_SUDO     privilege prefix for the raw-socket programs         (default: "sudo -n"; "" if root)
"""
import os
import re
import shlex
import subprocess
import time

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TX_IF = os.environ.get("WL_TX_IF", "")
RX_MAC = os.environ.get("WL_RX_MAC", "")
RX_IF = os.environ.get("WL_RX_IF", TX_IF)
RX_HOST = os.environ.get("WL_RX_HOST", "")
RX_DIR = os.environ.get("WL_RX_DIR", "~/projects/wirelink-ci")
SUDO = shlex.split(os.environ.get("WL_SUDO", "sudo -n"))

IDLE_SECONDS = 2.5   # receiver exits this long after its last matching frame (or after start)


@pytest.fixture(autouse=True)
def require_config():
    # Fail loudly instead of skipping: a CI run with no link tests must not look green.
    if not TX_IF or not RX_MAC:
        pytest.fail("set WL_TX_IF and WL_RX_MAC (see the docstring at the top of this file)")


def start_receiver():
    args = SUDO + ["./build/receiver", RX_IF, "--idle", str(IDLE_SECONDS), "--timeout", "60"]
    if RX_HOST:
        remote = "cd " + RX_DIR + " && " + " ".join(shlex.quote(a) for a in args)
        cmd = ["ssh", "-o", "BatchMode=yes", RX_HOST, remote]
        cwd = None
    else:
        cmd = args
        cwd = REPO
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    first = proc.stdout.readline()
    if not first.startswith("READY"):
        proc.kill()
        _, err = proc.communicate()
        pytest.fail("receiver did not start: %r %s" % (first, err))
    return proc


def send(count=1, size=100, extra=()):
    cmd = SUDO + [os.path.join(REPO, "build", "sender"), TX_IF, RX_MAC,
                  "--count", str(count), "--size", str(size)] + list(extra)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    assert "SENT" in result.stdout
    assert "dropped=0" in result.stdout, result.stdout   # the local transmit queue must not drop frames


def finish(proc):
    out, err = proc.communicate(timeout=90)
    match = re.search(r"SUMMARY (.*)", out)
    assert match, "no SUMMARY from receiver:\n%s\n%s" % (out, err)
    stats = {k: int(v) for k, v in (item.split("=") for item in match.group(1).split())}
    return stats, out


def test_single_frame_delivered():
    rx = start_receiver()
    send(count=1, size=100)
    stats, _ = finish(rx)
    assert stats["rx"] == 1
    assert stats["crc_err"] == 0 and stats["other_err"] == 0
    assert stats["lost"] == 0 and stats["payload_bad"] == 0


def test_sequence_of_1000_in_order_no_loss():
    rx = start_receiver()
    send(count=1000, size=200, extra=["--interval-us", "200"])
    stats, out = finish(rx)
    assert stats["rx"] == 1000, out
    assert stats["gaps"] == 0 and stats["lost"] == 0 and stats["dup"] == 0
    assert stats["crc_err"] == 0 and stats["payload_bad"] == 0


def test_corrupted_crc_is_dropped():
    rx = start_receiver()
    send(count=5, size=100, extra=["--corrupt", "2"])
    stats, out = finish(rx)
    assert stats["rx"] == 4, out
    assert stats["crc_err"] == 1
    assert stats["gaps"] == 1 and stats["lost"] == 1   # the dropped frame shows up as a sequence gap
    assert stats["payload_bad"] == 0


def test_maximum_size_payload():
    rx = start_receiver()
    send(count=3, size=1490)
    stats, out = finish(rx)
    assert stats["rx"] == 3, out
    assert stats["crc_err"] == 0 and stats["payload_bad"] == 0 and stats["lost"] == 0 and stats["dup"] == 0


def test_foreign_ethertype_is_ignored():
    rx = start_receiver()
    send(count=5, size=100, extra=["--ethertype", "88b6"])   # not our EtherType
    time.sleep(0.3)
    send(count=1, size=100)                                  # receiver must still be alive and correct
    stats, out = finish(rx)
    assert stats["rx"] == 1, out
    assert stats["crc_err"] == 0 and stats["other_err"] == 0


def test_back_to_back_burst():
    rx = start_receiver()
    send(count=500, size=1000)   # no gap between frames
    stats, out = finish(rx)
    assert stats["rx"] == 500, out
    assert stats["lost"] == 0 and stats["gaps"] == 0 and stats["dup"] == 0 and stats["payload_bad"] == 0
