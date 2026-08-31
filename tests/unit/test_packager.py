import json
import struct
import tempfile
import unittest
from pathlib import Path

from tools import packager


class PackagerManifestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "id": "DEMO",
            "name": "Demo",
            "version": "1.2.3",
            "api": "0.9",
            "entry": "APP.ZAP",
            "dependencies": "CORE,UI",
        }

    def test_json_manifest_and_dependencies(self):
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-manifest-") as root:
            path = Path(root) / "app.json"
            path.write_text(json.dumps({
                "id": "DEMO",
                "name": "Demo",
                "version": "1.2.3",
                "api": "0.9",
                "dependencies": ["CORE", "UI"],
            }), encoding="utf-8")
            self.assertEqual(packager.manifest_from_json(path), self.manifest)

    def test_manifest_rejects_invalid_identity_version_api_and_dependencies(self):
        cases = [
            ("id", "bad", "id"),
            ("name", "cafe\N{LATIN SMALL LETTER E WITH ACUTE}", "name"),
            ("version", "1.2", "version"),
            ("api", "0.2", "api"),
            ("dependencies", ["DEMO"], "dependency"),
            ("dependencies", ["CORE", "CORE"], "dependency"),
        ]
        for field, value, label in cases:
            with self.subTest(field=field, value=value):
                data = {
                    "id": "DEMO",
                    "name": "Demo",
                    "version": "1.2.3",
                    "api": "0.9",
                    "dependencies": ["CORE"],
                }
                data[field] = value
                with tempfile.TemporaryDirectory(prefix="zephyros-tst3-invalid-") as root:
                    path = Path(root) / "app.json"
                    path.write_text(json.dumps(data), encoding="utf-8")
                    with self.assertRaises(packager.PackageError, msg=label):
                        packager.manifest_from_json(path)

    def test_encoded_manifest_requires_exact_order_and_keys(self):
        encoded = packager.encode_manifest(self.manifest)
        self.assertEqual(packager.parse_manifest(encoded), self.manifest)
        invalid = [
            encoded.replace(b"id=DEMO\n", b"id=DEMO\nid=OTHER\n"),
            encoded.replace(b"name=Demo\n", b"name=Demo\nextra=x\n"),
            b"name=Demo\nid=DEMO\nversion=1.2.3\napi=0.9\nentry=APP.ZAP\ndependencies=\n",
            encoded.replace(b"Demo", "D\N{LATIN SMALL LETTER E WITH ACUTE}mo".encode("utf-8")),
        ]
        for candidate in invalid:
            with self.subTest(candidate=candidate[:20]):
                with self.assertRaises(packager.PackageError):
                    packager.parse_manifest(candidate)

    def test_manifest_file_errors_are_controlled(self):
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-file-") as root:
            missing = Path(root) / "missing.json"
            with self.assertRaises(packager.PackageError):
                packager.manifest_from_json(missing)
            malformed = Path(root) / "malformed.json"
            malformed.write_text("{", encoding="utf-8")
            with self.assertRaises(packager.PackageError):
                packager.manifest_from_json(malformed)


class PackagerContainerTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "id": "DEMO",
            "name": "Demo",
            "version": "1.2.3",
            "api": "0.9",
            "entry": "APP.ZAP",
            "dependencies": "",
        }
        self.zapp = packager.build_demo_zapp()
        self.package = packager.build_package(self.manifest, self.zapp)

    def test_zapp_and_package_round_trip(self):
        packager.validate_zapp(self.zapp)
        parsed = packager.parse_package(self.package)
        self.assertEqual(parsed.manifest, self.manifest)
        self.assertEqual(parsed.payload, self.zapp)

    def test_zapp_boundaries_and_layout(self):
        fields = list(packager.ZAPP_HEADER.unpack_from(self.zapp))
        for index, value in ((0, b"BAD!"), (1, 2), (2, 2)):
            broken = bytearray(self.zapp)
            if index == 0:
                broken[:4] = value
            else:
                struct.pack_into("<I", broken, index * 4, value)
            with self.subTest(index=index):
                with self.assertRaises(packager.PackageError):
                    packager.validate_zapp(bytes(broken))
        broken = list(fields)
        broken[8] = broken[5]
        invalid_layout = packager.ZAPP_HEADER.pack(*broken) + self.zapp[packager.ZAPP_HEADER.size:]
        with self.assertRaises(packager.PackageError):
            packager.validate_zapp(invalid_layout)
        with self.assertRaises(packager.PackageError):
            packager.validate_zapp(self.zapp[:packager.ZAPP_HEADER.size - 1])

    def test_package_rejects_header_crc_truncation_and_reserved_bits(self):
        candidates = []
        broken_crc = bytearray(self.package)
        broken_crc[-1] ^= 0xFF
        candidates.append(broken_crc)
        candidates.append(self.package[:-1])
        broken_flags = bytearray(self.package)
        struct.pack_into("<I", broken_flags, 24, 1)
        candidates.append(broken_flags)
        broken_header = bytearray(self.package)
        broken_header[:4] = b"BAD!"
        candidates.append(broken_header)
        for candidate in candidates:
            with self.subTest(size=len(candidate)):
                with self.assertRaises(packager.PackageError):
                    packager.parse_package(bytes(candidate))

    def test_alias_and_package_limits(self):
        self.assertEqual(packager.alias_name("DEMO"), "DEMO.ZPK")
        for value in ("lower", "TOO_LONG9", "BAD-NAME", ""):
            with self.subTest(value=value):
                with self.assertRaises(packager.PackageError):
                    packager.alias_name(value)
        with self.assertRaises(packager.PackageError):
            packager.inject_package(self.package, Path("missing.img"), "OTHER.ZPK")


