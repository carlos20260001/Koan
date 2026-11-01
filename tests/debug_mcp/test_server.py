import importlib.util
import json
from importlib.machinery import SourceFileLoader
from pathlib import Path
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from unittest.mock import patch


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SERVER_PATH = REPOSITORY_ROOT / "data" / "libexec" / "vos-debug-mcp"
LOADER = SourceFileLoader("vos_debug_mcp", str(SERVER_PATH))
SPEC = importlib.util.spec_from_loader("vos_debug_mcp", LOADER)
SERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SERVER)


class MCPServerTests(unittest.TestCase):
    def setUp(self):
        self.server = SERVER.create_server(0)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.url = "http://127.0.0.1:%d/mcp" % self.server.server_address[1]

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def post(self, payload, headers=None):
        request_headers = {"Content-Type": "application/json"}
        if headers:
            request_headers.update(headers)
        request = Request(
            self.url,
            data=json.dumps(payload).encode("utf-8"),
            headers=request_headers,
            method="POST",
        )
        with urlopen(request, timeout=3) as response:
            return response.status, json.loads(response.read().decode("utf-8"))

    def test_initialize_advertises_tool_capability(self):
        status, response = self.post(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-03-26"},
            }
        )
        self.assertEqual(status, 200)
        self.assertEqual(response["result"]["protocolVersion"], "2025-03-26")
        self.assertIn("tools", response["result"]["capabilities"])

    def test_tools_list_contains_expected_diagnostic_groups(self):
        _, response = self.post(
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}
        )
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertEqual(
            names,
            {
                "get_system_info",
                "list_processes",
                "read_logs",
                "get_service_status",
                "run_diagnostic_command",
            },
        )

    def test_notification_has_no_json_rpc_response(self):
        request = Request(
            self.url,
            data=json.dumps(
                {"jsonrpc": "2.0", "method": "notifications/initialized"}
            ).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urlopen(request, timeout=3) as response:
            self.assertEqual(response.status, 202)
            self.assertEqual(response.read(), b"")

    def test_external_host_header_is_rejected(self):
        request = Request(
            self.url,
            data=b"{}",
            headers={
                "Content-Type": "application/json",
                "Host": "attacker.example",
            },
            method="POST",
        )
        with self.assertRaises(HTTPError) as context:
            urlopen(request, timeout=3)
        self.assertEqual(context.exception.code, 421)
        context.exception.close()

    def test_tool_call_rejects_arbitrary_shell(self):
        _, response = self.post(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": "run_diagnostic_command",
                    "arguments": {"command": "uname; touch /tmp/not-allowed"},
                },
            }
        )
        self.assertTrue(response["result"]["isError"])
        self.assertIn("allowlist", response["result"]["content"][0]["text"])

    def test_tool_call_returns_system_diagnostics(self):
        _, response = self.post(
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {"name": "get_system_info", "arguments": {}},
            }
        )
        self.assertFalse(response["result"]["isError"])
        system_info = json.loads(response["result"]["content"][0]["text"])
        self.assertIn("kernel", system_info)

    def test_external_origin_is_rejected(self):
        request = Request(
            self.url,
            data=b'{"jsonrpc":"2.0","id":5,"method":"ping"}',
            headers={
                "Content-Type": "application/json",
                "Origin": "http://attacker.example",
            },
            method="POST",
        )
        with self.assertRaises(HTTPError) as context:
            urlopen(request, timeout=3)
        self.assertEqual(context.exception.code, 403)
        context.exception.close()

    def test_arbitrary_shell_command_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "allowlist"):
            SERVER.diagnostic_arguments("uname; touch /tmp/not-allowed")

    def test_diagnostic_command_maps_to_fixed_argv(self):
        self.assertEqual(SERVER.diagnostic_arguments("uname"), ("/usr/bin/uname", "-a"))

    def test_system_info_returns_core_fields(self):
        response = json.loads(SERVER.run_tool("get_system_info", {}))
        self.assertIn("kernel", response)
        self.assertIn("uptime_seconds", response)
        self.assertIn("root_filesystem_bytes", response)

    def test_process_list_is_bounded_and_omits_command_line(self):
        response = json.loads(SERVER.run_tool("list_processes", {"limit": 1}))
        self.assertLessEqual(len(response["processes"]), 1)
        self.assertTrue(response["truncated"] or response["count"] <= 1)
        for process in response["processes"]:
            self.assertEqual(set(process), {"pid", "name", "state", "uid"})

    def test_log_reader_uses_bounded_journal_arguments(self):
        with patch.object(SERVER, "_run_command", return_value="log output") as run:
            output = SERVER.run_tool(
                "read_logs",
                {"source": "system", "unit": "app_server.service", "lines": 25},
            )
        self.assertEqual(output, "log output")
        self.assertEqual(
            run.call_args.args[0],
            [
                "/usr/bin/journalctl",
                "--no-pager",
                "--output=short-iso",
                "-b",
                "-u",
                "app_server.service",
                "-n",
                "25",
            ],
        )


if __name__ == "__main__":
    unittest.main()
