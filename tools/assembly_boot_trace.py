#!/usr/bin/env python3
"""Executa fixtures QEMU controladas para cobrir caminhos Assembly de boot."""

from __future__ import annotations

import argparse
import json
import sys
import subprocess
import tempfile
import struct
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import assembly_trace_collector as collector


SCHEMA = "zephyros-assembly-boot-trace-v1"
COMMON_STAGE2_DEFINES = [
    "KERNEL_SECTORS=1",
    "KERNEL_BYTES=512",
    "RECOVERY_LOADER_SECTORS=1",
    "LEGACY_KERNEL_LBA=64",
    "RECOVERY_LOADER_LBA=6144",
    "FAT32_START_LBA=8192",
]
SCENARIOS: tuple[dict[str, Any], ...] = (
    {
        "id": "boot-disk-error",
        "boot_defines": ["STAGE2_SECTORS=2", "BOOT_TEST_DISK_ERROR"],
        "stage2_defines": ["BOOT_TEST_DISK_ERROR"],
        "disk": "floppy",
        "source": "src/boot/boot.asm",
        "symbols": ("disk_error", "print16"),
        "boot_only": True,
    },
    {
        "id": "stage2-memory-error",
        "boot_defines": [],
        "stage2_defines": ["STAGE2_TEST_MEMORY_ERROR"],
        "disk": "floppy",
        "source": "src/boot/stage2.asm",
        "symbols": ("memory_error", "fatal_error", "print16"),
    },
    {
        "id": "stage2-a20-error",
        "boot_defines": [],
        "stage2_defines": ["STAGE2_TEST_A20_ERROR"],
        "disk": "floppy",
        "source": "src/boot/stage2.asm",
        "symbols": (
            "a20_enable_kbc", "a20_error", "a20_wait_input",
            "a20_wait_output", "fatal_error", "print16",
        ),
    },
    {
        "id": "stage2-chs-error",
        "boot_defines": [],
        "stage2_defines": ["STAGE2_TEST_FORCE_CHS", "STAGE2_TEST_CHS_ERROR"],
        "disk": "floppy",
        "source": "src/boot/stage2.asm",
        "symbols": (
            "chs_disk_error", "detect_geometry", "fatal_error", "print16",
            "read_kernel_chs", "reset_boot_disk",
        ),
    },
    {
        "id": "stage2-lba-error",
        "boot_defines": [],
        "stage2_defines": ["STAGE2_TEST_LBA_ERROR"],
        "disk": "ide",
        "source": "src/boot/stage2.asm",
        "symbols": (
            "lba_disk_error", "fatal_error", "print16", "read_kernel_lba",
            "reset_boot_disk",
        ),
    },
    {
        "id": "stage2-overflow-error",
        "boot_defines": [],
        "stage2_defines": ["STAGE2_TEST_LOAD_OVERFLOW"],
        "disk": "floppy",
        "source": "src/boot/stage2.asm",
        "symbols": ("fatal_error", "load_overflow", "print16"),
    },
    {
        "id": "system-boot-invalid",
        "boot_defines": ["SYSTEM_BOOT_CORRUPT_HANDOFF"],
        "stage2_defines": [],
        "disk": "floppy",
        "source": "src/boot/system_boot.asm",
        "symbols": ("fail", "start"),
        "boot_source": "tests/fixtures/assembly/protected_mode_boot.asm",
        "system_boot_source": "src/boot/system_boot.asm",
        "system_boot_defines": ["SYSTEM_BOOT_CORRUPT_HANDOFF"],
        "source_base": 0x7C00,
        "boot_only": True,
    },
    {
        "id": "system-stage2-valid",
        "boot_defines": ["FIXTURE_VALID_HANDOFF"],
        "stage2_defines": [],
        "disk": "floppy",
        "source": "src/boot/system_stage2.asm",
        "symbols": ("fail", "start"),
        "boot_source": "tests/fixtures/assembly/protected_mode_boot.asm",
        "system_boot_source": "src/boot/system_boot.asm",
        "source_base": 0x5000,
        "system_stage2": True,
    },
)


class FixtureError(Exception):
    """Falha controlada na montagem ou execução de uma fixture."""


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def assemble(nasm: str, source: Path, output: Path, listing: Path,
             defines: list[str]) -> None:
    collector.assemble_listing(nasm, source, listing, output, defines)


def build_image(boot: bytes, stage2: bytes | None, path: Path) -> None:
    data = bytearray(boot)
    if stage2 is not None:
        data.extend(stage2)
        minimum_size = 64 * 512
        if len(data) < minimum_size:
            data.extend(b"\0" * (minimum_size - len(data)))
    path.write_bytes(data)


