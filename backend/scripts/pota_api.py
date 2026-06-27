#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ft8web POTA API helper.

    pota_api.py --user <email> --passfile <path> --adif <path>   # upload ADIF
    pota_api.py --user <email> --passfile <path> --activations   # list activated parks
    pota_api.py --selftest                                       # offline checks
"""

import argparse
import base64
import binascii
import datetime
import hashlib
import hmac
import json
import os
import sys

import requests

REGION = "us-east-2"
POOL_ID = "us-east-2_nA5jZ0klh"
CLIENT_ID = "7hluqct0n2nckib7i7sd5753oa"
COGNITO_URL = "https://cognito-idp.%s.amazonaws.com/" % REGION
ADIF_URL = "https://api.pota.app/adif"
JOBS_URL = "https://api.pota.app/user/jobs"
ACTIVATIONS_URL = "https://api.pota.app/user/activations?all=1"
USER_AGENT = "ft8web/1.0"

N_HEX = (
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
    "15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64"
    "ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B"
    "F12FFA06D98A0864D87602733EC86A64521F2B18177B200C"
    "BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31"
    "43DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF"
)
G_HEX = "2"
INFO_BITS = bytearray("Caldera Derived Key", "utf-8")

def _hash(buf):
    """SHA-256 of bytes as lowercase hex, zero-padded to 64 chars."""
    return hashlib.sha256(buf).hexdigest().rjust(64, "0")

def _hex_hash(hex_string):
    return _hash(bytearray.fromhex(hex_string))

def hex_to_long(h):
    return int(h, 16)

def long_to_hex(l):
    return "%x" % l

def pad_hex(value):
    """Left-pad a hex string per the SRP spec (even length, leading 00 if the
    high bit is set so it is read as a positive number)."""
    h = value if isinstance(value, str) else long_to_hex(value)
    if len(h) % 2 == 1:
        h = "0" + h
    elif h[0] in "89ABCDEFabcdef":
        h = "00" + h
    return h

def get_random(nbytes):
    return hex_to_long(binascii.hexlify(os.urandom(nbytes)))

def calculate_a(small_a, big_n, g):
    big_a = pow(g, small_a, big_n)
    if big_a % big_n == 0:
        raise ValueError("safety check for A failed")
    return big_a

def calculate_u(big_a, big_b):
    return hex_to_long(_hex_hash(pad_hex(big_a) + pad_hex(big_b)))

def compute_hkdf(ikm, salt):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    msg = INFO_BITS + bytearray(chr(1), "utf-8")
    return hmac.new(prk, msg, hashlib.sha256).digest()[:16]

def password_authentication_key(username, password, server_b, salt,
                                big_n, g, k, small_a, big_a):
    u_value = calculate_u(big_a, server_b)
    if u_value == 0:
        raise ValueError("U cannot be zero")
    pool_name = POOL_ID.split("_")[1]
    id_hash = _hash(("%s%s:%s" % (pool_name, username, password)).encode("utf-8"))
    x_value = hex_to_long(_hex_hash(pad_hex(salt) + id_hash))
    g_mod_pow_xn = pow(g, x_value, big_n)
    int_value2 = server_b - k * g_mod_pow_xn
    s_value = pow(int_value2, small_a + u_value * x_value, big_n)
    return compute_hkdf(
        bytearray.fromhex(pad_hex(long_to_hex(s_value))),
        bytearray.fromhex(pad_hex(long_to_hex(u_value))),
    )

def _timestamp():
    """AWS expects 'Ddd Mmm D HH:MM:SS UTC YYYY' in English with a non-padded
    day-of-month, regardless of the host locale."""
    now = datetime.datetime.now(datetime.timezone.utc)
    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
    return "%s %s %d %02d:%02d:%02d UTC %d" % (
        days[now.weekday()], months[now.month - 1], now.day,
        now.hour, now.minute, now.second, now.year)

def _cognito(target, payload):
    r = requests.post(
        COGNITO_URL,
        headers={
            "Content-Type": "application/x-amz-json-1.1",
            "X-Amz-Target": "AWSCognitoIdentityProviderService." + target,
            "User-Agent": USER_AGENT,
        },
        data=json.dumps(payload),
        timeout=20,
    )
    try:
        body = r.json()
    except ValueError:
        body = {}
    if r.status_code != 200:
        raise RuntimeError(body.get("message") or body.get("__type")
                           or ("Cognito HTTP %d" % r.status_code))
    return body

def authenticate(username, password):
    big_n = hex_to_long(N_HEX)
    g = hex_to_long(G_HEX)
    k = hex_to_long(_hex_hash(pad_hex(N_HEX) + pad_hex(G_HEX)))
    small_a = get_random(128) % big_n
    big_a = calculate_a(small_a, big_n, g)

    init = _cognito("InitiateAuth", {
        "AuthFlow": "USER_SRP_AUTH",
        "ClientId": CLIENT_ID,
        "AuthParameters": {"USERNAME": username, "SRP_A": long_to_hex(big_a)},
    })
    ch = init.get("ChallengeParameters", {})
    user_id = ch["USER_ID_FOR_SRP"]
    salt = ch["SALT"]
    secret_block = ch["SECRET_BLOCK"]
    server_b = hex_to_long(ch["SRP_B"])
    if server_b % big_n == 0:
        raise ValueError("B mod N cannot be zero")

    timestamp = _timestamp()
    hkdf = password_authentication_key(user_id, password, server_b, salt,
                                       big_n, g, k, small_a, big_a)
    msg = (POOL_ID.split("_")[1].encode("utf-8") + user_id.encode("utf-8")
           + base64.standard_b64decode(secret_block) + timestamp.encode("utf-8"))
    signature = base64.standard_b64encode(
        hmac.new(hkdf, msg, hashlib.sha256).digest()).decode("utf-8")

    resp = _cognito("RespondToAuthChallenge", {
        "ClientId": CLIENT_ID,
        "ChallengeName": "PASSWORD_VERIFIER",
        "ChallengeResponses": {
            "USERNAME": user_id,
            "PASSWORD_CLAIM_SECRET_BLOCK": secret_block,
            "PASSWORD_CLAIM_SIGNATURE": signature,
            "TIMESTAMP": timestamp,
        },
    })
    result = resp.get("AuthenticationResult")
    if not result or not result.get("IdToken"):
        raise RuntimeError("no auth result — check POTA username/password")
    return result["IdToken"]

def _adif_field(data, name):
    import re
    m = re.search(b"<" + name.encode() + rb":(\d+)>", data, re.I)
    if not m:
        return ""
    start = m.end()
    return data[start:start + int(m.group(1))].decode("latin-1").strip()

def pota_filename(data):
    """station_callsign@park_id-yyyymmdd.adi per POTA's naming convention."""
    import re
    call = _adif_field(data, "station_callsign") or _adif_field(data, "operator")
    park = _adif_field(data, "my_sig_info") or _adif_field(data, "my_pota_ref")
    dates = re.findall(rb"<qso_date:\d+>(\d{8})", data, re.I)
    date = min(d.decode() for d in dates) if dates else ""
    if call and park and date:
        return "%s@%s-%s.adi" % (call, park, date)
    return "ft8web.adi"

