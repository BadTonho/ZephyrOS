import json
import struct
import tempfile
import unittest
from pathlib import Path

from tools import updater


class UpdaterVersionTests(unittest.TestCase):
    def test_version_parsing_order_and_limits(self):
        first = updater.Version.parse("1.2.3")
        second = updater.Version.parse("1.2.4")
        self.assertEqual(str(first), "1.2.3")
        self.assertLess(first, second)
        self.assertTrue(updater.runtime_version_is_newer(
            second, 4, first, 4))
        self.assertTrue(updater.runtime_version_is_newer(
            first, 5, second, 4))
        self.assertFalse(updater.runtime_version_is_newer(
            second, 4, first, 5))
        for value in ("1.2", "1.2.3.4", "-1.0.0", "1.a.0",
                      "65536.0.0"):
            with self.subTest(value=value):
                with self.assertRaises(updater.UpdateError):
                    updater.Version.parse(value)

    def test_fixed_fields_and_paths_have_limits(self):
        self.assertEqual(updater.encode_path("SHELL.BMP")[:9], b"SHELL.BMP")
        for path in ("shell.bmp", "../X.BMP", "NOEXT", "A.BMP/../X"):
            with self.subTest(path=path):
                with self.assertRaises(updater.UpdateError):
                    updater.encode_path(path)
        self.assertEqual(
            updater.runtime_fixed_bytes("stable", 16, "channel")[:6], b"stable")
        with self.assertRaises(updater.UpdateError):
            updater.runtime_fixed_bytes("x" * 16, 16, "channel")
        with self.assertRaises(updater.UpdateError):
            updater.runtime_fixed_text(b"x\0\x01", "channel")


class UpdaterManifestTests(unittest.TestCase):
    def manifest(self, source="SHELL.BMP"):
        return {
            "format": "ZUPD v1",
            "architecture": "i386",
            "base_version": "1.0.0",
            "target_version": "1.1.0",
            "base_epoch": 10,
            "target_epoch": 11,
            "files": [{"path": "SHELL.BMP", "source": source}],
        }

    def write_manifest(self, directory: Path, data: dict) -> Path:
        path = directory / "update.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def test_valid_manifest_loads_payload_and_sorts_entries(self):
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-updater-") as root:
            directory = Path(root)
            (directory / "SHELL.BMP").write_bytes(b"shell fixture")
            manifest_path = self.write_manifest(directory, self.manifest())
            data = updater.manifest_data(manifest_path)
            base, target, base_epoch, target_epoch, files = \
                updater.validate_manifest(data, directory)
            self.assertEqual((base, target, base_epoch, target_epoch),
                             (updater.Version(1, 0, 0),
                              updater.Version(1, 1, 0), 10, 11))
            self.assertEqual(files, [("SHELL.BMP", b"shell fixture")])

    def test_manifest_rejects_order_versions_epochs_duplicates_and_unsafe_source(self):
        cases = []
        missing = self.manifest()
        del missing["files"]
        cases.append(missing)
        extra = self.manifest()
        extra["extra"] = True
        cases.append(extra)
        not_newer = self.manifest()
        not_newer["target_version"] = "1.0.0"
        cases.append(not_newer)
        bad_epoch = self.manifest()
        bad_epoch["target_epoch"] = -1
        cases.append(bad_epoch)
        duplicate = self.manifest()
        duplicate["files"] = [
            {"path": "SHELL.BMP", "source": "SHELL.BMP"},
            {"path": "SHELL.BMP", "source": "SHELL.BMP"},
        ]
        cases.append(duplicate)
        for data in cases:
            with self.subTest(data=data):
                with tempfile.TemporaryDirectory(prefix="zephyros-tst3-invalid-") as root:
                    directory = Path(root)
                    (directory / "SHELL.BMP").write_bytes(b"payload")
                    path = self.write_manifest(directory, data)
                    with self.assertRaises(updater.UpdateError):
                        updater.manifest_data(path) if "files" not in data else \
                            updater.validate_manifest(updater.manifest_data(path), directory)

        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-traversal-") as root:
            directory = Path(root)
            outside = directory.parent / "outside-tst3.bin"
            outside.write_bytes(b"must not be read")
            try:
                path = self.write_manifest(directory,
                                           self.manifest("../outside-tst3.bin"))
                with self.assertRaises(updater.UpdateError):
                    updater.validate_manifest(updater.manifest_data(path), directory)
            finally:
                outside.unlink()

    def test_manifest_rejects_missing_payload_and_invalid_json(self):
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-missing-") as root:
            directory = Path(root)
            path = self.write_manifest(directory, self.manifest())
            with self.assertRaises(updater.UpdateError):
                updater.validate_manifest(updater.manifest_data(path), directory)
            invalid = directory / "invalid.json"
            invalid.write_text("{", encoding="utf-8")
            with self.assertRaises(updater.UpdateError):
                updater.manifest_data(invalid)


class UpdaterArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ed25519, _, _ = updater.crypto_modules()
        cls.private_key = ed25519.Ed25519PrivateKey.generate()
        cls.trusted = updater.private_public_info(cls.private_key)
        cls.base = updater.Version(1, 0, 0)
        cls.target = updater.Version(1, 1, 0)

    def artifact(self):
        return updater.build_from_parts(
            self.private_key, self.base, self.target, 10, 11,
            [("SHELL.BMP", b"shell"), ("TASKMGR.BMP", b"task")])

    def test_signed_artifact_round_trip(self):
        artifact = self.artifact()
        header, entries = updater.parse_structure(artifact)
        self.assertEqual(len(entries), 2)
        self.assertEqual([entry.path for entry in entries],
                         ["SHELL.BMP", "TASKMGR.BMP"])
        info = updater.verify_artifact(artifact, self.trusted, self.base, 10)
        self.assertEqual(info.target_version, self.target)
        self.assertEqual(info.target_epoch, 11)

    def test_artifact_corruption_has_specific_reasons(self):
        artifact = self.artifact()
        header, _ = updater.parse_structure(artifact)

        truncated = artifact[:-1]
        with self.assertRaises(updater.Rejection) as context:
            updater.parse_structure(truncated)
        self.assertEqual(context.exception.reason, updater.REASON_SIZE)

        invalid_magic = bytearray(artifact)
        invalid_magic[:4] = b"BAD!"
        with self.assertRaises(updater.Rejection) as context:
            updater.parse_structure(bytes(invalid_magic))
        self.assertEqual(context.exception.reason, updater.REASON_FORMAT)

        bad_payload = bytearray(artifact)
        bad_payload[header["payload_offset"]] ^= 0x01
        with self.assertRaises(updater.Rejection) as context:
            updater.verify_artifact(bytes(bad_payload), self.trusted,
                                    self.base, 10)
        self.assertEqual(context.exception.reason, updater.REASON_HASH)

        bad_signature = bytearray(artifact)
        bad_signature[-1] ^= 0x01
        with self.assertRaises(updater.Rejection) as context:
            updater.verify_artifact(bytes(bad_signature), self.trusted,
                                    self.base, 10)
        self.assertEqual(context.exception.reason, updater.REASON_SIGNATURE)

        with self.assertRaises(updater.Rejection) as context:
            updater.verify_artifact(artifact, updater.public_key_info(bytes(32)),
                                    self.base, 10)
        self.assertEqual(context.exception.reason, updater.REASON_UNKNOWN_KEY)

    def test_artifact_compatibility_and_input_limits(self):
        artifact = self.artifact()
        with self.assertRaises(updater.Rejection) as context:
            updater.verify_artifact(artifact, self.trusted,
                                    updater.Version(1, 0, 1), 10)
        self.assertEqual(context.exception.reason, updater.REASON_BASE_VERSION)
        with self.assertRaises(updater.UpdateError):
            updater.parse_structure(updater.build_from_parts(
                self.private_key, self.base, self.target, 10, 11,
                [("SHELL.BMP", b"a"), ("SHELL.BMP", b"b")]))
        with self.assertRaises(updater.UpdateError):
            updater.build_from_parts(
                self.private_key, self.base, self.target, 10, 11,
                [("../BAD.BMP", b"a")])


class UpdaterTrustTests(unittest.TestCase):
    def test_public_key_json_is_canonical_and_rejects_mismatch(self):
        info = updater.public_key_info(bytes(range(32)))
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-trust-") as root:
            path = Path(root) / "public.json"
            path.write_text(updater.public_json(info), encoding="utf-8")
            self.assertEqual(updater.load_public_json(path), info)
            data = json.loads(path.read_text(encoding="utf-8"))
            data["key_id_hex"] = "00" * 16
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(updater.UpdateError):
                updater.load_public_json(path)


class UpdaterRollbackTests(unittest.TestCase):
    def test_redundant_state_selects_newest_and_falls_back_after_corruption(self):
        baseline = updater.encode_test_state(1, False)
        applied = updater.encode_test_state(2, True)
        baseline_state = updater.decode_state_record(baseline)
        applied_state = updater.decode_state_record(applied)
        self.assertFalse(baseline_state.rollback_available)
        self.assertTrue(applied_state.rollback_available)
        self.assertEqual(applied_state.previous_version,
                         updater.Version(0, 1, 0))
        selected = updater.select_redundant_record(
            (baseline, applied), updater.decode_state_record, "test state")
        self.assertEqual(selected.sequence, 2)
        corrupted = bytearray(applied)
        corrupted[100] ^= 0x01
        selected = updater.select_redundant_record(
            (baseline, bytes(corrupted)), updater.decode_state_record,
            "corrupted test state")
        self.assertEqual(selected.sequence, 1)
        with self.assertRaises(updater.UpdateError):
            updater.select_redundant_record(
                (bytes(corrupted), bytes(corrupted)),
                updater.decode_state_record, "lost test state")

    def test_journal_and_space_model_keep_terminal_decisions_explicit(self):
        journal = updater.decode_journal_record(
            updater.encode_test_clean_journal(7))
        self.assertEqual(journal.kind, updater.UPDATE_JOURNAL_NONE)
        required = updater.modeled_apply_clusters(
            (3126, 3126, 3126), (3126, 3126, 3126), 512)
        self.assertFalse(updater.modeled_apply_has_space(
            required - 1, (3126, 3126, 3126), (3126, 3126, 3126), 512))
        self.assertTrue(updater.modeled_apply_has_space(
            required, (3126, 3126, 3126), (3126, 3126, 3126), 512))


if __name__ == "__main__":
    unittest.main()