def make_fat32_fixture(path: Path) -> None:
    start_lba = 4096
    total_sectors = 8192
    reserved = 1
    sectors_per_fat = 64
    image = bytearray((start_lba + total_sectors) * packager.FAT32_SECTOR_SIZE)
    boot_offset = start_lba * packager.FAT32_SECTOR_SIZE
    boot = bytearray(packager.FAT32_SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    struct.pack_into("<H", boot, 11, 512)
    boot[13] = 1
    struct.pack_into("<H", boot, 14, reserved)
    boot[16] = 1
    struct.pack_into("<I", boot, 32, total_sectors)
    struct.pack_into("<I", boot, 36, sectors_per_fat)
    struct.pack_into("<I", boot, 44, 2)
    boot[510:512] = b"\x55\xAA"
    image[boot_offset:boot_offset + 512] = boot
    fat_offset = (start_lba + reserved) * 512
    fat = bytearray(sectors_per_fat * 512)
    packager._fat32_set(fat, 0, 0x0FFFFFF8)
    packager._fat32_set(fat, 1, packager.FAT32_EOF)
    packager._fat32_set(fat, 2, packager.FAT32_EOF)
    image[fat_offset:fat_offset + len(fat)] = fat
    root_offset = (start_lba + reserved + sectors_per_fat) * 512
    image[root_offset] = 0
    path.write_bytes(image)


class PackagerFatTests(unittest.TestCase):
    def test_fat12_injection_replacement_and_limits(self):
        manifest = {
            "id": "DEMO",
            "name": "Demo",
            "version": "1.0.0",
            "api": "0.9",
            "entry": "APP.ZAP",
            "dependencies": "",
        }
        package = packager.build_package(manifest, packager.build_demo_zapp())
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-fat12-") as root:
            image = Path(root) / "disk.img"
            packager.create_fixture_image(image)
            packager.inject_package(package, image, "DEMO.ZPK")
            self.assertEqual(packager.read_root_file(image, "DEMO.ZPK"), package)
            with self.assertRaises(packager.PackageError):
                packager.inject_package(package, image, "DEMO.ZPK")
            packager.inject_package(package, image, "DEMO.ZPK", replace=True)
            with self.assertRaises(packager.PackageError):
                packager.inject_root_file(b"", image, "EMPTY.BIN")
            with self.assertRaises(packager.PackageError):
                packager.inject_package(package, image, "OTHER.ZPK")

    def test_fat32_long_name_alias_and_unsafe_paths(self):
        start_lba = 4096
        with tempfile.TemporaryDirectory(prefix="zephyros-tst3-fat32-") as root:
            image = Path(root) / "disk.img"
            make_fat32_fixture(image)
            data = b"fat32 boundary payload"
            packager.inject_fat32_file(data, image, "Dados de Sistema.txt",
                                       fat32_start_lba=start_lba)
            self.assertEqual(
                packager.read_fat32_file(image, "DADOS DE SISTEMA.TXT",
                                         fat32_start_lba=start_lba), data)
            packager.inject_fat32_file(b"replacement", image,
                                       "Dados de Sistema.txt", replace=True,
                                       fat32_start_lba=start_lba)
            self.assertEqual(
                packager.read_fat32_file(image, "DADOSD~1.TXT",
                                         fat32_start_lba=start_lba), b"replacement")
            with self.assertRaises(packager.PackageError):
                packager.inject_fat32_file(b"x", image, "../unsafe.txt",
                                           fat32_start_lba=start_lba)
            with self.assertRaises(packager.PackageError):
                packager.inject_fat32_file(b"x", image, "file/",
                                           fat32_start_lba=start_lba)


class PackagerFixtureTests(unittest.TestCase):
    def test_store_fixture_catalog_is_deterministic(self):
        first = packager.build_store_fixtures()
        second = packager.build_store_fixtures()
        self.assertEqual(first, second)
        self.assertEqual(set(first), set(packager.STORE_FIXTURE_ALIASES))
        expectations = packager.store_fixture_expectations()
        self.assertEqual(packager.parse_package(first["VALID.ZPK"]).manifest["id"],
                         expectations["VALID.ZPK"]["id"])
        self.assertEqual(packager.parse_package(first["BADALIAS.ZPK"]).manifest["id"],
                         "ALIASOK")
        for alias in ("BADCRC.ZPK", "BADAPI.ZPK"):
            with self.subTest(alias=alias):
                with self.assertRaises(packager.PackageError):
                    packager.parse_package(first[alias])


if __name__ == "__main__":
    unittest.main()