def upload(id_token, adif_path):
    import subprocess
    import tempfile
    with open(adif_path, "rb") as f:
        data = f.read()
    fname = pota_filename(data)

    cfgfd, cfgpath = tempfile.mkstemp(prefix="ft8web_h_")
    os.write(cfgfd, ('header = "Authorization: %s"\n' % id_token).encode())
    os.write(cfgfd, ('header = "User-Agent: %s"\n' % USER_AGENT).encode())
    os.close(cfgfd)
    os.chmod(cfgpath, 0o600)
    bodyfd, bodypath = tempfile.mkstemp(prefix="ft8web_rb_")
    os.close(bodyfd)
    try:
        proc = subprocess.run(
            ["curl", "-s", "--http2", "-K", cfgpath,
             "-F", "adif=@%s;filename=%s;type=application/octet-stream" % (adif_path, fname),
             "-o", bodypath, "-w", "%{http_code}", ADIF_URL],
            capture_output=True, timeout=60)
        status = int((proc.stdout.decode("ascii", "replace").strip() or "0"))
        body_txt = open(bodypath, "r", encoding="utf-8", errors="replace").read()
    finally:
        os.unlink(cfgpath)
        os.unlink(bodypath)

    out = {"ok": 200 <= status < 300, "status": status}
    try:
        out["body"] = json.loads(body_txt) if body_txt.strip() else {}
    except ValueError:
        out["body"] = body_txt[:1000]
    if not out["ok"]:
        detail = proc.stderr.decode("ascii", "replace")[:200] if status == 0 else json.dumps(out["body"])[:300]
        out["error"] = "POTA upload rejected (HTTP %d): %s" % (status, detail)
    return out