def run_qemu(qemu: str, image: Path, trace: Path, disk: str,
             timeout: float, stdout_path: Path, stderr_path: Path,
             loaders: list[tuple[Path, int]] | None = None) -> bool:
    if disk == "floppy":
        storage = [
            "-drive", f"file={image},format=raw,if=floppy,index=0",
            "-boot", "order=a",
        ]
    else:
        storage = [
            "-drive", f"file={image},format=raw,if=none,id=bootdisk",
            "-device", "ide-hd,drive=bootdisk,bus=ide.0,unit=0,bootindex=1",
        ]
    command = [
        qemu, "-cpu", "max", "-snapshot", "-no-reboot", "-no-shutdown",
        "-display", "none", "-serial", "none", "-monitor", "none",
        "-d", "in_asm", "-D", str(trace), *storage,
    ]
    for path, address in loaders or []:
        command.extend([
            "-device",
            f"loader,file={path},addr=0x{address:X},force-raw=on",
        ])
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        try:
            process = subprocess.Popen(command, cwd=ROOT, stdout=stdout,
                                       stderr=stderr)
        except OSError as error:
            raise FixtureError(f"qemu_inicio:{error}") from error
        try:
            process.wait(timeout=timeout)
            return False
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            return True


def symbols_for_scenario(arguments: argparse.Namespace, scenario: dict[str, Any],
                         stage2_sectors: int) -> dict[str, Any]:
    symbol_arguments = argparse.Namespace(
        build_dir=arguments.build_dir,
        catalog=arguments.catalog,
        nasm=arguments.nasm,
        nm=arguments.nm,
        compiler=arguments.compiler,
        define=list(scenario["stage2_defines"]),
        boot_stage2_sectors=stage2_sectors,
    )
    if scenario["source"].endswith("system_boot.asm"):
        symbol_arguments.define = list(scenario.get("system_boot_defines", []))
    value = collector.build_symbols(symbol_arguments)
    allowed = set(scenario["symbols"])
    source_base = scenario.get("source_base")
    if isinstance(source_base, int):
        default_base = 0x7C00 if scenario["source"].endswith("system_boot.asm") else 0x5000
        delta = source_base - default_base
        for item in value["symbols"]:
            if item.get("source") == scenario["source"]:
                item["address"] += delta
    value["symbols"] = [
        item for item in value["symbols"]
        if item.get("source") == scenario["source"] and
        item.get("symbol") in allowed
    ]
    if {item.get("symbol") for item in value["symbols"]} != allowed:
        raise FixtureError(f"simbolos_ausentes:{scenario['id']}")
    return value


