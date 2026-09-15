import hashlib
import json
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from pathlib import Path
from unittest.mock import patch

from tools import release_baseline as baseline


class ReleaseBaselineHelpersTests(unittest.TestCase):
    def test_hash_and_artifact_record_are_relative(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "build" / "boot.bin"
            path.parent.mkdir()
            path.write_bytes(b"boot")
            record = baseline.artifact_record(path, root)
        self.assertEqual(record["path"], "build/boot.bin")
        self.assertEqual(record["size_bytes"], 4)
        self.assertEqual(record["sha256"], hashlib.sha256(b"boot").hexdigest())

    def test_versions_are_distinct_and_header_is_consistent(self):
        header = """
        #define ZEPHYROS_VERSION_MAJOR 0U
        #define ZEPHYROS_VERSION_MINOR 1U
        #define ZEPHYROS_VERSION_PATCH 0U
        #define ZEPHYROS_VERSION_TEXT "0.1.0"
        """
        self.assertEqual(baseline.version_from_header(header), "0.1.0")
        self.assertNotEqual("0.1.0", "1.0.0")
        with self.assertRaises(baseline.BaselineError):
            baseline.version_from_header(header.replace('"0.1.0"', '"1.0.1"'))

    def test_dirty_worktree_is_reported(self):
        completed = type("Completed", (), {"returncode": 0, "stdout": " M README.md\n"})

        def run(command, **_kwargs):
            if command[1:3] == ["status", "--porcelain"]:
                return completed()
            return type("Completed", (), {"returncode": 0, "stdout": "abc\n"})()

        with patch("tools.release_baseline.subprocess.run", side_effect=run):
            source = baseline.git_source(Path.cwd())
        self.assertFalse(source["worktree_clean"])
        self.assertEqual(source["worktree_entry_count"], 1)

    def test_missing_required_tool_is_blocked(self):
        with self.assertRaises(baseline.BaselineError) as context:
            baseline.run_tool_version("tool-that-does-not-exist", ["--version"])
        self.assertEqual(context.exception.status, "BLOCKED")

    def test_boot_size_and_signature_are_validated(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "boot.bin"
            path.write_bytes(b"x" * 511)
            with self.assertRaisesRegex(baseline.BaselineError, "boot_size_invalid"):
                baseline.validate_boot_artifact(path)
            path.write_bytes(b"x" * 512)
            with self.assertRaisesRegex(baseline.BaselineError, "boot_signature_invalid"):
                baseline.validate_boot_artifact(path)

    def test_lba_overlap_is_rejected(self):
        artifacts = {
            name: {"size_bytes": 512}
            for name in ("boot.bin", "stage2.bin", "kernel.bin",
                         "recovery_loader_padded.bin")
        }
        with self.assertRaisesRegex(baseline.BaselineError, "layout_overlap"):
            baseline.build_layout(artifacts, 268435456, recovery_lba=1)

    def test_fat32_signature_and_geometry(self):
        image_size = 4 * 1024 * 1024
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.bin"
            image = bytearray(image_size)
            start_lba = 1
            sector = start_lba * baseline.SECTOR_SIZE
            image[sector + 510:sector + 512] = b"\x55\xAA"
            struct.pack_into("<H", image, sector + 11, 512)
            image[sector + 13] = 1
            struct.pack_into("<H", image, sector + 14, 32)
            image[sector + 16] = 2
            struct.pack_into("<I", image, sector + 32, image_size // 512 - 1)
            struct.pack_into("<I", image, sector + 36, 64)
            struct.pack_into("<I", image, sector + 44, 2)
            image[sector + 82:sector + 87] = b"FAT32"
            path.write_bytes(image)
            geometry = baseline.validate_fat32(path, start_lba, image_size)
            self.assertEqual(geometry["bytes_per_sector"], 512)
            image[sector + 510:sector + 512] = b"\0\0"
            path.write_bytes(image)
            with self.assertRaisesRegex(baseline.BaselineError, "fat32_boot_signature_invalid"):
                baseline.validate_fat32(path, start_lba, image_size)

    def test_elf_symbols_and_sections_invalid(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "kernel.elf"
            path.write_bytes(b"not an elf")
            with self.assertRaisesRegex(baseline.BaselineError, "elf_header_invalid"):
                baseline.symbols_report(Path(directory), path, "nm", "objdump",
                                        Path(directory) / "output")
        self.assertEqual(baseline.parse_nm_listing("U missing\n"), [])
        self.assertEqual(baseline.parse_objdump_sections("not a section\n"), [])

    def test_parser_extracts_symbols_and_sections(self):
        symbols = baseline.parse_nm_listing("00000010 T start\n00000000 U ext\n")
        self.assertEqual(symbols, [{"address": 16, "type": "T", "symbol": "start"}])
        sections = baseline.parse_objdump_sections(
            "  0 .text         00000010  00100000  00100000  00000074  2**2\n")
        self.assertEqual(sections[0]["name"], ".text")
        self.assertEqual(sections[0]["size"], 16)

    def test_schema_statuses_and_no_personal_paths(self):
        report = baseline.base_report()
        self.assertEqual(report["schema"], "zephyros-rls1-baseline-v1")
        self.assertEqual(report["candidate_state"], "DOCUMENTAL_ONLY")
        self.assertEqual(baseline.status_from_errors([], False), "PASS")
        self.assertEqual(baseline.status_from_errors(["bad"], False), "FAIL")
        self.assertEqual(baseline.status_from_errors([], True), "BLOCKED")
        self.assertFalse(baseline.contains_absolute_path({"path": "build/image.bin"}))
        self.assertTrue(baseline.contains_absolute_path({"path": "C:/private/image.bin"}))
        serialized = json.dumps(report, ensure_ascii=False)
        self.assertNotIn(str(Path.home()), serialized)

    def test_command_helpers_and_report_writer(self):
        self.assertEqual(baseline.normalize_command(' "tool.exe" '), "tool.exe")
        self.assertEqual(baseline.display_command('C:\\tools\\tool.exe'), "tool.exe")
        self.assertTrue(baseline.command_available(sys.executable))
        with patch("tools.release_baseline.command_available", return_value=True), \
                patch("tools.release_baseline.subprocess.run") as run:
            run.return_value = SimpleNamespace(returncode=0, stdout="tool 1\n",
                                               stderr="")
            self.assertEqual(baseline.run_tool_version("tool", ["--version"]),
                             "tool 1")
            self.assertEqual(baseline.run_listing("tool", ["-h"], "objdump"),
                             "tool 1\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            baseline.write_report(path, baseline.base_report())
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["version"], 1)

    def test_tool_versions_and_updater_are_adapted_without_paths(self):
        arguments = SimpleNamespace(
            python="python", make="make", nasm="nasm", gcc="gcc", ld="ld",
            nm="nm", objdump="objdump")
        with patch("tools.release_baseline.run_tool_version", return_value="ok") as run:
            versions = baseline.tool_versions(arguments)
        self.assertEqual(set(versions), {"python", "make", "nasm", "gcc", "ld", "nm", "objdump"})
        self.assertEqual(run.call_count, 7)
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "image.bin"
            image.write_bytes(b"image")
            with patch("tools.release_baseline.subprocess.run") as run:
                run.return_value = SimpleNamespace(returncode=0, stdout="ok\n", stderr="")
                result = baseline.updater_audit(Path(directory), image, "0.1.0")
            self.assertEqual(result["status"], "PASS")

    def test_read_and_image_validation_failures_are_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.bin"
            path.write_bytes(b"abc")
            self.assertEqual(baseline.read_at(path, 1, 2), b"bc")
            with self.assertRaisesRegex(baseline.BaselineError, "image_size_invalid"):
                baseline.validate_image(path, {}, 512, {"overlap": False})

    def test_image_accepts_composed_legacy_bpb(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            boot_path = root / "boot.bin"
            stage2_path = root / "stage2.bin"
            kernel_path = root / "kernel.bin"
            loader_path = root / "recovery_loader_padded.bin"
            image_path = root / "image.bin"
            boot = bytearray(512)
            boot[510:512] = b"\x55\xAA"
            boot_path.write_bytes(boot)
            stage2_path.write_bytes(b"s2")
            kernel_path.write_bytes(b"k")
            loader_path.write_bytes(b"r")
            with image_path.open("wb") as stream:
                stream.seek(baseline.EXPECTED_IMAGE_BYTES - 1)
                stream.write(b"\0")
            artifact_paths = {
                "boot.bin": boot_path,
                "stage2.bin": stage2_path,
                "kernel.bin": kernel_path,
                "recovery_loader_padded.bin": loader_path,
            }
            records = {
                name: {"size_bytes": path.stat().st_size}
                for name, path in artifact_paths.items()
            }
            layout = baseline.build_layout(
                records, baseline.EXPECTED_IMAGE_BYTES)
            expected_boot = bytearray(boot)
            struct.pack_into(
                "<H", expected_boot, 14,
                layout["windows"][1]["end_lba"] + 1)
            partition = bytearray(16)
            partition[4] = 0x0C
            struct.pack_into("<I", partition, 8, baseline.EXPECTED_FAT32_LBA)
            struct.pack_into(
                "<I", partition, 12,
                baseline.EXPECTED_IMAGE_BYTES // baseline.SECTOR_SIZE -
                baseline.EXPECTED_FAT32_LBA)

            def read_image(_path, offset, size):
                if offset == 0 and size == 446:
                    return bytes(expected_boot[:446])
                if offset == 446:
                    return bytes(partition)
                if offset == 512:
                    return stage2_path.read_bytes()
                if offset == baseline.EXPECTED_KERNEL_LBA * baseline.SECTOR_SIZE:
                    return kernel_path.read_bytes()
                if offset == baseline.EXPECTED_RECOVERY_LBA * baseline.SECTOR_SIZE:
                    return loader_path.read_bytes()
                if offset in (510,):
                    return b"\x55\xAA"
                raise AssertionError((offset, size))

            with patch("tools.release_baseline.read_at", side_effect=read_image), \
                    patch("tools.release_baseline.validate_fat32",
                          return_value={"start_lba": baseline.EXPECTED_FAT32_LBA}):
                result = baseline.validate_image(
                    image_path, artifact_paths,
                    baseline.EXPECTED_IMAGE_BYTES, layout)
            self.assertTrue(result["layout_windows_valid"])
            self.assertTrue(result["mbr_signature"])

    def test_audit_and_main_expose_complete_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src/include/core").mkdir(parents=True)
            (root / "build").mkdir()
            (root / "src/include/core/version.h").write_text(
                '#define ZEPHYROS_VERSION_MAJOR 0U\n'
                '#define ZEPHYROS_VERSION_MINOR 1U\n'
                '#define ZEPHYROS_VERSION_PATCH 0U\n'
                '#define ZEPHYROS_VERSION_TEXT "0.1.0"\n', encoding="utf-8")
            names = ("boot.bin", "stage2.bin", "kernel.bin",
                     "recovery_loader.bin", "recovery_loader_padded.bin",
                     "kernel.elf", "zephyros.img")
            for name in names:
                (root / "build" / name).write_bytes(b"x")
            (root / "build" / "recovery_loader.bin").write_bytes(b"x")
            (root / "build" / "recovery_loader_padded.bin").write_bytes(b"x")
            args = baseline.parser().parse_args([
                "collect", "--repo-root", str(root), "--output", "build/out.json"])
            records = {name: {"path": "build/" + name, "size_bytes": 512,
                              "sha256": "a"} for name in names}
            records["zephyros.img"]["size_bytes"] = baseline.EXPECTED_IMAGE_BYTES
            with patch("tools.release_baseline.git_source", return_value={
                    "commit": "abc", "branch": "main", "worktree_clean": True,
                    "worktree_entry_count": 0}), \
                    patch("tools.release_baseline.tool_versions", return_value={}), \
                    patch("tools.release_baseline.artifact_record",
                          side_effect=lambda _path, _root: records[_path.name]), \
                    patch("tools.release_baseline.validate_boot_artifact"), \
                    patch("tools.release_baseline.validate_image", return_value={}), \
                    patch("tools.release_baseline.symbols_report", return_value={}), \
                    patch("tools.release_baseline.updater_audit", return_value={"status": "PASS"}):
                report = baseline.audit(args)
            self.assertEqual(report["status"], "PASS")
            self.assertEqual(report["source"]["commit"], "abc")
            output = root / "build" / "main-report.json"
            with patch("tools.release_baseline.audit",
                       return_value={**baseline.base_report(), "status": "PASS"}):
                self.assertEqual(baseline.main([
                    "collect", "--repo-root", str(root), "--output", str(output)]), 0)
            self.assertTrue(output.is_file())


if __name__ == "__main__":
    unittest.main()
