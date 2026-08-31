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
FRAME_ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:")
EVENT_TERMINAL = {"PASS", "FAIL", "SKIP", "BLOCKED"}
EVENT_FATAL = {"PANIC", "TIMEOUT"}
EVENT_NAMES = {"READY", "HEARTBEAT", "BEGIN", "PASS", "FAIL", "SKIP",
               "BLOCKED", "PANIC", "TIMEOUT"}
REPORT_TERMINATIONS = {
    "completed", "panic", "timeout", "qemu_exit", "watchdog", "interrupted",
}


class RunnerError(Exception):
    """Falha controlada do executor ou do contrato de teste."""

    def __init__(self, cause: str, termination: str, blocked: bool = False):
        super().__init__(cause)
        self.cause = cause
        self.termination = termination
        self.blocked = blocked


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
    for field in ("timeout_seconds", "heartbeat_timeout_seconds"):
        value = case.get(field)
        if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
            raise RunnerError(f"caso_{field}_invalido:{identifier}",
                              "catalog_error", True)


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

    def connect(self, deadline: float) -> None:
        while time.monotonic() < deadline:
            try:
                self.socket = socket.create_connection(("127.0.0.1", self.port), timeout=0.5)
                self.socket.settimeout(0.25)
                self._next_message(deadline)
                self.command("qmp_capabilities", deadline)
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
                        return message
                except JSONDecodeError:
                    pass
            self._receive(deadline)
        raise RunnerError("qmp_mensagem_invalida", "qmp_error", True)

    def command(self, name: str, deadline: float | None = None) -> dict[str, Any]:
        if not self.socket:
            raise RunnerError("qmp_fechado", "qmp_error", True)
        self.command_id += 1
        payload = {"execute": name, "id": self.command_id}
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
                raise RunnerError(f"qmp_{name}_erro", "qmp_error", True)
            return message
        raise RunnerError(f"qmp_{name}_timeout", "qmp_timeout", True)

    def close(self) -> None:
        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
        self.socket = None
        self.buffer = ""


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
        self.last_heartbeat = time.monotonic()
        self.host_sequence = 0
        self.guest_sequence = 0
        self.run_id: str | None = None
        self.qmp_status: dict[str, Any] | None = None
        self.stdout_thread: Any = None
        self.stderr_thread: Any = None

    def command(self) -> list[str]:
        qemu = self.arguments.qemu or os.environ.get("QEMU", "qemu-system-i386")
        image = str(self.image)
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
            self.guest_sequence = int(event["seq"])
            self.events.append(event)
            if event.get("event") == "HEARTBEAT":
                self.last_heartbeat = time.monotonic()
        if len(self.serial_buffer) > PROTOCOL_MAX_FRAME:
            self.protocol_errors.append("frame_overflow")
            self.serial_buffer.clear()

    def pump(self) -> list[dict[str, str]]:
        count = len(self.events)
        self._read_serial()
        if self.process and self.process.poll() is not None:
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


