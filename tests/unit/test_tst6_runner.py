import unittest
from argparse import Namespace

from tools import qemu_test_runner as runner


class Tst6ProfileTests(unittest.TestCase):
    def base_case(self):
        return {
            "id": "qemu:tst6:matrix:test",
            "executor": "qemu",
            "profile": "tst6",
            "guest_case": "qemu:tst6:matrix:test",
            "qemu_profile": "baseline",
            "required_capabilities": ["pci"],
            "isolation": "snapshot",
            "timeout_seconds": 30,
            "heartbeat_timeout_seconds": 5,
            "interaction": None,
        }

    def test_profiles_are_allowlisted(self):
        for name in runner.QEMU_PROFILE_NAMES:
            runner.validate_qemu_profile(name)
        with self.assertRaises(runner.RunnerError) as context:
            runner.validate_qemu_profile("-device;unsafe")
        self.assertTrue(context.exception.blocked)

    def test_case_rejects_unknown_profile(self):
        case = self.base_case()
        case["qemu_profile"] = "arbitrary-qemu-args"
        with self.assertRaises(runner.RunnerError):
            runner.validate_case_for_runner(case)

    def test_case_requires_capability_from_profile(self):
        case = self.base_case()
        case["required_capabilities"] = ["usb-storage-readonly"]
        with self.assertRaises(runner.RunnerError):
            runner.validate_case_for_runner(case)

    def test_profile_capabilities_are_copied(self):
        capabilities = runner.qemu_profile_capabilities("usb-storage")
        capabilities.append("mutated")
        self.assertNotIn("mutated", runner.QEMU_PROFILE_CAPABILITIES[
            "usb-storage"])


class Tst6LimitTests(unittest.TestCase):
    def arguments(self, **overrides):
        values = {
            "command": "stress",
            "boot_timeout": 30,
            "case_timeout": 30,
            "suite_timeout": 300,
            "heartbeat_timeout": 5,
            "seed": None,
            "iterations": 10,
            "max_iterations": None,
            "duration": None,
            "until_failure": False,
        }
        values.update(overrides)
        return Namespace(**values)

    def test_fixed_iterations_are_valid(self):
        runner.validate_arguments(self.arguments())

    def test_until_failure_requires_a_teto(self):
        with self.assertRaises(runner.RunnerError) as context:
            runner.validate_arguments(self.arguments(
                iterations=None, until_failure=True))
        self.assertEqual(context.exception.cause,
                         "stress_requer_teto_until_failure")

    def test_until_failure_accepts_iteration_teto(self):
        runner.validate_arguments(self.arguments(
            iterations=None, max_iterations=25, until_failure=True))

    def test_limits_reject_values_above_teto(self):
        with self.assertRaises(runner.RunnerError):
            runner.validate_arguments(self.arguments(iterations=1001))
        with self.assertRaises(runner.RunnerError):
            runner.validate_arguments(self.arguments(
                iterations=None, duration=601))
        with self.assertRaises(runner.RunnerError):
            runner.validate_arguments(self.arguments(suite_timeout=601))

    def test_max_iterations_without_until_failure_is_invalid(self):
        with self.assertRaises(runner.RunnerError):
            runner.validate_arguments(self.arguments(
                iterations=None, max_iterations=25, duration=10))


class Tst6ResultTests(unittest.TestCase):
    def test_expected_blocked_result_is_distinct(self):
        self.assertEqual(
            runner.result_status([], runner.RunnerError(
                "hardware_ausente", "precondition", True)),
            ("BLOCKED", "precondition", "hardware_ausente"),
        )

    def test_unexpected_fault_is_fail(self):
        self.assertEqual(
            runner.result_status([], runner.RunnerError(
                "estado_inconsistente", "completed")),
            ("FAIL", "completed", "estado_inconsistente"),
        )


if __name__ == "__main__":
    unittest.main()
