import tempfile
import unittest
from pathlib import Path

from tools import net1_external_connectivity as net1
from tools import qemu_test_runner as runner


def metric(value):
    return {"value": str(value)}


def records_for(lane="external"):
    values = {
        "network_interfaces": 1,
        "network_ipv4_configured": 1,
        "ipv4_configured": 1,
        "dhcp_initialized": 1,
        "dhcp_bound": 1,
        "dns_initialized": 1,
        "dns_configured": 1,
        "dns_replies": 1,
        "http_initialized": 1,
        "http_responses": 1 if lane == "external" else 0,
        "http_status_code": 200 if lane == "external" else 0,
        "socket_active_count": 0,
        "net_socket_active_count": 0,
        "net_buffer_active_buffers": 0,
        "sk_buff_active_buffers": 0,
    }
    if lane == "no-nic":
        values.update({
            "network_interfaces": 0,
            "network_ipv4_configured": 0,
            "ipv4_configured": 0,
            "dhcp_bound": 0,
        })
    return [{
        "seq": 1,
        "status": "ok",
        "metrics": {name: metric(value) for name, value in values.items()},
    }]


class Net1ConnectivityTests(unittest.TestCase):
    def test_defaults_and_target_validation(self):
        self.assertEqual(net1.validate_external_host("example.com"),
                         "example.com")
        self.assertEqual(net1.validate_external_url("http://example.com/"),
                         "http://example.com/")
        with self.assertRaises(net1.Net1Error):
            net1.validate_external_url("http://user:secret@example.com/")
        with self.assertRaises(net1.Net1Error):
            net1.validate_external_host("example.com;echo secret")

    def test_matrix_has_twelve_sessions(self):
        plan = net1.matrix_plan()
        self.assertEqual(len(plan), 12)
        self.assertEqual({(item["profile"], item["mode"])
                          for item in plan}, set(net1.NET1_LANES))
        self.assertEqual(net1.validate_workers(4), 4)

    def test_interaction_phases_and_command_targets(self):
        interaction = net1.build_interaction(
            "simple", "external", "example.com", "http://example.com/")
        phases = [item["phase"] for item in interaction["steps"]
                  if item.get("op") == "phase"]
        texts = [item["text"] for item in interaction["steps"]
                 if item.get("op") == "text"]
        self.assertEqual(phases, list(net1.NET1_PHASES))
        self.assertIn("nslookup example.com", texts)
        self.assertIn("http get http://example.com/", texts)
        self.assertIn("echo tst5-network", texts)
        self.assertIn("echo net1-external-connectivity", texts)
        self.assertIn({"op": "wait", "seconds": net1.NET1_DHCP_WAIT_SECONDS},
                      interaction["steps"])

    def test_external_restricted_and_no_nic_outcomes(self):
        trace = [{"op": "phase", "phase": phase}
                 for phase in net1.NET1_PHASES]
        event = {"event": "PASS"}
        status, error, observations = net1.validate_run(
            "external", records_for(), trace, event, [], "zephyr>")
        self.assertEqual((status, error), ("PASS", None))
        self.assertTrue(observations["dhcp_automatic"])

        status, error, _ = net1.validate_run(
            "restricted", records_for("restricted"), trace, event, [],
            "zephyr>")
        self.assertEqual((status, error), ("PASS", None))
        no_nic_trace = [{"op": "phase", "phase": phase}
                        for phase in ("boot", "baseline", "final")]
        status, error, _ = net1.validate_run(
            "no-nic", records_for("no-nic"), no_nic_trace, event, [],
            "zephyr>")
        self.assertEqual((status, error), ("PASS", None))

    def test_protocol_prompt_and_qmp_blocked_states(self):
        trace = [{"op": "phase", "phase": phase}
                 for phase in net1.NET1_PHASES]
        status, error, _ = net1.validate_run(
            "external", records_for(), trace, {"event": "PASS"},
            ["duplicate_key"], "zephyr>")
        self.assertEqual(status, "FAIL")
        self.assertIn("protocolo_guest", error)
        status, error, _ = net1.validate_run(
            "external", records_for(), trace, {"event": "PASS"}, [], "")
        self.assertEqual((status, error), ("FAIL", "prompt_ausente"))
        status, error, _ = net1.validate_run(
            "external", [], trace, None, [], "", "qemu ausente", True)
        self.assertEqual((status, error), ("BLOCKED", "qemu ausente"))

    def test_prompt_validation_uses_final_state_not_historical_blocks(self):
        values = {
            "shell_lifecycle_prompt_rendered": 4,
            "shell_lifecycle_prompt_missing": 0,
            "shell_lifecycle_prompt_duplicates": 0,
            "shell_lifecycle_prompt_blocked": 4,
            "shell_lifecycle_prompt_state": 0,
            "shell_lifecycle_input_blocked": 0,
        }
        self.assertTrue(net1._prompt_observed("", values))

    def test_prompt_validation_accepts_nd_counters_with_restored_state(self):
        values = {
            "shell_lifecycle_prompt_rendered": None,
            "shell_lifecycle_prompt_state": 0,
            "shell_lifecycle_input_blocked": 0,
            "shell_lifecycle_terminal_active": 1,
            "shell_lifecycle_focus_shell": 1,
            "shell_lifecycle_scene_active": 0,
            "shell_lifecycle_job_active": 0,
            "shell_lifecycle_loader_active": 0,
        }
        self.assertTrue(net1._prompt_observed("", values))

    def test_report_nd_host_and_restricted_qemu_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            report = net1.build_report(
                Path(directory) / "missing.img", [
                    {"profile": "external", "mode": "simple",
                     "iteration": 1, "status": "PASS"},
                ])
        self.assertEqual(report["schema"], net1.NET1_SCHEMA)
        self.assertEqual(report["status"], "PASS")
        self.assertFalse(report["credentials_stored"])
        runner.validate_qemu_profile("network-restricted")
        args = runner.qemu_profile_args("network-restricted")
        self.assertIn("restrict=on", " ".join(args))

    def test_runner_arguments_use_default_qemu_transport(self):
        options = type("Options", (), {
            "image": Path("build/zephyros.img"),
            "catalog": Path("tests/catalog.json"),
            "results": Path("build/test-results/net1-external-connectivity"),
            "qemu": "qemu-system-i386",
            "qemu_arg": [],
            "cpu": "max",
            "snapshot": True,
            "boot_timeout": 60,
            "case_timeout": 240,
            "suite_timeout": 1800,
            "heartbeat_timeout": 30,
            "network": "user,model=e1000",
        })()
        arguments = net1._arguments(options, "external", "external", "simple")
        self.assertEqual(arguments.input_transport,
                         runner.QEMU_INPUT_TRANSPORT_DEFAULT)
        self.assertEqual(arguments.qemu_arg[-2:], ["-k", "pt-br"])


if __name__ == "__main__":
    unittest.main()