def json_document(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def case_timeout(case: dict[str, Any], default: float) -> float:
    value = case.get("timeout_seconds", default)
    return float(value) if isinstance(value, (int, float)) and value > 0 else default


def wait_for_ready(session: QemuSession, run_id: str) -> None:
    session.run_id = run_id
    hello_sequence = session.host_sequence + 1
    hello = [("cmd", "HELLO"), ("run", run_id),
             ("seq", str(hello_sequence))]
    session.send(hello)
    deadline = time.monotonic() + session.arguments.boot_timeout
    retry_at = time.monotonic() + HELLO_RETRY_INTERVAL
    while time.monotonic() < deadline:
        for event in session.pump():
            if event.get("event") == "READY" and event.get("run") == run_id:
                return
            if event.get("event") in EVENT_FATAL:
                termination = "panic" if event.get("event") == "PANIC" else "timeout"
                raise RunnerError("guest_fatal_no_boot", termination)
        if time.monotonic() >= retry_at:
            session.send(hello)
            retry_at = time.monotonic() + HELLO_RETRY_INTERVAL
        time.sleep(0.01)
    raise RunnerError("boot_ready_timeout", "timeout")


def wait_for_case(session: QemuSession, case_id: str, iteration: int,
                  seed: int, timeout: float, heartbeat_timeout: float) -> dict[str, str]:
    session.send([
        ("cmd", "RUN"), ("case", case_id), ("iteration", str(iteration)),
        ("seed", str(seed)),
    ])
    deadline = time.monotonic() + timeout
    begun = False
    while time.monotonic() < deadline:
        for event in session.pump():
            if event.get("event") in EVENT_FATAL:
                termination = "panic" if event.get("event") == "PANIC" else "timeout"
                raise RunnerError(event.get("reason", "guest_fatal"), termination)
            if event.get("event") == "BEGIN":
                if event.get("case") != case_id or \
                        event.get("iteration") != str(iteration) or \
                        event.get("seed") != str(seed):
                    session.protocol_errors.append("begin_guest_invalido")
                    continue
                begun = True
                continue
            if event.get("event") not in EVENT_TERMINAL:
                continue
            if event.get("case") != case_id:
                continue
            if event.get("event") in {"PASS", "FAIL", "SKIP"} and not begun:
                session.protocol_errors.append("terminal_sem_begin_guest")
                continue
            return event
        if heartbeat_expired(session.last_heartbeat, time.monotonic(),
                             heartbeat_timeout):
            raise RunnerError("guest_sem_heartbeat", "watchdog")
        time.sleep(0.01)
    raise RunnerError(f"case_timeout:{case_id}", "timeout")


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
    if arguments.seed is not None and not 0 <= arguments.seed <= 0xFFFFFFFF:
        raise RunnerError("seed_invalida", "catalog_error", True)
    if arguments.command != "stress":
        return
    limiters = sum(value is not None for value in
                   (arguments.iterations, arguments.duration))
    if arguments.until_failure:
        limiters += 1
    if limiters != 1:
        raise RunnerError("stress_requer_um_limite", "catalog_error", True)
    if arguments.iterations is not None and arguments.iterations <= 0:
        raise RunnerError("iteracoes_invalidas", "catalog_error", True)
    if arguments.duration is not None and arguments.duration <= 0:
        raise RunnerError("duracao_invalida", "catalog_error", True)


def initialize_artifacts(path: Path) -> None:
    for name in ("serial.log", "qemu.stdout.log", "qemu.stderr.log"):
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
            "fixture": arguments.fixture,
            "seed": seed,
            "command": session.command(),
            "snapshot": bool(arguments.snapshot),
        }
        write_json(artifact_dir / "manifest.json", manifest)
        session.start()
        wait_for_ready(session, run_id)
        stress_started = time.monotonic()
        suite_deadline = time.monotonic() + arguments.suite_timeout if arguments.command == "run" else None
        iteration = 0
        while True:
            if arguments.command == "run":
                if iteration >= len(selected):
                    break
                case = selected[iteration]
                case_iteration = 0
            else:
                if arguments.iterations and iteration >= arguments.iterations:
                    break
                if arguments.duration and time.monotonic() - stress_started >= arguments.duration:
                    break
                case = selected[0]
                case_iteration = iteration
            if suite_deadline and time.monotonic() >= suite_deadline:
                raise RunnerError("suite_timeout", "timeout")
            case_id = str(case.get("guest_case", case["id"]))
            event = wait_for_case(
                session, case_id, case_iteration, seed,
                case_timeout(case, arguments.case_timeout),
                float(case.get("heartbeat_timeout_seconds",
                               arguments.heartbeat_timeout)),
            )
            case_result = dict(event)
            case_result["catalog_case"] = str(case["id"])
            case_result["status"] = event.get("event", "BLOCKED")
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
        status, termination, cause = result_status(cases, error)
        report = {
            "schema": "zephyros-test-result-v1",
            "run_id": run_id,
            "status": status,
            "termination": termination,
            "cause": cause,
            "profile": arguments.profile,
            "fixture": arguments.fixture,
            "seed": seed,
            "iteration_completed": len(cases),
            "duration_seconds": round(time.monotonic() - started, 6),
            "image": str(image),
            "image_sha256": image_hash,
            "qemu_exit_code": exit_code,
            "qmp_status": session.qmp_status if session else None,
            "protocol_errors": session.protocol_errors if session else [],
            "cases": cases,
            "artifacts": {
                "manifest": "manifest.json",
                "serial": "serial.log",
                "stdout": "qemu.stdout.log",
                "stderr": "qemu.stderr.log",
            },
        }
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
        subparser.add_argument("--network", default="user,model=e1000")
        subparser.add_argument("--no-snapshot", dest="snapshot", action="store_false")
        subparser.set_defaults(snapshot=True)
        subparser.add_argument("--fixture")
        subparser.add_argument("--profile", default="smoke")
        subparser.add_argument("--boot-timeout", type=float, default=BOOT_TIMEOUT_DEFAULT)
        subparser.add_argument("--case-timeout", type=float, default=CASE_TIMEOUT_DEFAULT)
        subparser.add_argument("--suite-timeout", type=float, default=SUITE_TIMEOUT_DEFAULT)
        subparser.add_argument("--heartbeat-timeout", type=float, default=PROTOCOL_HEARTBEAT_DEFAULT)
        subparser.add_argument("--seed", type=int)
        if name == "stress":
            subparser.add_argument("--case")
            subparser.add_argument("--iterations", type=int)
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
