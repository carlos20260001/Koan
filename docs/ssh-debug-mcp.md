# SSH Debug MCP

The optional MCP diagnostics server is included in images configured with
`./configure --sshdebug`. It starts only when the **Vitruvian (SSH Debug)**
boot entry adds `vitruvian.sshdebug` to the kernel command line.

The server listens on the guest's `127.0.0.1:8765` only. It is not exposed on
the guest network interface. Connect from the host with an SSH local forward:

```sh
ssh -N -L 8765:127.0.0.1:8765 root@<guest-ip>
```

The debug image currently uses the existing SSH-debug credentials (`root` /
`live`). Keep the debug guest on a trusted network and stop the SSH tunnel when
finished. MCP clients should connect to:

```text
http://127.0.0.1:8765/mcp
```

For MCP clients that accept a Streamable HTTP server entry, the URL is the
server endpoint; no additional authentication header is needed over the local
SSH tunnel.

The repository includes this VS Code workspace config in `.vscode/mcp.json`:

```json
{
  "servers": {
    "vitruvian-ssh-debug": {
      "type": "http",
      "url": "http://127.0.0.1:8765/mcp"
    }
  }
}
```

For OpenCode, add this to the project-root `opencode.json` (or merge the
`mcp` object into your existing OpenCode config):

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "vitruvian-ssh-debug": {
      "type": "remote",
      "url": "http://127.0.0.1:8765/mcp",
      "enabled": true
    }
  }
}
```

In both clients, start the SSH tunnel before connecting. For a guest reachable
directly on the network:

```sh
ssh -N -L 8765:127.0.0.1:8765 root@<guest-ip>
```

For the repository's QEMU user-network forwarding (host port `2222` to guest
port `22`), use:

```sh
ssh -p 2222 -N -L 8765:127.0.0.1:8765 root@127.0.0.1
```

Available tools:

- `get_system_info`: OS, kernel, uptime, load, memory, and root filesystem data.
- `list_processes`: bounded process IDs, names, states, and UIDs; command-line
  arguments are deliberately omitted.
- `read_logs`: bounded current-boot system or kernel journal output, optionally
  filtered to a supported service.
- `get_service_status`: status for a fixed list of system services.
- `run_diagnostic_command`: a fixed allowlist of read-only diagnostics
  (`uname`, `uptime`, memory, disk, IP addresses, and IP routes). This is not an
  arbitrary shell and does not accept command text or arguments.

The service is sandboxed by systemd and runs without Linux capabilities. The
server uses only Python's standard library; `python3` and `iproute2` are
installed in the image only when SSH-debug support is enabled.

Run its protocol and allowlist tests from the repository root with:

```sh
python3 -m unittest discover -s tests/debug_mcp -v
```