def latest_jobs(id_token):
    try:
        r = requests.get(JOBS_URL,
                         headers={"Authorization": id_token, "User-Agent": USER_AGENT},
                         timeout=20)
        if 200 <= r.status_code < 300:
            return r.json()
    except Exception:
        pass
    return None

def selftest():
    ok = [True]

    def check(name, got, want):
        if got != want:
            ok[0] = False
            sys.stderr.write("FAIL %s: %r != %r\n" % (name, got, want))

    check("sha256(abc)", hashlib.sha256(b"abc").hexdigest(),
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    check("hash_len", len(_hash(b"abc")), 64)
    check("hmac_rfc4231",
          hmac.new(b"\x0b" * 20, b"Hi There", hashlib.sha256).hexdigest(),
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")
    check("pad_hex(2)", pad_hex("2"), "02")
    check("pad_hex(ff)", pad_hex("ff"), "00ff")
    check("pad_hex(N)[:2]", pad_hex(N_HEX)[:2], "00")
    check("N_bits", hex_to_long(N_HEX).bit_length(), 3072)
    big_n = hex_to_long(N_HEX)
    g = hex_to_long(G_HEX)
    k = hex_to_long(_hex_hash(pad_hex(N_HEX) + pad_hex(G_HEX)))
    check("k_nonzero", k > 0, True)
    big_a = calculate_a(12345, big_n, g)
    check("A_in_range", 0 < big_a < big_n, True)
    hkdf = password_authentication_key(
        "user", "pass", (big_a * 3) % big_n, "ABCDEF0123456789",
        big_n, g, k, 12345, big_a)
    check("hkdf_len", len(hkdf), 16)
    check("ts_has_utc", "UTC" in _timestamp(), True)
    if ok[0]:
        sys.stderr.write("selftest OK\n")
    return ok[0]

def list_activations(id_token):
    r = requests.get(ACTIVATIONS_URL,
                     headers={"Authorization": id_token, "User-Agent": USER_AGENT},
                     timeout=30)
    r.raise_for_status()
    refs = sorted({a.get("reference", "") for a in r.json().get("activations", [])
                   if a.get("reference")})
    return {"ok": True, "refs": refs}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--user")
    ap.add_argument("--passfile")
    ap.add_argument("--adif")
    ap.add_argument("--activations", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    if not (a.user and a.passfile and (a.adif or a.activations)):
        print(json.dumps({"ok": False, "error": "missing --user/--passfile/--adif"}))
        sys.exit(2)

    try:
        with open(a.passfile) as f:
            password = f.read().rstrip("\n")
        if not password:
            raise RuntimeError("empty POTA password")
        token = authenticate(a.user, password)
        if a.activations:
            result = list_activations(token)
        else:
            result = upload(token, a.adif)
            if result["ok"]:
                result["jobs"] = latest_jobs(token)
        print(json.dumps(result))
        sys.exit(0 if result["ok"] else 4)
    except Exception as e:
        print(json.dumps({"ok": False, "error": str(e)}))
        sys.exit(3)

if __name__ == "__main__":
    main()
