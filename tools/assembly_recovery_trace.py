#!/usr/bin/env python3
"""Executa o loader de recuperacao em QEMU para cobrir entradas Assembly reais."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import assembly_trace_collector as collector


SCHEMA = "zephyros-assembly-recovery-trace-v1"
RECOVERY_LBA = 6144
RECOVERY_SYMBOLS = (
    "recovery_bios_write_sector",
    "recovery_boot_system_entry",
)
STAGE2_DEFINES = (
    "KERNEL_SECTORS=1",
    "KERNEL_BYTES=512",
    "LEGACY_KERNEL_LBA=64",
    f"RECOVERY_LOADER_LBA={RECOVERY_LBA}",
    "FAT32_START_LBA=8192",
)


class FixtureError(Exception):
    """Falha controlada na montagem ou execucao da fixture."""


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def resolve_tool(requested: str, compiler: str | None,
                 sibling: str) -> str:
    if shutil.which(requested):
        return requested
    path = Path(requested)
    if path.is_file():
        return str(path)
    if compiler:
        candidate = Path(compiler).with_name(sibling)
        if candidate.is_file():
            return str(candidate)
    raise FixtureError(f"ferramenta_ausente:{requested}")


def run_tool(command: list[str]) -> None:
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False, timeout=60.0)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise FixtureError(f"ferramenta_falhou:{error}") from error
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip()
        raise FixtureError(f"ferramenta_falhou:{' '.join(command)}:{message}")


def assemble_object(nasm: str, source: Path, output: Path) -> None:
    run_tool([nasm, "-f", "elf32", str(source), "-o", str(output)])


def write_harness(path: Path, system_boot_return: Path) -> None:
    include_path = system_boot_return.resolve().as_posix()
    path.write_text(
        "[BITS 32]\n"
        "global recovery_loader_main\n"
        "global __recovery_bss_start\n"
        "global __recovery_bss_end\n"
        "extern recovery_bios_write_sector\n"
        "extern recovery_boot_system_entry\n"
        "section .text\n"
        "recovery_loader_main:\n"
        "    push ebp\n"
        "    mov ebp, esp\n"
        "    push dword write_buffer\n"
        "    push dword 1\n"
        "    call recovery_bios_write_sector\n"
        "    add esp, 8\n"
        "    mov esi, system_boot_return\n"
        "    mov edi, 0x00007C00\n"
        "    mov ecx, 128\n"
        "    cld\n"
        "    rep movsd\n"
        "    call recovery_boot_system_entry\n"
        "    pop ebp\n"
        "    ret\n"
        "\n"
        "align 4\n"
        "write_buffer:\n"
        "    times 512 db 0xA5\n"
        "\n"
        "system_boot_return:\n"
        f"    incbin {include_path!r}\n"
        "\n"
        "section .bss\n"
        "align 4\n"
        "__recovery_bss_start:\n"
        "    resb 4\n"
        "__recovery_bss_end:\n",
        encoding="utf-8",
    )


def build_loader(nasm: str, ld: str, objcopy: str,
                 system_boot_return: Path, directory: Path) -> Path:
    recovery_object = directory / "recovery_entry.o"
    harness_source = directory / "recovery_harness.asm"
    harness_object = directory / "recovery_harness.o"
    loader_elf = directory / "recovery_fixture.elf"
    loader_bin = directory / "recovery_fixture.bin"
    write_harness(harness_source, system_boot_return)
    assemble_object(nasm, ROOT / "src/boot/recovery_entry.asm", recovery_object)
    assemble_object(nasm, harness_source, harness_object)
    run_tool([
        ld, "-m", "elf_i386", "-Ttext", "0x900000", "-e", "_start",
        str(recovery_object), str(harness_object), "-o", str(loader_elf),
    ])
    run_tool([objcopy, "-O", "binary", str(loader_elf), str(loader_bin)])
    data = loader_bin.read_bytes()
    if not data:
        raise FixtureError("loader_fixture_nao_alinhado")
    data += bytes((-len(data)) % 512)
    loader_bin.write_bytes(data)
    return loader_bin


def build_image(boot: bytes, stage2: bytes, loader: bytes, path: Path) -> None:
    image = bytearray(boot)
    image.extend(stage2)
    recovery_offset = RECOVERY_LBA * 512
    if len(image) > recovery_offset:
        raise FixtureError("stage2_invade_area_recovery")
    image.extend(bytes(recovery_offset - len(image)))
    image.extend(loader)
    image.extend(bytes((-len(image)) % 512))
    path.write_bytes(image)


def run_qemu(qemu: str, image: Path, trace: Path, timeout: float,
             stdout_path: Path, stderr_path: Path) -> bool:
    command = [
        qemu, "-cpu", "max", "-snapshot", "-no-reboot", "-no-shutdown",
        "-display", "none", "-serial", "none", "-monitor", "none",
        "-d", "in_asm", "-D", str(trace),
        "-drive", f"file={image},format=raw,if=none,id=recoveryfixture",
        "-device", "ide-hd,drive=recoveryfixture,bootindex=1",
    ]
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


def symbols(arguments: argparse.Namespace) -> dict[str, Any]:
    value = collector.build_symbols(argparse.Namespace(
        build_dir=arguments.build_dir,
        catalog=arguments.catalog,
        nasm=arguments.nasm,
        nm=arguments.nm,
        compiler=arguments.compiler,
        define=[],
        boot_stage2_sectors=None,
    ))
    selected = [item for item in value["symbols"]
                if item.get("source") == "src/boot/recovery_entry.asm" and
                item.get("symbol") in RECOVERY_SYMBOLS]
    if {item.get("symbol") for item in selected} != set(RECOVERY_SYMBOLS):
        raise FixtureError("simbolos_recovery_ausentes")
    return {"schema": value["schema"], "symbols": selected}


def run(arguments: argparse.Namespace) -> int:
    output_dir = Path(arguments.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    case: dict[str, Any] | None = None
    try:
        nasm = resolve_tool(arguments.nasm, arguments.compiler, "nasm.exe")
        ld = resolve_tool(arguments.ld, arguments.compiler, "i686-elf-ld.exe")
        objcopy = resolve_tool(arguments.objcopy, arguments.compiler,
                               "i686-elf-objcopy.exe")
        system_boot_return = Path(arguments.system_boot_return)
        if not system_boot_return.is_absolute():
            system_boot_return = ROOT / system_boot_return
        if not system_boot_return.is_file() or system_boot_return.stat().st_size != 512:
            raise FixtureError("system_boot_return_ausente")
        with tempfile.TemporaryDirectory(prefix="zephyros-recovery-fixture-") as name:
            temporary = Path(name)
            loader = build_loader(nasm, ld, objcopy, system_boot_return, temporary)
            recovery_sectors = math.ceil(loader.stat().st_size / 512)
            stage2_path = temporary / "stage2.bin"
            stage2_defines = list(STAGE2_DEFINES)
            stage2_defines.append(f"RECOVERY_LOADER_SECTORS={recovery_sectors}")
            collector.assemble_listing(
                nasm, ROOT / "src/boot/stage2.asm", temporary / "stage2.lst",
                stage2_path, stage2_defines,
            )
            stage2 = stage2_path.read_bytes()
            boot_path = temporary / "boot.bin"
            collector.assemble_listing(
                nasm, ROOT / "src/boot/boot.asm", temporary / "boot.lst",
                boot_path, [f"STAGE2_SECTORS={math.ceil(len(stage2) / 512)}"],
            )
            boot = boot_path.read_bytes()
            if len(boot) != 512 or boot[510:512] != b"\x55\xAA":
                raise FixtureError("boot_sector_invalido")
            image = output_dir / "fixture.img"
            build_image(boot, stage2, loader.read_bytes(), image)
            trace = output_dir / "qemu-in_asm.log"
            stdout_path = output_dir / "qemu.stdout.log"
            stderr_path = output_dir / "qemu.stderr.log"
            timed_out = run_qemu(arguments.qemu, image, trace, arguments.timeout,
                                 stdout_path, stderr_path)
            symbol_value = symbols(arguments)
            symbol_path = output_dir / "symbols.json"
            write_json(symbol_path, symbol_value)
            report = collector.collect_trace(argparse.Namespace(
                trace=str(trace), symbols=str(symbol_path),
                case_id="qemu:tst7:assembly:recovery",
                source=["src/boot/recovery_entry.asm"],
            ))
            case = report["cases"][0]
            case["qemu_timed_out"] = timed_out
            case["expected_timeout"] = True
            case["expected_symbols"] = list(RECOVERY_SYMBOLS)
            case["covered_surface_ids"] = report["covered_surface_ids"]
            case["status"] = "PASS" if report["complete"] else "FAIL"
    except (FixtureError, collector.AssemblyTraceError, OSError) as error:
        errors.append(str(error))
    covered = case.get("covered_surface_ids", []) if case else []
    report = {
        "schema": SCHEMA,
        "status": "PASS" if not errors and case and case["status"] == "PASS" else "FAIL",
        "covered_surface_ids": sorted(covered),
        "errors": errors,
        "cases": [case] if case else [],
    }
    write_json(output_dir / "assembly-recovery-trace.json", report)
    print(f"Assembly recovery trace: {report['status']}")
    print(f"Relatorio: {output_dir / 'assembly-recovery-trace.json'}")
    return 0 if report["status"] == "PASS" else 1


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    command.add_argument("--build-dir", required=True)
    command.add_argument("--catalog", required=True)
    command.add_argument("--output-dir", required=True)
    command.add_argument("--system-boot-return", required=True)
    command.add_argument("--nasm", default="nasm")
    command.add_argument("--ld", default="i686-elf-ld")
    command.add_argument("--objcopy", default="i686-elf-objcopy")
    command.add_argument("--nm", default="i686-elf-nm")
    command.add_argument("--compiler")
    command.add_argument("--qemu", default="qemu-system-i386")
    command.add_argument("--timeout", type=float, default=30.0)
    return command


if __name__ == "__main__":
    raise SystemExit(run(parser().parse_args()))