def run_scenario(arguments: argparse.Namespace, scenario: dict[str, Any],
                 output_dir: Path) -> dict[str, Any]:
    scenario_dir = output_dir / scenario["id"]
    scenario_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zephyros-boot-fixture-") as name:
        temporary = Path(name)
        boot_path = temporary / "boot.bin"
        boot_listing = temporary / "boot.lst"
        boot_defines = list(scenario["boot_defines"])
        boot_source = ROOT / scenario.get("boot_source", "src/boot/boot.asm")
        loaders: list[tuple[Path, int]] = []
        stage2: bytes | None = None
        stage2_sectors = 1
        fixture_payload: bytes | None = None
        if scenario.get("system_stage2"):
            system_boot_path = temporary / "system_boot.bin"
            system_boot_listing = temporary / "system_boot.lst"
            assemble(arguments.nasm, ROOT / scenario["system_boot_source"],
                     system_boot_path, system_boot_listing,
                     list(scenario.get("system_boot_defines", [])))
            stage2_path = temporary / "system_stage2.bin"
            stage2_listing = temporary / "system_stage2.lst"
            assemble(arguments.nasm, ROOT / "src/boot/system_stage2.asm",
                     stage2_path, stage2_listing,
                     list(scenario["stage2_defines"]))
            handoff_path = temporary / "handoff.bin"
            write_system_handoff(handoff_path)
            kernel_path = temporary / "return.bin"
            kernel_path.write_bytes(b"\xC3")
            fixture_payload = (
                system_boot_path.read_bytes() + stage2_path.read_bytes() +
                handoff_path.read_bytes().ljust(512, b"\0") +
                kernel_path.read_bytes().ljust(512, b"\0")
            )
            stage2 = None
            stage2_sectors = 1
        elif not scenario.get("boot_only"):
            stage2_path = temporary / "stage2.bin"
            stage2_listing = temporary / "stage2.lst"
            stage2_defines = COMMON_STAGE2_DEFINES + list(
                scenario["stage2_defines"])
            assemble(arguments.nasm, ROOT / "src/boot/stage2.asm", stage2_path,
                     stage2_listing, stage2_defines)
            stage2 = stage2_path.read_bytes()
            stage2_sectors = (len(stage2) + 511) // 512
            boot_defines = [f"STAGE2_SECTORS={stage2_sectors}"]
        elif scenario.get("system_boot_source"):
            system_boot_path = temporary / "system_boot.bin"
            system_boot_listing = temporary / "system_boot.lst"
            assemble(arguments.nasm, ROOT / scenario["system_boot_source"],
                     system_boot_path, system_boot_listing,
                     list(scenario.get("system_boot_defines", [])))
            zero_handoff = temporary / "handoff.bin"
            zero_handoff.write_bytes(bytes(64))
            fixture_payload = (
                system_boot_path.read_bytes() + bytes(512) +
                zero_handoff.read_bytes().ljust(512, b"\0") + bytes(512)
            )
        else:
            stage2 = None
            stage2_sectors = 2
        assemble(arguments.nasm, boot_source, boot_path,
                 boot_listing, boot_defines)
        boot = boot_path.read_bytes()
        if len(boot) != 512 or boot[510:512] != b"\x55\xaa":
            raise FixtureError(f"boot_sector_invalido:{scenario['id']}")
        image = scenario_dir / "fixture.img"
        build_image(boot, fixture_payload if fixture_payload is not None else stage2,
                    image)
        trace = scenario_dir / "qemu-in_asm.log"
        stdout_path = scenario_dir / "qemu.stdout.log"
        stderr_path = scenario_dir / "qemu.stderr.log"
        timed_out = run_qemu(arguments.qemu, image, trace, scenario["disk"],
                             arguments.timeout, stdout_path, stderr_path,
                             loaders)
        symbols = symbols_for_scenario(arguments, scenario, stage2_sectors)
        symbol_path = scenario_dir / "symbols.json"
        write_json(symbol_path, symbols)
        collect_arguments = argparse.Namespace(
            trace=str(trace), symbols=str(symbol_path),
            case_id=f"qemu:tst7:assembly:{scenario['id']}",
            source=[scenario["source"]],
        )
        report = collector.collect_trace(collect_arguments)
        case = report["cases"][0]
        case["qemu_timed_out"] = timed_out
        case["expected_symbols"] = list(scenario["symbols"])
        case["covered_surface_ids"] = report["covered_surface_ids"]
        case["status"] = "PASS" if report["complete"] else "FAIL"
        return case


def write_system_handoff(path: Path) -> None:
    """Cria um handoff ABI-2 minimo para o fixture system_stage2."""
    words = [
        0x4342535A, 0x00400001, 2, 0x7C00, 512, 0x5000, 0x0F00,
        0x100000, 1, 0x3000, 0x2000, 0x2800,
    ]
    words.append((~sum(words)) & 0xFFFFFFFF)
    path.write_bytes(b"".join(struct.pack("<I", value) for value in words))


def run(arguments: argparse.Namespace) -> int:
    output_dir = Path(arguments.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    cases = []
    errors = []
    selected = [scenario for scenario in SCENARIOS
                if not arguments.scenario or scenario["id"] in arguments.scenario]
    for scenario in selected:
        try:
            cases.append(run_scenario(arguments, scenario, output_dir))
        except (FixtureError, collector.AssemblyTraceError, OSError) as error:
            errors.append(f"{scenario['id']}:{error}")
    covered = sorted({surface_id for case in cases
                      for surface_id in case.get("covered_surface_ids", [])})
    missing = [item for case in cases for item in case.get("missing_symbols", [])]
    report = {
        "schema": SCHEMA,
        "status": "PASS" if not errors and not missing and
        all(case.get("status") == "PASS" for case in cases) else "FAIL",
        "fixtures": [scenario["id"] for scenario in selected],
        "covered_surface_ids": covered,
        "missing_symbols": missing,
        "errors": errors,
        "cases": cases,
    }
    write_json(output_dir / "assembly-boot-trace.json", report)
    print(f"Assembly boot trace: {report['status']}")
    print(f"Relatorio: {output_dir / 'assembly-boot-trace.json'}")
    return 0 if report["status"] == "PASS" else 1


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    command.add_argument("--build-dir", required=True)
    command.add_argument("--catalog", required=True)
    command.add_argument("--output-dir", required=True)
    command.add_argument("--nasm", default="nasm")
    command.add_argument("--nm", default="nm")
    command.add_argument("--compiler")
    command.add_argument("--qemu", default="qemu-system-i386")
    command.add_argument("--timeout", type=float, default=15.0)
    command.add_argument("--scenario", action="append", default=[])
    return command


if __name__ == "__main__":
    raise SystemExit(run(parser().parse_args()))
