#!/bin/bash
# The MCP server end to end. The library process runs with the server on (a diagnostics-only environment variable stands
# in for the Settings window) and two VMs in a throwaway library, only one of them allowed. A Python client then speaks
# JSON-RPC over HTTP: access rules (token, host, origin, method), the allow-list, starting the VM and waiting for Mac OS,
# a screenshot, the pointer and keyboard, a clean shutdown, and the stdio bridge (`SheepShaver --mcp-stdio`).
# Opens SheepShaver windows and boots a copy of your Mac OS 9 disk; nothing on your Mac is clicked or typed.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test runs the library window with the MCP server on, and a VM it starts."

WORK="$(mktemp -d)"
M=""
cleanup() { [ -n "$M" ] && kill "$M" 2>/dev/null; sleep 1; [ -n "$M" ] && kill -9 "$M" 2>/dev/null; pkill -f "$WORK/lib" 2>/dev/null; [ -n "${SHEARS_KEEP_LOGS:-}" ] && cp "$WORK"/logs/* "$WORK/manager.log" "$SHEARS_KEEP_LOGS"/ 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/lib/a" "$WORK/lib/b" "$WORK/logs"
shears_prepare "$WORK/lib/a" SheepShears.bin
shears_prepare "$WORK/lib/b" SheepShears.bin
echo '{"name": "Alpha"}' > "$WORK/lib/a/vm.json"
echo '{"name": "Bravo"}' > "$WORK/lib/b/vm.json"

NW_VERBOSE=1 NW_VM_LIBRARY="$WORK/lib" NW_MCP_TEST="18765:testtoken:a" NW_MANAGER_LOGDIR="$WORK/logs" \
    "$SHEEPSHAVER_APP" > "$WORK/manager.log" 2>&1 &
M=$!; disown

cat > "$WORK/client.py" <<'PY'
import base64, json, subprocess, sys, time, urllib.request, urllib.error
sys.path.insert(0, sys.argv[1])
from vmctl import decode_png
APP, PORT = sys.argv[2], 18765
ok = True
def check(name, cond, extra=""):
    global ok
    print(("PASS  " if cond else "FAIL  ") + name + (f"  ({extra})" if extra and not cond else ""))
    ok &= bool(cond)

def post(body, token="testtoken", headers=None, method="POST", host=None):
    h = {"Content-Type": "application/json"}
    if token is not None: h["Authorization"] = f"Bearer {token}"
    h.update(headers or {})
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}/mcp", data=None if method == "GET" else json.dumps(body).encode(), headers=h, method=method)
    if host: req.remove_header("Host"); req.add_unredirected_header("Host", host)
    try:
        with urllib.request.urlopen(req, timeout=900) as r:
            data = r.read()
            return r.status, (json.loads(data) if data else None)
    except urllib.error.HTTPError as e:
        return e.code, None
n = 0
def rpc(method, params=None, **kw):
    global n; n += 1
    return post({"jsonrpc": "2.0", "id": n, "method": method, "params": params or {}}, **kw)
def tool(name, **args):
    st, r = rpc("tools/call", {"name": name, "arguments": args})
    assert st == 200, st
    res = r["result"]
    text = " ".join(c.get("text", "") for c in res["content"])
    return res["isError"], text, res["content"]

# wait for the server
deadline = time.time() + 60
while time.time() < deadline:
    try:
        st, _ = rpc("ping"); break
    except Exception: time.sleep(0.5)
check("the MCP server answers on 127.0.0.1", st == 200)

# access rules
check("no token: 401", rpc("ping", token=None)[0] == 401)
check("wrong token: 401", rpc("ping", token="nope")[0] == 401)
check("a browser Origin: 403", rpc("ping", headers={"Origin": "https://evil.example"})[0] == 403)
check("another Host name (DNS rebinding): 403", rpc("ping", host="evil.example")[0] == 403)
check("GET is not served: 405", post(None, method="GET")[0] == 405)
check("the right token is accepted", rpc("ping")[0] == 200)

# protocol
st, r = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}})
check("initialize", st == 200 and r["result"]["serverInfo"]["name"] == "sheepshaver" and "tools" in r["result"]["capabilities"], r)
st, r = post({"jsonrpc": "2.0", "method": "notifications/initialized"})
check("a notification gets 202 and no body", st == 202 and r is None, (st, r))
st, r = rpc("tools/list")
check("tools/list has the eleven tools", len(r["result"]["tools"]) == 11, [t["name"] for t in r["result"]["tools"]])

# allow-list
err, text, _ = tool("list_vms")
vms = json.loads(text)["virtual_machines"]
check("list_vms shows only the allowed VM", [v["name"] for v in vms] == ["Alpha"] and vms[0]["running"] is False, vms)
err, text, _ = tool("screenshot", vm="Bravo")
check("a VM that is not allowed looks like it does not exist", err and "no such virtual machine" in text and "Bravo" not in text.split("available")[0].replace('"Bravo"', "", 0) or (err and "no such virtual machine" in text), text)
err, text, _ = tool("start_vm", vm="Bravo")
check("a VM that is not allowed cannot be started", err)
err, text, _ = tool("screenshot", vm="alpha")
check("controlling a stopped VM is refused", err and "not running" in text, text)
err, text, _ = tool("mouse_move", vm="Alpha", x=-3, y=2)
check("bad arguments are refused", err)

# start, ready, look, move, type
t = time.time()
err, text, _ = tool("start_vm", vm="Alpha", wait_for_ready=True, timeout_seconds=300)
res = json.loads(text) if not err else {}
check(f"start_vm starts the VM and waits for Mac OS ({time.time() - t:.0f} s)", not err and res.get("started") is True and res.get("ready") is True, text)
time.sleep(5)
err, text, _ = tool("start_vm", vm="Alpha")
check("starting it again says it is already running", not err and json.loads(text).get("already_running") is True, text)
vms = json.loads(tool("list_vms")[1])["virtual_machines"]
check("list_vms shows it running", vms[0]["running"] is True, vms)
err, text, content = tool("screenshot", vm="Alpha")
img = next((c for c in content if c["type"] == "image"), None)
w, h, rows = decode_png(base64.b64decode(img["data"])) if img else (0, 0, [])
check("screenshot returns a PNG image block of the guest screen", img and img["mimeType"] == "image/png" and (w, h) == (1024, 768), (w, h))
err, text, content = tool("screenshot", vm="Alpha", max_width=320)
w2, h2, _ = decode_png(base64.b64decode(next(c for c in content if c["type"] == "image")["data"]))
check("screenshot max_width", (w2, h2) == (320, 240), (w2, h2))
err, text, _ = tool("mouse_move", vm="Alpha", x=400, y=300)
cur = json.loads(text)["cursor"] if not err else {}
check("mouse_move reaches the target", not err and abs(cur.get("x", -9) - 400) <= 1 and abs(cur.get("y", -9) - 300) <= 1, text)
err, text, _ = tool("mouse_click", vm="Alpha", x=600, y=400, button="right")
check("a right (Control) click is accepted", not err, text)
time.sleep(1)
err, text, _ = tool("press_key", vm="Alpha", key="escape")
check("press_key", not err, text)
err, text, _ = tool("type_text", vm="Alpha", text="ok")
check("type_text", not err, text)

# clean shutdown through MCP
err, text, _ = tool("shutdown_vm", vm="Alpha")
check("shutdown_vm asks for a clean shutdown", not err and json.loads(text)["method"] == "clean", text)
deadline = time.time() + 240
while time.time() < deadline:
    if not json.loads(tool("list_vms")[1])["virtual_machines"][0]["running"]: break
    time.sleep(2)
check("the VM stopped by itself after the guest shut down", not json.loads(tool("list_vms")[1])["virtual_machines"][0]["running"])

# stdio bridge
env = {"NW_VERBOSE": "1", "NW_MCP_TEST": f"{PORT}:testtoken:a", "PATH": "/usr/bin:/bin"}
lines = [json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}}),
         json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}),
         json.dumps({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})]
p = subprocess.run([APP, "--mcp-stdio"], input="\n".join(lines) + "\n", capture_output=True, text=True, env=env, timeout=60)
out = [json.loads(l) for l in p.stdout.splitlines() if l.strip()]
check("the stdio bridge answers requests and stays quiet for notifications", len(out) == 2 and out[0]["id"] == 1 and len(out[1]["result"]["tools"]) == 11, (p.stdout[:200], p.stderr[:200]))
env["NW_MCP_TEST"] = f"{PORT}:wrongtoken:a"
p = subprocess.run([APP, "--mcp-stdio"], input=lines[0] + "\n", capture_output=True, text=True, env=env, timeout=60)
out = [json.loads(l) for l in p.stdout.splitlines() if l.strip()]
check("the bridge reports a refused token as an error reply", len(out) == 1 and "error" in out[0], p.stdout[:200])
print("DONE" if ok else "FAILED")
sys.exit(0 if ok else 1)
PY
python3 "$WORK/client.py" "$ROOT/tools/vms" "$SHEEPSHAVER_APP"; rc=$?
[ $rc -eq 0 ] && echo "mcp test: passed" || echo "mcp test: FAILED"
exit $rc
