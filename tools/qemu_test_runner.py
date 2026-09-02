#!/usr/bin/env python3
"""Executa casos declarativos do ZephyrOS no QEMU e guarda evidencias."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import shutil
import socket
import subprocess
import sys
import time
import zlib
from datetime import datetime, timezone
from json import JSONDecodeError
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import coverage_collector
DEFAULT_IMAGE = Path("build/zephyros.img")
DEFAULT_CATALOG = Path("tests/catalog.json")
DEFAULT_RESULTS = Path("build/test-results")
PROTOCOL_PREFIX = "@@ZTEST/1 "
PROTOCOL_MAX_FRAME = 512
PROTOCOL_CRC_LENGTH = 13
PROTOCOL_HEARTBEAT_DEFAULT = 5.0
BOOT_TIMEOUT_DEFAULT = 30.0
CASE_TIMEOUT_DEFAULT = 30.0
SUITE_TIMEOUT_DEFAULT = 300.0
QMP_TIMEOUT = 2.0
QEMU_TERMINATE_TIMEOUT = 3.0
HELLO_RETRY_INTERVAL = 0.5
QMP_KEY_HOLD_TIME_MS = 20
QMP_KEY_GAP_SECONDS = 0.025
FRAME_ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:")
EVENT_TERMINAL = {"PASS", "FAIL", "SKIP", "BLOCKED"}
EVENT_FATAL = {"PANIC", "TIMEOUT"}
EVENT_NAMES = {"READY", "HEARTBEAT", "BEGIN", "PASS", "FAIL", "SKIP",
               "BLOCKED", "PANIC", "TIMEOUT"}
PROGRESS_HISTORY_LIMIT = 32
PROGRESS_BOOT = "BOOT"
PROGRESS_HELLO = "HELLO"
PROGRESS_READY = "READY"
PROGRESS_RUN_SENT = "RUN_SENT"
PROGRESS_BEGIN = "BEGIN"
PROGRESS_RUNNING = "RUNNING"
PROGRESS_INPUT_SENT = "INPUT_SENT"
PROGRESS_OBSERVING = "OBSERVING"
PROGRESS_RESTART_WAIT = "RESTART_WAIT"
PROGRESS_SHUTDOWN_WAIT = "SHUTDOWN_WAIT"
PROGRESS_PASS = "PASS"
PROGRESS_FAIL = "FAIL"
PROGRESS_SKIP = "SKIP"
PROGRESS_BLOCKED = "BLOCKED"
PROGRESS_PANIC = "PANIC"
PROGRESS_TIMEOUT = "TIMEOUT"
PROGRESS_TERMINAL = {
    PROGRESS_PASS, PROGRESS_FAIL, PROGRESS_SKIP, PROGRESS_BLOCKED,
    PROGRESS_PANIC, PROGRESS_TIMEOUT,
}
REPORT_TERMINATIONS = {
    "completed", "panic", "timeout", "qemu_exit", "watchdog", "interrupted",
    "precondition", "catalog_error",
}
QEMU_PROFILE_NAMES = {
    "baseline", "minimal", "network", "usb-hid", "usb-storage", "audio",
    "display", "pci",
}
QEMU_PROFILE_CAPABILITIES = {
    "baseline": ["acpi", "pci", "vga", "network-e1000"],
    "minimal": ["pci", "vga"],
    "network": ["pci", "network-e1000"],
    "usb-hid": ["usb", "usb-hid"],
    "usb-storage": ["usb", "usb-hid", "usb-storage-readonly"],
    "audio": ["pci", "audio-ac97"],
    "display": ["pci", "vga-cirrus"],
    "pci": ["pci", "pci-extra"],
}
QEMU_PROFILE_ARGS = {
    "baseline": [],
    "minimal": ["-machine", "pc,acpi=off"],
    "network": [],
    "usb-hid": [
        "-device", "piix3-usb-uhci,id=tst6usb",
        "-device", "usb-kbd,bus=tst6usb.0",
        "-device", "usb-mouse,bus=tst6usb.0",
    ],
    "usb-storage": [
        "-device", "piix3-usb-uhci,id=tst6usb",
        "-device", "usb-kbd,bus=tst6usb.0",
        "-device", "usb-mouse,bus=tst6usb.0",
    ],
    "audio": [
        "-audiodev", "driver=none,id=tst6audio",
        "-device", "AC97,audiodev=tst6audio",
    ],
    "display": ["-vga", "cirrus"],
    "pci": ["-device", "virtio-rng-pci,id=tst6rng"],
}
QEMU_FIXTURE_NAMES = {"readonly", "readonly-update"}
TST6_MAX_ITERATIONS = 1000
TST6_MAX_DURATION_SECONDS = 600.0
TST6_DEFAULT_ITERATIONS = 100
TST6_DEFAULT_DURATION_SECONDS = 300.0
INPUT_SCRIPT_MAX_STEPS = 128
INPUT_TEXT_MAX_LENGTH = 160
INPUT_KEYS_MAX_COUNT = 4
INPUT_WAIT_MAX_SECONDS = 10.0
INPUT_TEXT_CHARACTERS = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-./"
)
INPUT_KEY_NAMES = {
    "enter", "esc", "backspace", "tab", "up", "down", "left", "right",
    "home", "end", "pageup", "pagedown", "ctrl", "shift", "alt",
    "meta_l",
    "c", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9",
    "f10", "f11", "f12",
}
QMP_KEY_NAMES = {
    "enter": "ret", "esc": "esc", "backspace": "backspace", "tab": "tab",
    "up": "up", "down": "down", "left": "left", "right": "right",
    "home": "home", "end": "end", "pageup": "pgup", "pagedown": "pgdn",
    "ctrl": "ctrl", "shift": "shift", "alt": "alt", "c": "c",
    "meta_l": "meta_l",
    **{f"f{index}": f"f{index}" for index in range(1, 13)},
}


class RunnerError(Exception):
    """Falha controlada do executor ou do contrato de teste."""

    def __init__(self, cause: str, termination: str, blocked: bool = False):
        super().__init__(cause)
        self.cause = cause
        self.termination = termination
        self.blocked = blocked


class ProgressTracker:
    """Mantem o ultimo estado observavel sem depender do QEMU."""

    def __init__(self) -> None:
        self.state = PROGRESS_BOOT
        self.last_event: str | None = None
        self.history: list[dict[str, str]] = []

    def mark_state(self, state: str) -> None:
        self.state = state

    def record(self, event: dict[str, str]) -> None:
        event_name = event.get("event")
        self.last_event = event_name
        if event_name == "READY":
            self.state = PROGRESS_READY
        elif event_name == "BEGIN":
            self.state = PROGRESS_BEGIN
        elif event_name == "HEARTBEAT" and self.state in {
                PROGRESS_BEGIN, PROGRESS_RUNNING}:
            self.state = PROGRESS_RUNNING
        elif event_name in PROGRESS_TERMINAL:
            self.state = event_name
        self.history.append(dict(event))
        if len(self.history) > PROGRESS_HISTORY_LIMIT:
            del self.history[:-PROGRESS_HISTORY_LIMIT]


def resolve_path(value: str | None, default: Path) -> Path:
    path = Path(value) if value else default
    return path if path.is_absolute() else ROOT / path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                return digest.hexdigest()
            digest.update(block)


def utc_run_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"qemu-{stamp}-{os.getpid()}"


def token_valid(value: str) -> bool:
    return bool(value) and all(char in FRAME_ALLOWED for char in value)


def validate_qemu_profile(name: str) -> None:
    if name not in QEMU_PROFILE_NAMES:
        raise RunnerError(f"perfil_qemu_invalido:{name}", "catalog_error", True)


def validate_fixture(name: str | None) -> None:
    if name is not None and name not in QEMU_FIXTURE_NAMES:
        raise RunnerError(f"fixture_invalida:{name}", "catalog_error", True)


def qemu_profile_capabilities(name: str) -> list[str]:
    validate_qemu_profile(name)
    return list(QEMU_PROFILE_CAPABILITIES[name])


def frame_crc(prefix: str) -> int:
    return zlib.crc32(prefix.encode("ascii")) & 0xFFFFFFFF


def build_frame(fields: list[tuple[str, str]]) -> bytes:
    tokens = []
    keys: set[str] = set()
    for key, value in fields:
        if not token_valid(key) or not token_valid(value):
            raise RunnerError("token_invalido", "protocol_error", True)
        if key in keys:
            raise RunnerError("campo_duplicado", "protocol_error", True)
        keys.add(key)
        tokens.append(f"{key}={value}")
    prefix = PROTOCOL_PREFIX + " ".join(tokens)
    frame = f"{prefix} crc={frame_crc(prefix):08X}\n"
    if len(frame.encode("ascii")) > PROTOCOL_MAX_FRAME:
        raise RunnerError("frame_overflow", "protocol_error", True)
    return frame.encode("ascii")


def parse_frame(raw: bytes) -> dict[str, str]:
    if len(raw) > PROTOCOL_MAX_FRAME or not raw.endswith(b"\n"):
        raise ValueError("frame_length")
    if b"\x00" in raw or b"\r" in raw:
        raise ValueError("frame_control")
    try:
        text = raw[:-1].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("frame_ascii") from error
    if not text.startswith(PROTOCOL_PREFIX):
        raise ValueError("frame_prefix")
    marker = text.rfind(" crc=")
    if marker < len(PROTOCOL_PREFIX) or len(text) - marker != PROTOCOL_CRC_LENGTH:
        raise ValueError("frame_crc_position")
    prefix = text[:marker]
    supplied = text[marker + 5:]
    if len(supplied) != 8:
        raise ValueError("frame_crc_length")
    try:
        supplied_crc = int(supplied, 16)
    except ValueError as error:
        raise ValueError("frame_crc_format") from error
    if frame_crc(prefix) != supplied_crc:
        raise ValueError("frame_crc_value")
    fields: dict[str, str] = {}
    for token in prefix[len(PROTOCOL_PREFIX):].split(" "):
        if token.count("=") != 1:
            raise ValueError("frame_field")
        key, value = token.split("=", 1)
        if not token_valid(key) or not token_valid(value):
            raise ValueError("frame_token")
        if key in fields:
            raise ValueError("frame_duplicate")
        fields[key] = value
    if not fields:
        raise ValueError("frame_empty")
    return fields


def load_catalog(path: Path) -> dict[str, Any]:
    try:
        catalog = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, JSONDecodeError) as error:
        raise RunnerError(f"catalogo_invalido:{error}", "catalog_error", True) from error
    if not isinstance(catalog, dict) or catalog.get("schema") != "zephyros-test-catalog-v1":
        raise RunnerError("schema_catalogo_invalido", "catalog_error", True)
    if not isinstance(catalog.get("cases"), list):
        raise RunnerError("casos_ausentes", "catalog_error", True)
    return catalog


def select_cases(catalog: dict[str, Any], profile: str) -> list[dict[str, Any]]:
    cases = []
    for case in catalog["cases"]:
        if not isinstance(case, dict):
            continue
        if case.get("status") != "AUTOMATED":
            continue
        if case.get("executor") != "qemu":
            continue
        if case.get("profile") != profile:
            continue
        cases.append(case)
    return sorted(cases, key=lambda item: str(item.get("id", "")))


def select_case(catalog: dict[str, Any], identifier: str) -> dict[str, Any]:
    for case in catalog["cases"]:
        if isinstance(case, dict) and case.get("id") == identifier:
            if case.get("status") != "AUTOMATED" or case.get("executor") != "qemu":
                raise RunnerError("caso_nao_executavel", "catalog_error", True)
            return case
    raise RunnerError("caso_inexistente", "catalog_error", True)


def sequence_valid(value: str, previous: int) -> bool:
    if not value.isdigit():
        return False
    sequence = int(value)
    return sequence == ((previous + 1) & 0xFFFFFFFF)


def heartbeat_expired(last: float, now: float, timeout: float) -> bool:
    return now - last > timeout


def validate_case_for_runner(case: dict[str, Any]) -> None:
    identifier = case.get("id", "<sem-id>")
    for field in ("executor", "profile", "guest_case", "isolation"):
        if not isinstance(case.get(field), str) or not case[field]:
            raise RunnerError(f"caso_sem_{field}:{identifier}",
                              "catalog_error", True)
    if case["executor"] != "qemu" or case["isolation"] not in {
            "snapshot", "fixture"}:
        raise RunnerError(f"caso_configuracao_invalida:{identifier}",
                          "catalog_error", True)
    if not token_valid(case["guest_case"]):
        raise RunnerError(f"guest_case_invalido:{identifier}",
                          "catalog_error", True)
    qemu_profile = case.get("qemu_profile", "baseline")
    if not isinstance(qemu_profile, str):
        raise RunnerError(f"perfil_qemu_invalido:{identifier}",
                          "catalog_error", True)
    validate_qemu_profile(qemu_profile)
    capabilities = case.get("required_capabilities", [])
    if not isinstance(capabilities, list) or any(
            not isinstance(capability, str) or not token_valid(capability)
            for capability in capabilities):
        raise RunnerError(f"capacidades_invalidas:{identifier}",
                          "catalog_error", True)
    if not set(capabilities).issubset(
            set(qemu_profile_capabilities(qemu_profile))):
        raise RunnerError(f"capacidade_nao_publicada:{identifier}",
                          "catalog_error", True)
    for field in ("timeout_seconds", "heartbeat_timeout_seconds"):
        value = case.get(field)
        if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
            raise RunnerError(f"caso_{field}_invalido:{identifier}",
                              "catalog_error", True)
    validate_interaction(case.get("interaction"), identifier)


def validate_input_step(step: Any, identifier: str) -> None:
    if not isinstance(step, dict) or step.get("op") not in {
            "key", "keys", "text", "wait"}:
        raise RunnerError(f"script_entrada_invalido:{identifier}",
                          "catalog_error", True)
    operation = step["op"]
    if operation == "wait":
        value = step.get("seconds")
        if (not isinstance(value, (int, float)) or isinstance(value, bool) or
                value <= 0 or value > INPUT_WAIT_MAX_SECONDS):
            raise RunnerError(f"espera_entrada_invalida:{identifier}",
                              "catalog_error", True)
        return
    if operation == "text":
        value = step.get("text")
        if not isinstance(value, str) or not value or len(value) > INPUT_TEXT_MAX_LENGTH:
            raise RunnerError(f"texto_entrada_invalido:{identifier}",
                              "catalog_error", True)
        if any(character not in INPUT_TEXT_CHARACTERS for character in value):
            raise RunnerError(f"caractere_entrada_invalido:{identifier}",
                              "catalog_error", True)
        return
    if operation == "key":
        keys = [step.get("key")]
    else:
        keys = step.get("keys")
        if not isinstance(keys, list) or not 0 < len(keys) <= INPUT_KEYS_MAX_COUNT:
            raise RunnerError(f"teclas_entrada_invalidas:{identifier}",
                              "catalog_error", True)
    if any(not isinstance(key, str) or key not in INPUT_KEY_NAMES for key in keys):
        raise RunnerError(f"tecla_entrada_invalida:{identifier}",
                          "catalog_error", True)


def validate_interaction(interaction: Any, identifier: str) -> None:
    if interaction is None:
        return
    if not isinstance(interaction, dict) or interaction.get("mode") != "qmp-keyboard":
        raise RunnerError(f"interacao_invalida:{identifier}", "catalog_error", True)
    steps = interaction.get("steps")
    if not isinstance(steps, list) or not steps or len(steps) > INPUT_SCRIPT_MAX_STEPS:
        raise RunnerError(f"script_entrada_invalido:{identifier}",
                          "catalog_error", True)
    for step in steps:
        validate_input_step(step, identifier)
    post_action = interaction.get("post_action")
    if not isinstance(post_action, dict) or post_action.get("type") not in {
            "none", "reboot", "poweroff"}:
        raise RunnerError(f"pos_acao_invalida:{identifier}", "catalog_error", True)
    if "timeout_seconds" in post_action and (
            not isinstance(post_action["timeout_seconds"], (int, float)) or
            isinstance(post_action["timeout_seconds"], bool) or
            post_action["timeout_seconds"] <= 0):
        raise RunnerError(f"timeout_pos_acao_invalido:{identifier}",
                          "catalog_error", True)
    post_steps = post_action.get("steps", [])
    if not isinstance(post_steps, list) or len(post_steps) > INPUT_SCRIPT_MAX_STEPS:
        raise RunnerError(f"script_pos_acao_invalido:{identifier}",
                          "catalog_error", True)
    for step in post_steps:
        validate_input_step(step, identifier)


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(("127.0.0.1", 0))
        return int(server.getsockname()[1])


class QmpClient:
    """Cliente minimo para comandos externos do QMP."""

    def __init__(self, port: int):
        self.port = port
        self.socket: socket.socket | None = None
        self.buffer = ""
        self.command_id = 0
        self.event_queue: list[dict[str, Any]] = []

    def connect(self, deadline: float) -> None:
        while time.monotonic() < deadline:
            try:
                self.socket = socket.create_connection(("127.0.0.1", self.port), timeout=0.5)
                self.socket.settimeout(0.25)
                self._next_message(deadline)
                self.command("qmp_capabilities", deadline=deadline)
                return
            except (OSError, TimeoutError, ValueError, RunnerError):
                self.close()
                time.sleep(0.05)
        raise RunnerError("qmp_indisponivel", "qmp_error", True)

    def _receive(self, deadline: float) -> None:
        if not self.socket:
            raise RunnerError("qmp_fechado", "qmp_error", True)
        while time.monotonic() < deadline:
            try:
                data = self.socket.recv(4096)
            except socket.timeout:
                continue
            except OSError as error:
                raise RunnerError("qmp_conexao_interrompida", "qmp_error",
                                  True) from error
            if not data:
                raise RunnerError("qmp_encerrado", "qmp_error", True)
            self.buffer += data.decode("utf-8", errors="replace")
            return
        raise RunnerError("qmp_timeout", "qmp_timeout", True)

    def _next_message(self, deadline: float) -> dict[str, Any]:
        decoder = json.JSONDecoder()
        while time.monotonic() < deadline:
            text = self.buffer.lstrip()
            if text:
                try:
                    message, consumed = decoder.raw_decode(text)
                    self.buffer = text[consumed:]
                    if isinstance(message, dict):
                        if "event" in message:
                            self.event_queue.append(message)
                            continue
                        return message
                except JSONDecodeError:
                    pass
            self._receive(deadline)
        raise RunnerError("qmp_mensagem_invalida", "qmp_error", True)

    def command(self, name: str, arguments: dict[str, Any] | None = None,
                deadline: float | None = None) -> dict[str, Any]:
        if not self.socket:
            raise RunnerError("qmp_fechado", "qmp_error", True)
        self.command_id += 1
        payload = {"execute": name, "id": self.command_id}
        if arguments is not None:
            payload["arguments"] = arguments
        try:
            self.socket.sendall((json.dumps(payload) + "\r\n").encode("ascii"))
        except OSError as error:
            raise RunnerError(f"qmp_{name}_escrita", "qmp_error", True) from error
        limit = deadline or (time.monotonic() + QMP_TIMEOUT)
        while time.monotonic() < limit:
            message = self._next_message(limit)
            if message.get("id") != self.command_id:
                continue
            if "error" in message:
                detail = message.get("error")
                if isinstance(detail, dict):
                    error_class = str(detail.get("class", "unknown"))
                    description = str(detail.get("desc", "unknown"))
                    raise RunnerError(
                        f"qmp_{name}_erro:{error_class}:{description}",
                        "qmp_error", True)
                raise RunnerError(f"qmp_{name}_erro", "qmp_error", True)
            return message
        raise RunnerError(f"qmp_{name}_timeout", "qmp_timeout", True)

    def poll_events(self) -> list[dict[str, Any]]:
        if not self.socket:
            return []
        previous_timeout = self.socket.gettimeout()
        self.socket.settimeout(0.0)
        try:
            while True:
                try:
                    data = self.socket.recv(4096)
                except (BlockingIOError, socket.timeout):
                    break
                except OSError:
                    self.close()
                    break
                if not data:
                    break
                self.buffer += data.decode("utf-8", errors="replace")
        finally:
            if self.socket:
                self.socket.settimeout(previous_timeout)
        decoder = json.JSONDecoder()
        while True:
            text = self.buffer.lstrip()
            if not text:
                self.buffer = text
                break
            try:
                message, consumed = decoder.raw_decode(text)
            except JSONDecodeError:
                break
            self.buffer = text[consumed:]
            if isinstance(message, dict) and "event" in message:
                self.event_queue.append(message)
        events = list(self.event_queue)
        self.event_queue.clear()
        return events

    def close(self) -> None:
        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
        self.socket = None
        self.buffer = ""
        self.event_queue.clear()


class QemuSession:
    """Processo QEMU, sockets e logs de uma execucao."""

    def __init__(self, arguments: argparse.Namespace, artifact_dir: Path):
        self.arguments = arguments
        self.artifact_dir = artifact_dir
        self.image = resolve_path(arguments.image, DEFAULT_IMAGE)
        self.serial_port = free_port()
        self.qmp_port = free_port()
        self.serial: socket.socket | None = None
        self.qmp = QmpClient(self.qmp_port)
        self.process: subprocess.Popen[bytes] | None = None
        self.serial_buffer = bytearray()
        self.protocol_errors: list[str] = []
        self.events: list[dict[str, str]] = []
        self.progress = ProgressTracker()
        self.last_heartbeat: float | None = None
        self.host_sequence = 0
        self.guest_sequence = 0
        self.run_id: str | None = None
        self.qmp_status: dict[str, Any] | None = None
        self.qmp_events: list[dict[str, Any]] = []
        self.input_trace: list[dict[str, Any]] = []
        self.diagnostics: list[str] = []
        self.observed_capabilities: list[str] = []
        self.allow_qemu_exit = False
        self.stdout_thread: Any = None
        self.stderr_thread: Any = None

    def command(self) -> list[str]:
        qemu = self.arguments.qemu or os.environ.get("QEMU", "qemu-system-i386")
        image = str(self.image)
        qemu_profile = getattr(self.arguments, "qemu_profile", "baseline")
        validate_qemu_profile(qemu_profile)
        command = [qemu]
        command.extend(["-cpu", self.arguments.cpu])
        if self.arguments.snapshot:
            command.append("-snapshot")
        command.extend([
            "-drive", f"file={image},format=raw,if=none,id=bootdisk",
            "-device", "ide-hd,drive=bootdisk,bus=ide.0,unit=0,bootindex=1",
        ])
        if self.arguments.network:
            command.extend(["-nic", self.arguments.network])
        command.extend([
            "-serial", f"tcp:127.0.0.1:{self.serial_port},server=on,wait=on",
            "-qmp", f"tcp:127.0.0.1:{self.qmp_port},server=on,wait=off",
        ])
        command.extend(QEMU_PROFILE_ARGS[qemu_profile])
        if qemu_profile == "usb-storage":
            storage_image = resolve_path(
                getattr(self.arguments, "storage_image", None),
                Path("build/storage-valid.img"),
            )
            command.extend([
                "-drive", f"if=none,id=tst6stick,format=raw,"
                f"file={storage_image},readonly=on",
                "-device", "usb-storage,bus=tst6usb.0,drive=tst6stick",
            ])
        command.extend(self.arguments.qemu_arg or [])
        return command

    def _drain_pipe(self, stream: Any, path: Path) -> None:
        with path.open("wb") as output:
            while True:
                block = stream.read(4096)
                if not block:
                    return
                output.write(block)
                output.flush()

    def start(self) -> None:
        if not self.image.is_file():
            raise RunnerError(f"imagem_ausente:{self.image}", "precondition", True)
        qemu_profile = getattr(self.arguments, "qemu_profile", "baseline")
        validate_qemu_profile(qemu_profile)
        if qemu_profile == "usb-storage":
            storage_image = resolve_path(
                getattr(self.arguments, "storage_image", None),
                Path("build/storage-valid.img"),
            )
            if not storage_image.is_file():
                raise RunnerError(
                    f"fixture_storage_ausente:{storage_image}",
                    "precondition", True,
                )
        qemu = self.command()[0]
        if not Path(qemu).is_file() and shutil.which(qemu) is None:
            raise RunnerError(f"qemu_ausente:{qemu}", "precondition", True)
        try:
            self.process = subprocess.Popen(
                self.command(),
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except OSError as error:
            raise RunnerError(f"qemu_inicio:{error}", "qemu_exit", True) from error
        import threading
        self.stdout_thread = threading.Thread(
            target=self._drain_pipe,
            args=(self.process.stdout, self.artifact_dir / "qemu.stdout.log"),
            daemon=True,
        )
        self.stderr_thread = threading.Thread(
            target=self._drain_pipe,
            args=(self.process.stderr, self.artifact_dir / "qemu.stderr.log"),
            daemon=True,
        )
        self.stdout_thread.start()
        self.stderr_thread.start()
        deadline = time.monotonic() + self.arguments.boot_timeout
        self._connect_serial(deadline)
        self.qmp.connect(deadline)

    def _connect_serial(self, deadline: float) -> None:
        while time.monotonic() < deadline:
            try:
                self.serial = socket.create_connection(
                    ("127.0.0.1", self.serial_port), timeout=0.5)
                self.serial.settimeout(0.2)
                return
            except OSError:
                if self.process and self.process.poll() is not None:
                    raise RunnerError("qemu_encerrou_antes_do_serial", "qemu_exit")
                time.sleep(0.05)
        raise RunnerError("serial_timeout", "serial_timeout", True)

    def send(self, fields: list[tuple[str, str]]) -> None:
        if not self.serial:
            raise RunnerError("serial_fechado", "serial_error", True)
        fields = list(fields)
        sequence = next((value for key, value in fields if key == "seq"), None)
        if sequence is None:
            self.host_sequence += 1
            fields.append(("seq", str(self.host_sequence)))
        else:
            if not sequence.isdigit() or int(sequence) > 0xFFFFFFFF:
                raise RunnerError("sequencia_host_invalida", "protocol_error", True)
            self.host_sequence = int(sequence)
        try:
            self.serial.sendall(build_frame(fields))
        except OSError as error:
            raise RunnerError(f"serial_escrita:{error}", "serial_error") from error

    def poll_qmp_events(self) -> list[dict[str, Any]]:
        events = self.qmp.poll_events()
        if events:
            self.qmp_events.extend(events)
            with (self.artifact_dir / "qmp-events.log").open(
                    "a", encoding="utf-8") as output:
                for event in events:
                    output.write(json.dumps(event, ensure_ascii=False) + "\n")
        return events

    def _record_input(self, entry: dict[str, Any]) -> None:
        entry = dict(entry)
        entry["time"] = round(time.monotonic(), 6)
        self.input_trace.append(entry)
        with (self.artifact_dir / "input.log").open(
                "a", encoding="utf-8") as output:
            output.write(json.dumps(entry, ensure_ascii=False) + "\n")

    def _send_qmp_keys(self, qmp_keys: list[str]) -> None:
        self.qmp.command("send-key", {
            "keys": [{"type": "qcode", "data": key} for key in qmp_keys],
            "hold-time": QMP_KEY_HOLD_TIME_MS,
        })
        time.sleep(QMP_KEY_GAP_SECONDS)

    def send_key(self, key: str) -> None:
        qmp_key = QMP_KEY_NAMES[key]
        self._send_qmp_keys([qmp_key])
        self._record_input({"op": "key", "key": key, "qmp_key": qmp_key})

    def send_keys(self, keys: list[str]) -> None:
        qmp_keys = [QMP_KEY_NAMES[key] for key in keys]
        self._send_qmp_keys(qmp_keys)
        self._record_input({"op": "keys", "keys": list(keys),
                            "qmp_keys": qmp_keys})

    def send_text(self, value: str) -> None:
        self._record_input({"op": "text", "text": value})
        for character in value:
            if character == " ":
                keys = ["spc"]
            elif character == "-":
                keys = ["minus"]
            elif character == ".":
                keys = ["dot"]
            elif character == "/":
                keys = ["slash"]
            elif character.isupper():
                keys = ["shift", character.lower()]
            else:
                keys = [character]
            self._send_qmp_keys(keys)

    def execute_input_step(self, step: dict[str, Any]) -> None:
        if step["op"] == "key":
            self.send_key(step["key"])
        elif step["op"] == "keys":
            self.send_keys(step["keys"])
        elif step["op"] == "text":
            self.send_text(step["text"])
        else:
            deadline = time.monotonic() + float(step["seconds"])
            while time.monotonic() < deadline:
                self.pump()
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    time.sleep(min(0.05, remaining))

    def execute_interaction(self, interaction: dict[str, Any]) -> None:
        for step in interaction.get("steps", []):
            self.execute_input_step(step)

    def capture_screenshot(self, label: str) -> Path | None:
        path = self.artifact_dir / f"screenshot-{label}.ppm"
        try:
            self.qmp.command("screendump", {"filename": str(path)})
        except RunnerError as error:
            self.diagnostics.append(f"screenshot:{label}:{error.cause}")
            return None
        return path

    def reset_protocol(self) -> None:
        self.serial_buffer.clear()
        self.events.clear()
        self.host_sequence = 0
        self.guest_sequence = 0
        self.last_heartbeat = None

    def _read_serial(self) -> None:
        if not self.serial:
            return
        try:
            data = self.serial.recv(4096)
        except socket.timeout:
            return
        except OSError as error:
            raise RunnerError(f"serial_leitura:{error}", "serial_error") from error
        if not data:
            raise RunnerError("serial_encerrada", "qemu_exit")
        with (self.artifact_dir / "serial.log").open("ab") as output:
            output.write(data)
            output.flush()
        self.serial_buffer.extend(data)
        while b"\n" in self.serial_buffer:
            index = self.serial_buffer.index(b"\n") + 1
            raw = bytes(self.serial_buffer[:index])
            del self.serial_buffer[:index]
            if not raw.startswith(PROTOCOL_PREFIX.encode("ascii")):
                continue
            try:
                event = parse_frame(raw)
            except ValueError as error:
                self.protocol_errors.append(str(error))
                continue
            if not sequence_valid(event.get("seq", ""), self.guest_sequence):
                self.protocol_errors.append("sequencia_guest_invalida")
                continue
            if self.run_id and event.get("run") != self.run_id:
                self.protocol_errors.append("run_guest_invalido")
                continue
            if event.get("event") not in EVENT_NAMES:
                self.protocol_errors.append("evento_guest_invalido")
                continue
            if event.get("event") == "HEARTBEAT" and \
                    (not event.get("ticks") or
                     not event["ticks"].isdigit()):
                self.protocol_errors.append("heartbeat_guest_invalido")
                continue
            capabilities = event.get("capabilities")
            if capabilities:
                self.observed_capabilities = [
                    item for item in capabilities.split(",")
                    if token_valid(item)
                ]
            self.guest_sequence = int(event["seq"])
            self.events.append(event)
            self.progress.record(event)
            if event.get("event") == "HEARTBEAT":
                self.last_heartbeat = time.monotonic()
        if len(self.serial_buffer) > PROTOCOL_MAX_FRAME:
            self.protocol_errors.append("frame_overflow")
            self.serial_buffer.clear()

    def pump(self) -> list[dict[str, str]]:
        count = len(self.events)
        self.poll_qmp_events()
        self._read_serial()
        if self.process and self.process.poll() is not None and not self.allow_qemu_exit:
            if len(self.events) == count:
                raise RunnerError("qemu_encerrou", "qemu_exit")
        new_events = self.events[count:]
        return new_events

    def stop(self) -> int | None:
        if self.process is None:
            return None
        if self.process.poll() is None:
            try:
                self.qmp_status = self.qmp.command("query-status")
            except RunnerError:
                self.qmp_status = None
            try:
                self.qmp.command("quit")
            except RunnerError:
                pass
            try:
                self.process.wait(timeout=QEMU_TERMINATE_TIMEOUT)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=QEMU_TERMINATE_TIMEOUT)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
        exit_code = self.process.returncode
        for thread in (self.stdout_thread, self.stderr_thread):
            if thread:
                thread.join(timeout=QEMU_TERMINATE_TIMEOUT)
        self.qmp.close()
        if self.serial:
            try:
                self.serial.close()
            except OSError:
                pass
            self.serial = None
        return exit_code


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json_document(value), encoding="utf-8")


def collect_coverage_artifact(artifact_dir: Path,
                              arguments: argparse.Namespace) -> dict[str, Any] | None:
    symbols_value = getattr(arguments, "coverage_symbols", None)
    if not symbols_value:
        return None
    coverage_path = artifact_dir / "coverage.json"
    try:
        symbols = coverage_collector.load_symbols(
            resolve_path(symbols_value, Path("build-coverage/coverage-symbols.json")))
        catalog = load_catalog(resolve_path(arguments.catalog, DEFAULT_CATALOG))
        serial = (artifact_dir / "serial.log").read_text(
            encoding="utf-8", errors="replace")
        coverage = coverage_collector.collect_report(serial, symbols, catalog)
    except (coverage_collector.CoverageError, OSError, RunnerError) as error:
        coverage = {
            "schema": coverage_collector.SCHEMA,
            "status": "FAIL",
            "errors": [f"coleta:{error}"],
            "cases": [],
            "covered_surface_ids": [],
        }
    write_json(coverage_path, coverage)
    return coverage


def json_document(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def case_timeout(case: dict[str, Any], default: float) -> float:
    value = case.get("timeout_seconds", default)
    return float(value) if isinstance(value, (int, float)) and value > 0 else default


def wait_for_ready(session: QemuSession, run_id: str,
                   reset_protocol: bool = False) -> None:
    if reset_protocol:
        session.reset_protocol()
    session.run_id = run_id
    session.progress.mark_state(PROGRESS_HELLO)
    hello_sequence = session.host_sequence + 1
    hello = [("cmd", "HELLO"), ("run", run_id),
             ("seq", str(hello_sequence))]
    session.send(hello)
    deadline = time.monotonic() + session.arguments.boot_timeout
    retry_at = time.monotonic() + HELLO_RETRY_INTERVAL
    ready_seen = False
    heartbeat_requested = False
    while time.monotonic() < deadline:
        for event in session.pump():
            if event.get("event") == "READY" and event.get("run") == run_id:
                ready_seen = True
                if not heartbeat_requested:
                    session.send([("cmd", "PING")])
                    heartbeat_requested = True
                continue
            if heartbeat_requested and event.get("event") == "HEARTBEAT":
                return
            if event.get("event") in EVENT_FATAL:
                termination = "panic" if event.get("event") == "PANIC" else "timeout"
                raise RunnerError("guest_fatal_no_boot", termination)
        if not ready_seen and time.monotonic() >= retry_at:
            session.send(hello)
            retry_at = time.monotonic() + HELLO_RETRY_INTERVAL
        time.sleep(0.01)
    if ready_seen:
        raise RunnerError(
            f"boot_heartbeat_timeout:state={session.progress.state}", "timeout")
    raise RunnerError(
        f"boot_ready_timeout:state={session.progress.state}", "timeout")


def wait_for_case(session: QemuSession, case_id: str, iteration: int,
                  seed: int, timeout: float, heartbeat_timeout: float,
                  case: dict[str, Any] | None = None) -> dict[str, str]:
    session.progress.mark_state(PROGRESS_RUN_SENT)
    session.send([
        ("cmd", "RUN"), ("case", case_id), ("iteration", str(iteration)),
        ("seed", str(seed)),
    ])
    deadline = time.monotonic() + timeout
    begun = False
    heartbeat_reference: float | None = None
    while time.monotonic() < deadline:
        for event in session.pump():
            if event.get("event") in EVENT_FATAL:
                termination = "panic" if event.get("event") == "PANIC" else "timeout"
                raise RunnerError(
                    f"{event.get('reason', 'guest_fatal')}:state={session.progress.state}",
                    termination)
            if event.get("event") == "BEGIN":
                if event.get("case") != case_id or \
                        event.get("iteration") != str(iteration) or \
                        event.get("seed") != str(seed):
                    session.protocol_errors.append("begin_guest_invalido")
                    continue
                begun = True
                session.progress.mark_state(PROGRESS_RUNNING)
                heartbeat_reference = time.monotonic()
                if case and case.get("interaction"):
                    session.progress.mark_state(PROGRESS_INPUT_SENT)
                    session.execute_interaction(case["interaction"])
                    session.capture_screenshot("after-input")
                    heartbeat_reference = time.monotonic()
                    session.progress.mark_state(PROGRESS_OBSERVING)
                continue
            if event.get("event") == "HEARTBEAT" and begun:
                heartbeat_reference = time.monotonic()
            if event.get("event") not in EVENT_TERMINAL:
                continue
            if event.get("case") != case_id:
                continue
            if event.get("event") in {"PASS", "FAIL", "SKIP"} and not begun:
                session.protocol_errors.append("terminal_sem_begin_guest")
                continue
            session.progress.mark_state(event.get("event", PROGRESS_BLOCKED))
            return event
        if begun and heartbeat_reference is not None and \
                heartbeat_expired(heartbeat_reference, time.monotonic(),
                                  heartbeat_timeout):
            raise RunnerError(
                f"guest_sem_heartbeat:state={session.progress.state}",
                "watchdog")
        time.sleep(0.01)
    raise RunnerError(
        f"case_timeout:{case_id}:state={session.progress.state}", "timeout")


def wait_for_restart(session: QemuSession, run_id: str,
                     timeout: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    session.progress.mark_state(PROGRESS_RESTART_WAIT)
    while time.monotonic() < deadline:
        for event in session.poll_qmp_events():
            if event.get("event") == "RESET":
                wait_for_ready(session, run_id, reset_protocol=True)
                return {"status": "PASS", "event": "RESET",
                        "handshake": "HELLO_READY_HEARTBEAT"}
        if session.process and session.process.poll() is not None:
            raise RunnerError("reboot_qemu_exit", "qemu_exit")
        time.sleep(0.01)
    raise RunnerError("reboot_event_timeout", "timeout")


def wait_for_poweroff(session: QemuSession, timeout: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    session.progress.mark_state(PROGRESS_SHUTDOWN_WAIT)
    session.allow_qemu_exit = True
    while time.monotonic() < deadline:
        for event in session.poll_qmp_events():
            if event.get("event") == "SHUTDOWN":
                return {"status": "PASS", "event": "SHUTDOWN"}
        if session.process and session.process.poll() is not None:
            return {"status": "PASS", "event": "QEMU_EXIT"}
        time.sleep(0.01)
    raise RunnerError("poweroff_shutdown_timeout", "timeout")


def run_post_action(session: QemuSession, case: dict[str, Any],
                    run_id: str) -> dict[str, Any] | None:
    interaction = case.get("interaction")
    if not interaction:
        return None
    action = interaction["post_action"]
    action_type = action["type"]
    if action_type == "none":
        return {"status": "PASS", "event": "NONE"}
    session.capture_screenshot(f"before-{action_type}")
    for step in action.get("steps", []):
        session.execute_input_step(step)
    lifecycle_timeout = float(action.get(
        "timeout_seconds", case_timeout(case, CASE_TIMEOUT_DEFAULT)))
    if action_type == "reboot":
        return wait_for_restart(session, run_id, lifecycle_timeout)
    return wait_for_poweroff(session, lifecycle_timeout)


def result_status(cases: list[dict[str, Any]], error: RunnerError | None) -> tuple[str, str, str]:
    if error:
        termination = error.termination
        if termination not in REPORT_TERMINATIONS:
            termination = "timeout" if "timeout" in termination else "completed"
        if error.termination in {"qmp_error", "qmp_timeout", "serial_error",
                                 "serial_timeout", "protocol_error"}:
            termination = "timeout" if "timeout" in error.termination else "qemu_exit"
        return ("BLOCKED" if error.blocked else "FAIL", termination, error.cause)
    if not cases:
        return "BLOCKED", "completed", "nenhum_caso_executavel"
    if any(item.get("status") == "FAIL" for item in cases):
        return "FAIL", "completed", "caso_reprovado"
    if any(item.get("status") == "BLOCKED" for item in cases):
        return "BLOCKED", "completed", "caso_bloqueado"
    if any(item.get("status") == "SKIP" for item in cases):
        return "SKIP", "completed", "recurso_indisponivel"
    return "PASS", "completed", "suite_concluida"


def validate_arguments(arguments: argparse.Namespace) -> None:
    if arguments.boot_timeout <= 0 or arguments.case_timeout <= 0 or \
            arguments.suite_timeout <= 0 or arguments.heartbeat_timeout <= 0:
        raise RunnerError("timeout_invalido", "catalog_error", True)
    if arguments.boot_timeout > TST6_MAX_DURATION_SECONDS or \
            arguments.case_timeout > TST6_MAX_DURATION_SECONDS or \
            arguments.suite_timeout > TST6_MAX_DURATION_SECONDS:
        raise RunnerError("timeout_excede_teto", "catalog_error", True)
    if arguments.seed is not None and not 0 <= arguments.seed <= 0xFFFFFFFF:
        raise RunnerError("seed_invalida", "catalog_error", True)
    validate_fixture(getattr(arguments, "fixture", None))
    if arguments.command != "stress":
        return
    iterations = getattr(arguments, "iterations", None)
    duration = getattr(arguments, "duration", None)
    max_iterations = getattr(arguments, "max_iterations", None)
    until_failure = bool(getattr(arguments, "until_failure", False))
    if until_failure:
        if iterations is not None or (max_iterations is None and duration is None):
            raise RunnerError("stress_requer_teto_until_failure",
                              "catalog_error", True)
    elif max_iterations is not None:
        raise RunnerError("max_iteracoes_requer_until_failure",
                          "catalog_error", True)
    elif (iterations is None) == (duration is None):
        raise RunnerError("stress_requer_um_limite", "catalog_error", True)
    if iterations is not None and iterations <= 0:
        raise RunnerError("iteracoes_invalidas", "catalog_error", True)
    if max_iterations is not None and max_iterations <= 0:
        raise RunnerError("max_iteracoes_invalidas", "catalog_error", True)
    if duration is not None and duration <= 0:
        raise RunnerError("duracao_invalida", "catalog_error", True)
    if iterations is not None and iterations > TST6_MAX_ITERATIONS:
        raise RunnerError("iteracoes_excedem_teto", "catalog_error", True)
    if max_iterations is not None and max_iterations > TST6_MAX_ITERATIONS:
        raise RunnerError("max_iteracoes_excedem_teto", "catalog_error", True)
    if duration is not None and duration > TST6_MAX_DURATION_SECONDS:
        raise RunnerError("duracao_excede_teto", "catalog_error", True)


def initialize_artifacts(path: Path) -> None:
    for name in ("serial.log", "qemu.stdout.log", "qemu.stderr.log",
                 "input.log", "qmp-events.log"):
        (path / name).touch()


def run_execution(arguments: argparse.Namespace) -> int:
    image = resolve_path(arguments.image, DEFAULT_IMAGE)
    catalog_path = resolve_path(arguments.catalog, DEFAULT_CATALOG)
    results_root = resolve_path(arguments.results, DEFAULT_RESULTS)
    run_id = arguments.run_id or utc_run_id()
    if not token_valid(run_id) or len(run_id) >= 48:
        raise RunnerError("run_id_invalido", "catalog_error", True)
    artifact_dir = results_root / run_id
    artifact_dir.mkdir(parents=True, exist_ok=False)
    initialize_artifacts(artifact_dir)
    started = time.monotonic()
    cases: list[dict[str, Any]] = []
    error: RunnerError | None = None
    session: QemuSession | None = None
    seed = int(arguments.seed) if arguments.seed is not None else random.SystemRandom().randrange(0, 0xFFFFFFFF)
    image_hash: str | None = None
    try:
        validate_arguments(arguments)
        catalog = load_catalog(catalog_path)
        if arguments.command == "run":
            selected = select_cases(catalog, arguments.profile)
        else:
            selected = [select_case(catalog, arguments.case)]
        for case in selected:
            validate_case_for_runner(case)
        qemu_profile = getattr(arguments, "qemu_profile", "baseline")
        validate_qemu_profile(qemu_profile)
        available_capabilities = set(qemu_profile_capabilities(qemu_profile))
        for case in selected:
            case_profile = case.get("qemu_profile", "baseline")
            if case_profile != qemu_profile:
                raise RunnerError(
                    f"perfil_qemu_divergente:{case['id']}:"
                    f"{case_profile}!={qemu_profile}",
                    "catalog_error", True,
                )
            required = set(case.get("required_capabilities", []))
            if not required.issubset(available_capabilities):
                missing = ",".join(sorted(required - available_capabilities))
                raise RunnerError(
                    f"capacidade_nao_publicada:{case['id']}:{missing}",
                    "catalog_error", True,
                )
        if not image.is_file():
            raise RunnerError(f"imagem_ausente:{image}", "precondition", True)
        image_hash = sha256_file(image)
        session = QemuSession(arguments, artifact_dir)
        manifest = {
            "schema": "zephyros-test-run-v1",
            "run_id": run_id,
            "image": str(image),
            "image_sha256": image_hash,
            "catalog": str(catalog_path),
            "profile": arguments.profile,
            "qemu_profile": qemu_profile,
            "profile_capabilities": qemu_profile_capabilities(qemu_profile),
            "capabilities_expected": qemu_profile_capabilities(qemu_profile),
            "capabilities_observed": [],
            "fixture": arguments.fixture,
            "seed": seed,
            "command": session.command(),
            "snapshot": bool(arguments.snapshot),
            "coverage_symbols": getattr(arguments, "coverage_symbols", None),
            "cases": [{
                "id": str(case["id"]),
                "guest_case": str(case["guest_case"]),
                "qemu_profile": str(case.get("qemu_profile", "baseline")),
                "required_capabilities": list(
                    case.get("required_capabilities", [])),
                "interaction": case.get("interaction"),
            } for case in selected],
        }
        write_json(artifact_dir / "manifest.json", manifest)
        session.start()
        wait_for_ready(session, run_id)
        session.capture_screenshot("ready")
        stress_started = time.monotonic()
        if arguments.command == "run":
            suite_deadline = time.monotonic() + arguments.suite_timeout
            stress_deadline = None
        else:
            suite_deadline = None
            requested_duration = getattr(arguments, "duration", None)
            stress_deadline = stress_started + min(
                requested_duration or TST6_MAX_DURATION_SECONDS,
                arguments.suite_timeout,
                TST6_MAX_DURATION_SECONDS,
            )
        iteration = 0
        while True:
            if arguments.command == "run":
                if iteration >= len(selected):
                    break
                case = selected[iteration]
                case_iteration = 0
            else:
                iteration_limit = arguments.iterations or getattr(
                    arguments, "max_iterations", None)
                if iteration_limit and iteration >= iteration_limit:
                    break
                if getattr(arguments, "duration", None) and \
                        time.monotonic() - stress_started >= arguments.duration:
                    break
                case = selected[0]
                case_iteration = iteration
            if suite_deadline and time.monotonic() >= suite_deadline:
                raise RunnerError("suite_timeout", "timeout")
            if stress_deadline and time.monotonic() >= stress_deadline:
                raise RunnerError("stress_teto_tempo", "timeout")
            case_id = str(case.get("guest_case", case["id"]))
            event = wait_for_case(
                session, case_id, case_iteration,
                (seed + case_iteration * 2654435761) & 0xFFFFFFFF,
                case_timeout(case, arguments.case_timeout),
                float(case.get("heartbeat_timeout_seconds",
                               arguments.heartbeat_timeout)),
                case,
            )
            case_result = dict(event)
            case_result["catalog_case"] = str(case["id"])
            case_result["status"] = event.get("event", "BLOCKED")
            if case_result["status"] != "PASS":
                session.capture_screenshot(f"case-{iteration}-failure")
            try:
                lifecycle = None
                if case_result["status"] == "PASS":
                    lifecycle = run_post_action(session, case, run_id)
            except RunnerError as failure:
                case_result["status"] = "BLOCKED" if failure.blocked else "FAIL"
                case_result["lifecycle"] = {
                    "status": case_result["status"], "cause": failure.cause,
                }
                cases.append(case_result)
                iteration += 1
                raise
            if lifecycle:
                case_result["lifecycle"] = lifecycle
            cases.append(case_result)
            iteration += 1
            if case_result["status"] in {"FAIL", "BLOCKED"}:
                break
        if arguments.command == "stress" and not cases:
            raise RunnerError("stress_sem_iteracao", "catalog_error", True)
    except KeyboardInterrupt:
        error = RunnerError("interrompido_pelo_usuario", "interrupted", True)
    except RunnerError as failure:
        error = failure
    finally:
        exit_code = session.stop() if session else None
        manifest_path = artifact_dir / "manifest.json"
        if manifest_path.is_file():
            manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest_data["capabilities_observed"] = (
                session.observed_capabilities if session else [])
            write_json(manifest_path, manifest_data)
        status, termination, cause = result_status(cases, error)
        report = {
            "schema": "zephyros-test-result-v1",
            "run_id": run_id,
            "status": status,
            "termination": termination,
            "cause": cause,
            "profile": arguments.profile,
            "qemu_profile": getattr(arguments, "qemu_profile", "baseline"),
            "profile_capabilities": qemu_profile_capabilities(
                getattr(arguments, "qemu_profile", "baseline")),
            "capabilities_expected": qemu_profile_capabilities(
                getattr(arguments, "qemu_profile", "baseline")),
            "fixture": arguments.fixture,
            "seed": seed,
            "limits": {
                "iterations": getattr(arguments, "iterations", None),
                "max_iterations": getattr(arguments, "max_iterations", None),
                "duration_seconds": getattr(arguments, "duration", None),
                "max_iterations_allowed": TST6_MAX_ITERATIONS,
                "max_duration_seconds": TST6_MAX_DURATION_SECONDS,
            },
            "iteration_completed": len(cases),
            "duration_seconds": round(time.monotonic() - started, 6),
            "image": str(image),
            "image_sha256": image_hash,
            "qemu_exit_code": exit_code,
            "qmp_status": session.qmp_status if session else None,
            "protocol_errors": session.protocol_errors if session else [],
            "last_state": session.progress.state if session else None,
            "last_event": session.progress.last_event if session else None,
            "progress_history": session.progress.history if session else [],
            "capabilities_observed": session.observed_capabilities
            if session else [],
            "input_trace": session.input_trace if session else [],
            "qmp_events": session.qmp_events if session else [],
            "diagnostics": session.diagnostics if session else [],
            "cases": cases,
            "artifacts": {
                "manifest": "manifest.json",
                "serial": "serial.log",
                "stdout": "qemu.stdout.log",
                "stderr": "qemu.stderr.log",
                "input": "input.log",
                "qmp_events": "qmp-events.log",
                "screenshots": [path.name for path in artifact_dir.glob(
                    "screenshot-*.ppm")],
            },
        }
        coverage = collect_coverage_artifact(artifact_dir, arguments)
        if coverage is not None:
            report["coverage"] = {
                "status": coverage["status"],
                "errors": coverage["errors"],
                "covered_surface_ids": coverage["covered_surface_ids"],
                "artifact": "coverage.json",
            }
            report["artifacts"]["coverage"] = "coverage.json"
            if report["status"] == "PASS" and coverage["status"] != "PASS":
                report["status"] = "FAIL"
                report["cause"] = "coverage_incomplete"
        write_json(artifact_dir / "result.json", report)
    print(f"QEMU test: {status} termination={termination} run={run_id}")
    print(f"Artefatos: {artifact_dir}")
    return 0 if status == "PASS" else 2 if status in {"SKIP", "BLOCKED"} else 1


def self_test() -> int:
    valid = build_frame([("cmd", "HELLO"), ("run", "selftest"), ("seq", "1")])
    heartbeat = build_frame([
        ("event", "HEARTBEAT"), ("run", "selftest"), ("seq", "2"),
        ("ticks", "25"),
    ])
    timeout_error = RunnerError("timeout", "timeout")
    interrupt_error = RunnerError("interrupt", "interrupted", True)
    deterministic_report = '{\n  "b": 2,\n  "a": 1\n}\n'
    checks = [
        ("frame_valid", parse_frame(valid).get("cmd") == "HELLO"),
        ("heartbeat_ticks", parse_frame(heartbeat).get("ticks") == "25"),
        ("crc_rejeitado", _invalid_frame_rejected(
            valid.replace(b"run=selftest", b"run=selftesX"))),
        ("ascii_rejeitado", _invalid_frame_rejected(valid.replace(b"HELLO", b"HEL\x80LO"))),
        ("overflow_rejeitado", _invalid_frame_rejected(b"x" * (PROTOCOL_MAX_FRAME + 1))),
        ("ids_rejeitados", _invalid_build_rejected()),
        ("campos_duplicados_rejeitados", _duplicate_build_rejected()),
        ("sequencia_valida", sequence_valid("1", 0) and not sequence_valid("3", 1)),
        ("heartbeat_expirado", heartbeat_expired(10.0, 16.0, 5.0)),
        ("panic_reprovado", result_status([], RunnerError("panic", "panic"))[0] == "FAIL"),
        ("timeout_reprovado", result_status([], timeout_error)[1] == "timeout"),
        ("interrupcao_bloqueada", result_status([], interrupt_error) ==
         ("BLOCKED", "interrupted", "interrupt")),
        ("relatorio_deterministico", json_document({"b": 2, "a": 1}) ==
         deterministic_report),
    ]
    passed = True
    for name, result in checks:
        print(f"{name} {'OK' if result else 'ERRO'}")
        passed = passed and result
    print(f"QEMU test runner selftest {'OK' if passed else 'ERRO'}")
    return 0 if passed else 1


def _invalid_frame_rejected(frame: bytes) -> bool:
    try:
        parse_frame(frame)
    except ValueError:
        return True
    return False


def _invalid_build_rejected() -> bool:
    try:
        build_frame([("cmd", "HELLO"), ("run", "bad value")])
    except RunnerError:
        return True
    return False


def _duplicate_build_rejected() -> bool:
    try:
        build_frame([("cmd", "HELLO"), ("cmd", "PING")])
    except RunnerError:
        return True
    return False


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    command_parser.add_argument("--self-test", action="store_true")
    subparsers = command_parser.add_subparsers(dest="command")
    for name in ("run", "stress"):
        subparser = subparsers.add_parser(name)
        subparser.add_argument("--image")
        subparser.add_argument("--catalog")
        subparser.add_argument("--results")
        subparser.add_argument("--run-id")
        subparser.add_argument("--qemu")
        subparser.add_argument("--qemu-arg", action="append")
        subparser.add_argument("--cpu", default="max")
        subparser.add_argument("--qemu-profile", default="baseline")
        subparser.add_argument("--network", default="user,model=e1000")
        subparser.add_argument("--storage-image")
        subparser.add_argument("--no-snapshot", dest="snapshot", action="store_false")
        subparser.set_defaults(snapshot=True)
        subparser.add_argument("--fixture")
        subparser.add_argument("--profile", default="smoke")
        subparser.add_argument("--boot-timeout", type=float, default=BOOT_TIMEOUT_DEFAULT)
        subparser.add_argument("--case-timeout", type=float, default=CASE_TIMEOUT_DEFAULT)
        subparser.add_argument("--suite-timeout", type=float, default=SUITE_TIMEOUT_DEFAULT)
        subparser.add_argument("--heartbeat-timeout", type=float, default=PROTOCOL_HEARTBEAT_DEFAULT)
        subparser.add_argument("--seed", type=int)
        subparser.add_argument("--coverage-symbols")
        if name == "stress":
            subparser.add_argument("--case")
            subparser.add_argument("--iterations", type=int)
            subparser.add_argument("--max-iterations", type=int)
            subparser.add_argument("--duration", type=float)
            subparser.add_argument("--until-failure", action="store_true")
    return command_parser


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.command is None:
        print("Uso: qemu_test_runner.py [--self-test] run|stress", file=sys.stderr)
        return 2
    if arguments.command == "stress" and not arguments.case:
        print("ERRO: stress requer --case", file=sys.stderr)
        return 2
    try:
        return run_execution(arguments)
    except RunnerError as error:
        print(f"ERRO: {error.cause}", file=sys.stderr)
        return 2 if error.blocked else 1


if __name__ == "__main__":
    raise SystemExit(main())
