#!/usr/bin/env python3
"""Empacotador e verificador host para os contratos ZUPD v1 e v2."""

from __future__ import annotations

import argparse
import getpass
import hashlib
import http.server
import json
import re
import ssl
import subprocess
import struct
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = REPO_ROOT / "docs" / "14-atualizacoes" / "contrato-zupd-v1.md"
U1_FIXTURES = REPO_ROOT / "docs" / "fixtures" / "updates" / "v1"
U2_FIXTURES = REPO_ROOT / "docs" / "fixtures" / "updates" / "u2"
U3_FIXTURES = REPO_ROOT / "docs" / "fixtures" / "updates" / "u3"
U5_FIXTURES = REPO_ROOT / "docs" / "fixtures" / "updates" / "u5"
RELEASE_PUBLIC = REPO_ROOT / "config" / "update-release-public.json"
REMOTE_CONFIG = REPO_ROOT / "config" / "update-remote.json"
VERSION_HEADER = REPO_ROOT / "src" / "include" / "core" / "version.h"
SHELL_BMP = REPO_ROOT / "assets" / "icons" / "SHELL.BMP"
UPDATE_ASSETS = {
    name: REPO_ROOT / "assets" / "icons" / name
    for name in ("EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP")
}

ZUPD_MAGIC = b"ZUPD"
ZUPD_FORMAT_VERSION = 1
ZUPD_ARCH_I386 = 1
ZUPD_HEADER_SIZE = 128
ZUPD_ENTRY_SIZE = 128
ZUPD_SIGNATURE_SIZE = 64
ZUPD_SIGNATURE_ED25519 = 1
ZUPD_HASH_SHA256 = 1
ZUPD_OPERATION_REPLACE = 1
ZUPD_COMPRESSION_NONE = 0
ZUPD_TARGET_SYSTEM_FILE = 1
ZUPD_MIN_ENTRIES = 1
ZUPD_MAX_ENTRIES = 16
ZUPD_MAX_TOTAL_SIZE = 128 * 1024
ZUPD_MAX_PAYLOAD_SIZE = 64 * 1024
ZUPD_DOMAIN = b"ZEPHYROS-UPDATE-V1\0"
ZUPD_ALLOWLIST = ("EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP")

HEADER = struct.Struct("<4sHH8I12H2I16s32s8s")
ENTRY = struct.Struct("<64sIIIHHHH32s12s")
VERSION_RE = re.compile(r"^([0-9]+)\.([0-9]+)\.([0-9]+)$")
RELEASE_ID_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
FAT_PATH_RE = re.compile(r"^[A-Z0-9_]{1,8}\.[A-Z0-9]{1,3}$")
DEFINE_RE = re.compile(r"^#define\s+(ZEPHYROS_VERSION_[A-Z]+)\s+([0-9]+)U?\s*$")

UPDATE_CONTROL_SIZE = 512
UPDATE_CONTROL_VERSION = 1
UPDATE_CONTROL_HASH_OFFSET = 480
UPDATE_TARGET_COUNT = 3
UPDATE_STATE_CURRENT_OFFSET = 40
UPDATE_STATE_ROLLBACK_OFFSET = 160
UPDATE_STATE_FILE_SIZE = 40
UPDATE_JOURNAL_ENTRY_OFFSET = 48
UPDATE_JOURNAL_ENTRY_SIZE = 76
UPDATE_JOURNAL_NONE = 0
UPDATE_JOURNAL_APPLY = 1
UPDATE_JOURNAL_ROLLBACK = 2
UPDATE_PHASE_NONE = 0
UPDATE_PHASE_PREPARED = 1
UPDATE_PHASE_REPLACING = 2
UPDATE_PHASE_COMMITTED = 3
UPDATE_TARGETS = ("EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP")
UPDATE_STATE_ALIASES = ("ZUPD0.STA", "ZUPD1.STA")
UPDATE_JOURNAL_ALIASES = ("ZUPD0.JRN", "ZUPD1.JRN")
UPDATE_HISTORY_ALIASES = ("ZUPD0.HIS", "ZUPD1.HIS")
UPDATE_HISTORY_MAX_ENTRIES = 8
UPDATE_HISTORY_HEADER_SIZE = 32
UPDATE_HISTORY_ENTRY_SIZE = 56
UPDATE_HISTORY_ENTRY_OFFSET = 32
UPDATE_HISTORY_FLAG_REBOOT = 0x01
UPDATE_HISTORY_OPERATION_NAMES = {
    1: "apply",
    2: "rollback",
    3: "recovery-apply",
    4: "recovery-rollback",
}
UPDATE_HISTORY_OUTCOME_NAMES = {
    1: "success",
    2: "failed",
    3: "cancelled",
    4: "recovered",
}
UPDATE_BACKUP_ALIASES = (
    ("ZBA0.BAK", "ZBA1.BAK", "ZBA2.BAK"),
    ("ZBB0.BAK", "ZBB1.BAK", "ZBB2.BAK"),
)
UPDATE_STAGE_ALIASES = (
    ("ZSA0.NEW", "ZSA1.NEW", "ZSA2.NEW"),
    ("ZSB0.NEW", "ZSB1.NEW", "ZSB2.NEW"),
)

REMOTE_MAGIC = b"ZUM1"
REMOTE_FORMAT_VERSION = 1
REMOTE_MANIFEST_SIZE = 256
REMOTE_SIGNED_SIZE = 192
REMOTE_ARCH_I386 = 1
REMOTE_CHANNEL_STABLE = 1
REMOTE_DOMAIN = b"ZEPHYROS-REMOTE-V1\0"
REMOTE_PATH_SIZE = 100
REMOTE_RECORD_MAGIC = b"ZUR1"
REMOTE_RECORD_VERSION = 1
REMOTE_RECORD_SIZE = 512
REMOTE_RECORD_HASH_OFFSET = 480
REMOTE_RECORD_RESERVED_OFFSET = 108
REMOTE_RECORD_ALIASES = ("ZUR0.STA", "ZUR1.STA")
REMOTE_PACKAGE_ALIASES = ("ZUR0.ZUP", "ZUR1.ZUP")
REMOTE_SLOT_NONE = 0xFF
REMOTE_PHASE_CLEAN = 0
REMOTE_PHASE_DOWNLOADING = 1
REMOTE_ATTRIBUTES = 0x26
REMOTE_DEFAULT_URL = "http://10.0.2.2:8000/zephyros/stable.zum"

RUNTIME_MAGIC = b"ZUM2"
RUNTIME_PACKAGE_MAGIC = b"ZUPD"
RUNTIME_FORMAT_VERSION = 2
RUNTIME_ARCH_I386 = 1
RUNTIME_MANIFEST_SIZE = 4096
RUNTIME_MANIFEST_SIGNED_SIZE = 4032
RUNTIME_MANIFEST_BASE_OFFSET = 256
RUNTIME_MANIFEST_BASE_SIZE = 16
RUNTIME_MANIFEST_MAX_BASES = 8
RUNTIME_MANIFEST_ENTRY_OFFSET = 384
RUNTIME_MANIFEST_ENTRY_SIZE = 224
RUNTIME_MAX_ENTRIES = 16
RUNTIME_PACKAGE_HEADER_SIZE = 128
RUNTIME_PACKAGE_ENTRY_SIZE = 128
RUNTIME_PACKAGE_MAX_SIZE = 128 * 1024
RUNTIME_FILE_MAX_SIZE = 64 * 1024
RUNTIME_PATH_SIZE = 64
RUNTIME_ASSET_NAME_SIZE = 64
RUNTIME_SIGNATURE_SIZE = 64
RUNTIME_DOMAIN = b"ZEPHYROS-RUNTIME-MANIFEST-V2\0"
RUNTIME_PACKAGE_DOMAIN = b"ZEPHYROS-RUNTIME-PACKAGE-V2\0"
RUNTIME_OPERATION_REPLACE = 0x01
RUNTIME_OPERATION_CREATE = 0x02
RUNTIME_OPERATION_DELETE = 0x04
RUNTIME_CATALOG = ("EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP")
RUNTIME_MANIFEST_ALIASES = ("ZRV0.MAN", "ZRV1.MAN")
RUNTIME_PACKAGE_ALIASES = ("ZRV0.PKG", "ZRV1.PKG")
RUNTIME_RECORD_ALIASES = ("ZRV0.STA", "ZRV1.STA")
RUNTIME_ASSET_ATTRIBUTE = 0x26
RUNTIME_RECORD_SIZE = 512
RUNTIME_RECORD_HASH_OFFSET = 480
RUNTIME_RECORD_MAGIC = b"ZRV2"
RUNTIME_STATE_ALIASES = ("ZTV0.STA", "ZTV1.STA")
RUNTIME_JOURNAL_ALIASES = ("ZTV0.JRN", "ZTV1.JRN")
RUNTIME_STAGE_PREFIXES = ("ZTS0.", "ZTS1.")
RUNTIME_BACKUP_PREFIXES = ("ZTB0.", "ZTB1.")
RUNTIME_CONTROL_VERSION = 1
RUNTIME_CONTROL_SIZE = 4096
RUNTIME_CONTROL_HASH_OFFSET = 4064
RUNTIME_STATE_CURRENT_OFFSET = 64
RUNTIME_STATE_ROLLBACK_OFFSET = 704
RUNTIME_FILE_STATE_SIZE = 40
RUNTIME_JOURNAL_HEADER_SIZE = 64
RUNTIME_JOURNAL_ENTRY_OFFSET = 64
RUNTIME_JOURNAL_ENTRY_SIZE = 80
RUNTIME_JOURNAL_ENTRY_OLD = 0
RUNTIME_JOURNAL_ENTRY_NEW = 40
RUNTIME_JOURNAL_NONE = 0
RUNTIME_JOURNAL_APPLY = 1
RUNTIME_JOURNAL_ROLLBACK = 2
RUNTIME_PHASE_NONE = 0
RUNTIME_PHASE_PREPARED = 1
RUNTIME_PHASE_STAGING = 2
RUNTIME_PHASE_REPLACING = 3
RUNTIME_PHASE_COMMITTED = 4
RUNTIME_SLOT_NONE = 0xFF

SYSTEM_MAGIC = b"ZSYS"
SYSTEM_FORMAT_VERSION = 1
SYSTEM_ARCH_I386 = 1
SYSTEM_HEADER_SIZE = 1024
SYSTEM_SIGNATURE_SIZE = 64
SYSTEM_KEY_ID_SIZE = 16
SYSTEM_MAX_IMAGE_SIZE = 8 * 1024 * 1024
SYSTEM_PAYLOAD_OFFSET = SYSTEM_HEADER_SIZE
SYSTEM_DOMAIN = b"ZEPHYROS-SYSTEM-IMAGE-V1\0"
SYSTEM_COMPONENT_ENTRY_SIZE = 64
SYSTEM_COMPONENT_COUNT = 3
SYSTEM_COMPONENT_BOOT = 1
SYSTEM_COMPONENT_STAGE2 = 2
SYSTEM_COMPONENT_KERNEL = 3
SYSTEM_COMPONENT_HASH_OFFSET = 12
SYSTEM_BASE_ENTRY_SIZE = 12
SYSTEM_MAX_BASES = 8
SYSTEM_CHECKPOINT_SIZE = 64
SYSTEM_MAX_CHECKPOINTS = 4
SYSTEM_ROUTE_DIRECT = 1
SYSTEM_ROUTE_CHECKPOINT = 2
SYSTEM_FLAG_REQUIRES_REBOOT = 0x0001
SYSTEM_FLAG_BRIDGE_REQUIRED = 0x0002
SYSTEM_CHANNEL_SIZE = 16
SYSTEM_IDENTIFIER_SIZE = 64
SYSTEM_HEADER_IMAGE_HASH_OFFSET = 28
SYSTEM_HEADER_TARGET_VERSION_OFFSET = 60
SYSTEM_HEADER_TARGET_EPOCH_OFFSET = 66
SYSTEM_HEADER_BASE_COUNT_OFFSET = 70
SYSTEM_HEADER_BASE_ENTRY_SIZE_OFFSET = 72
SYSTEM_HEADER_BASES_OFFSET = 76
SYSTEM_HEADER_MIN_UPDATER_OFFSET = SYSTEM_HEADER_BASES_OFFSET + SYSTEM_MAX_BASES * SYSTEM_BASE_ENTRY_SIZE
SYSTEM_HEADER_BOOT_ABI_OFFSET = SYSTEM_HEADER_MIN_UPDATER_OFFSET + SYSTEM_BASE_ENTRY_SIZE
SYSTEM_HEADER_SCHEMA_FROM_OFFSET = SYSTEM_HEADER_BOOT_ABI_OFFSET + 4
SYSTEM_HEADER_SCHEMA_TO_OFFSET = SYSTEM_HEADER_SCHEMA_FROM_OFFSET + 4
SYSTEM_HEADER_ROUTE_OFFSET = SYSTEM_HEADER_SCHEMA_TO_OFFSET + 4
SYSTEM_HEADER_CHECKPOINT_COUNT_OFFSET = SYSTEM_HEADER_ROUTE_OFFSET + 1
SYSTEM_HEADER_CHANNEL_OFFSET = SYSTEM_HEADER_CHECKPOINT_COUNT_OFFSET + 1
SYSTEM_HEADER_RELEASE_ID_OFFSET = SYSTEM_HEADER_CHANNEL_OFFSET + SYSTEM_CHANNEL_SIZE
SYSTEM_HEADER_RELEASE_TAG_OFFSET = SYSTEM_HEADER_RELEASE_ID_OFFSET + SYSTEM_IDENTIFIER_SIZE
SYSTEM_HEADER_COMPONENT_COUNT_OFFSET = SYSTEM_HEADER_RELEASE_TAG_OFFSET + SYSTEM_IDENTIFIER_SIZE
SYSTEM_HEADER_COMPONENT_ENTRY_SIZE_OFFSET = SYSTEM_HEADER_COMPONENT_COUNT_OFFSET + 2
SYSTEM_HEADER_COMPONENTS_OFFSET = SYSTEM_HEADER_COMPONENT_ENTRY_SIZE_OFFSET + 2
SYSTEM_HEADER_CHECKPOINT_ENTRY_SIZE_OFFSET = SYSTEM_HEADER_COMPONENTS_OFFSET + SYSTEM_COMPONENT_COUNT * SYSTEM_COMPONENT_ENTRY_SIZE
SYSTEM_HEADER_CHECKPOINTS_OFFSET = SYSTEM_HEADER_CHECKPOINT_ENTRY_SIZE_OFFSET + 2
SYSTEM_HEADER_KEY_ID_OFFSET = SYSTEM_HEADER_CHECKPOINTS_OFFSET + SYSTEM_MAX_CHECKPOINTS * SYSTEM_CHECKPOINT_SIZE
SYSTEM_HEADER_SIGNATURE_OFFSET = SYSTEM_HEADER_KEY_ID_OFFSET + SYSTEM_KEY_ID_SIZE
SYSTEM_HEADER_SIGNATURE_SIZE_OFFSET = SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE
SYSTEM_HEADER_SIGNATURE_OFFSET_FIELD = SYSTEM_HEADER_SIGNATURE_SIZE_OFFSET + 4
SYSTEM_HEADER_RESERVED_OFFSET = SYSTEM_HEADER_SIGNATURE_OFFSET_FIELD + 4

REASON_NONE = "NONE"
REASON_FORMAT = "FORMAT"
REASON_SIZE = "SIZE"
REASON_HASH = "HASH"
REASON_UNKNOWN_KEY = "UNKNOWN_KEY"
REASON_SIGNATURE = "SIGNATURE"
REASON_ARCHITECTURE = "ARCHITECTURE"
REASON_BASE_VERSION = "BASE_VERSION"
REASON_DOWNGRADE = "DOWNGRADE"
REASON_PATH_POLICY = "PATH_POLICY"
REASON_COMPATIBILITY = "COMPATIBILITY"
REASON_DUPLICATE_TARGET = "DUPLICATE_TARGET"
REASON_UNSUPPORTED = "UNSUPPORTED"

REASON_VALUES = {
    REASON_NONE: 0,
    REASON_FORMAT: 1,
    REASON_SIZE: 2,
    REASON_HASH: 3,
    REASON_UNKNOWN_KEY: 4,
    REASON_SIGNATURE: 5,
    REASON_ARCHITECTURE: 6,
    REASON_BASE_VERSION: 7,
    REASON_DOWNGRADE: 8,
    REASON_PATH_POLICY: 9,
    REASON_DUPLICATE_TARGET: 10,
    REASON_UNSUPPORTED: 11,
    REASON_COMPATIBILITY: 12,
}


class UpdateError(ValueError):
    """Erro controlado de entrada, chave ou artefato."""


class Rejection(UpdateError):
    """Rejeicao ZUPD com motivo publico estavel."""

    def __init__(self, reason: str, message: str):
        super().__init__(message)
        self.reason = reason


@dataclass(frozen=True, order=True)
class Version:
    """Versao semantica numerica limitada aos campos uint16_t."""

    major: int
    minor: int
    patch: int

    @classmethod
    def parse(cls, text: str) -> "Version":
        match = VERSION_RE.fullmatch(text) if isinstance(text, str) else None
        if not match:
            raise UpdateError("versao deve usar MAJOR.MINOR.PATCH")
        values = tuple(int(item) for item in match.groups())
        if any(item > 0xFFFF for item in values):
            raise UpdateError("componente de versao excede uint16")
        return cls(*values)

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


@dataclass(frozen=True)
class PublicKeyInfo:
    """Chave publica Ed25519 e identificador do contrato."""

    public_key: bytes
    key_id: bytes


@dataclass(frozen=True)
class EntryInfo:
    """Entrada ZUPD ja validada estruturalmente."""

    path: str
    payload_offset: int
    payload_size: int
    payload_sha256: bytes


@dataclass(frozen=True)
class ArtifactInfo:
    """Metadados autenticados retornados pelo verificador."""

    base_version: Version
    target_version: Version
    base_epoch: int
    target_epoch: int
    total_size: int
    entries: tuple[EntryInfo, ...]


@dataclass(frozen=True)
class StoredFileState:
    """Tamanho e hash persistidos para um arquivo controlado pela U3."""

    size: int
    sha256: bytes
    present: bool


@dataclass(frozen=True)
class StoredUpdateState:
    """Estado redundante instalado decodificado de ZUPD*.STA."""

    sequence: int
    installed_version: Version
    installed_epoch: int
    rollback_available: bool
    rollback_slot: int
    rollback_entry_count: int
    previous_version: Version
    previous_epoch: int
    current: tuple[StoredFileState, ...]
    rollback: tuple[StoredFileState, ...]


@dataclass(frozen=True)
class StoredJournal:
    """Resumo suficiente para auditar um registro ZUPD*.JRN."""

    sequence: int
    kind: int
    phase: int
    slot: int
    entry_count: int
    progress: int


@dataclass(frozen=True)
class StoredHistoryEntry:
    """Evento persistido em um slot de ZUPD*.HIS."""

    sequence: int
    operation: int
    outcome: int
    action_reason: int
    verification_reason: int
    from_version: Version
    to_version: Version
    from_epoch: int
    to_epoch: int
    entry_count: int
    completed_entries: int
    reboot_required: bool
    package_alias: str


@dataclass(frozen=True)
class StoredHistory:
    """Ring buffer redundante decodificado de ZUPD*.HIS."""

    sequence: int
    count: int
    next_index: int
    entries: tuple[StoredHistoryEntry | None, ...]


@dataclass(frozen=True)
class RemoteManifest:
    """Manifesto remoto ZUM1 autenticado."""

    generation: int
    base_version: Version
    target_version: Version
    base_epoch: int
    target_epoch: int
    package_size: int
    package_sha256: bytes
    package_path: str


@dataclass(frozen=True)
class RuntimeEntry:
    """Entrada autorizada do manifesto ZUM2."""

    path: str
    target_present: bool
    operations: int
    target_size: int
    target_sha256: bytes
    asset_name: str
    asset_size: int
    asset_sha256: bytes


@dataclass(frozen=True)
class RuntimeManifest:
    """Manifesto ZUM2 validado, sem confiar no descritor de transporte."""

    generation: int
    target_version: Version
    target_epoch: int
    base_versions: tuple[tuple[Version, int], ...]
    package_size: int
    package_sha256: bytes
    release_tag: str
    release_id: str
    entries: tuple[RuntimeEntry, ...]


@dataclass(frozen=True)
class SystemBase:
    """Versao/epoch aceitos como origem da imagem ZSYS."""

    version: Version
    epoch: int


@dataclass(frozen=True)
class SystemComponent:
    """Componente autenticado dentro da imagem completa."""

    kind: int
    offset: int
    size: int
    sha256: bytes


@dataclass(frozen=True)
class SystemCompatibility:
    """Metadados assinados que condicionam a ativacao futura."""

    supported_from: tuple[SystemBase, ...]
    minimum_updater: SystemBase
    boot_abi: int
    data_schema_from: int
    data_schema_to: int
    requires_reboot: bool
    channel: str
    route_kind: int
    checkpoints: tuple[str, ...]
    bridge_required: bool


@dataclass(frozen=True)
class SystemManifest:
    """Especificacao host usada para construir um ZSYS v1."""

    release_id: str
    release_tag: str
    target_version: Version
    target_epoch: int
    compatibility: SystemCompatibility


@dataclass(frozen=True)
class SystemPackage:
    """ZSYS v1 validado por estrutura, hash e assinatura."""

    release_id: str
    release_tag: str
    target_version: Version
    target_epoch: int
    image_size: int
    image_sha256: bytes
    components: tuple[SystemComponent, ...]
    compatibility: SystemCompatibility


@dataclass(frozen=True)
class RuntimePackage:
    """Pacote ZUPD v2 validado estruturalmente e por assinatura."""

    generation: int
    base_version: Version
    base_epoch: int
    target_version: Version
    target_epoch: int
    entries: tuple["RuntimePackageEntry", ...]
    total_size: int


@dataclass(frozen=True)
class RuntimePackageEntry:
    """Entrada de payload do pacote ZUPD v2."""

    path: str
    present: bool
    operation: int
    payload_offset: int
    payload_size: int
    payload_sha256: bytes


@dataclass(frozen=True)
class RemoteRecord:
    """Estado redundante do cache remoto U5."""

    sequence: int
    phase: int
    active_slot: int
    pending_slot: int
    manifest_generation: int
    package_size: int
    package_sha256: bytes
    manifest_sha256: bytes
    base_version: Version
    target_version: Version
    base_epoch: int
    target_epoch: int


@dataclass(frozen=True)
class RuntimeRemoteRecord:
    """Estado redundante dos slots remotos ZRV2."""

    sequence: int
    phase: int
    active_slot: int
    pending_slot: int
    mode: int
    entry_count: int
    asset_mask: int
    manifest_sha256: bytes
    package_sha256: bytes
    package_size: int
    target_version: Version
    target_epoch: int


@dataclass(frozen=True)
class RuntimeStoredFileState:
    """Estado de um arquivo no controle local ZRT2/ZRTJ."""

    size: int
    sha256: bytes
    present: bool


@dataclass(frozen=True)
class RuntimeStoredState:
    """Estado instalado e rollback decodificado do runtime v2."""

    sequence: int
    installed_version: Version
    installed_epoch: int
    rollback_available: bool
    rollback_slot: int
    entry_count: int
    rollback_entry_count: int
    previous_version: Version
    previous_epoch: int
    current: tuple[RuntimeStoredFileState, ...]
    rollback: tuple[RuntimeStoredFileState, ...]


@dataclass(frozen=True)
class RuntimeStoredPlanEntry:
    """Entrada de journal local com estados anterior e desejado."""

    catalog_index: int
    operation: int
    old_state: RuntimeStoredFileState
    new_state: RuntimeStoredFileState


@dataclass(frozen=True)
class RuntimeStoredJournal:
    """Journal local ZRTJ decodificado para auditoria offline."""

    sequence: int
    phase: int
    kind: int
    slot: int
    progress: int
    entry_count: int
    base_version: Version
    base_epoch: int
    target_version: Version
    target_epoch: int
    entries: tuple[RuntimeStoredPlanEntry, ...]


@dataclass(frozen=True)
class ReleaseBundle:
    """Release host autenticada e coerente com sua origem Git."""

    release_id: str
    release_name: str
    source_commit: str
    tag: str | None
    target_version: Version
    minimum_version: Version
    base_epoch: int
    target_epoch: int
    package_name: str
    manifest_name: str
    package_sha256: bytes
    manifest_sha256: bytes


def crypto_modules() -> tuple[Any, Any, Any]:
    """Carrega cryptography apenas nos comandos que realmente dependem dela."""
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ed25519
    except ImportError as error:
        raise UpdateError(
            "dependencia ausente: python -m pip install -r "
            "tools/requirements-updater.txt"
        ) from error
    return ed25519, serialization, InvalidSignature


def is_within(path: Path, directory: Path) -> bool:
    """Indica se um caminho resolvido esta dentro de um diretorio."""
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def require_new_file(path: Path, label: str) -> Path:
    """Recusa sobrescrita e exige que o diretorio pai exista."""
    resolved = path.expanduser().resolve()
    if resolved.exists():
        raise UpdateError(f"{label} ja existe: {resolved}")
    if not resolved.parent.is_dir():
        raise UpdateError(f"diretorio de {label} nao existe: {resolved.parent}")
    return resolved


def validate_private_output(path: Path) -> Path:
    """Impede que uma nova chave privada seja criada dentro do repositorio."""
    resolved = require_new_file(path, "chave privada")
    if is_within(resolved, REPO_ROOT):
        raise UpdateError("a chave privada deve ficar fora do repositorio")
    return resolved


def validate_private_input(path: Path) -> Path:
    """Exige que uma chave privada existente permaneca fora do repositorio."""
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise UpdateError(f"chave privada ausente: {resolved}")
    if is_within(resolved, REPO_ROOT):
        raise UpdateError("a chave privada deve ficar fora do repositorio")
    return resolved


def write_new_bytes(path: Path, data: bytes) -> None:
    """Grava um arquivo novo sem permitir substituicao acidental."""
    try:
        with path.open("xb") as output:
            output.write(data)
    except OSError as error:
        raise UpdateError(f"nao foi possivel criar {path}") from error


def write_new_text(path: Path, text: str) -> None:
    """Grava texto UTF-8 novo sem sobrescrever arquivo existente."""
    write_new_bytes(path, text.encode("utf-8"))


def public_key_info(public_key: bytes) -> PublicKeyInfo:
    """Calcula o key_id ZUPD de uma chave publica crua."""
    if len(public_key) != 32:
        raise UpdateError("chave publica Ed25519 deve ter 32 bytes")
    return PublicKeyInfo(public_key, hashlib.sha256(public_key).digest()[:16])


def public_json(info: PublicKeyInfo) -> str:
    """Serializa a raiz publica em formato estavel e sem dados privados."""
    data = {
        "format": "zephyros-ed25519-public-v1",
        "algorithm": "Ed25519",
        "public_key_hex": info.public_key.hex(),
        "key_id_hex": info.key_id.hex(),
    }
    return json.dumps(data, indent=2, sort_keys=False) + "\n"


def load_public_json(path: Path) -> PublicKeyInfo:
    """Le e valida o arquivo publico versionavel."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError(f"nao foi possivel ler a chave publica {path}") from error
    if not isinstance(data, dict) or tuple(data) != (
        "format",
        "algorithm",
        "public_key_hex",
        "key_id_hex",
    ):
        raise UpdateError("arquivo de chave publica possui campos invalidos")
    if data["format"] != "zephyros-ed25519-public-v1" or data["algorithm"] != "Ed25519":
        raise UpdateError("formato ou algoritmo da chave publica invalido")
    try:
        public_key = bytes.fromhex(data["public_key_hex"])
        declared_key_id = bytes.fromhex(data["key_id_hex"])
    except (TypeError, ValueError) as error:
        raise UpdateError("chave publica nao usa hexadecimal valido") from error
    info = public_key_info(public_key)
    if len(declared_key_id) != 16 or declared_key_id != info.key_id:
        raise UpdateError("key_id da chave publica diverge do SHA-256")
    return info


def load_private_key(path: Path, password: bytes) -> Any:
    """Carrega exclusivamente uma chave Ed25519 privada criptografada."""
    _, serialization, _ = crypto_modules()
    try:
        key = serialization.load_pem_private_key(path.read_bytes(), password=password)
    except (OSError, TypeError, ValueError) as error:
        raise UpdateError("chave privada ou senha invalida") from error
    ed25519, _, _ = crypto_modules()
    if not isinstance(key, ed25519.Ed25519PrivateKey):
        raise UpdateError("a chave privada nao e Ed25519")
    return key


def private_public_info(private_key: Any) -> PublicKeyInfo:
    """Extrai somente os bytes publicos de uma chave privada carregada."""
    _, serialization, _ = crypto_modules()
    raw = private_key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    return public_key_info(raw)


def prompt_new_password() -> bytes:
    """Solicita e confirma uma senha forte sem eco no terminal."""
    first = getpass.getpass("Senha da nova chave (minimo 16 caracteres): ")
    second = getpass.getpass("Confirme a senha: ")
    if first != second:
        raise UpdateError("as senhas nao coincidem")
    if len(first) < 16:
        raise UpdateError("a senha deve ter pelo menos 16 caracteres")
    return first.encode("utf-8")


def prompt_password() -> bytes:
    """Solicita a senha de uma chave existente sem eco."""
    password = getpass.getpass("Senha da chave privada: ")
    if not password:
        raise UpdateError("senha vazia")
    return password.encode("utf-8")


def generate_key(private_path: Path, public_path: Path, password: bytes) -> PublicKeyInfo:
    """Gera e grava um par Ed25519 com parte privada criptografada."""
    private_output = validate_private_output(private_path)
    public_output = require_new_file(public_path, "chave publica")
    if len(password.decode("utf-8")) < 16:
        raise UpdateError("a senha deve ter pelo menos 16 caracteres")
    ed25519, serialization, _ = crypto_modules()
    private_key = ed25519.Ed25519PrivateKey.generate()
    private_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.BestAvailableEncryption(password),
    )
    info = private_public_info(private_key)
    write_new_bytes(private_output, private_pem)
    try:
        write_new_text(public_output, public_json(info))
    except Exception:
        raise UpdateError(
            "chave privada criada, mas a chave publica falhou; "
            f"preserve e revise {private_output}"
        )
    return info


def encode_path(path: str) -> bytes:
    """Codifica um caminho FAT canonico no campo fixo de 64 bytes."""
    if not isinstance(path, str) or not FAT_PATH_RE.fullmatch(path):
        raise UpdateError(f"caminho FAT nao canonico: {path!r}")
    raw = path.encode("ascii")
    if len(raw) >= 64:
        raise UpdateError("caminho excede o campo ZUPD")
    return raw + b"\0" * (64 - len(raw))


def manifest_data(path: Path) -> dict[str, Any]:
    """Le um manifesto host com conjunto exato de campos."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError(f"nao foi possivel ler o manifesto {path}") from error
    required = (
        "format",
        "architecture",
        "base_version",
        "target_version",
        "base_epoch",
        "target_epoch",
        "files",
    )
    if not isinstance(data, dict) or tuple(data) != required:
        raise UpdateError("manifesto possui campos ausentes, extras ou fora de ordem")
    if data["format"] != "ZUPD v1" or data["architecture"] != "i386":
        raise UpdateError("formato ou arquitetura do manifesto invalido")
    return data


def validate_manifest(data: dict[str, Any], directory: Path) -> tuple[Version, Version, int, int, list[tuple[str, bytes]]]:
    """Valida o manifesto e carrega payloads permitidos."""
    base = Version.parse(data["base_version"])
    target = Version.parse(data["target_version"])
    if target <= base:
        raise UpdateError("versao alvo deve ser superior a versao base")
    base_epoch = data["base_epoch"]
    target_epoch = data["target_epoch"]
    if not isinstance(base_epoch, int) or not isinstance(target_epoch, int):
        raise UpdateError("epochs devem ser inteiros")
    if not 0 <= base_epoch <= 0xFFFFFFFF or not 0 <= target_epoch <= 0xFFFFFFFF:
        raise UpdateError("epoch excede uint32")
    if target_epoch < base_epoch:
        raise UpdateError("epoch alvo nao pode diminuir")
    files = data["files"]
    if not isinstance(files, list) or not ZUPD_MIN_ENTRIES <= len(files) <= ZUPD_MAX_ENTRIES:
        raise UpdateError("manifesto deve conter de 1 a 16 arquivos")
    loaded: list[tuple[str, bytes]] = []
    seen: set[str] = set()
    for item in files:
        if not isinstance(item, dict) or tuple(item) != ("path", "source"):
            raise UpdateError("entrada de arquivo deve conter path e source")
        path = item["path"]
        encode_path(path)
        if path not in ZUPD_ALLOWLIST:
            raise UpdateError(f"caminho fora da allowlist: {path}")
        if path in seen:
            raise UpdateError(f"caminho duplicado: {path}")
        if not isinstance(item["source"], str) or not item["source"]:
            raise UpdateError("source deve ser um caminho textual")
        source = (directory / item["source"]).resolve()
        try:
            payload = source.read_bytes()
        except OSError as error:
            raise UpdateError(f"nao foi possivel ler payload {source}") from error
        if not 1 <= len(payload) <= ZUPD_MAX_PAYLOAD_SIZE:
            raise UpdateError(f"payload {path} excede os limites")
        loaded.append((path, payload))
        seen.add(path)
    loaded.sort(key=lambda item: item[0].encode("ascii"))
    return base, target, base_epoch, target_epoch, loaded


def pack_header(
    *,
    total_size: int,
    entry_count: int,
    payload_size: int,
    base: Version,
    target: Version,
    base_epoch: int,
    target_epoch: int,
    key_id: bytes,
    content_hash: bytes,
) -> bytes:
    """Serializa o header ZUPD v1 sem padding implicito."""
    manifest_size = entry_count * ZUPD_ENTRY_SIZE
    payload_offset = ZUPD_HEADER_SIZE + manifest_size
    signature_offset = payload_offset + payload_size
    return HEADER.pack(
        ZUPD_MAGIC,
        ZUPD_FORMAT_VERSION,
        ZUPD_HEADER_SIZE,
        ZUPD_ARCH_I386,
        0,
        total_size,
        ZUPD_HEADER_SIZE,
        manifest_size,
        payload_offset,
        payload_size,
        signature_offset,
        ZUPD_SIGNATURE_SIZE,
        ZUPD_SIGNATURE_ED25519,
        ZUPD_HASH_SHA256,
        entry_count,
        ZUPD_ENTRY_SIZE,
        0,
        base.major,
        base.minor,
        base.patch,
        target.major,
        target.minor,
        target.patch,
        base_epoch,
        target_epoch,
        key_id,
        content_hash,
        bytes(8),
    )


def build_from_parts(
    private_key: Any,
    base: Version,
    target: Version,
    base_epoch: int,
    target_epoch: int,
    files: list[tuple[str, bytes]],
) -> bytes:
    """Monta e assina um artefato ZUPD a partir de dados ja validados."""
    payload_offset = ZUPD_HEADER_SIZE + len(files) * ZUPD_ENTRY_SIZE
    next_offset = payload_offset
    entries: list[bytes] = []
    payloads: list[bytes] = []
    for path, payload in files:
        entries.append(
            ENTRY.pack(
                encode_path(path),
                next_offset,
                len(payload),
                len(payload),
                ZUPD_OPERATION_REPLACE,
                ZUPD_COMPRESSION_NONE,
                ZUPD_TARGET_SYSTEM_FILE,
                0,
                hashlib.sha256(payload).digest(),
                bytes(12),
            )
        )
        payloads.append(payload)
        next_offset += len(payload)
    table = b"".join(entries)
    payload_blob = b"".join(payloads)
    content_hash = hashlib.sha256(table + payload_blob).digest()
    info = private_public_info(private_key)
    total_size = next_offset + ZUPD_SIGNATURE_SIZE
    if total_size > ZUPD_MAX_TOTAL_SIZE:
        raise UpdateError("artefato excede 128 KiB")
    header = pack_header(
        total_size=total_size,
        entry_count=len(files),
        payload_size=len(payload_blob),
        base=base,
        target=target,
        base_epoch=base_epoch,
        target_epoch=target_epoch,
        key_id=info.key_id,
        content_hash=content_hash,
    )
    unsigned = header + table + payload_blob
    return unsigned + private_key.sign(ZUPD_DOMAIN + unsigned)


def build_artifact(manifest_path: Path, private_key: Any) -> bytes:
    """Constroi um ZUPD a partir do manifesto JSON informado."""
    data = manifest_data(manifest_path)
    base, target, base_epoch, target_epoch, files = validate_manifest(
        data, manifest_path.resolve().parent
    )
    return build_from_parts(
        private_key, base, target, base_epoch, target_epoch, files
    )


def reject(reason: str, message: str) -> None:
    """Interrompe a validacao com um motivo publico."""
    raise Rejection(reason, message)


def decode_path(raw: bytes) -> str:
    """Valida terminacao, padding e sintaxe do caminho FAT."""
    nul = raw.find(b"\0")
    if nul <= 0 or any(raw[nul + 1 :]):
        reject(REASON_FORMAT, "campo path sem terminacao ou padding canonico")
    try:
        path = raw[:nul].decode("ascii")
    except UnicodeDecodeError:
        reject(REASON_PATH_POLICY, "path nao usa ASCII")
    if not FAT_PATH_RE.fullmatch(path):
        reject(REASON_PATH_POLICY, "path FAT nao canonico")
    return path


def parse_header_values(data: bytes) -> tuple[Any, ...]:
    """Decodifica o header depois de validar os limites externos."""
    minimum_size = ZUPD_HEADER_SIZE + ZUPD_ENTRY_SIZE + ZUPD_SIGNATURE_SIZE + 1
    if len(data) < minimum_size:
        reject(REASON_SIZE, "artefato menor que o minimo")
    if len(data) > ZUPD_MAX_TOTAL_SIZE:
        reject(REASON_SIZE, "artefato excede 128 KiB")
    try:
        return HEADER.unpack_from(data)
    except struct.error:
        reject(REASON_SIZE, "header truncado")


def parse_header_structure(data: bytes) -> dict[str, Any]:
    """Valida o layout do header antes da tabela de entradas."""
    (
        magic,
        format_version,
        header_size,
        architecture,
        flags,
        total_size,
        manifest_offset,
        manifest_size,
        payload_offset,
        payload_size,
        signature_offset,
        signature_size,
        signature_algorithm,
        hash_algorithm,
        entry_count,
        entry_size,
        reserved0,
        base_major,
        base_minor,
        base_patch,
        target_major,
        target_minor,
        target_patch,
        base_epoch,
        target_epoch,
        key_id,
        content_hash,
        reserved,
    ) = parse_header_values(data)
    if total_size != len(data) or not 321 <= total_size <= ZUPD_MAX_TOTAL_SIZE:
        reject(REASON_SIZE, "tamanho real diverge do header")
    if not ZUPD_MIN_ENTRIES <= entry_count <= ZUPD_MAX_ENTRIES:
        reject(REASON_SIZE, "quantidade de entradas invalida")
    if signature_size != ZUPD_SIGNATURE_SIZE:
        reject(REASON_SIZE, "tamanho da assinatura invalido")
    if signature_algorithm != ZUPD_SIGNATURE_ED25519 or hash_algorithm != ZUPD_HASH_SHA256:
        reject(REASON_UNSUPPORTED, "algoritmo ZUPD nao suportado")
    expected_manifest_size = entry_count * ZUPD_ENTRY_SIZE
    expected_payload_offset = ZUPD_HEADER_SIZE + expected_manifest_size
    if (
        magic != ZUPD_MAGIC
        or format_version != ZUPD_FORMAT_VERSION
        or header_size != ZUPD_HEADER_SIZE
        or flags != 0
        or manifest_offset != ZUPD_HEADER_SIZE
        or manifest_size != expected_manifest_size
        or payload_offset != expected_payload_offset
        or signature_offset != payload_offset + payload_size
        or total_size != signature_offset + signature_size
        or entry_size != ZUPD_ENTRY_SIZE
        or reserved0 != 0
        or any(reserved)
    ):
        reject(REASON_FORMAT, "layout ou campos reservados do header invalidos")
    return {
        "architecture": architecture,
        "total_size": total_size,
        "manifest_offset": manifest_offset,
        "manifest_size": manifest_size,
        "payload_offset": payload_offset,
        "payload_size": payload_size,
        "signature_offset": signature_offset,
        "entry_count": entry_count,
        "base": Version(base_major, base_minor, base_patch),
        "target": Version(target_major, target_minor, target_patch),
        "base_epoch": base_epoch,
        "target_epoch": target_epoch,
        "key_id": key_id,
        "content_hash": content_hash,
    }


def parse_entry_structure(
    data: bytes,
    offset: int,
    next_payload: int,
    signature_offset: int,
) -> tuple[EntryInfo, bytes, int]:
    """Valida uma entrada e devolve seu proximo offset contiguo."""
    try:
        fields = ENTRY.unpack_from(data, offset)
    except struct.error:
        reject(REASON_SIZE, "tabela truncada")
    (
        path_raw,
        entry_payload_offset,
        entry_payload_size,
        installed_size,
        operation,
        compression,
        target_class,
        entry_flags,
        payload_hash,
        entry_reserved,
    ) = fields
    path = decode_path(path_raw)
    if not 1 <= entry_payload_size <= ZUPD_MAX_PAYLOAD_SIZE:
        reject(REASON_SIZE, "payload individual fora dos limites")
    if (
        operation != ZUPD_OPERATION_REPLACE
        or compression != ZUPD_COMPRESSION_NONE
        or target_class != ZUPD_TARGET_SYSTEM_FILE
    ):
        reject(
            REASON_UNSUPPORTED,
            "operacao, compressao ou classe nao suportada",
        )
    if (
        installed_size != entry_payload_size
        or entry_payload_offset != next_payload
        or entry_flags != 0
        or any(entry_reserved)
    ):
        reject(
            REASON_FORMAT,
            "entrada possui layout ou campo reservado invalido",
        )
    end = entry_payload_offset + entry_payload_size
    if end > signature_offset:
        reject(REASON_SIZE, "payload excede a regiao declarada")
    return (
        EntryInfo(path, entry_payload_offset, entry_payload_size, payload_hash),
        path_raw,
        end,
    )


def parse_entries_structure(
    data: bytes,
    header: dict[str, Any],
) -> tuple[EntryInfo, ...]:
    """Valida ordenacao, unicidade e continuidade da tabela."""
    entries: list[EntryInfo] = []
    previous_path: bytes | None = None
    next_payload = header["payload_offset"]
    seen: set[str] = set()
    for index in range(header["entry_count"]):
        offset = header["manifest_offset"] + index * ZUPD_ENTRY_SIZE
        entry, path_raw, next_offset = parse_entry_structure(
            data, offset, next_payload, header["signature_offset"]
        )
        if entry.path in seen:
            reject(REASON_DUPLICATE_TARGET, "target duplicado")
        if previous_path is not None and path_raw <= previous_path:
            reject(REASON_FORMAT, "tabela nao esta ordenada")
        entries.append(entry)
        next_payload = next_offset
        previous_path = path_raw
        seen.add(entry.path)
    if next_payload != header["signature_offset"]:
        reject(REASON_FORMAT, "payloads possuem lacuna ou sobreposicao")
    return tuple(entries)


def parse_structure(data: bytes) -> tuple[dict[str, Any], tuple[EntryInfo, ...]]:
    """Valida header e tabela antes de qualquer verificacao criptografica."""
    header = parse_header_structure(data)
    return header, parse_entries_structure(data, header)


def verify_artifact(
    data: bytes,
    trusted_key: PublicKeyInfo,
    system_version: Version,
    system_epoch: int,
) -> ArtifactInfo:
    """Valida integralmente um artefato na ordem publica do contrato."""
    header, entries = parse_structure(data)
    content_start = header["manifest_offset"]
    signature_offset = header["signature_offset"]
    if hashlib.sha256(data[content_start:signature_offset]).digest() != header["content_hash"]:
        reject(REASON_HASH, "SHA-256 global divergente")
    for entry in entries:
        payload = data[
            entry.payload_offset : entry.payload_offset + entry.payload_size
        ]
        if hashlib.sha256(payload).digest() != entry.payload_sha256:
            reject(REASON_HASH, f"SHA-256 divergente para {entry.path}")
    if header["key_id"] != trusted_key.key_id:
        reject(REASON_UNKNOWN_KEY, "key_id nao corresponde a raiz confiavel")
    ed25519, _, invalid_signature = crypto_modules()
    public_key = ed25519.Ed25519PublicKey.from_public_bytes(trusted_key.public_key)
    try:
        public_key.verify(
            data[signature_offset:],
            ZUPD_DOMAIN + data[:signature_offset],
        )
    except invalid_signature:
        reject(REASON_SIGNATURE, "assinatura Ed25519 invalida")
    if header["architecture"] != ZUPD_ARCH_I386:
        reject(REASON_ARCHITECTURE, "arquitetura nao e i386")
    if header["base"] != system_version or header["base_epoch"] != system_epoch:
        reject(REASON_BASE_VERSION, "versao ou epoch base nao coincide")
    if header["target"] <= header["base"] or header["target_epoch"] < header["base_epoch"]:
        reject(REASON_DOWNGRADE, "versao ou epoch alvo representa downgrade")
    if any(entry.path not in ZUPD_ALLOWLIST for entry in entries):
        reject(REASON_PATH_POLICY, "target fora da allowlist")
    return ArtifactInfo(
        header["base"],
        header["target"],
        header["base_epoch"],
        header["target_epoch"],
        header["total_size"],
        entries,
    )


RUNTIME_OPERATION_NAMES = {
    "replace": RUNTIME_OPERATION_REPLACE,
    "create": RUNTIME_OPERATION_CREATE,
    "replace_or_create": RUNTIME_OPERATION_REPLACE | RUNTIME_OPERATION_CREATE,
    "delete": RUNTIME_OPERATION_DELETE,
}


def runtime_fixed_text(raw: bytes, label: str) -> str:
    """Decodifica um texto ASCII terminado e preenchido com zeros."""
    end = raw.find(b"\0")
    if end <= 0 or any(raw[end + 1 :]):
        raise UpdateError(f"campo {label} nao possui padding canonico")
    try:
        value = raw[:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise UpdateError(f"campo {label} nao usa ASCII") from error
    if not value or any(ord(char) < 0x20 for char in value):
        raise UpdateError(f"campo {label} possui caracteres invalidos")
    return value


def runtime_fixed_bytes(value: str, capacity: int, label: str) -> bytes:
    """Codifica texto ASCII nos campos fixos do contrato ZUM2."""
    if not isinstance(value, str) or not value or any(
        ord(char) < 0x20 for char in value
    ):
        raise UpdateError(f"campo {label} invalido")
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise UpdateError(f"campo {label} nao usa ASCII") from error
    if len(raw) >= capacity:
        raise UpdateError(f"campo {label} excede {capacity - 1} bytes")
    return raw + b"\0" * (capacity - len(raw))


def runtime_version_is_newer(
    target: Version, target_epoch: int, base: Version, base_epoch: int
) -> bool:
    """Compara epoch antes da versao semantica, como o kernel."""
    return target_epoch > base_epoch or (
        target_epoch == base_epoch and target > base
    )


def runtime_manifest_entry_from_bytes(
    raw: bytes, index: int
) -> RuntimeEntry:
    """Decodifica uma entrada ZUM2 de 224 bytes."""
    if len(raw) != RUNTIME_MANIFEST_ENTRY_SIZE:
        raise UpdateError(f"entrada ZUM2 {index} truncada")
    path = decode_path(raw[:RUNTIME_PATH_SIZE])
    if raw[64] not in (0, 1):
        raise UpdateError(f"entrada ZUM2 {path} possui presenca invalida")
    target_present = bool(raw[64])
    operations = raw[65]
    flags = struct.unpack_from("<H", raw, 66)[0]
    target_size = struct.unpack_from("<I", raw, 68)[0]
    target_hash = raw[72:104]
    asset_name = runtime_fixed_text(raw[104:168], f"asset {path}")
    asset_size = struct.unpack_from("<I", raw, 168)[0]
    asset_hash = raw[172:204]
    if (
        flags
        or not operations
        or operations & ~(
            RUNTIME_OPERATION_REPLACE
            | RUNTIME_OPERATION_CREATE
            | RUNTIME_OPERATION_DELETE
        )
        or any(raw[204:])
    ):
        raise UpdateError(f"entrada ZUM2 {path} possui flags invalidas")
    if target_present:
        if (
            not target_size
            or target_size > RUNTIME_FILE_MAX_SIZE
            or target_hash == bytes(32)
            or asset_size != target_size
            or asset_hash == bytes(32)
            or asset_name != path
            or not operations
            & (RUNTIME_OPERATION_REPLACE | RUNTIME_OPERATION_CREATE)
            or operations & RUNTIME_OPERATION_DELETE
        ):
            raise UpdateError(f"entrada ZUM2 {path} possui alvo invalido")
    elif (
        target_size
        or target_hash != bytes(32)
        or asset_size
        or asset_hash != bytes(32)
        or asset_name != path
        or operations != RUNTIME_OPERATION_DELETE
    ):
        raise UpdateError(f"entrada ZUM2 {path} possui remocao invalida")
    return RuntimeEntry(
        path,
        target_present,
        operations,
        target_size,
        target_hash,
        asset_name,
        asset_size,
        asset_hash,
    )


def parse_runtime_manifest(
    data: bytes, trusted_key: PublicKeyInfo
) -> RuntimeManifest:
    """Valida estrutura, assinatura e politica do manifesto ZUM2."""
    if len(data) != RUNTIME_MANIFEST_SIZE:
        reject(REASON_SIZE, "manifesto ZUM2 possui tamanho invalido")
    if data[:4] != RUNTIME_MAGIC:
        reject(REASON_FORMAT, "magic ZUM2 invalido")
    version, size, architecture, flags = struct.unpack_from("<HHHH", data, 4)
    generation = struct.unpack_from("<I", data, 12)[0]
    target = Version(*struct.unpack_from("<6H", data, 16)[:3])
    target_epoch = struct.unpack_from("<I", data, 22)[0]
    entry_count, entry_size, base_count = struct.unpack_from("<HHH", data, 26)
    package_size = struct.unpack_from("<I", data, 32)[0]
    if (
        version != RUNTIME_FORMAT_VERSION
        or size != RUNTIME_MANIFEST_SIZE
        or architecture != RUNTIME_ARCH_I386
        or flags
        or entry_size != RUNTIME_MANIFEST_ENTRY_SIZE
        or not 1 <= entry_count <= RUNTIME_MAX_ENTRIES
        or not 1 <= base_count <= RUNTIME_MANIFEST_MAX_BASES
        or not 1 <= package_size <= RUNTIME_PACKAGE_MAX_SIZE
    ):
        reject(REASON_FORMAT, "cabecalho ZUM2 invalido")
    if data[68:84] != trusted_key.key_id:
        reject(REASON_UNKNOWN_KEY, "key_id do ZUM2 nao corresponde a raiz")
    ed25519, _, invalid_signature = crypto_modules()
    public_key = ed25519.Ed25519PublicKey.from_public_bytes(
        trusted_key.public_key
    )
    try:
        public_key.verify(
            data[RUNTIME_MANIFEST_SIGNED_SIZE:],
            RUNTIME_DOMAIN + data[:RUNTIME_MANIFEST_SIGNED_SIZE],
        )
    except invalid_signature as error:
        reject(REASON_SIGNATURE, "assinatura Ed25519 do ZUM2 invalida")
    if any(data[3968:RUNTIME_MANIFEST_SIGNED_SIZE]):
        reject(REASON_FORMAT, "padding assinado do ZUM2 nao esta zerado")
    release_tag = runtime_fixed_text(data[84:148], "release_tag")
    release_id = runtime_fixed_text(data[148:212], "release_id")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,63}", release_tag):
        reject(REASON_PATH_POLICY, "release_tag do ZUM2 invalido")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,63}", release_id):
        reject(REASON_PATH_POLICY, "release_id do ZUM2 invalido")
    package_hash = data[36:68]
    if package_hash == bytes(32):
        reject(REASON_HASH, "hash do pacote ZUM2 esta zerado")
    if any(data[212:RUNTIME_MANIFEST_BASE_OFFSET]):
        reject(REASON_FORMAT, "reservado do cabecalho ZUM2 nao esta zerado")
    bases: list[tuple[Version, int]] = []
    seen_bases: set[tuple[Version, int]] = set()
    for index in range(base_count):
        offset = RUNTIME_MANIFEST_BASE_OFFSET + index * RUNTIME_MANIFEST_BASE_SIZE
        base = Version(*struct.unpack_from("<3H", data, offset))
        epoch = struct.unpack_from("<I", data, offset + 6)[0]
        if any(data[offset + 10 : offset + RUNTIME_MANIFEST_BASE_SIZE]):
            reject(REASON_FORMAT, "base_version ZUM2 possui padding invalido")
        if (base, epoch) in seen_bases:
            reject(REASON_DUPLICATE_TARGET, "base_version ZUM2 duplicada")
        if not runtime_version_is_newer(target, target_epoch, base, epoch):
            reject(REASON_DOWNGRADE, "alvo ZUM2 nao e superior a uma base")
        seen_bases.add((base, epoch))
        bases.append((base, epoch))
    base_end = RUNTIME_MANIFEST_BASE_OFFSET + base_count * RUNTIME_MANIFEST_BASE_SIZE
    if any(data[base_end:RUNTIME_MANIFEST_ENTRY_OFFSET]):
        reject(REASON_FORMAT, "bases nao usadas do ZUM2 nao estao zeradas")
    entries: list[RuntimeEntry] = []
    seen_paths: set[str] = set()
    for index in range(entry_count):
        offset = RUNTIME_MANIFEST_ENTRY_OFFSET + index * RUNTIME_MANIFEST_ENTRY_SIZE
        entry = runtime_manifest_entry_from_bytes(
            data[offset : offset + RUNTIME_MANIFEST_ENTRY_SIZE], index
        )
        if entry.path not in RUNTIME_CATALOG:
            reject(REASON_PATH_POLICY, f"path ZUM2 fora do catalogo: {entry.path}")
        if entry.path in seen_paths:
            reject(REASON_DUPLICATE_TARGET, f"target ZUM2 duplicado: {entry.path}")
        seen_paths.add(entry.path)
        entries.append(entry)
    if seen_paths != set(RUNTIME_CATALOG):
        reject(REASON_PATH_POLICY, "catalogo ZUM2 incompleto")
    entry_end = RUNTIME_MANIFEST_ENTRY_OFFSET + entry_count * RUNTIME_MANIFEST_ENTRY_SIZE
    if any(data[entry_end:RUNTIME_MANIFEST_SIGNED_SIZE]):
        reject(REASON_FORMAT, "entradas nao usadas do ZUM2 nao estao zeradas")
    return RuntimeManifest(
        generation,
        target,
        target_epoch,
        tuple(bases),
        package_size,
        package_hash,
        release_tag,
        release_id,
        tuple(entries),
    )


def runtime_package_entry_from_bytes(
    raw: bytes, index: int
) -> RuntimePackageEntry:
    """Decodifica uma entrada ZUPD v2 de 128 bytes."""
    if len(raw) != RUNTIME_PACKAGE_ENTRY_SIZE:
        raise UpdateError(f"entrada ZUPD v2 {index} truncada")
    path = decode_path(raw[:RUNTIME_PATH_SIZE])
    payload_offset, payload_size = struct.unpack_from("<II", raw, 64)
    present = raw[72]
    operation = raw[73]
    payload_hash = raw[76:108]
    if (
        present not in (0, 1)
        or not operation
        or operation
        & ~(RUNTIME_OPERATION_REPLACE | RUNTIME_OPERATION_CREATE | RUNTIME_OPERATION_DELETE)
        or any(raw[74:76])
        or any(raw[108:])
    ):
        raise UpdateError(f"entrada ZUPD v2 {path} possui flags invalidas")
    if not present:
        if (
            operation != RUNTIME_OPERATION_DELETE
            or payload_offset
            or payload_size
            or payload_hash != bytes(32)
        ):
            raise UpdateError(f"remocao ZUPD v2 {path} possui payload")
    elif (
        not payload_size
        or payload_size > RUNTIME_FILE_MAX_SIZE
        or payload_hash == bytes(32)
        or not operation & (RUNTIME_OPERATION_REPLACE | RUNTIME_OPERATION_CREATE)
        or operation & RUNTIME_OPERATION_DELETE
    ):
        raise UpdateError(f"payload ZUPD v2 {path} invalido")
    return RuntimePackageEntry(
        path, bool(present), operation, payload_offset, payload_size, payload_hash
    )


def parse_runtime_package(
    data: bytes,
    trusted_key: PublicKeyInfo,
    manifest: RuntimeManifest | None = None,
    system_version: Version | None = None,
    system_epoch: int | None = None,
) -> RuntimePackage:
    """Valida pacote completo, assinatura, hashes e compatibilidade ZUPD v2."""
    if not RUNTIME_PACKAGE_HEADER_SIZE + RUNTIME_PACKAGE_ENTRY_SIZE + RUNTIME_SIGNATURE_SIZE <= len(data) <= RUNTIME_PACKAGE_MAX_SIZE:
        reject(REASON_SIZE, "pacote ZUPD v2 fora dos limites")
    if data[:4] != RUNTIME_PACKAGE_MAGIC:
        reject(REASON_FORMAT, "magic ZUPD v2 invalido")
    version, header_size, architecture, reserved = struct.unpack_from(
        "<HHHH", data, 4
    )
    flags = struct.unpack_from("<I", data, 12)[0]
    total_size = struct.unpack_from("<I", data, 16)[0]
    payload_offset, payload_size = struct.unpack_from("<II", data, 20)
    signature_offset, signature_size = struct.unpack_from("<II", data, 28)
    generation = struct.unpack_from("<I", data, 36)[0]
    target = Version(*struct.unpack_from("<3H", data, 40))
    target_epoch = struct.unpack_from("<I", data, 46)[0]
    entry_count, entry_size, entry_reserved = struct.unpack_from(
        "<HHH", data, 50
    )
    key_id = data[56:72]
    base_reserved = data[72:74]
    base = Version(*struct.unpack_from("<3H", data, 74))
    base_epoch = struct.unpack_from("<I", data, 80)[0]
    if (
        version != RUNTIME_FORMAT_VERSION
        or header_size != RUNTIME_PACKAGE_HEADER_SIZE
        or architecture != RUNTIME_ARCH_I386
        or reserved
        or flags
        or total_size != len(data)
        or signature_size != RUNTIME_SIGNATURE_SIZE
        or entry_size != RUNTIME_PACKAGE_ENTRY_SIZE
        or entry_reserved
        or base_reserved != bytes(2)
        or any(data[84:RUNTIME_PACKAGE_HEADER_SIZE])
        or not 1 <= entry_count <= RUNTIME_MAX_ENTRIES
        or key_id != trusted_key.key_id
    ):
        reject(REASON_FORMAT if key_id == trusted_key.key_id else REASON_UNKNOWN_KEY,
               "cabecalho ZUPD v2 invalido")
    table_end = RUNTIME_PACKAGE_HEADER_SIZE + entry_count * RUNTIME_PACKAGE_ENTRY_SIZE
    if (
        payload_offset != table_end
        or signature_offset != payload_offset + payload_size
        or signature_offset + signature_size != total_size
        or payload_size > RUNTIME_PACKAGE_MAX_SIZE
    ):
        reject(REASON_FORMAT, "regioes do pacote ZUPD v2 divergem")
    ed25519, _, invalid_signature = crypto_modules()
    public_key = ed25519.Ed25519PublicKey.from_public_bytes(
        trusted_key.public_key
    )
    try:
        public_key.verify(
            data[signature_offset:],
            RUNTIME_PACKAGE_DOMAIN + data[:signature_offset],
        )
    except invalid_signature as error:
        reject(REASON_SIGNATURE, "assinatura Ed25519 do ZUPD v2 invalida")
    entries: list[RuntimePackageEntry] = []
    seen: set[str] = set()
    next_payload = payload_offset
    for index in range(entry_count):
        offset = RUNTIME_PACKAGE_HEADER_SIZE + index * RUNTIME_PACKAGE_ENTRY_SIZE
        entry = runtime_package_entry_from_bytes(
            data[offset : offset + RUNTIME_PACKAGE_ENTRY_SIZE], index
        )
        if entry.path not in RUNTIME_CATALOG:
            reject(REASON_PATH_POLICY, f"path ZUPD v2 fora do catalogo: {entry.path}")
        if entry.path in seen:
            reject(REASON_DUPLICATE_TARGET, f"target ZUPD v2 duplicado: {entry.path}")
        if entry.present:
            if entry.payload_offset != next_payload:
                reject(REASON_FORMAT, "payloads ZUPD v2 nao sao contiguos")
            payload = data[entry.payload_offset : entry.payload_offset + entry.payload_size]
            if hashlib.sha256(payload).digest() != entry.payload_sha256:
                reject(REASON_HASH, f"hash ZUPD v2 divergente: {entry.path}")
            next_payload += entry.payload_size
        elif entry.payload_offset or entry.payload_size:
            reject(REASON_FORMAT, f"remocao ZUPD v2 possui regiao de payload")
        seen.add(entry.path)
        entries.append(entry)
    if next_payload != signature_offset or seen != set(RUNTIME_CATALOG):
        reject(REASON_FORMAT, "tabela ZUPD v2 incompleta")
    if manifest is not None:
        if (
            generation != manifest.generation
            or target != manifest.target_version
            or target_epoch != manifest.target_epoch
        ):
            reject(REASON_PACKAGE_MISMATCH, "pacote nao corresponde ao ZUM2")
        if (base, base_epoch) not in manifest.base_versions:
            reject(REASON_PACKAGE_MISMATCH, "base do pacote nao esta no ZUM2")
        manifest_by_path = {entry.path: entry for entry in manifest.entries}
        for entry in entries:
            desired = manifest_by_path[entry.path]
            if (
                entry.present != desired.target_present
                or entry.payload_size != desired.target_size
                or entry.payload_sha256 != desired.target_sha256
                or not desired.operations & entry.operation
            ):
                reject(REASON_PACKAGE_MISMATCH, f"entrada diverge do ZUM2: {entry.path}")
        if len(data) != manifest.package_size or hashlib.sha256(data).digest() != manifest.package_sha256:
            reject(REASON_PACKAGE_MISMATCH, "hash do pacote diverge do ZUM2")
    if system_version is not None and system_epoch is not None:
        compatible_base = (system_version, system_epoch)
        if manifest is not None:
            if compatible_base not in manifest.base_versions:
                reject(REASON_BASE_VERSION, "base ZUPD v2 nao esta no ZUM2")
        elif base != system_version or base_epoch != system_epoch:
            reject(REASON_BASE_VERSION, "base ZUPD v2 nao coincide com o runtime")
        if not runtime_version_is_newer(
            target, target_epoch, system_version, system_epoch
        ):
            reject(REASON_DOWNGRADE, "alvo ZUPD v2 representa downgrade")
    return RuntimePackage(
        generation,
        base,
        base_epoch,
        target,
        target_epoch,
        tuple(entries),
        total_size,
    )


def runtime_operation_value(value: Any) -> int:
    """Converte a politica textual de uma entrada para flags ZUM2."""
    if not isinstance(value, str) or value not in RUNTIME_OPERATION_NAMES:
        raise UpdateError(
            "operation deve ser replace, create, replace_or_create ou delete"
        )
    return RUNTIME_OPERATION_NAMES[value]


def runtime_manifest_spec(
    path: Path,
) -> tuple[int, str, str, Version, int, tuple[tuple[Version, int], ...], tuple[RuntimeEntry, ...], dict[str, bytes]]:
    """Le o manifesto JSON de build v2 e carrega somente o catalogo permitido."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError(f"nao foi possivel ler o manifesto runtime {path}") from error
    if not isinstance(data, dict) or tuple(data) != (
        "format",
        "generation",
        "release_tag",
        "release_id",
        "target_version",
        "target_epoch",
        "base_versions",
        "files",
    ):
        raise UpdateError("manifesto runtime possui campos invalidos ou fora de ordem")
    if data["format"] != "ZUM2 v2":
        raise UpdateError("formato do manifesto runtime invalido")
    generation = data["generation"]
    if not isinstance(generation, int) or not 0 <= generation <= 0xFFFFFFFF:
        raise UpdateError("generation runtime excede uint32")
    release_tag = data["release_tag"]
    release_id = data["release_id"]
    if not isinstance(release_tag, str) or not re.fullmatch(
        r"[A-Za-z0-9._-]{1,63}", release_tag
    ):
        raise UpdateError("release_tag runtime invalido")
    if not isinstance(release_id, str) or not re.fullmatch(
        r"[A-Za-z0-9._-]{1,63}", release_id
    ):
        raise UpdateError("release_id runtime invalido")
    target = Version.parse(data["target_version"])
    target_epoch = data["target_epoch"]
    if not isinstance(target_epoch, int) or not 0 <= target_epoch <= 0xFFFFFFFF:
        raise UpdateError("target_epoch runtime excede uint32")
    raw_bases = data["base_versions"]
    if not isinstance(raw_bases, list) or not 1 <= len(raw_bases) <= RUNTIME_MANIFEST_MAX_BASES:
        raise UpdateError("runtime deve declarar de 1 a 8 versoes-base")
    bases: list[tuple[Version, int]] = []
    seen_bases: set[tuple[Version, int]] = set()
    for item in raw_bases:
        if not isinstance(item, dict) or tuple(item) != ("version", "epoch"):
            raise UpdateError("base_versions runtime possui formato invalido")
        base = Version.parse(item["version"])
        epoch = item["epoch"]
        if not isinstance(epoch, int) or not 0 <= epoch <= 0xFFFFFFFF:
            raise UpdateError("epoch de base runtime excede uint32")
        if (base, epoch) in seen_bases:
            raise UpdateError("base_versions runtime possui duplicidade")
        if not runtime_version_is_newer(target, target_epoch, base, epoch):
            raise UpdateError("target runtime nao e superior a todas as bases")
        seen_bases.add((base, epoch))
        bases.append((base, epoch))
    raw_files = data["files"]
    if not isinstance(raw_files, list) or not 1 <= len(raw_files) <= RUNTIME_MAX_ENTRIES:
        raise UpdateError("runtime deve conter de 1 a 16 entradas")
    entries: list[RuntimeEntry] = []
    payloads: dict[str, bytes] = {}
    seen_paths: set[str] = set()
    base_dir = path.resolve().parent
    for item in raw_files:
        if not isinstance(item, dict) or tuple(item) != (
            "path", "operation", "source"
        ):
            raise UpdateError("entrada runtime deve conter path, operation e source")
        target_path = item["path"]
        encode_path(target_path)
        if target_path not in RUNTIME_CATALOG:
            raise UpdateError(f"path runtime fora do catalogo: {target_path}")
        if target_path in seen_paths:
            raise UpdateError(f"path runtime duplicado: {target_path}")
        operations = runtime_operation_value(item["operation"])
        source = item["source"]
        if operations == RUNTIME_OPERATION_DELETE:
            if source is not None:
                raise UpdateError(f"source da remocao runtime deve ser null: {target_path}")
            entry = RuntimeEntry(
                target_path,
                False,
                operations,
                0,
                bytes(32),
                target_path,
                0,
                bytes(32),
            )
        else:
            if not isinstance(source, str) or not source:
                raise UpdateError(f"source ausente para runtime: {target_path}")
            source_path = (base_dir / source).resolve()
            try:
                payload = source_path.read_bytes()
            except OSError as error:
                raise UpdateError(f"payload runtime ausente: {source_path}") from error
            if not 1 <= len(payload) <= RUNTIME_FILE_MAX_SIZE:
                raise UpdateError(f"payload runtime fora dos limites: {target_path}")
            digest = hashlib.sha256(payload).digest()
            entry = RuntimeEntry(
                target_path,
                True,
                operations,
                len(payload),
                digest,
                target_path,
                len(payload),
                digest,
            )
            payloads[target_path] = payload
        entries.append(entry)
        seen_paths.add(target_path)
    if seen_paths != set(RUNTIME_CATALOG):
        raise UpdateError("manifesto runtime deve cobrir o catalogo completo")
    entries.sort(key=lambda entry: entry.path.encode("ascii"))
    return (
        generation,
        release_tag,
        release_id,
        target,
        target_epoch,
        tuple(bases),
        tuple(entries),
        payloads,
    )


def build_runtime_package(
    private_key: Any,
    public: PublicKeyInfo,
    generation: int,
    base: Version,
    base_epoch: int,
    target: Version,
    target_epoch: int,
    entries: tuple[RuntimeEntry, ...],
    payloads: dict[str, bytes],
) -> bytes:
    """Monta e assina o pacote completo independente de deltas."""
    if len(entries) != len(RUNTIME_CATALOG):
        raise UpdateError("pacote runtime deve cobrir o catalogo completo")
    table_end = RUNTIME_PACKAGE_HEADER_SIZE + len(entries) * RUNTIME_PACKAGE_ENTRY_SIZE
    payload_offset = table_end
    entry_bytes: list[bytes] = []
    payload_blob = bytearray()
    for entry in entries:
        raw = bytearray(RUNTIME_PACKAGE_ENTRY_SIZE)
        raw[:RUNTIME_PATH_SIZE] = encode_path(entry.path)
        payload = payloads.get(entry.path, b"")
        if entry.target_present:
            if len(payload) != entry.target_size or hashlib.sha256(payload).digest() != entry.target_sha256:
                raise UpdateError(f"payload runtime divergiu: {entry.path}")
            struct.pack_into("<II", raw, 64, payload_offset, len(payload))
            raw[72] = 1
            raw[73] = entry.operations
            raw[76:108] = entry.target_sha256
            payload_blob.extend(payload)
            payload_offset += len(payload)
        else:
            raw[73] = RUNTIME_OPERATION_DELETE
        entry_bytes.append(bytes(raw))
    signature_offset = payload_offset
    total_size = signature_offset + RUNTIME_SIGNATURE_SIZE
    if total_size > RUNTIME_PACKAGE_MAX_SIZE:
        raise UpdateError("pacote runtime excede 128 KiB")
    header = bytearray(RUNTIME_PACKAGE_HEADER_SIZE)
    header[:4] = RUNTIME_PACKAGE_MAGIC
    struct.pack_into("<HHHHI", header, 4, RUNTIME_FORMAT_VERSION,
                     RUNTIME_PACKAGE_HEADER_SIZE, RUNTIME_ARCH_I386, 0, 0)
    struct.pack_into("<IIII", header, 16, total_size, table_end,
                     len(payload_blob), signature_offset)
    struct.pack_into("<I", header, 32, RUNTIME_SIGNATURE_SIZE)
    struct.pack_into("<I", header, 36, generation)
    struct.pack_into("<3H", header, 40, target.major, target.minor, target.patch)
    struct.pack_into("<I", header, 46, target_epoch)
    struct.pack_into("<HH", header, 50, len(entries), RUNTIME_PACKAGE_ENTRY_SIZE)
    header[56:72] = public.key_id
    struct.pack_into("<3H", header, 74, base.major, base.minor, base.patch)
    struct.pack_into("<I", header, 80, base_epoch)
    unsigned = bytes(header) + b"".join(entry_bytes) + bytes(payload_blob)
    return unsigned + private_key.sign(RUNTIME_PACKAGE_DOMAIN + unsigned)


def build_runtime_manifest(
    private_key: Any,
    public: PublicKeyInfo,
    generation: int,
    release_tag: str,
    release_id: str,
    target: Version,
    target_epoch: int,
    bases: tuple[tuple[Version, int], ...],
    entries: tuple[RuntimeEntry, ...],
    package: bytes,
) -> bytes:
    """Monta e assina o manifesto ZUM2 de 4 KiB."""
    raw = bytearray(RUNTIME_MANIFEST_SIZE)
    raw[:4] = RUNTIME_MAGIC
    struct.pack_into("<HHHHI", raw, 4, RUNTIME_FORMAT_VERSION,
                     RUNTIME_MANIFEST_SIZE, RUNTIME_ARCH_I386, 0, generation)
    struct.pack_into("<3H", raw, 16, target.major, target.minor, target.patch)
    struct.pack_into("<I", raw, 22, target_epoch)
    struct.pack_into("<HHH", raw, 26, len(entries), RUNTIME_MANIFEST_ENTRY_SIZE,
                     len(bases))
    struct.pack_into("<I", raw, 32, len(package))
    raw[36:68] = hashlib.sha256(package).digest()
    raw[68:84] = public.key_id
    raw[84:148] = runtime_fixed_bytes(release_tag, 64, "release_tag")
    raw[148:212] = runtime_fixed_bytes(release_id, 64, "release_id")
    for index, (base, epoch) in enumerate(bases):
        offset = RUNTIME_MANIFEST_BASE_OFFSET + index * RUNTIME_MANIFEST_BASE_SIZE
        struct.pack_into("<3H", raw, offset, base.major, base.minor, base.patch)
        struct.pack_into("<I", raw, offset + 6, epoch)
    for index, entry in enumerate(entries):
        offset = RUNTIME_MANIFEST_ENTRY_OFFSET + index * RUNTIME_MANIFEST_ENTRY_SIZE
        target_raw = bytearray(RUNTIME_MANIFEST_ENTRY_SIZE)
        target_raw[:RUNTIME_PATH_SIZE] = encode_path(entry.path)
        target_raw[64] = 1 if entry.target_present else 0
        target_raw[65] = entry.operations
        struct.pack_into("<H", target_raw, 66, 0)
        struct.pack_into("<I", target_raw, 68, entry.target_size)
        target_raw[72:104] = entry.target_sha256
        target_raw[104:168] = runtime_fixed_bytes(entry.asset_name, 64, "asset_name")
        struct.pack_into("<I", target_raw, 168, entry.asset_size)
        target_raw[172:204] = entry.asset_sha256
        raw[offset : offset + RUNTIME_MANIFEST_ENTRY_SIZE] = target_raw
    raw[RUNTIME_MANIFEST_SIGNED_SIZE:] = private_key.sign(
        RUNTIME_DOMAIN + bytes(raw[:RUNTIME_MANIFEST_SIGNED_SIZE])
    )
    return bytes(raw)


def runtime_release_descriptor(
    release_id: str,
    release_name: str,
    tag: str,
    manifest: bytes,
    package: bytes,
    entries: tuple[RuntimeEntry, ...],
    payloads: dict[str, bytes],
) -> str:
    """Serializa metadados de transporte sem substituir a autoridade assinada."""
    assets = [
        release_asset_metadata(entry.asset_name, payloads[entry.path])
        for entry in entries
        if entry.target_present
    ]
    runtime = {
        "zum2": release_asset_metadata("runtime.zum2", manifest),
        "zephyrosupd": release_asset_metadata("runtime.zephyrosupd", package),
        "assets": assets,
    }
    return json.dumps(
        {
            "format": "zephyros-runtime-release-v2",
            "release_id": release_id,
            "release_name": release_name,
            "channel": "stable",
            "tag": tag,
            "runtime": runtime,
        },
        indent=2,
        ensure_ascii=True,
    ) + "\n"


def build_runtime_bundle(
    manifest_path: Path,
    private_key: Any,
    public: PublicKeyInfo,
    output_dir: Path,
) -> Path:
    """Gera ZUM2, pacote completo, assets individuais e release.json v2."""
    if output_dir.exists():
        raise UpdateError(f"diretorio runtime ja existe: {output_dir}")
    (
        generation,
        release_tag,
        release_id,
        target,
        target_epoch,
        bases,
        entries,
        payloads,
    ) = runtime_manifest_spec(manifest_path)
    base, base_epoch = bases[0]
    package = build_runtime_package(
        private_key,
        public,
        generation,
        base,
        base_epoch,
        target,
        target_epoch,
        entries,
        payloads,
    )
    manifest = build_runtime_manifest(
        private_key,
        public,
        generation,
        release_tag,
        release_id,
        target,
        target_epoch,
        bases,
        entries,
        package,
    )
    parse_manifest = parse_runtime_manifest(manifest, public)
    parse_runtime_package(package, public, parse_manifest)
    output_dir.mkdir(parents=True, exist_ok=False)
    write_new_bytes(output_dir / "runtime.zum2", manifest)
    write_new_bytes(output_dir / "runtime.zephyrosupd", package)
    for entry in entries:
        if entry.target_present:
            write_new_bytes(output_dir / entry.asset_name, payloads[entry.path])
    descriptor = runtime_release_descriptor(
        release_id,
        f"ZephyrOS Runtime {target}",
        release_tag,
        manifest,
        package,
        entries,
        payloads,
    )
    write_new_text(
        output_dir / "release.json",
        descriptor,
    )
    return output_dir


def system_fixed_text(raw: bytes, label: str) -> str:
    """Decodifica texto ASCII fixo do cabecalho ZSYS."""
    nul = raw.find(b"\0")
    if nul <= 0 or any(raw[nul + 1 :]):
        raise UpdateError(f"{label} ZSYS possui padding invalido")
    try:
        value = raw[:nul].decode("ascii")
    except UnicodeDecodeError as error:
        raise UpdateError(f"{label} ZSYS nao usa ASCII") from error
    if any(ord(char) < 0x20 for char in value):
        raise UpdateError(f"{label} ZSYS possui caractere invalido")
    return value


def system_fixed_bytes(value: str, capacity: int, label: str) -> bytes:
    """Codifica identificador ASCII sem permitir truncamento silencioso."""
    if not isinstance(value, str) or not value or len(value) >= capacity:
        raise UpdateError(f"{label} ZSYS invalido")
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise UpdateError(f"{label} ZSYS deve usar ASCII") from error
    if any(byte < 0x20 for byte in encoded):
        raise UpdateError(f"{label} ZSYS possui caractere invalido")
    return encoded + bytes(capacity - len(encoded))


def system_identifier(value: object, label: str) -> str:
    """Valida identificadores usados para correlacionar um ZSYS."""
    if not isinstance(value, str) or not re.fullmatch(
        r"[A-Za-z0-9._-]{1,63}", value
    ):
        raise UpdateError(f"{label} ZSYS invalido")
    return value


def system_version_compare(first: Version, second: Version) -> int:
    """Compara versões sem depender da ordenação da classe compartilhada."""
    first_value = (first.major, first.minor, first.patch)
    second_value = (second.major, second.minor, second.patch)
    return (first_value > second_value) - (first_value < second_value)


def system_base_spec(value: object, label: str) -> SystemBase:
    """Valida uma origem versionada do pacote de sistema."""
    if not isinstance(value, dict) or tuple(value) != ("version", "epoch"):
        raise UpdateError(f"{label} ZSYS invalido")
    version = Version.parse(value["version"])
    epoch = value["epoch"]
    if not isinstance(epoch, int) or not 0 <= epoch <= 0xFFFFFFFF:
        raise UpdateError(f"epoch de {label} ZSYS invalido")
    return SystemBase(version, epoch)


def system_manifest_spec(path: Path) -> SystemManifest:
    """Le e valida a especificacao JSON de um pacote ZSYS v1."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError(f"nao foi possivel ler manifesto ZSYS: {path}") from error
    if not isinstance(data, dict) or tuple(data) != (
        "format", "release_id", "release_tag", "target_version",
        "target_epoch", "supported_from", "min_updater", "boot_abi",
        "data_schema_from", "data_schema_to", "requires_reboot", "channel",
        "upgrade_route",
    ):
        raise UpdateError("manifesto ZSYS possui campos invalidos")
    if data["format"] != "ZSYS v1":
        raise UpdateError("formato do manifesto ZSYS invalido")
    release_id = system_identifier(data["release_id"], "release_id")
    release_tag = system_identifier(data["release_tag"], "release_tag")
    target_version = Version.parse(data["target_version"])
    target_epoch = data["target_epoch"]
    if not isinstance(target_epoch, int) or not 0 <= target_epoch <= 0xFFFFFFFF:
        raise UpdateError("target_epoch ZSYS invalido")
    raw_supported = data["supported_from"]
    if not isinstance(raw_supported, list) or not 1 <= len(raw_supported) <= SYSTEM_MAX_BASES:
        raise UpdateError("supported_from ZSYS deve conter de 1 a 8 origens")
    supported = tuple(
        system_base_spec(item, "supported_from") for item in raw_supported
    )
    if len({(item.version, item.epoch) for item in supported}) != len(supported):
        raise UpdateError("supported_from ZSYS possui duplicatas")
    minimum_updater = system_base_spec(data["min_updater"], "min_updater")
    if any(
        system_version_compare(target_version, item.version) < 0
        or (
            system_version_compare(target_version, item.version) == 0
            and target_epoch <= item.epoch
        )
        for item in supported
    ):
        raise UpdateError("target_version ZSYS deve ser superior a supported_from")
    integer_fields = (
        "boot_abi", "data_schema_from", "data_schema_to",
    )
    for field in integer_fields:
        value = data[field]
        if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
            raise UpdateError(f"{field} ZSYS invalido")
    if data["data_schema_from"] > data["data_schema_to"]:
        raise UpdateError("intervalo de schema ZSYS invertido")
    if data["requires_reboot"] is not True:
        raise UpdateError("ZSYS v1 exige requires_reboot=true")
    channel = data["channel"]
    if channel != "stable":
        raise UpdateError("canal ZSYS invalido")
    route = data["upgrade_route"]
    if not isinstance(route, dict) or tuple(route) != (
        "kind", "checkpoints", "bridge_required",
    ):
        raise UpdateError("upgrade_route ZSYS invalido")
    route_kind_text = route["kind"]
    route_kind = {
        "direct": SYSTEM_ROUTE_DIRECT,
        "checkpoint": SYSTEM_ROUTE_CHECKPOINT,
    }.get(route_kind_text)
    if route_kind is None:
        raise UpdateError("tipo de rota ZSYS invalido")
    checkpoints = route["checkpoints"]
    if not isinstance(checkpoints, list) or len(checkpoints) > SYSTEM_MAX_CHECKPOINTS:
        raise UpdateError("checkpoints ZSYS excedem o limite")
    checkpoint_values = tuple(
        system_identifier(item, "checkpoint") for item in checkpoints
    )
    if len(set(checkpoint_values)) != len(checkpoint_values):
        raise UpdateError("checkpoints ZSYS possuem duplicatas")
    if route_kind == SYSTEM_ROUTE_DIRECT and checkpoint_values:
        raise UpdateError("rota direta ZSYS nao pode possuir checkpoints")
    bridge_required = route["bridge_required"]
    if bridge_required is not False:
        raise UpdateError("bridge_required deve ser false na EP9.0A")
    compatibility = SystemCompatibility(
        supported, minimum_updater, data["boot_abi"],
        data["data_schema_from"], data["data_schema_to"], True, channel,
        route_kind, checkpoint_values, False,
    )
    return SystemManifest(
        release_id, release_tag, target_version, target_epoch, compatibility
    )


def system_pack_version(raw: bytearray, offset: int, version: Version,
                        epoch: int) -> None:
    """Serializa version/epoch no layout fixo do ZSYS."""
    struct.pack_into("<HHH", raw, offset, version.major, version.minor, version.patch)
    struct.pack_into("<I", raw, offset + 6, epoch)
    struct.pack_into("<H", raw, offset + 10, 0)


def system_unpack_version(raw: bytes, offset: int, label: str) -> SystemBase:
    """Decodifica version/epoch e confere o padding do registro."""
    major, minor, patch = struct.unpack_from("<HHH", raw, offset)
    epoch = struct.unpack_from("<I", raw, offset + 6)[0]
    if any(raw[offset + 10 : offset + SYSTEM_BASE_ENTRY_SIZE]):
        raise UpdateError(f"padding de {label} ZSYS nao e zero")
    return SystemBase(Version(major, minor, patch), epoch)


def system_component_header(kind: int, offset: int, data: bytes) -> bytes:
    """Serializa uma entrada de componente autenticado."""
    raw = bytearray(SYSTEM_COMPONENT_ENTRY_SIZE)
    struct.pack_into("<HHII", raw, 0, kind, 0, offset, len(data))
    raw[SYSTEM_COMPONENT_HASH_OFFSET : SYSTEM_COMPONENT_HASH_OFFSET + 32] = hashlib.sha256(data).digest()
    return bytes(raw)


def system_header(
    manifest: SystemManifest,
    image: bytes,
    components: tuple[SystemComponent, ...],
    public: PublicKeyInfo,
) -> bytearray:
    """Monta o cabeçalho ZSYS com a área de assinatura zerada."""
    raw = bytearray(SYSTEM_HEADER_SIZE)
    raw[:4] = SYSTEM_MAGIC
    struct.pack_into(
        "<HHHHIIII", raw, 4, SYSTEM_FORMAT_VERSION, SYSTEM_HEADER_SIZE,
        SYSTEM_ARCH_I386, SYSTEM_FLAG_REQUIRES_REBOOT |
        (SYSTEM_FLAG_BRIDGE_REQUIRED if manifest.compatibility.bridge_required else 0),
        SYSTEM_HEADER_SIZE + len(image),
        SYSTEM_PAYLOAD_OFFSET, len(image), len(image),
    )
    raw[SYSTEM_HEADER_IMAGE_HASH_OFFSET : SYSTEM_HEADER_IMAGE_HASH_OFFSET + 32] = hashlib.sha256(image).digest()
    struct.pack_into(
        "<HHHI",
        raw,
        SYSTEM_HEADER_TARGET_VERSION_OFFSET,
        manifest.target_version.major,
        manifest.target_version.minor,
        manifest.target_version.patch,
        manifest.target_epoch,
    )
    struct.pack_into("<HH", raw, SYSTEM_HEADER_BASE_COUNT_OFFSET,
                     len(manifest.compatibility.supported_from),
                     SYSTEM_BASE_ENTRY_SIZE)
    for index, base in enumerate(manifest.compatibility.supported_from):
        system_pack_version(
            raw, SYSTEM_HEADER_BASES_OFFSET + index * SYSTEM_BASE_ENTRY_SIZE,
            base.version, base.epoch,
        )
    system_pack_version(raw, SYSTEM_HEADER_MIN_UPDATER_OFFSET,
                        manifest.compatibility.minimum_updater.version,
                        manifest.compatibility.minimum_updater.epoch)
    struct.pack_into("<III", raw, SYSTEM_HEADER_BOOT_ABI_OFFSET,
                     manifest.compatibility.boot_abi,
                     manifest.compatibility.data_schema_from,
                     manifest.compatibility.data_schema_to)
    raw[SYSTEM_HEADER_ROUTE_OFFSET] = manifest.compatibility.route_kind
    raw[SYSTEM_HEADER_CHECKPOINT_COUNT_OFFSET] = len(manifest.compatibility.checkpoints)
    raw[SYSTEM_HEADER_CHANNEL_OFFSET : SYSTEM_HEADER_CHANNEL_OFFSET + SYSTEM_CHANNEL_SIZE] = system_fixed_bytes(
        manifest.compatibility.channel, SYSTEM_CHANNEL_SIZE, "channel"
    )
    raw[SYSTEM_HEADER_RELEASE_ID_OFFSET : SYSTEM_HEADER_RELEASE_ID_OFFSET + SYSTEM_IDENTIFIER_SIZE] = system_fixed_bytes(
        manifest.release_id, SYSTEM_IDENTIFIER_SIZE, "release_id"
    )
    raw[SYSTEM_HEADER_RELEASE_TAG_OFFSET : SYSTEM_HEADER_RELEASE_TAG_OFFSET + SYSTEM_IDENTIFIER_SIZE] = system_fixed_bytes(
        manifest.release_tag, SYSTEM_IDENTIFIER_SIZE, "release_tag"
    )
    struct.pack_into("<HH", raw, SYSTEM_HEADER_COMPONENT_COUNT_OFFSET,
                     len(components), SYSTEM_COMPONENT_ENTRY_SIZE)
    for index, component in enumerate(components):
        offset = SYSTEM_HEADER_COMPONENTS_OFFSET + index * SYSTEM_COMPONENT_ENTRY_SIZE
        raw[offset : offset + SYSTEM_COMPONENT_ENTRY_SIZE] = system_component_header(
            component.kind, component.offset, image[component.offset : component.offset + component.size]
        )
    struct.pack_into("<H", raw, SYSTEM_HEADER_CHECKPOINT_ENTRY_SIZE_OFFSET,
                     SYSTEM_CHECKPOINT_SIZE)
    for index, checkpoint in enumerate(manifest.compatibility.checkpoints):
        offset = SYSTEM_HEADER_CHECKPOINTS_OFFSET + index * SYSTEM_CHECKPOINT_SIZE
        raw[offset : offset + SYSTEM_CHECKPOINT_SIZE] = system_fixed_bytes(
            checkpoint, SYSTEM_CHECKPOINT_SIZE, "checkpoint"
        )
    raw[SYSTEM_HEADER_KEY_ID_OFFSET : SYSTEM_HEADER_KEY_ID_OFFSET + SYSTEM_KEY_ID_SIZE] = public.key_id
    struct.pack_into("<II", raw, SYSTEM_HEADER_SIGNATURE_SIZE_OFFSET,
                     SYSTEM_SIGNATURE_SIZE, SYSTEM_HEADER_SIGNATURE_OFFSET)
    return raw


def system_build_package(
    manifest: SystemManifest,
    image: bytes,
    boot: bytes,
    stage2: bytes,
    kernel: bytes,
    private_key: Any,
    public: PublicKeyInfo,
) -> bytes:
    """Constroi e assina um envelope ZSYS v1."""
    if not image or len(image) > SYSTEM_MAX_IMAGE_SIZE or len(image) % 512:
        raise UpdateError("imagem ZSYS deve ser alinhada e ter no maximo 8 MiB")
    if len(boot) != 512 or not stage2 or not kernel:
        raise UpdateError("componentes ZSYS possuem tamanho invalido")
    stage2_offset = len(boot)
    kernel_offset = stage2_offset + len(stage2)
    if len(stage2) % 512 or kernel_offset + len(kernel) > len(image):
        raise UpdateError("layout dos componentes ZSYS excede a imagem")
    if image[: len(boot)] != boot or image[stage2_offset:kernel_offset] != stage2:
        raise UpdateError("boot ou stage2 divergem da imagem ZSYS")
    if image[kernel_offset : kernel_offset + len(kernel)] != kernel:
        raise UpdateError("kernel diverge da imagem ZSYS")
    components = (
        SystemComponent(SYSTEM_COMPONENT_BOOT, 0, len(boot), hashlib.sha256(boot).digest()),
        SystemComponent(SYSTEM_COMPONENT_STAGE2, stage2_offset, len(stage2), hashlib.sha256(stage2).digest()),
        SystemComponent(SYSTEM_COMPONENT_KERNEL, kernel_offset, len(kernel), hashlib.sha256(kernel).digest()),
    )
    raw = system_header(manifest, image, components, public)
    signature_input = SYSTEM_DOMAIN + bytes(raw) + image
    signature = private_key.sign(signature_input)
    raw[SYSTEM_HEADER_SIGNATURE_OFFSET : SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE] = signature
    return bytes(raw) + image


def system_parse_header(raw: bytes, trusted: PublicKeyInfo) -> tuple[SystemPackage, bytes]:
    """Valida o cabeçalho e retorna metadados mais o cabeçalho sem assinatura."""
    if len(raw) < SYSTEM_HEADER_SIZE or raw[:4] != SYSTEM_MAGIC:
        raise Rejection(REASON_FORMAT, "cabecalho ZSYS invalido")
    version, header_size, arch, flags = struct.unpack_from("<HHHH", raw, 4)
    total_size, payload_offset, payload_size, image_size = struct.unpack_from("<IIII", raw, 12)
    if version != SYSTEM_FORMAT_VERSION or header_size != SYSTEM_HEADER_SIZE or arch != SYSTEM_ARCH_I386:
        raise Rejection(REASON_FORMAT, "versao ou arquitetura ZSYS invalida")
    if flags & ~(SYSTEM_FLAG_REQUIRES_REBOOT | SYSTEM_FLAG_BRIDGE_REQUIRED):
        raise Rejection(REASON_FORMAT, "flags ZSYS invalidas")
    signature_size, signature_offset = struct.unpack_from("<II", raw, SYSTEM_HEADER_SIGNATURE_SIZE_OFFSET)
    if payload_offset != SYSTEM_PAYLOAD_OFFSET or payload_size != image_size or not image_size or image_size > SYSTEM_MAX_IMAGE_SIZE or image_size % 512:
        raise Rejection(REASON_SIZE, "tamanho ou alinhamento da imagem ZSYS invalido")
    if signature_size != SYSTEM_SIGNATURE_SIZE or signature_offset != SYSTEM_HEADER_SIGNATURE_OFFSET or total_size != SYSTEM_HEADER_SIZE + image_size:
        raise Rejection(REASON_SIZE, "layout da assinatura ZSYS invalido")
    if any(raw[SYSTEM_HEADER_RESERVED_OFFSET:]):
        raise Rejection(REASON_FORMAT, "reservado ZSYS nao zero")
    release_id = system_identifier(system_fixed_text(raw[SYSTEM_HEADER_RELEASE_ID_OFFSET : SYSTEM_HEADER_RELEASE_ID_OFFSET + SYSTEM_IDENTIFIER_SIZE], "release_id"), "release_id")
    release_tag = system_identifier(system_fixed_text(raw[SYSTEM_HEADER_RELEASE_TAG_OFFSET : SYSTEM_HEADER_RELEASE_TAG_OFFSET + SYSTEM_IDENTIFIER_SIZE], "release_tag"), "release_tag")
    channel = system_fixed_text(raw[SYSTEM_HEADER_CHANNEL_OFFSET : SYSTEM_HEADER_CHANNEL_OFFSET + SYSTEM_CHANNEL_SIZE], "channel")
    if channel != "stable":
        raise Rejection(REASON_UNSUPPORTED, "canal ZSYS nao suportado")
    target = SystemBase(
        Version(
            *struct.unpack_from(
                "<HHH", raw, SYSTEM_HEADER_TARGET_VERSION_OFFSET
            )
        ),
        struct.unpack_from("<I", raw, SYSTEM_HEADER_TARGET_EPOCH_OFFSET)[0],
    )
    base_count, base_size = struct.unpack_from("<HH", raw, SYSTEM_HEADER_BASE_COUNT_OFFSET)
    if not 1 <= base_count <= SYSTEM_MAX_BASES or base_size != SYSTEM_BASE_ENTRY_SIZE:
        raise Rejection(REASON_FORMAT, "origens ZSYS invalidas")
    bases = tuple(
        system_unpack_version(raw, SYSTEM_HEADER_BASES_OFFSET + index * SYSTEM_BASE_ENTRY_SIZE, "supported_from")
        for index in range(base_count)
    )
    if any(
        system_version_compare(target.version, base.version) < 0
        or (
            system_version_compare(target.version, base.version) == 0
            and target.epoch <= base.epoch
        )
        for base in bases
    ):
        raise Rejection(REASON_COMPATIBILITY, "alvo ZSYS nao supera uma origem")
    base_end = SYSTEM_HEADER_BASES_OFFSET + base_count * SYSTEM_BASE_ENTRY_SIZE
    if any(raw[base_end:SYSTEM_HEADER_MIN_UPDATER_OFFSET]):
        raise Rejection(REASON_FORMAT, "origens ZSYS nao usadas nao estao zeradas")
    minimum_updater = system_unpack_version(raw, SYSTEM_HEADER_MIN_UPDATER_OFFSET, "min_updater")
    boot_abi, schema_from, schema_to = struct.unpack_from("<III", raw, SYSTEM_HEADER_BOOT_ABI_OFFSET)
    route_kind = raw[SYSTEM_HEADER_ROUTE_OFFSET]
    checkpoint_count = raw[SYSTEM_HEADER_CHECKPOINT_COUNT_OFFSET]
    if route_kind not in (SYSTEM_ROUTE_DIRECT, SYSTEM_ROUTE_CHECKPOINT) or checkpoint_count > SYSTEM_MAX_CHECKPOINTS:
        raise Rejection(REASON_UNSUPPORTED, "rota ZSYS invalida")
    checkpoints = tuple(
        system_fixed_text(raw[SYSTEM_HEADER_CHECKPOINTS_OFFSET + index * SYSTEM_CHECKPOINT_SIZE : SYSTEM_HEADER_CHECKPOINTS_OFFSET + (index + 1) * SYSTEM_CHECKPOINT_SIZE], "checkpoint")
        for index in range(checkpoint_count)
    )
    if route_kind == SYSTEM_ROUTE_DIRECT and checkpoints:
        raise Rejection(REASON_UNSUPPORTED, "rota direta ZSYS possui checkpoints")
    if (
        boot_abi != 1
        or schema_from > schema_to
        or not (flags & SYSTEM_FLAG_REQUIRES_REBOOT)
        or flags & SYSTEM_FLAG_BRIDGE_REQUIRED
    ):
        raise Rejection(REASON_UNSUPPORTED, "compatibilidade ZSYS nao suportada")
    checkpoint_end = (
        SYSTEM_HEADER_CHECKPOINTS_OFFSET + checkpoint_count * SYSTEM_CHECKPOINT_SIZE
    )
    if any(raw[checkpoint_end:SYSTEM_HEADER_KEY_ID_OFFSET]):
        raise Rejection(REASON_FORMAT, "checkpoints ZSYS nao usados nao estao zerados")
    component_count, component_size = struct.unpack_from("<HH", raw, SYSTEM_HEADER_COMPONENT_COUNT_OFFSET)
    checkpoint_size = struct.unpack_from("<H", raw, SYSTEM_HEADER_CHECKPOINT_ENTRY_SIZE_OFFSET)[0]
    if component_count != SYSTEM_COMPONENT_COUNT or component_size != SYSTEM_COMPONENT_ENTRY_SIZE or checkpoint_size != SYSTEM_CHECKPOINT_SIZE:
        raise Rejection(REASON_FORMAT, "tabelas ZSYS invalidas")
    components: list[SystemComponent] = []
    expected_kinds = (SYSTEM_COMPONENT_BOOT, SYSTEM_COMPONENT_STAGE2, SYSTEM_COMPONENT_KERNEL)
    previous_end = 0
    for index, kind in enumerate(expected_kinds):
        offset = SYSTEM_HEADER_COMPONENTS_OFFSET + index * SYSTEM_COMPONENT_ENTRY_SIZE
        actual_kind, flags_value, component_offset, component_length = struct.unpack_from("<HHII", raw, offset)
        digest = raw[offset + SYSTEM_COMPONENT_HASH_OFFSET : offset + SYSTEM_COMPONENT_HASH_OFFSET + 32]
        if (
            actual_kind != kind
            or flags_value
            or not component_length
            or component_offset + component_length > image_size
            or component_offset != previous_end
            or (index == 0 and (component_offset != 0 or component_length != 512))
            or (index == 1 and (component_offset != 512 or component_length % 512))
        ):
            raise Rejection(REASON_FORMAT, "componente ZSYS invalido")
        if any(raw[offset + 44 : offset + SYSTEM_COMPONENT_ENTRY_SIZE]):
            raise Rejection(REASON_FORMAT, "padding de componente ZSYS nao zero")
        previous_end = component_offset + component_length
        components.append(SystemComponent(kind, component_offset, component_length, digest))
    compatibility = SystemCompatibility(
        bases, minimum_updater, boot_abi, schema_from, schema_to,
        bool(flags & SYSTEM_FLAG_REQUIRES_REBOOT), channel, route_kind,
        checkpoints, bool(flags & SYSTEM_FLAG_BRIDGE_REQUIRED),
    )
    package = SystemPackage(
        release_id, release_tag, target.version, target.epoch, image_size,
        raw[SYSTEM_HEADER_IMAGE_HASH_OFFSET : SYSTEM_HEADER_IMAGE_HASH_OFFSET + 32],
        tuple(components), compatibility,
    )
    unsigned = bytearray(raw[:SYSTEM_HEADER_SIZE])
    unsigned[SYSTEM_HEADER_SIGNATURE_OFFSET : SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE] = bytes(SYSTEM_SIGNATURE_SIZE)
    signature = raw[SYSTEM_HEADER_SIGNATURE_OFFSET : SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE]
    if raw[SYSTEM_HEADER_KEY_ID_OFFSET : SYSTEM_HEADER_KEY_ID_OFFSET + SYSTEM_KEY_ID_SIZE] != trusted.key_id:
        raise Rejection(REASON_UNKNOWN_KEY, "key id ZSYS desconhecido")
    return package, bytes(unsigned) + signature


def parse_system_package(
    data: bytes,
    trusted: PublicKeyInfo,
    current_version: Version | None = None,
    current_epoch: int = 0,
) -> SystemPackage:
    """Valida completamente um ZSYS, inclusive assinatura e hashes."""
    if len(data) < SYSTEM_HEADER_SIZE:
        raise Rejection(REASON_SIZE, "ZSYS menor que o cabecalho")
    package, _ = system_parse_header(data[:SYSTEM_HEADER_SIZE], trusted)
    if len(data) != SYSTEM_HEADER_SIZE + package.image_size:
        raise Rejection(REASON_SIZE, "tamanho total ZSYS diverge")
    signature = data[SYSTEM_HEADER_SIGNATURE_OFFSET : SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE]
    unsigned = bytearray(data[:SYSTEM_HEADER_SIZE])
    unsigned[SYSTEM_HEADER_SIGNATURE_OFFSET : SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE] = bytes(SYSTEM_SIGNATURE_SIZE)
    ed25519, _, invalid_signature = crypto_modules()
    try:
        ed25519.Ed25519PublicKey.from_public_bytes(trusted.public_key).verify(
            signature, SYSTEM_DOMAIN + bytes(unsigned) + data[SYSTEM_PAYLOAD_OFFSET:]
        )
    except invalid_signature as error:
        raise Rejection(REASON_SIGNATURE, "assinatura ZSYS invalida") from error
    image = data[SYSTEM_PAYLOAD_OFFSET : SYSTEM_PAYLOAD_OFFSET + package.image_size]
    if hashlib.sha256(image).digest() != package.image_sha256:
        raise Rejection(REASON_HASH, "hash da imagem ZSYS diverge")
    for component in package.components:
        if hashlib.sha256(image[component.offset : component.offset + component.size]).digest() != component.sha256:
            raise Rejection(REASON_HASH, "hash de componente ZSYS diverge")
    if current_version is not None:
        if SystemBase(current_version, current_epoch) not in package.compatibility.supported_from:
            raise Rejection(REASON_BASE_VERSION, "versao atual nao e suportada pelo ZSYS")
        if (
            system_version_compare(
                current_version, package.compatibility.minimum_updater.version
            ) < 0
            or (
                system_version_compare(
                    current_version,
                    package.compatibility.minimum_updater.version,
                )
                == 0
                and current_epoch < package.compatibility.minimum_updater.epoch
            )
        ):
            raise Rejection(REASON_UNSUPPORTED, "updater abaixo do minimo do ZSYS")
        if (
            system_version_compare(package.target_version, current_version) < 0
            or (
                system_version_compare(package.target_version, current_version)
                == 0
                and package.target_epoch < current_epoch
            )
        ):
            raise Rejection(REASON_DOWNGRADE, "ZSYS representa downgrade")
    return package


def system_compatibility_json(compatibility: SystemCompatibility) -> dict[str, Any]:
    """Converte compatibilidade assinada para o descritor JSON redundante."""
    return {
        "supported_from": [
            {"version": str(item.version), "epoch": item.epoch}
            for item in compatibility.supported_from
        ],
        "min_updater": {
            "version": str(compatibility.minimum_updater.version),
            "epoch": compatibility.minimum_updater.epoch,
        },
        "boot_abi": compatibility.boot_abi,
        "data_schema_from": compatibility.data_schema_from,
        "data_schema_to": compatibility.data_schema_to,
        "requires_reboot": compatibility.requires_reboot,
        "channel": compatibility.channel,
        "upgrade_route": {
            "kind": "direct" if compatibility.route_kind == SYSTEM_ROUTE_DIRECT else "checkpoint",
            "checkpoints": list(compatibility.checkpoints),
            "bridge_required": compatibility.bridge_required,
        },
    }


def system_compatibility_equal(first: SystemCompatibility,
                               second: SystemCompatibility) -> bool:
    """Compara todos os campos de compatibilidade autenticados."""
    return first == second


def system_load_file(path: Path, label: str) -> bytes:
    """Carrega um asset local sem aceitar caminho ausente ou vazio."""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise UpdateError(f"asset {label} ausente: {path}") from error
    if not data:
        raise UpdateError(f"asset {label} vazio: {path}")
    return data


def build_system_bundle(
    manifest_path: Path,
    image_path: Path,
    boot_path: Path,
    stage2_path: Path,
    kernel_path: Path,
    private_key: Any,
    public: PublicKeyInfo,
    output_path: Path,
) -> Path:
    """Gera um arquivo ZSYS v1 sem sobrescrever artefatos existentes."""
    manifest = system_manifest_spec(manifest_path)
    if private_public_info(private_key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    package = system_build_package(
        manifest,
        system_load_file(image_path, "imagem"),
        system_load_file(boot_path, "boot"),
        system_load_file(stage2_path, "stage2"),
        system_load_file(kernel_path, "kernel"),
        private_key,
        public,
    )
    parse_system_package(package, public)
    output = output_path.expanduser().resolve()
    if output.exists():
        raise UpdateError(f"arquivo ZSYS ja existe: {output}")
    if not output.parent.is_dir():
        raise UpdateError(f"diretorio pai do ZSYS nao existe: {output.parent}")
    write_new_bytes(output, package)
    return output


def system_descriptor_asset(name: str, data: bytes) -> dict[str, Any]:
    """Cria metadados de transporte para um asset ZSYS."""
    if not name or Path(name).name != name:
        raise UpdateError("nome de asset ZSYS invalido")
    return release_asset_metadata(name, data)


def release_v2_descriptor(
    release_id: str,
    release_name: str,
    source_commit: str,
    tag: str,
    compatibility: SystemCompatibility,
    legacy: dict[str, dict[str, Any]],
    runtime: dict[str, Any],
    system: dict[str, dict[str, Any]],
) -> str:
    """Serializa a Release combinada sem atribuir confianca ao JSON."""
    return json.dumps(
        {
            "format": "zephyros-release-v2",
            "release_id": release_id,
            "release_name": release_name,
            "channel": "stable",
            "source_commit": source_commit,
            "tag": tag,
            "compatibility": system_compatibility_json(compatibility),
            "legacy": legacy,
            "runtime": runtime,
            "system": system,
        },
        indent=2,
        ensure_ascii=True,
    ) + "\n"


def release_v2_asset(
    directory: Path, metadata: object, label: str, suffix: str | tuple[str, ...]
) -> tuple[str, bytes]:
    """Carrega e confere um asset de namespace da Release v2."""
    name, data, _ = load_release_asset(directory, metadata, label, suffix)
    return name, data


def load_release_v2_descriptor(path: Path) -> dict[str, Any]:
    """Le o descritor combinado com ordem e namespaces canonicos."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError(f"nao foi possivel ler Release v2: {path}") from error
    if not isinstance(data, dict) or tuple(data) != (
        "format", "release_id", "release_name", "channel", "source_commit",
        "tag", "compatibility", "legacy", "runtime", "system",
    ):
        raise UpdateError("descritor da Release v2 possui campos invalidos")
    if data["format"] != "zephyros-release-v2" or data["channel"] != "stable":
        raise UpdateError("formato ou canal da Release v2 invalido")
    return data


def release_v2_compatibility(data: object) -> SystemCompatibility:
    """Valida e converte a compatibilidade redundante do descritor v2."""
    if not isinstance(data, dict) or tuple(data) != (
        "supported_from", "min_updater", "boot_abi", "data_schema_from",
        "data_schema_to", "requires_reboot", "channel", "upgrade_route",
    ):
        raise UpdateError("compatibilidade da Release v2 invalida")
    value = {
        "format": "ZSYS v1",
        "release_id": "release",
        "release_tag": "release",
        "target_version": "0.0.0",
        "target_epoch": 0,
        "supported_from": data["supported_from"],
        "min_updater": data["min_updater"],
        "boot_abi": data["boot_abi"],
        "data_schema_from": data["data_schema_from"],
        "data_schema_to": data["data_schema_to"],
        "requires_reboot": data["requires_reboot"],
        "channel": data["channel"],
        "upgrade_route": data["upgrade_route"],
    }
    return system_manifest_spec_from_data(value).compatibility


def system_manifest_spec_from_data(data: dict[str, Any]) -> SystemManifest:
    """Valida uma especificacao ZSYS ja decodificada."""
    supported = data["supported_from"]
    minimum = data["min_updater"]
    route = data["upgrade_route"]
    if not isinstance(route, dict):
        raise UpdateError("upgrade_route ZSYS invalido")
    normalized = {
        "format": "ZSYS v1",
        "release_id": data["release_id"],
        "release_tag": data["release_tag"],
        "target_version": data["target_version"],
        "target_epoch": data["target_epoch"],
        "supported_from": supported,
        "min_updater": minimum,
        "boot_abi": data["boot_abi"],
        "data_schema_from": data["data_schema_from"],
        "data_schema_to": data["data_schema_to"],
        "requires_reboot": data["requires_reboot"],
        "channel": data["channel"],
        "upgrade_route": route,
    }
    return SystemManifest(
        system_identifier(normalized["release_id"], "release_id"),
        system_identifier(normalized["release_tag"], "release_tag"),
        Version.parse(normalized["target_version"]),
        normalized["target_epoch"],
        system_compatibility_from_values(normalized),
    )


def system_compatibility_from_values(data: dict[str, Any]) -> SystemCompatibility:
    """Valida os campos de compatibilidade sem depender de arquivo temporario."""
    raw_supported = data["supported_from"]
    if not isinstance(raw_supported, list) or not 1 <= len(raw_supported) <= SYSTEM_MAX_BASES:
        raise UpdateError("supported_from ZSYS invalido")
    supported = tuple(system_base_spec(item, "supported_from") for item in raw_supported)
    if len({(item.version, item.epoch) for item in supported}) != len(supported):
        raise UpdateError("supported_from ZSYS possui duplicatas")
    minimum = system_base_spec(data["min_updater"], "min_updater")
    if not isinstance(data["boot_abi"], int) or data["boot_abi"] != 1:
        raise UpdateError("boot_abi ZSYS invalido")
    if not isinstance(data["data_schema_from"], int) or not isinstance(data["data_schema_to"], int) or data["data_schema_from"] > data["data_schema_to"]:
        raise UpdateError("schema ZSYS invalido")
    if data["requires_reboot"] is not True or data["channel"] != "stable":
        raise UpdateError("politica ZSYS invalida")
    route = data["upgrade_route"]
    if not isinstance(route, dict) or tuple(route) != ("kind", "checkpoints", "bridge_required"):
        raise UpdateError("upgrade_route ZSYS invalido")
    route_kind = {"direct": SYSTEM_ROUTE_DIRECT, "checkpoint": SYSTEM_ROUTE_CHECKPOINT}.get(route["kind"])
    checkpoints = route["checkpoints"]
    if route_kind is None or not isinstance(checkpoints, list) or len(checkpoints) > SYSTEM_MAX_CHECKPOINTS:
        raise UpdateError("rota ZSYS invalida")
    checkpoint_values = tuple(system_identifier(item, "checkpoint") for item in checkpoints)
    if len(set(checkpoint_values)) != len(checkpoint_values):
        raise UpdateError("checkpoints ZSYS possuem duplicatas")
    if route_kind == SYSTEM_ROUTE_DIRECT and checkpoint_values:
        raise UpdateError("rota direta nao pode possuir checkpoints")
    if route["bridge_required"] is not False:
        raise UpdateError("bridge_required ZSYS invalido")
    return SystemCompatibility(
        supported, minimum, data["boot_abi"], data["data_schema_from"],
        data["data_schema_to"], True, data["channel"], route_kind,
        checkpoint_values, False,
    )


def release_v2_runtime_namespace(directory: Path, trusted: PublicKeyInfo) -> dict[str, Any]:
    """Monta o namespace runtime a partir de uma Release EP6.3."""
    manifest_data = system_load_file(directory / "runtime.zum2", "runtime.zum2")
    package_data = system_load_file(directory / "runtime.zephyrosupd", "runtime.zephyrosupd")
    manifest = parse_runtime_manifest(manifest_data, trusted)
    parse_runtime_package(package_data, trusted, manifest)
    assets = []
    for entry in manifest.entries:
        if not entry.target_present:
            continue
        asset = system_load_file(directory / entry.asset_name, entry.asset_name)
        assets.append(release_asset_metadata(entry.asset_name, asset))
    return {
        "zum2": release_asset_metadata("runtime.zum2", manifest_data),
        "zephyrosupd": release_asset_metadata("runtime.zephyrosupd", package_data),
        "assets": assets,
    }


def release_v2_legacy_namespace(directory: Path, trusted: PublicKeyInfo) -> dict[str, Any]:
    """Monta o namespace legado e valida ZUPD v1/ZUM1."""
    package_candidates = sorted(
        path for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() in (".zup", ".zephyrosupd")
    )
    manifest_candidates = sorted(
        path for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() == ".zum"
    )
    if len(package_candidates) != 1 or len(manifest_candidates) != 1:
        raise UpdateError("namespace legacy deve conter um ZUPD e um ZUM1")
    package_path = package_candidates[0]
    manifest_path = manifest_candidates[0]
    package = system_load_file(package_path, package_path.name)
    manifest = system_load_file(manifest_path, manifest_path.name)
    header, _ = parse_structure(package)
    verify_artifact(package, trusted, header["base"], header["base_epoch"])
    remote = parse_remote_manifest(manifest, trusted)
    if (
        remote.package_size != len(package)
        or remote.package_sha256 != hashlib.sha256(package).digest()
        or Path(remote.package_path).name != package_path.name
    ):
        raise UpdateError("namespace legacy diverge entre ZUPD e ZUM1")
    return {
        "package": release_asset_metadata(package_path.name, package),
        "manifest": release_asset_metadata(manifest_path.name, manifest),
    }


def release_v2_system_namespace(
    directory: Path, trusted: PublicKeyInfo
) -> tuple[dict[str, Any], SystemPackage]:
    """Monta o namespace system e valida o ZSYS assinado."""
    package = system_load_file(directory / "system.zsys", "system.zsys")
    parsed = parse_system_package(package, trusted)
    return {"zsys": system_descriptor_asset("system.zsys", package)}, parsed


def build_release_v2_bundle(
    release_id: str,
    release_name: str,
    legacy_dir: Path,
    runtime_dir: Path,
    system_dir: Path,
    private_key: Any,
    trusted: PublicKeyInfo,
    source_reference: str,
    tag: str,
    output_dir: Path,
) -> Path:
    """Gera atomicamente a Release combinada v2."""
    release_id, release_name = validate_release_identity(release_id, release_name)
    if not isinstance(tag, str) or not RELEASE_ID_RE.fullmatch(tag):
        raise UpdateError("tag da Release v2 invalida")
    if private_public_info(private_key) != trusted:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    source_commit = resolve_git_commit(source_reference)
    if resolve_git_commit(f"refs/tags/{tag}") != source_commit:
        raise UpdateError("tag nao aponta para o commit selecionado")
    legacy = release_v2_legacy_namespace(legacy_dir, trusted)
    runtime = release_v2_runtime_namespace(runtime_dir, trusted)
    system, system_package = release_v2_system_namespace(system_dir, trusted)
    if system_package.release_id != release_id or system_package.release_tag != tag:
        raise UpdateError("identidade do ZSYS diverge da Release v2")
    descriptor = release_v2_descriptor(
        release_id, release_name, source_commit, tag,
        system_package.compatibility, legacy, runtime, system,
    )
    output = output_dir.expanduser().resolve()
    if output.exists() or not output.parent.is_dir():
        raise UpdateError(f"diretorio de Release v2 indisponivel: {output}")
    try:
        with tempfile.TemporaryDirectory(prefix=".zephyros-release-v2-", dir=output.parent) as temp_name:
            temporary = Path(temp_name)
            for source_dir, names in (
                (
                    legacy_dir,
                    (legacy["package"]["name"], legacy["manifest"]["name"]),
                ),
                (runtime_dir, ("runtime.zum2", "runtime.zephyrosupd")),
                (system_dir, ("system.zsys",)),
            ):
                for name in names:
                    write_new_bytes(temporary / name, system_load_file(source_dir / name, name))
            for metadata in runtime["assets"]:
                name = metadata["name"]
                write_new_bytes(temporary / name, system_load_file(runtime_dir / name, name))
            write_new_text(temporary / "release.json", descriptor)
            temporary.replace(output)
    except OSError as error:
        raise UpdateError("nao foi possivel publicar a Release v2") from error
    return output / "release.json"


def verify_release_v2_bundle(
    descriptor_path: Path,
    trusted: PublicKeyInfo,
    repository: Path = REPO_ROOT,
    resolver: Any | None = None,
) -> SystemPackage:
    """Valida a Release v2 e exige coerencia com o ZSYS assinado."""
    if resolver is None:
        resolver = resolve_git_commit
    data = load_release_v2_descriptor(descriptor_path)
    release_id, release_name = validate_release_identity(data["release_id"], data["release_name"])
    source_commit = data["source_commit"]
    if not isinstance(source_commit, str) or not GIT_COMMIT_RE.fullmatch(source_commit):
        raise UpdateError("source_commit da Release v2 invalido")
    if resolver(source_commit, repository) != source_commit:
        raise UpdateError("source_commit da Release v2 divergiu")
    tag = data["tag"]
    if not isinstance(tag, str) or resolver(f"refs/tags/{tag}", repository) != source_commit:
        raise UpdateError("tag da Release v2 divergiu")
    descriptor_compat = release_v2_compatibility(data["compatibility"])
    root = descriptor_path.parent
    legacy = data["legacy"]
    runtime = data["runtime"]
    system = data["system"]
    if not isinstance(legacy, dict) or tuple(legacy) != ("package", "manifest"):
        raise UpdateError("namespace legacy da Release v2 invalido")
    if not isinstance(runtime, dict) or tuple(runtime) != ("zum2", "zephyrosupd", "assets"):
        raise UpdateError("namespace runtime da Release v2 invalido")
    if not isinstance(system, dict) or tuple(system) != ("zsys",):
        raise UpdateError("namespace system da Release v2 invalido")
    package_name, package = release_v2_asset(root, legacy["package"], "ZUPD legado", (".zephyrosupd", ".zup"))
    manifest_name, manifest = release_v2_asset(root, legacy["manifest"], "ZUM1 legado", ".zum")
    header, _ = parse_structure(package)
    verify_artifact(package, trusted, header["base"], header["base_epoch"])
    remote = parse_remote_manifest(manifest, trusted)
    if remote.package_size != len(package) or remote.package_sha256 != hashlib.sha256(package).digest() or Path(remote.package_path).name != package_name:
        raise UpdateError("namespace legacy da Release v2 diverge")
    runtime_manifest_name, runtime_manifest_data = release_v2_asset(root, runtime["zum2"], "ZUM2", ".zum2")
    del runtime_manifest_name
    runtime_package_name, runtime_package_data = release_v2_asset(root, runtime["zephyrosupd"], "ZUPD v2", (".zephyrosupd", ".zup"))
    del runtime_package_name
    runtime_manifest = parse_runtime_manifest(runtime_manifest_data, trusted)
    parse_runtime_package(runtime_package_data, trusted, runtime_manifest)
    assets = runtime["assets"]
    if not isinstance(assets, list):
        raise UpdateError("assets runtime da Release v2 invalidos")
    expected_assets = {
        entry.asset_name for entry in runtime_manifest.entries if entry.target_present
    }
    if len(assets) != len(expected_assets):
        raise UpdateError("catalogo de assets runtime da Release v2 incompleto")
    seen_assets: set[str] = set()
    for metadata in assets:
        name, data_asset = release_v2_asset(root, metadata, "asset runtime", ".bmp")
        if name in seen_assets:
            raise UpdateError("asset runtime duplicado na Release v2")
        seen_assets.add(name)
        expected = next((entry for entry in runtime_manifest.entries if entry.asset_name == name), None)
        if expected is None or len(data_asset) != expected.asset_size or hashlib.sha256(data_asset).digest() != expected.asset_sha256:
            raise UpdateError("asset runtime nao corresponde ao ZUM2")
    if seen_assets != expected_assets:
        raise UpdateError("catalogo de assets runtime da Release v2 diverge")
    zsys_name, zsys_data = release_v2_asset(root, system["zsys"], "ZSYS", ".zsys")
    if zsys_name != "system.zsys":
        raise UpdateError("nome do ZSYS da Release v2 invalido")
    system_package = parse_system_package(zsys_data, trusted)
    if system_package.release_id != release_id or system_package.release_tag != tag:
        raise UpdateError("identidade do ZSYS diverge da Release v2")
    if (
        runtime_manifest.release_id != release_id
        or runtime_manifest.release_tag != tag
        or runtime_manifest.target_version != system_package.target_version
        or runtime_manifest.target_epoch != system_package.target_epoch
    ):
        raise UpdateError("identidade ou alvo runtime diverge do ZSYS")
    if not system_compatibility_equal(system_package.compatibility, descriptor_compat):
        raise UpdateError("compatibilidade da Release v2 diverge do ZSYS")
    return system_package


def current_system_version() -> tuple[Version, int]:
    """Le a versao canonica compartilhada com o kernel."""
    values: dict[str, int] = {}
    try:
        lines = VERSION_HEADER.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise UpdateError(f"header de versao ausente: {VERSION_HEADER}") from error
    for line in lines:
        match = DEFINE_RE.match(line)
        if match:
            values[match.group(1)] = int(match.group(2))
    required = (
        "ZEPHYROS_VERSION_MAJOR",
        "ZEPHYROS_VERSION_MINOR",
        "ZEPHYROS_VERSION_PATCH",
        "ZEPHYROS_VERSION_EPOCH",
    )
    if any(name not in values for name in required):
        raise UpdateError("header de versao nao contem todos os campos canonicos")
    return (
        Version(
            values["ZEPHYROS_VERSION_MAJOR"],
            values["ZEPHYROS_VERSION_MINOR"],
            values["ZEPHYROS_VERSION_PATCH"],
        ),
        values["ZEPHYROS_VERSION_EPOCH"],
    )


def c_bytes(data: bytes) -> str:
    """Formata bytes publicos como inicializador C estavel."""
    return ", ".join(f"0x{byte:02X}U" for byte in data)


def render_trust_header(info: PublicKeyInfo) -> str:
    """Gera o header confiavel sem qualquer material privado."""
    return (
        "#ifndef UPDATE_TRUST_H\n"
        "#define UPDATE_TRUST_H\n\n"
        '#include "types.h"\n\n'
        "/* Gerado por tools/updater.py; contem apenas material publico. */\n"
        f'#define UPDATE_TRUST_PUBLIC_KEY_HEX "{info.public_key.hex()}"\n'
        f'#define UPDATE_TRUST_KEY_ID_HEX "{info.key_id.hex()}"\n\n'
        "static const uint8_t UPDATE_TRUST_PUBLIC_KEY[32] = {\n"
        f"    {c_bytes(info.public_key)}\n"
        "};\n\n"
        "static const uint8_t UPDATE_TRUST_KEY_ID[16] = {\n"
        f"    {c_bytes(info.key_id)}\n"
        "};\n\n"
        "#endif\n"
    )


def sync_trust(public_path: Path, output_path: Path) -> None:
    """Cria um header novo a partir do JSON publico."""
    info = load_public_json(public_path)
    output = require_new_file(output_path, "header de confianca")
    write_new_text(output, render_trust_header(info))


def check_trust(public_path: Path, header_path: Path) -> None:
    """Confere se JSON e header publico sao exatamente equivalentes."""
    info = load_public_json(public_path)
    try:
        actual = header_path.read_text(encoding="utf-8")
    except OSError as error:
        raise UpdateError(f"nao foi possivel ler {header_path}") from error
    if actual != render_trust_header(info):
        raise UpdateError("header de confianca esta dessincronizado")


def resign_with_bad_manifest(valid: bytes, private_key: Any) -> bytes:
    """Cria uma tabela malformada, com hashes e assinatura ainda coerentes."""
    malformed = bytearray(valid[:-ZUPD_SIGNATURE_SIZE])
    malformed[ZUPD_HEADER_SIZE + 116] = 1
    signature_offset = len(malformed)
    content_hash = hashlib.sha256(malformed[ZUPD_HEADER_SIZE:]).digest()
    malformed[88:120] = content_hash
    signature = private_key.sign(ZUPD_DOMAIN + malformed)
    if len(signature) != ZUPD_SIGNATURE_SIZE or signature_offset + len(signature) != len(valid):
        raise UpdateError("falha interna ao criar fixture malformada")
    return bytes(malformed) + signature


def fixture_blobs(private_key: Any) -> dict[str, tuple[bytes, str]]:
    """Produz a matriz publica de diagnosticos da U2."""
    payload = SHELL_BMP.read_bytes()
    valid = build_from_parts(
        private_key,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        [("SHELL.BMP", payload)],
    )
    bad_hash = bytearray(valid)
    bad_hash[ZUPD_HEADER_SIZE + ZUPD_ENTRY_SIZE] ^= 1
    bad_signature = bytearray(valid)
    bad_signature[-1] ^= 1
    bad_version = build_from_parts(
        private_key,
        Version(9, 0, 0),
        Version(9, 0, 1),
        0,
        0,
        [("SHELL.BMP", payload)],
    )
    unknown = (U1_FIXTURES / "unknown-key.zephyrosupd").read_bytes()
    return {
        "VALID.ZUP": (valid, REASON_NONE),
        "TRUNC.ZUP": (valid[:-1], REASON_SIZE),
        "BADHASH.ZUP": (bytes(bad_hash), REASON_HASH),
        "BADSIG.ZUP": (bytes(bad_signature), REASON_SIGNATURE),
        "BADVER.ZUP": (bad_version, REASON_BASE_VERSION),
        "BADFMT.ZUP": (resign_with_bad_manifest(valid, private_key), REASON_FORMAT),
        "UNKKEY.ZUP": (unknown, REASON_UNKNOWN_KEY),
    }


def write_fixtures(private_key: Any, output_dir: Path) -> None:
    """Grava fixtures U2 e seu manifesto publico sem sobrescrever dados."""
    output = output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise UpdateError(f"diretorio de fixtures nao esta vazio: {output}")
    output.mkdir(parents=True, exist_ok=True)
    info = private_public_info(private_key)
    blobs = fixture_blobs(private_key)
    manifest: dict[str, Any] = {
        "format": "ZUPD v1",
        "contract": "../../../14-atualizacoes/contrato-zupd-v1.md",
        "release_public_key": {
            "public_key_hex": info.public_key.hex(),
            "key_id_hex": info.key_id.hex(),
        },
        "fixtures": {},
    }
    for name, (data, reason) in blobs.items():
        write_new_bytes(output / name, data)
        manifest["fixtures"][name] = {
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "expected_reason": f"ZUPD_REASON_{reason}",
        }
    write_new_text(
        output / "fixtures.json",
        json.dumps(manifest, indent=2, sort_keys=False) + "\n",
    )


def invert_bmp_pixels(data: bytes) -> bytes:
    """Inverte apenas bytes RGB de um BMP 24-bit sem alterar padding/header."""
    if len(data) < 54 or data[:2] != b"BM":
        raise UpdateError("asset U3 nao e um BMP valido")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if (
        dib_size < 40
        or width <= 0
        or height == 0
        or planes != 1
        or bits_per_pixel != 24
        or compression != 0
    ):
        raise UpdateError("fixture U3 requer BMP 24-bit sem compressao")
    row_pixels = width * 3
    row_size = (row_pixels + 3) & ~3
    rows = abs(height)
    if pixel_offset < 14 + dib_size or pixel_offset + row_size * rows > len(data):
        raise UpdateError("layout de pixels do BMP U3 e invalido")
    transformed = bytearray(data)
    for row in range(rows):
        start = pixel_offset + row * row_size
        for offset in range(start, start + row_pixels):
            transformed[offset] ^= 0xFF
    return bytes(transformed)


def u3_fixture_payloads() -> list[tuple[str, bytes]]:
    """Produz os tres BMPs publicos e deterministicamente transformados."""
    payloads: list[tuple[str, bytes]] = []
    for name in UPDATE_TARGETS:
        try:
            original = UPDATE_ASSETS[name].read_bytes()
        except OSError as error:
            raise UpdateError(f"asset U3 ausente: {name}") from error
        transformed = invert_bmp_pixels(original)
        if len(transformed) != len(original) or transformed == original:
            raise UpdateError(f"transformacao U3 invalida para {name}")
        payloads.append((name, transformed))
    return payloads


def write_u3_fixtures(private_key: Any, output_dir: Path) -> None:
    """Grava APPLY.ZUP, payloads publicos e manifesto sem sobrescrever."""
    output = output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise UpdateError(f"diretorio de fixtures nao esta vazio: {output}")
    output.mkdir(parents=True, exist_ok=True)
    info = private_public_info(private_key)
    payloads = u3_fixture_payloads()
    artifact = build_from_parts(
        private_key,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        payloads,
    )
    manifest: dict[str, Any] = {
        "format": "ZUPD v1",
        "contract": "../../../14-atualizacoes/contrato-zupd-v1.md",
        "transformation": "invert-rgb-24bit-v1",
        "base_version": "0.1.0",
        "target_version": "0.1.1",
        "base_epoch": 0,
        "target_epoch": 0,
        "release_public_key": {
            "public_key_hex": info.public_key.hex(),
            "key_id_hex": info.key_id.hex(),
        },
        "payloads": {},
        "artifact": {
            "name": "APPLY.ZUP",
            "size": len(artifact),
            "sha256": hashlib.sha256(artifact).hexdigest(),
            "expected_reason": "ZUPD_REASON_NONE",
        },
    }
    for name, payload in payloads:
        write_new_bytes(output / name, payload)
        manifest["payloads"][name] = {
            "source_sha256": hashlib.sha256(
                UPDATE_ASSETS[name].read_bytes()
            ).hexdigest(),
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
    write_new_bytes(output / "APPLY.ZUP", artifact)
    write_new_text(
        output / "fixtures.json",
        json.dumps(manifest, indent=2, sort_keys=False) + "\n",
    )


def write_u5_fixtures(private_key: Any, output_dir: Path) -> None:
    """Gera manifestos remotos publicos sem copiar a chave privada."""
    output = output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise UpdateError(f"diretorio de fixtures nao esta vazio: {output}")
    output.mkdir(parents=True, exist_ok=True)
    public = private_public_info(private_key)
    apply_package = (U3_FIXTURES / "APPLY.ZUP").read_bytes()
    bad_package = (U2_FIXTURES / "BADHASH.ZUP").read_bytes()
    stable = build_remote_manifest(
        private_key,
        public,
        1,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        apply_package,
        "/zephyros/APPLY.ZUP",
    )
    stable2 = build_remote_manifest(
        private_key,
        public,
        2,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        apply_package,
        "/zephyros/APPLY.ZUP",
    )
    badpkg = build_remote_manifest(
        private_key,
        public,
        3,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        bad_package,
        "/zephyros/BADHASH.ZUP",
    )
    tampered = bytearray(stable)
    tampered[40] ^= 1
    artifact = verify_artifact(
        apply_package, public, Version(0, 1, 0), 0
    )
    releases = {
        "ep6-stable.json": json.loads(
            release_descriptor(
                "ep6-stable",
                "EP6 fixture stable",
                "1" * 40,
                "ep6-stable",
                artifact,
                "APPLY.ZUP",
                apply_package,
                "stable.zum",
                stable,
            )
        ),
        "ep6-alt.json": json.loads(
            release_descriptor(
                "ep6-alt",
                "EP6 fixture alternate",
                "2" * 40,
                "ep6-alt",
                artifact,
                "APPLY.ZUP",
                apply_package,
                "stable2.zum",
                stable2,
            )
        ),
    }
    invalid_releases = {"ep6-invalid-json.json": None}
    missing_tag = json.loads(json.dumps(releases["ep6-stable.json"]))
    missing_tag.pop("tag")
    invalid_releases["ep6-no-tag.json"] = missing_tag
    missing_asset = json.loads(json.dumps(releases["ep6-stable.json"]))
    missing_asset["release_id"] = "ep6-missing-asset"
    missing_asset["release_name"] = "EP6 fixture missing asset"
    missing_asset["source_commit"] = "5" * 40
    missing_asset["tag"] = "ep6-missing-asset"
    missing_asset["assets"].pop("manifest")
    invalid_releases["ep6-missing-asset.json"] = missing_asset
    invalid_releases["ep6-tampered-manifest.json"] = json.loads(
        release_descriptor(
            "ep6-tampered-manifest",
            "EP6 fixture tampered manifest",
            "3" * 40,
            "ep6-tampered-manifest",
            artifact,
            "APPLY.ZUP",
            apply_package,
            "tampered.zum",
            bytes(tampered),
        )
    )
    invalid_releases["ep6-invalid-package.json"] = json.loads(
        release_descriptor(
            "ep6-invalid-package",
            "EP6 fixture invalid package",
            "4" * 40,
            "ep6-invalid-package",
            artifact,
            "BADHASH.ZUP",
            bad_package,
            "badpkg.zum",
            badpkg,
        )
    )
    invalid_json = "{\n  \"format\": \"zephyros-release-v1\"\n"
    divergent_hash = json.loads(json.dumps(releases["ep6-stable.json"]))
    divergent_hash["release_id"] = "ep6-divergent-hash"
    divergent_hash["release_name"] = "EP6 fixture divergent hash"
    divergent_hash["source_commit"] = "6" * 40
    divergent_hash["tag"] = "ep6-divergent-hash"
    divergent_hash["assets"]["package"]["sha256"] = "0" * 64
    invalid_releases["ep6-divergent-hash.json"] = divergent_hash
    divergent_lock = json.loads(json.dumps(releases["ep6-stable.json"]))
    divergent_lock["release_id"] = "ep6-divergent-lock"
    divergent_lock["release_name"] = "EP6 fixture divergent lock"
    divergent_lock["source_commit"] = "7" * 40
    divergent_lock["tag"] = "ep6-divergent-lock"
    divergent_lock["version_lock"]["target_version"] = "0.1.2"
    invalid_releases["ep6-divergent-lock.json"] = divergent_lock
    divergent_tag = json.loads(json.dumps(releases["ep6-stable.json"]))
    divergent_tag["tag"] = "ep6-other"
    invalid_releases["ep6-divergent-tag.json"] = divergent_tag
    fixtures = {
        "stable.zum": (stable, "OK"),
        "stable2.zum": (stable2, "OK"),
        "tampered.zum": (bytes(tampered), "MANIFEST_SIGNATURE"),
        "badpkg.zum": (badpkg, "PACKAGE_VERIFY/HASH"),
        "truncated.zum": (stable[:-1], "HTTP"),
    }
    published: dict[str, Any] = {
        "format": "zephyros-update-remote-fixtures-v1",
        "release_public_key": {
            "public_key_hex": public.public_key.hex(),
            "key_id_hex": public.key_id.hex(),
        },
        "packages": {
            "APPLY.ZUP": {
                "source": "../u3/APPLY.ZUP",
                "size": len(apply_package),
                "sha256": hashlib.sha256(apply_package).hexdigest(),
            },
            "BADHASH.ZUP": {
                "source": "../u2/BADHASH.ZUP",
                "size": len(bad_package),
                "sha256": hashlib.sha256(bad_package).hexdigest(),
            },
        },
        "fixtures": {},
        "releases": {},
        "invalid_releases": {},
    }
    for name, (data, expected) in fixtures.items():
        write_new_bytes(output / name, data)
        published["fixtures"][name] = {
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "expected": expected,
        }
    for name, descriptor in releases.items():
        data = json.dumps(descriptor, indent=2, ensure_ascii=True) + "\n"
        write_new_text(output / name, data)
        encoded = data.encode("utf-8")
        published["releases"][name] = {
            "tag": descriptor["tag"],
            "size": len(encoded),
            "sha256": hashlib.sha256(encoded).hexdigest(),
        }
    for name, descriptor in invalid_releases.items():
        if name == "ep6-invalid-json.json":
            data = invalid_json
        else:
            data = json.dumps(descriptor, indent=2, ensure_ascii=True) + "\n"
        write_new_text(output / name, data)
        encoded = data.encode("utf-8")
        published["invalid_releases"][name] = {
            "size": len(encoded),
            "sha256": hashlib.sha256(encoded).hexdigest(),
        }
    write_new_text(
        output / "fixtures.json",
        json.dumps(published, indent=2, sort_keys=False) + "\n",
    )


def write_github_fixtures(
    private_key: Any, public: PublicKeyInfo, output_dir: Path, tag: str
) -> None:
    """Gera os tres assets publicos usados pelo servidor HTTPS GitHub."""
    if not RELEASE_ID_RE.fullmatch(tag):
        raise UpdateError("tag GitHub de fixture invalida")
    output = output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise UpdateError(f"diretorio de fixtures GitHub nao esta vazio: {output}")
    output.mkdir(parents=True, exist_ok=True)
    package = (U3_FIXTURES / "APPLY.ZUP").read_bytes()
    artifact = verify_artifact(package, public, Version(0, 1, 0), 0)
    manifest = build_remote_manifest(
        private_key,
        public,
        62,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        package,
        f"/releases/download/{tag}/update.zephyrosupd",
    )
    descriptor = release_descriptor(
        "ep62-fixture",
        "EP6.2 HTTPS fixture",
        "8" * 40,
        tag,
        artifact,
        "update.zephyrosupd",
        package,
        "release.zum",
        manifest,
    ).encode("utf-8")
    write_new_text(output / "release.json", descriptor.decode("utf-8"))
    write_new_bytes(output / "release.zum", manifest)
    write_new_bytes(output / "update.zephyrosupd", package)
    published = {
        "format": "zephyros-update-github-fixtures-v1",
        "tag": tag,
        "assets": {
            name: {
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            for name, data in (
                ("release.json", descriptor),
                ("release.zum", manifest),
                ("update.zephyrosupd", package),
            )
        },
    }
    write_new_text(
        output / "fixtures.json",
        json.dumps(published, indent=2, sort_keys=False) + "\n",
    )


def command_fixtures_github(args: argparse.Namespace) -> None:
    """Gera release.json, release.zum e update.zephyrosupd para HTTPS."""
    key = load_private_key(Path(args.private), prompt_password())
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_github_fixtures(key, public, Path(args.output_dir), args.tag)
    print(f"Fixtures GitHub criadas em {Path(args.output_dir).resolve()}")
    print("Certificado e chave TLS devem permanecer fora do repositorio.")


def command_serve_github(args: argparse.Namespace) -> None:
    """Serve a API de Release e os assets por HTTPS com certificados externos."""
    root = Path(args.root).resolve()
    cert = Path(args.cert).resolve()
    key = Path(args.key).resolve()
    names = ("release.json", "release.zum", "update.zephyrosupd")
    assets = {name: root / name for name in names}
    missing = [path for path in (*assets.values(), cert, key) if not path.is_file()]
    if missing:
        raise UpdateError(f"fixture GitHub ou credencial TLS ausente: {missing[0]}")
    api_path = (
        f"/repos/{args.owner}/{args.repository}/releases/tags/{args.tag}"
    )
    asset_prefix = f"/releases/download/{args.tag}"
    redirect_http_prefix = f"/fixtures/redirect-http{api_path}"
    redirect_https_prefix = f"/fixtures/redirect-https{api_path}"
    public_base = f"https://{args.public_host}:{args.port}"
    asset_data = {name: path.read_bytes() for name, path in assets.items()}

    def api_payload() -> dict[str, Any]:
        entries = [
            {
                "name": name,
                "size": len(data),
                "browser_download_url": f"{public_base}{asset_prefix}/{name}",
                "state": "uploaded",
                "digest": f"sha256:{hashlib.sha256(data).hexdigest()}",
            }
            for name, data in asset_data.items()
        ]
        payload: dict[str, Any] = {
            "id": 62062,
            "tag_name": args.tag,
            "name": "EP6.2 HTTPS fixture",
            "draft": False,
            "prerelease": False,
            "published_at": "2026-08-22T12:00:00Z",
            "assets": entries,
        }
        if args.variant == "missing-asset":
            payload["assets"] = entries[:-1]
        elif args.variant == "tag-divergent":
            payload["tag_name"] = f"{args.tag}-other"
        elif args.variant == "draft":
            payload["draft"] = True
        elif args.variant == "prerelease":
            payload["prerelease"] = True
        elif args.variant == "bad-digest":
            payload["assets"][0]["digest"] = f"sha256:{'0' * 64}"
        return payload

    class GitHubHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == redirect_http_prefix:
                self.send_response(302)
                self.send_header("Location", f"http://{args.public_host}:{args.port}{api_path}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path == redirect_https_prefix:
                self.send_response(302)
                self.send_header("Location", f"{public_base}{api_path}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path == api_path:
                if args.variant == "invalid-json":
                    data = b'{"tag_name":'
                else:
                    data = json.dumps(api_payload(), separators=(",", ":")).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/vnd.github+json")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data)
                return
            for name, data in asset_data.items():
                if self.path == f"{asset_prefix}/{name}":
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(data)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(data)
                    return
            self.send_error(404, "fixture GitHub inexistente")

        def log_message(self, format: str, *values: object) -> None:
            print(f"GitHub HTTPS {self.address_string()} - {format % values}")

    server = http.server.ThreadingHTTPServer((args.bind, args.port), GitHubHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    # A fixture must exercise the same TLS 1.2 contract as the kernel client.
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    try:
        context.load_cert_chain(certfile=cert, keyfile=key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    except (OSError, ssl.SSLError) as error:
        server.server_close()
        raise UpdateError("certificado ou chave TLS de fixture invalido") from error
    print(f"Servidor GitHub HTTPS em {public_base}{api_path}")
    print(f"Variante da API: {args.variant}")
    print("Pressione Ctrl+C para encerrar.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def load_remote_config(path: Path) -> dict[str, str]:
    """Valida a configuracao publica do canal remoto."""
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError("configuracao remota invalida") from error
    if not isinstance(config, dict) or tuple(config) != (
        "format",
        "channel",
        "manifest_url",
        "release_url_template",
        "github_api_url",
        "github_owner",
        "github_repository",
        "github_release_url_template",
        "github_api_version",
        "github_descriptor_name",
        "github_manifest_name",
        "github_package_name",
        "github_system_name",
        "runtime_release_url_template",
        "runtime_manifest_url",
        "runtime_package_name",
        "github_runtime_manifest_name",
        "github_runtime_package_name",
    ):
        raise UpdateError("campos da configuracao remota divergem")
    release_url_template = config["release_url_template"]
    github_release_url_template = config["github_release_url_template"]
    asset_names = (
        config["github_descriptor_name"],
        config["github_manifest_name"],
        config["github_package_name"],
        config["github_system_name"],
        config["github_runtime_manifest_name"],
        config["github_runtime_package_name"],
    )
    owner = config["github_owner"]
    repository = config["github_repository"]
    api_version = config["github_api_version"]
    if not isinstance(owner, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", owner):
        raise UpdateError("proprietario GitHub invalido")
    if not isinstance(repository, str) or not re.fullmatch(
        r"[A-Za-z0-9_.-]{1,100}", repository
    ):
        raise UpdateError("repositorio GitHub invalido")
    if not isinstance(api_version, str) or not re.fullmatch(
        r"[0-9]{4}-[0-9]{2}-[0-9]{2}", api_version
    ):
        raise UpdateError("versao da API GitHub invalida")
    if any(
        not isinstance(name, str)
        or not re.fullmatch(r"[A-Za-z0-9._-]{1,99}", name)
        for name in asset_names
    ) or len(set(asset_names)) != len(asset_names):
        raise UpdateError("nomes de assets GitHub invalidos")
    if (
        config["format"] != "zephyros-update-remote-v2"
        or config["channel"] != "stable"
        or not isinstance(config["manifest_url"], str)
        or not config["manifest_url"].startswith("http://")
        or len(config["manifest_url"]) > 511
        or not isinstance(release_url_template, str)
        or not release_url_template.startswith("http://")
        or len(release_url_template) > 511
        or release_url_template.count("{tag}") != 1
        or "{" in release_url_template.replace("{tag}", "")
        or "}" in release_url_template.replace("{tag}", "")
        or len(release_url_template.replace("{tag}", "A" * 64)) > 511
        or not isinstance(config["github_api_url"], str)
        or not config["github_api_url"].startswith("https://")
        or len(config["github_api_url"]) > 511
        or config["github_api_url"].endswith("/")
        or not isinstance(github_release_url_template, str)
        or not github_release_url_template.startswith("/")
        or len(github_release_url_template) > 511
        or github_release_url_template.count("{owner}") != 1
        or github_release_url_template.count("{repo}") != 1
        or github_release_url_template.count("{tag}") != 1
        or "{" in github_release_url_template.replace("{owner}", "")
        .replace("{repo}", "")
        .replace("{tag}", "")
        or "}" in github_release_url_template.replace("{owner}", "")
        .replace("{repo}", "")
        .replace("{tag}", "")
        or len(
            github_release_url_template.replace("{owner}", "A" * 64)
            .replace("{repo}", "A" * 100)
            .replace("{tag}", "A" * 64)
        ) > 511
        or not isinstance(api_version, str)
        or not isinstance(config["runtime_release_url_template"], str)
        or not config["runtime_release_url_template"].startswith("http://")
        or config["runtime_release_url_template"].count("{tag}") != 1
        or "{" in config["runtime_release_url_template"].replace("{tag}", "")
        or "}" in config["runtime_release_url_template"].replace("{tag}", "")
        or len(config["runtime_release_url_template"].replace("{tag}", "A" * 64)) > 511
        or not isinstance(config["runtime_manifest_url"], str)
        or not config["runtime_manifest_url"].startswith("http://")
        or len(config["runtime_manifest_url"]) > 511
        or not isinstance(config["runtime_package_name"], str)
        or not re.fullmatch(r"[A-Za-z0-9._-]{1,99}", config["runtime_package_name"])
    ):
        raise UpdateError("canal ou URL remota invalida")
    return config


def render_remote_config_header(config: dict[str, str]) -> str:
    """Renderiza o header derivado usado pelo kernel."""
    return (
        "#ifndef UPDATE_REMOTE_CONFIG_H\n"
        "#define UPDATE_REMOTE_CONFIG_H\n\n"
        f'#define UPDATE_REMOTE_CONFIG_FORMAT "{config["format"]}"\n'
        f'#define UPDATE_REMOTE_CHANNEL_NAME "{config["channel"]}"\n'
        "#define UPDATE_REMOTE_DEFAULT_MANIFEST_URL \\\n"
        f'    "{config["manifest_url"]}"\n'
        "#define UPDATE_REMOTE_RELEASE_URL_TEMPLATE \\\n"
        f'    "{config["release_url_template"]}"\n\n'
        "#define UPDATE_REMOTE_GITHUB_API_URL \\\n"
        f'    "{config["github_api_url"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_OWNER "{config["github_owner"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_REPOSITORY "{config["github_repository"]}"\n'
        "#define UPDATE_REMOTE_GITHUB_RELEASE_URL_TEMPLATE \\\n"
        f'    "{config["github_release_url_template"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_API_VERSION "{config["github_api_version"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_DESCRIPTOR_NAME "{config["github_descriptor_name"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_MANIFEST_NAME "{config["github_manifest_name"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_PACKAGE_NAME "{config["github_package_name"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_SYSTEM_NAME "{config["github_system_name"]}"\n\n'
        f'#define UPDATE_REMOTE_RUNTIME_RELEASE_URL_TEMPLATE \\\n'
        f'    "{config["runtime_release_url_template"]}"\n'
        f'#define UPDATE_REMOTE_RUNTIME_MANIFEST_URL \\\n'
        f'    "{config["runtime_manifest_url"]}"\n'
        f'#define UPDATE_REMOTE_RUNTIME_PACKAGE_NAME "{config["runtime_package_name"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_RUNTIME_MANIFEST_NAME "{config["github_runtime_manifest_name"]}"\n'
        f'#define UPDATE_REMOTE_GITHUB_RUNTIME_PACKAGE_NAME "{config["github_runtime_package_name"]}"\n\n'
        "#endif\n"
    )


def sync_remote_config(config_path: Path, output_path: Path) -> None:
    """Cria um header novo a partir da configuracao publica."""
    output = require_new_file(output_path, "header remoto")
    write_new_text(
        output, render_remote_config_header(load_remote_config(config_path))
    )


def check_remote_config(config_path: Path, header_path: Path) -> None:
    """Confere equivalencia exata entre JSON e header remoto."""
    expected = render_remote_config_header(load_remote_config(config_path))
    try:
        actual = header_path.read_text(encoding="utf-8")
    except OSError as error:
        raise UpdateError("header remoto nao pode ser lido") from error
    if actual != expected:
        raise UpdateError("header remoto esta dessincronizado")


def expected_reason(
    data: bytes,
    key: PublicKeyInfo,
    version: Version,
    epoch: int,
) -> str:
    """Retorna NONE ou o motivo publico de uma verificacao."""
    try:
        verify_artifact(data, key, version, epoch)
        return REASON_NONE
    except Rejection as error:
        return error.reason


def decode_stored_file(record: bytes, offset: int) -> StoredFileState:
    """Decodifica um descritor de arquivo de 40 bytes do estado U3."""
    size = struct.unpack_from("<I", record, offset)[0]
    digest = record[offset + 4 : offset + 36]
    present = record[offset + 36]
    if present not in (0, 1) or any(record[offset + 37 : offset + 40]):
        raise UpdateError("descritor de arquivo U3 invalido")
    if present and not 1 <= size <= ZUPD_MAX_PAYLOAD_SIZE:
        raise UpdateError("tamanho persistido de arquivo U3 invalido")
    if not present and (size != 0 or any(digest)):
        raise UpdateError("descritor ausente de arquivo U3 nao esta zerado")
    return StoredFileState(size, digest, bool(present))


def validate_control_record(record: bytes, magic: bytes) -> None:
    """Valida envelope, reservados comuns e SHA-256 de um controle U3."""
    if len(record) != UPDATE_CONTROL_SIZE:
        raise UpdateError("registro U3 nao possui 512 bytes")
    if (
        record[:4] != magic
        or struct.unpack_from("<H", record, 4)[0] != UPDATE_CONTROL_VERSION
        or struct.unpack_from("<H", record, 6)[0] != UPDATE_CONTROL_SIZE
    ):
        raise UpdateError("cabecalho de controle U3 invalido")
    expected = hashlib.sha256(record[:UPDATE_CONTROL_HASH_OFFSET]).digest()
    if expected != record[UPDATE_CONTROL_HASH_OFFSET:]:
        raise UpdateError("SHA-256 de controle U3 invalido")


def decode_state_record(record: bytes) -> StoredUpdateState:
    """Replica a validacao little-endian de ZUPD*.STA feita no kernel."""
    validate_control_record(record, b"ZUST")
    if (
        any(record[18:20])
        or record[27] != 0
        or any(record[34:36])
        or any(record[280:UPDATE_CONTROL_HASH_OFFSET])
    ):
        raise UpdateError("reservado de estado U3 nao esta zerado")
    current = tuple(
        decode_stored_file(
            record, UPDATE_STATE_CURRENT_OFFSET + index * UPDATE_STATE_FILE_SIZE
        )
        for index in range(UPDATE_TARGET_COUNT)
    )
    rollback = tuple(
        decode_stored_file(
            record, UPDATE_STATE_ROLLBACK_OFFSET + index * UPDATE_STATE_FILE_SIZE
        )
        for index in range(UPDATE_TARGET_COUNT)
    )
    rollback_available = record[24]
    rollback_slot = record[25]
    rollback_count = record[26]
    previous = Version(*struct.unpack_from("<3H", record, 28))
    previous_epoch = struct.unpack_from("<I", record, 36)[0]
    if rollback_available not in (0, 1) or rollback_slot not in (0, 1):
        raise UpdateError("flags de rollback U3 invalidas")
    if rollback_count != sum(item.present for item in rollback):
        raise UpdateError("contagem de rollback U3 divergente")
    if bool(rollback_available) != bool(rollback_count):
        raise UpdateError("disponibilidade de rollback U3 inconsistente")
    if not rollback_available and (
        previous != Version(0, 0, 0) or previous_epoch != 0
    ):
        raise UpdateError("versao anterior sem rollback U3")
    return StoredUpdateState(
        sequence=struct.unpack_from("<I", record, 8)[0],
        installed_version=Version(*struct.unpack_from("<3H", record, 12)),
        installed_epoch=struct.unpack_from("<I", record, 20)[0],
        rollback_available=bool(rollback_available),
        rollback_slot=rollback_slot,
        rollback_entry_count=rollback_count,
        previous_version=previous,
        previous_epoch=previous_epoch,
        current=current,
        rollback=rollback,
    )


def decode_journal_record(record: bytes) -> StoredJournal:
    """Replica a validacao estrutural de ZUPD*.JRN feita no kernel."""
    validate_control_record(record, b"ZUJ1")
    if (
        any(record[17:20])
        or any(record[44:48])
        or any(record[276:UPDATE_CONTROL_HASH_OFFSET])
    ):
        raise UpdateError("reservado de journal U3 nao esta zerado")
    kind, phase, slot, count, progress = record[12:17]
    if (
        kind not in (UPDATE_JOURNAL_NONE, UPDATE_JOURNAL_APPLY,
                     UPDATE_JOURNAL_ROLLBACK)
        or phase not in (UPDATE_PHASE_NONE, UPDATE_PHASE_PREPARED,
                         UPDATE_PHASE_REPLACING, UPDATE_PHASE_COMMITTED)
        or slot not in (0, 1)
        or count > UPDATE_TARGET_COUNT
        or progress > count
    ):
        raise UpdateError("campos de journal U3 invalidos")
    if kind == UPDATE_JOURNAL_NONE:
        if (
            phase != UPDATE_PHASE_NONE
            or count != 0
            or progress != 0
            or any(record[12:UPDATE_CONTROL_HASH_OFFSET])
        ):
            raise UpdateError("journal U3 limpo inconsistente")
    elif phase == UPDATE_PHASE_NONE or count == 0:
        raise UpdateError("journal U3 pendente inconsistente")
    seen: set[int] = set()
    for index in range(UPDATE_TARGET_COUNT):
        offset = UPDATE_JOURNAL_ENTRY_OFFSET + index * UPDATE_JOURNAL_ENTRY_SIZE
        entry = record[offset : offset + UPDATE_JOURNAL_ENTRY_SIZE]
        if index >= count:
            if any(entry):
                raise UpdateError("entrada nao usada do journal U3 nao zerada")
            continue
        target_id, old_present, new_present, reserved = entry[:4]
        old_size, new_size = struct.unpack_from("<II", entry, 4)
        if (
            target_id >= UPDATE_TARGET_COUNT
            or target_id in seen
            or old_present != 1
            or new_present != 1
            or reserved != 0
            or not 1 <= old_size <= ZUPD_MAX_PAYLOAD_SIZE
            or not 1 <= new_size <= ZUPD_MAX_PAYLOAD_SIZE
        ):
            raise UpdateError("entrada ativa do journal U3 invalida")
        seen.add(target_id)
    return StoredJournal(
        struct.unpack_from("<I", record, 8)[0],
        kind,
        phase,
        slot,
        count,
        progress,
    )


def decode_history_alias(raw: bytes) -> str:
    """Valida o alias ASCII terminado em NUL armazenado no evento U4."""
    try:
        terminator = raw.index(0)
    except ValueError as error:
        raise UpdateError("alias do historico U4 nao possui terminador") from error
    if any(raw[terminator:]):
        raise UpdateError("padding do alias do historico U4 nao esta zerado")
    alias = raw[:terminator]
    if any(value < 0x20 or value > 0x7E for value in alias):
        raise UpdateError("alias do historico U4 nao usa ASCII imprimivel")
    if b"/" in alias or b"\\" in alias:
        raise UpdateError("alias do historico U4 contem separador")
    return alias.decode("ascii")


def decode_history_entry(
    record: bytes, offset: int, record_sequence: int
) -> StoredHistoryEntry:
    """Decodifica e valida uma entrada de 56 bytes do historico U4."""
    sequence = struct.unpack_from("<I", record, offset)[0]
    operation, outcome, action_reason, verification_reason = record[
        offset + 4 : offset + 8
    ]
    from_version = Version(*struct.unpack_from("<3H", record, offset + 8))
    to_version = Version(*struct.unpack_from("<3H", record, offset + 14))
    from_epoch, to_epoch = struct.unpack_from("<II", record, offset + 20)
    entry_count, completed = struct.unpack_from("<HH", record, offset + 28)
    flags = record[offset + 32]
    alias = decode_history_alias(record[offset + 33 : offset + 46])
    if (
        sequence == 0
        or sequence > record_sequence
        or operation not in UPDATE_HISTORY_OPERATION_NAMES
        or outcome not in UPDATE_HISTORY_OUTCOME_NAMES
        or action_reason > 8
        or verification_reason > 11
        or completed > entry_count
        or flags & ~UPDATE_HISTORY_FLAG_REBOOT
        or any(record[offset + 46 : offset + UPDATE_HISTORY_ENTRY_SIZE])
    ):
        raise UpdateError("entrada do historico U4 invalida")
    return StoredHistoryEntry(
        sequence,
        operation,
        outcome,
        action_reason,
        verification_reason,
        from_version,
        to_version,
        from_epoch,
        to_epoch,
        entry_count,
        completed,
        bool(flags & UPDATE_HISTORY_FLAG_REBOOT),
        alias,
    )


def decode_history_record(record: bytes) -> StoredHistory:
    """Replica a validacao little-endian de ZUPD*.HIS feita no kernel."""
    validate_control_record(record, b"ZUH1")
    sequence = struct.unpack_from("<I", record, 8)[0]
    count, next_index = record[12:14]
    if (
        count > UPDATE_HISTORY_MAX_ENTRIES
        or next_index >= UPDATE_HISTORY_MAX_ENTRIES
        or (count < UPDATE_HISTORY_MAX_ENTRIES and next_index != count)
        or any(record[14:UPDATE_HISTORY_HEADER_SIZE])
    ):
        raise UpdateError("cabecalho do historico U4 invalido")
    entries: list[StoredHistoryEntry | None] = []
    for index in range(UPDATE_HISTORY_MAX_ENTRIES):
        offset = UPDATE_HISTORY_ENTRY_OFFSET + index * UPDATE_HISTORY_ENTRY_SIZE
        active = count == UPDATE_HISTORY_MAX_ENTRIES or index < count
        if active:
            entries.append(decode_history_entry(record, offset, sequence))
        else:
            if any(record[offset : offset + UPDATE_HISTORY_ENTRY_SIZE]):
                raise UpdateError("slot vazio do historico U4 nao esta zerado")
            entries.append(None)
    if count:
        if sequence < count:
            raise UpdateError("sequencia do historico U4 inconsistente")
        oldest = next_index if count == UPDATE_HISTORY_MAX_ENTRIES else 0
        for order in range(count):
            slot = (oldest + order) % UPDATE_HISTORY_MAX_ENTRIES
            expected = sequence - count + 1 + order
            if entries[slot] is None or entries[slot].sequence != expected:
                raise UpdateError("ordem do ring do historico U4 inconsistente")
    return StoredHistory(sequence, count, next_index, tuple(entries))


def history_newest(history: StoredHistory, index: int) -> StoredHistoryEntry:
    """Retorna um evento do ring buffer usando a ordem mais recente primeiro."""
    if index < 0 or index >= history.count:
        raise UpdateError("indice do historico U4 fora do intervalo")
    slot = (
        history.next_index + UPDATE_HISTORY_MAX_ENTRIES - 1 - index
    ) % UPDATE_HISTORY_MAX_ENTRIES
    entry = history.entries[slot]
    if entry is None:
        raise UpdateError("slot ativo do historico U4 esta vazio")
    return entry


def select_redundant_record(
    records: tuple[bytes | None, bytes | None],
    decoder: Any,
    label: str,
) -> Any | None:
    """Seleciona a copia valida de maior sequencia ou denuncia perda total."""
    valid: list[tuple[int, Any]] = []
    for slot, record in enumerate(records):
        if record is None:
            continue
        try:
            decoded = decoder(record)
        except UpdateError:
            continue
        valid.append((slot, decoded))
    if not valid:
        if any(record is not None for record in records):
            raise UpdateError(f"nenhuma copia valida de {label}")
        return None
    valid.sort(key=lambda item: item[1].sequence, reverse=True)
    if (
        len(valid) == 2
        and valid[0][1].sequence == valid[1][1].sequence
        and records[valid[0][0]] != records[valid[1][0]]
    ):
        raise UpdateError(f"copias de {label} empatadas e divergentes")
    return valid[0][1]


def encode_remote_path(path: str) -> bytes:
    """Codifica o caminho relativo same-origin aceito pelo kernel."""
    if not isinstance(path, str) or not path.startswith("/"):
        raise UpdateError("caminho remoto deve iniciar com /")
    try:
        encoded = path.encode("ascii", "strict")
    except UnicodeEncodeError as error:
        raise UpdateError("caminho remoto deve usar ASCII") from error
    if ".." in path or "\\" in path or len(encoded) >= REMOTE_PATH_SIZE:
        raise UpdateError("caminho remoto invalido ou longo")
    if not re.fullmatch(r"/[A-Za-z0-9._/-]+", path):
        raise UpdateError("caminho remoto possui caractere invalido")
    return encoded + bytes(REMOTE_PATH_SIZE - len(encoded))


def build_remote_manifest(
    private_key: Any,
    public: PublicKeyInfo,
    generation: int,
    base_version: Version,
    target_version: Version,
    base_epoch: int,
    target_epoch: int,
    package: bytes,
    package_path: str,
) -> bytes:
    """Monta e assina o registro ZUM1 de 256 bytes."""
    if generation < 0 or generation > 0xFFFFFFFF:
        raise UpdateError("geracao remota excede uint32")
    if (
        not 0 <= base_epoch <= 0xFFFFFFFF
        or not 0 <= target_epoch <= 0xFFFFFFFF
    ):
        raise UpdateError("epoch remoto excede uint32")
    if not package or len(package) > ZUPD_MAX_TOTAL_SIZE:
        raise UpdateError("pacote remoto possui tamanho invalido")
    if target_epoch < base_epoch or (
        target_epoch == base_epoch and target_version <= base_version
    ):
        raise UpdateError("versao remota alvo nao e superior")
    raw = bytearray(REMOTE_MANIFEST_SIZE)
    raw[0:4] = REMOTE_MAGIC
    struct.pack_into(
        "<HHHHI",
        raw,
        4,
        REMOTE_FORMAT_VERSION,
        REMOTE_MANIFEST_SIZE,
        REMOTE_ARCH_I386,
        REMOTE_CHANNEL_STABLE,
        generation,
    )
    struct.pack_into(
        "<6H",
        raw,
        16,
        base_version.major,
        base_version.minor,
        base_version.patch,
        target_version.major,
        target_version.minor,
        target_version.patch,
    )
    struct.pack_into("<III", raw, 28, base_epoch, target_epoch, len(package))
    raw[40:72] = hashlib.sha256(package).digest()
    raw[72:88] = public.key_id
    struct.pack_into("<I", raw, 88, 0)
    raw[92:192] = encode_remote_path(package_path)
    raw[192:256] = private_key.sign(REMOTE_DOMAIN + bytes(raw[:192]))
    return bytes(raw)


def parse_remote_manifest(data: bytes, trusted: PublicKeyInfo) -> RemoteManifest:
    """Valida estrutura, campos reservados e assinatura do ZUM1."""
    if len(data) != REMOTE_MANIFEST_SIZE or data[:4] != REMOTE_MAGIC:
        raise UpdateError("manifesto remoto possui magic ou tamanho invalido")
    version, size, architecture, channel, generation = struct.unpack_from(
        "<HHHHI", data, 4
    )
    if (
        version != REMOTE_FORMAT_VERSION
        or size != REMOTE_MANIFEST_SIZE
        or architecture != REMOTE_ARCH_I386
        or channel != REMOTE_CHANNEL_STABLE
        or struct.unpack_from("<I", data, 88)[0] != 0
    ):
        raise UpdateError("cabecalho remoto invalido")
    if data[72:88] != trusted.key_id:
        raise UpdateError("key_id remoto desconhecido")
    ed25519, _, invalid_signature = crypto_modules()
    try:
        ed25519.Ed25519PublicKey.from_public_bytes(trusted.public_key).verify(
            data[192:256], REMOTE_DOMAIN + data[:192]
        )
    except invalid_signature as error:
        raise UpdateError("assinatura do manifesto remoto invalida") from error
    versions = struct.unpack_from("<6H", data, 16)
    base_version = Version(*versions[:3])
    target_version = Version(*versions[3:])
    base_epoch, target_epoch, package_size = struct.unpack_from("<III", data, 28)
    if not package_size or package_size > ZUPD_MAX_TOTAL_SIZE:
        raise UpdateError("tamanho de pacote remoto invalido")
    path_raw = data[92:192]
    try:
        end = path_raw.index(0)
        package_path = path_raw[:end].decode("ascii")
    except (ValueError, UnicodeDecodeError) as error:
        raise UpdateError("caminho remoto sem terminador valido") from error
    if any(path_raw[end + 1 :]) or encode_remote_path(package_path) != path_raw:
        raise UpdateError("padding ou caminho remoto invalido")
    if target_epoch < base_epoch or (
        target_epoch == base_epoch and target_version <= base_version
    ):
        raise UpdateError("transicao remota nao e crescente")
    return RemoteManifest(
        generation,
        base_version,
        target_version,
        base_epoch,
        target_epoch,
        package_size,
        data[40:72],
        package_path,
    )


def resolve_git_commit(reference: str, repository: Path = REPO_ROOT) -> str:
    """Resolve uma referencia Git para o commit completo correspondente."""
    if not isinstance(reference, str) or not reference or any(
        char.isspace() for char in reference
    ):
        raise UpdateError("referencia Git vazia ou invalida")
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--verify", f"{reference}^{{commit}}"],
            cwd=repository,
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise UpdateError("nao foi possivel consultar o repositorio Git") from error
    commit = result.stdout.strip().lower()
    if result.returncode != 0 or not GIT_COMMIT_RE.fullmatch(commit):
        raise UpdateError(f"referencia Git inexistente: {reference}")
    return commit


def validate_release_identity(release_id: Any, release_name: Any) -> tuple[str, str]:
    """Valida identificador estavel e nome humano da Release."""
    if not isinstance(release_id, str) or not RELEASE_ID_RE.fullmatch(release_id):
        raise UpdateError("identificador da Release invalido")
    if (
        not isinstance(release_name, str)
        or not release_name.strip()
        or release_name != release_name.strip()
        or len(release_name) > 100
        or any(ord(char) < 0x20 for char in release_name)
    ):
        raise UpdateError("nome da Release invalido")
    return release_id, release_name


def release_asset_metadata(name: str, content: bytes) -> dict[str, Any]:
    """Publica tamanho e hash de um asset sem atribuir confianca a eles."""
    return {
        "name": name,
        "size": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
    }


def release_descriptor(
    release_id: str,
    release_name: str,
    source_commit: str,
    tag: str | None,
    artifact: ArtifactInfo,
    package_name: str,
    package: bytes,
    manifest_name: str,
    manifest: bytes,
) -> str:
    """Serializa o inventario publico; a confianca permanece no ZUPD/ZUM1."""
    data = {
        "format": "zephyros-release-v1",
        "release_id": release_id,
        "release_name": release_name,
        "channel": "stable",
        "source_commit": source_commit,
        "tag": tag,
        "version_lock": {
            "minimum_version": str(artifact.base_version),
            "target_version": str(artifact.target_version),
            "base_epoch": artifact.base_epoch,
            "target_epoch": artifact.target_epoch,
        },
        "assets": {
            "package": release_asset_metadata(package_name, package),
            "manifest": release_asset_metadata(manifest_name, manifest),
        },
    }
    return json.dumps(data, indent=2, ensure_ascii=True) + "\n"


def load_release_descriptor(path: Path) -> dict[str, Any]:
    """Le o descritor EP5 exigindo campos e ordem canonicos."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError(f"nao foi possivel ler a Release {path}") from error
    if not isinstance(data, dict) or tuple(data) != (
        "format", "release_id", "release_name", "channel", "source_commit",
        "tag", "version_lock", "assets",
    ):
        raise UpdateError("descritor da Release possui campos invalidos")
    if data["format"] != "zephyros-release-v1" or data["channel"] != "stable":
        raise UpdateError("formato ou canal da Release invalido")
    return data


def load_release_asset(
    directory: Path,
    metadata: Any,
    label: str,
    suffix: str | tuple[str, ...],
) -> tuple[str, bytes, bytes]:
    """Carrega um asset co-localizado e confere seu inventario SHA-256."""
    if not isinstance(metadata, dict) or tuple(metadata) != (
        "name", "size", "sha256",
    ):
        raise UpdateError(f"metadados do {label} invalidos")
    name = metadata["name"]
    size = metadata["size"]
    digest_text = metadata["sha256"]
    if (
        not isinstance(name, str)
        or Path(name).name != name
        or not name.lower().endswith(suffix)
    ):
        raise UpdateError(f"nome do {label} invalido")
    if not isinstance(size, int) or size <= 0:
        raise UpdateError(f"tamanho publicado do {label} invalido")
    if not isinstance(digest_text, str) or not re.fullmatch(
        r"[0-9a-f]{64}", digest_text
    ):
        raise UpdateError(f"SHA-256 publicado do {label} invalido")
    try:
        content = (directory / name).read_bytes()
    except OSError as error:
        raise UpdateError(f"asset da Release ausente: {name}") from error
    digest = hashlib.sha256(content).digest()
    if len(content) != size or digest.hex() != digest_text:
        raise UpdateError(f"tamanho ou SHA-256 do {label} divergiu")
    return name, content, digest


def verify_release_bundle(
    descriptor_path: Path,
    trusted: PublicKeyInfo,
    repository: Path = REPO_ROOT,
    resolver: Any = resolve_git_commit,
) -> ReleaseBundle:
    """Valida origem, integridade e trava assinada antes da publicacao."""
    data = load_release_descriptor(descriptor_path)
    release_id, release_name = validate_release_identity(
        data["release_id"], data["release_name"]
    )
    source_commit = data["source_commit"]
    if (
        not isinstance(source_commit, str)
        or not GIT_COMMIT_RE.fullmatch(source_commit)
    ):
        raise UpdateError("commit de origem da Release invalido")
    if resolver(source_commit, repository) != source_commit:
        raise UpdateError("commit de origem da Release divergiu")
    tag = data["tag"]
    if tag is not None:
        if not isinstance(tag, str) or not tag or len(tag) > 100:
            raise UpdateError("tag auxiliar da Release invalida")
        if resolver(f"refs/tags/{tag}", repository) != source_commit:
            raise UpdateError("tag auxiliar nao aponta para o commit da Release")
    lock = data["version_lock"]
    if not isinstance(lock, dict) or tuple(lock) != (
        "minimum_version", "target_version", "base_epoch", "target_epoch",
    ):
        raise UpdateError("trava de versao da Release invalida")
    minimum_version = Version.parse(lock["minimum_version"])
    target_version = Version.parse(lock["target_version"])
    base_epoch = lock["base_epoch"]
    target_epoch = lock["target_epoch"]
    if (
        not isinstance(base_epoch, int)
        or not isinstance(target_epoch, int)
        or not 0 <= base_epoch <= target_epoch <= 0xFFFFFFFF
    ):
        raise UpdateError("epochs da trava de versao invalidos")
    assets = data["assets"]
    if not isinstance(assets, dict) or tuple(assets) != ("package", "manifest"):
        raise UpdateError("inventario de assets da Release invalido")
    package_name, package, package_hash = load_release_asset(
        descriptor_path.parent, assets["package"], "pacote",
        (".zephyrosupd", ".zup"),
    )
    manifest_name, manifest, manifest_hash = load_release_asset(
        descriptor_path.parent, assets["manifest"], "manifesto", ".zum"
    )
    try:
        artifact = verify_artifact(package, trusted, minimum_version, base_epoch)
    except Rejection as error:
        raise UpdateError(f"ZUPD da Release foi recusado: {error.reason}") from error
    remote = parse_remote_manifest(manifest, trusted)
    if (
        artifact.base_version != minimum_version
        or artifact.target_version != target_version
        or artifact.base_epoch != base_epoch
        or artifact.target_epoch != target_epoch
        or remote.base_version != minimum_version
        or remote.target_version != target_version
        or remote.base_epoch != base_epoch
        or remote.target_epoch != target_epoch
        or remote.package_size != len(package)
        or remote.package_sha256 != package_hash
        or Path(remote.package_path).name != package_name
    ):
        raise UpdateError("ZUPD, ZUM1 e trava de versao da Release divergem")
    return ReleaseBundle(
        release_id, release_name, source_commit, tag, target_version,
        minimum_version, base_epoch, target_epoch, package_name, manifest_name,
        package_hash, manifest_hash,
    )


def build_release_bundle(
    release_id: str,
    manifest_path: Path,
    private_key: Any,
    trusted: PublicKeyInfo,
    generation: int,
    source_reference: str,
    tag: str | None,
    output_dir: Path,
) -> Path:
    """Gera atomicamente os tres arquivos publicos de uma Release EP5."""
    release_id, release_name = validate_release_identity(release_id, release_id)
    if private_public_info(private_key) != trusted:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    source_commit = resolve_git_commit(source_reference)
    if tag is not None and resolve_git_commit(f"refs/tags/{tag}") != source_commit:
        raise UpdateError("tag auxiliar nao aponta para o commit selecionado")
    package = build_artifact(manifest_path, private_key)
    manifest_data_value = manifest_data(manifest_path)
    base, target, base_epoch, target_epoch, _ = validate_manifest(
        manifest_data_value, manifest_path.resolve().parent
    )
    artifact = verify_artifact(package, trusted, base, base_epoch)
    package_name = "update.zephyrosupd"
    manifest_name = "release.zum"
    remote = build_remote_manifest(
        private_key, trusted, generation, base, target, base_epoch,
        target_epoch, package, f"/zephyros/{package_name}",
    )
    parse_remote_manifest(remote, trusted)
    descriptor = release_descriptor(
        release_id, release_name, source_commit, tag, artifact, package_name,
        package, manifest_name, remote,
    )
    output = output_dir.expanduser().resolve()
    if output.exists():
        raise UpdateError(f"diretorio da Release ja existe: {output}")
    if not output.parent.is_dir():
        raise UpdateError(f"diretorio pai da Release nao existe: {output.parent}")
    try:
        with tempfile.TemporaryDirectory(
            prefix=".zephyros-release-", dir=output.parent
        ) as temporary_name:
            temporary = Path(temporary_name)
            write_new_bytes(temporary / package_name, package)
            write_new_bytes(temporary / manifest_name, remote)
            write_new_text(temporary / "release.json", descriptor)
            temporary.replace(output)
    except OSError as error:
        raise UpdateError("nao foi possivel publicar o diretorio local") from error
    return output / "release.json"


def encode_remote_record(record: RemoteRecord) -> bytes:
    """Serializa o controle redundante ZUR1 usado pela auditoria."""
    raw = bytearray(REMOTE_RECORD_SIZE)
    raw[:4] = REMOTE_RECORD_MAGIC
    struct.pack_into(
        "<HHIBBBBII",
        raw,
        4,
        REMOTE_RECORD_VERSION,
        REMOTE_RECORD_SIZE,
        record.sequence,
        record.phase,
        record.active_slot,
        record.pending_slot,
        0,
        record.manifest_generation,
        record.package_size,
    )
    raw[24:56] = record.package_sha256
    raw[56:88] = record.manifest_sha256
    struct.pack_into(
        "<6HII",
        raw,
        88,
        record.base_version.major,
        record.base_version.minor,
        record.base_version.patch,
        record.target_version.major,
        record.target_version.minor,
        record.target_version.patch,
        record.base_epoch,
        record.target_epoch,
    )
    raw[480:512] = hashlib.sha256(raw[:480]).digest()
    return bytes(raw)


def decode_remote_record(data: bytes) -> RemoteRecord:
    """Decodifica e valida um registro ZUR1 de 512 bytes."""
    if len(data) != REMOTE_RECORD_SIZE or data[:4] != REMOTE_RECORD_MAGIC:
        raise UpdateError("registro remoto possui magic ou tamanho invalido")
    version, size, sequence = struct.unpack_from("<HHI", data, 4)
    phase, active, pending, flags = struct.unpack_from("<BBBB", data, 12)
    if (
        version != REMOTE_RECORD_VERSION
        or size != REMOTE_RECORD_SIZE
        or phase not in (REMOTE_PHASE_CLEAN, REMOTE_PHASE_DOWNLOADING)
        or active not in (0, 1, REMOTE_SLOT_NONE)
        or pending not in (0, 1, REMOTE_SLOT_NONE)
        or flags != 0
        or (phase == REMOTE_PHASE_CLEAN and pending != REMOTE_SLOT_NONE)
        or (phase == REMOTE_PHASE_DOWNLOADING and pending == REMOTE_SLOT_NONE)
        or (pending in (0, 1) and pending == active)
        or any(data[REMOTE_RECORD_RESERVED_OFFSET:REMOTE_RECORD_HASH_OFFSET])
        or hashlib.sha256(data[:480]).digest() != data[480:512]
    ):
        raise UpdateError("registro remoto estruturalmente invalido")
    generation, package_size = struct.unpack_from("<II", data, 16)
    values = struct.unpack_from("<6HII", data, 88)
    record = RemoteRecord(
        sequence,
        phase,
        active,
        pending,
        generation,
        package_size,
        data[24:56],
        data[56:88],
        Version(*values[:3]),
        Version(*values[3:6]),
        values[6],
        values[7],
    )
    if active == REMOTE_SLOT_NONE and package_size:
        raise UpdateError("cache remoto vazio declara pacote")
    if active in (0, 1) and not package_size:
        raise UpdateError("cache remoto ativo possui tamanho zero")
    return record


def decode_runtime_remote_record(data: bytes) -> RuntimeRemoteRecord:
    """Decodifica e valida um registro ZRV2 de 512 bytes."""
    if len(data) != RUNTIME_RECORD_SIZE or data[:4] != RUNTIME_RECORD_MAGIC:
        raise UpdateError("registro runtime remoto possui magic ou tamanho invalido")
    version, size = struct.unpack_from("<HH", data, 4)
    sequence = struct.unpack_from("<I", data, 8)[0]
    phase, active, pending, mode = data[12:16]
    entry_count, asset_mask = struct.unpack_from("<HH", data, 16)
    package_size = struct.unpack_from("<I", data, 84)[0]
    target = Version(*struct.unpack_from("<3H", data, 88))
    target_epoch = struct.unpack_from("<I", data, 94)[0]
    if (
        version != 1
        or size != RUNTIME_RECORD_SIZE
        or phase not in (0, 1)
        or active not in (0, 1, REMOTE_SLOT_NONE)
        or pending not in (0, 1, REMOTE_SLOT_NONE)
        or mode not in (0, 1)
        or entry_count > RUNTIME_MAX_ENTRIES
        or asset_mask & ~((1 << RUNTIME_MAX_ENTRIES) - 1)
        or package_size > RUNTIME_PACKAGE_MAX_SIZE
        or any(data[98:RUNTIME_RECORD_HASH_OFFSET])
        or hashlib.sha256(data[:RUNTIME_RECORD_HASH_OFFSET]).digest()
        != data[RUNTIME_RECORD_HASH_OFFSET:]
    ):
        raise UpdateError("registro runtime remoto estruturalmente invalido")
    if phase == 1 and pending not in (0, 1):
        raise UpdateError("download runtime remoto nao possui slot pendente")
    if phase == 1 and pending == active:
        raise UpdateError("download runtime remoto reutiliza slot ativo")
    if phase == 0 and pending not in (REMOTE_SLOT_NONE, active):
        raise UpdateError("slot runtime remoto limpo diverge do ativo")
    if phase == 0 and active == REMOTE_SLOT_NONE and package_size:
        raise UpdateError("cache runtime vazio declara pacote")
    if phase == 0 and active == REMOTE_SLOT_NONE and (
        entry_count
        or asset_mask
        or data[20:52] != bytes(32)
        or data[52:84] != bytes(32)
        or target != Version(0, 0, 0)
        or target_epoch
    ):
        raise UpdateError("cache runtime vazio possui metadados ativos")
    if phase == 0 and active in (0, 1) and (
        not entry_count
        or asset_mask & ~((1 << entry_count) - 1)
        or data[20:52] == bytes(32)
        or (mode == 1 and (
            not package_size
            or data[52:84] == bytes(32)
            or asset_mask
        ))
        or (mode == 0 and (package_size or data[52:84] != bytes(32)))
    ):
        raise UpdateError("modo e tamanho do cache runtime divergem")
    return RuntimeRemoteRecord(
        sequence,
        phase,
        active,
        pending,
        mode,
        entry_count,
        asset_mask,
        data[20:52],
        data[52:84],
        package_size,
        target,
        target_epoch,
    )


def decode_runtime_file_state(
    raw: bytes, label: str, journal_old: bool = False
) -> RuntimeStoredFileState:
    """Decodifica uma entrada de estado ZRT2 ou de journal ZRTJ."""
    if len(raw) != RUNTIME_FILE_STATE_SIZE:
        raise UpdateError(f"estado runtime truncado: {label}")
    present = raw[0]
    if present not in (0, 1) or raw[3] != 0:
        raise UpdateError(f"estado runtime possui flags invalidas: {label}")
    if not journal_old and any(raw[1:3]):
        raise UpdateError(f"estado runtime possui reservado invalido: {label}")
    size = struct.unpack_from("<I", raw, 4)[0]
    digest = raw[8:40]
    if not present:
        if size or digest != bytes(32):
            raise UpdateError(f"estado runtime ausente possui dados: {label}")
        return RuntimeStoredFileState(0, bytes(32), False)
    if not 1 <= size <= RUNTIME_FILE_MAX_SIZE or digest == bytes(32):
        raise UpdateError(f"estado runtime presente possui dados invalidos: {label}")
    return RuntimeStoredFileState(size, digest, True)


def decode_runtime_state_record(data: bytes) -> RuntimeStoredState:
    """Decodifica e valida um controle ZRT2 de 4096 bytes."""
    if len(data) != RUNTIME_CONTROL_SIZE or data[:4] != b"ZRT2":
        raise UpdateError("estado runtime possui magic ou tamanho invalido")
    version, size = struct.unpack_from("<HH", data, 4)
    if (
        version != RUNTIME_CONTROL_VERSION
        or size != RUNTIME_CONTROL_SIZE
        or any(data[38:64])
        or any(data[RUNTIME_STATE_ROLLBACK_OFFSET + RUNTIME_MAX_ENTRIES * RUNTIME_FILE_STATE_SIZE : RUNTIME_CONTROL_HASH_OFFSET])
        or hashlib.sha256(data[:RUNTIME_CONTROL_HASH_OFFSET]).digest()
        != data[RUNTIME_CONTROL_HASH_OFFSET:]
    ):
        raise UpdateError("estado runtime estruturalmente invalido")
    sequence = struct.unpack_from("<I", data, 8)[0]
    installed = Version(*struct.unpack_from("<3H", data, 12))
    installed_epoch = struct.unpack_from("<I", data, 18)[0]
    rollback_available = data[22]
    rollback_slot = data[23]
    entry_count, rollback_count = struct.unpack_from("<HH", data, 24)
    previous = Version(*struct.unpack_from("<3H", data, 28))
    previous_epoch = struct.unpack_from("<I", data, 34)[0]
    if (
        rollback_available not in (0, 1)
        or rollback_slot not in (0, 1, RUNTIME_SLOT_NONE)
        or entry_count != len(RUNTIME_CATALOG)
        or rollback_count > len(RUNTIME_CATALOG)
    ):
        raise UpdateError("estado runtime possui contadores invalidos")
    current = tuple(
        decode_runtime_file_state(
            data[RUNTIME_STATE_CURRENT_OFFSET + index * RUNTIME_FILE_STATE_SIZE :
                 RUNTIME_STATE_CURRENT_OFFSET + (index + 1) * RUNTIME_FILE_STATE_SIZE],
            f"current[{index}]",
        )
        for index in range(RUNTIME_MAX_ENTRIES)
    )
    rollback = tuple(
        decode_runtime_file_state(
            data[RUNTIME_STATE_ROLLBACK_OFFSET + index * RUNTIME_FILE_STATE_SIZE :
                 RUNTIME_STATE_ROLLBACK_OFFSET + (index + 1) * RUNTIME_FILE_STATE_SIZE],
            f"rollback[{index}]",
        )
        for index in range(RUNTIME_MAX_ENTRIES)
    )
    if any(state.present for state in current[len(RUNTIME_CATALOG) :]) or any(
        state.present for state in rollback[len(RUNTIME_CATALOG) :]
    ):
        raise UpdateError("estado runtime usa entradas fora do catalogo")
    if (
        (not rollback_available and (rollback_slot != RUNTIME_SLOT_NONE or rollback_count))
        or (rollback_available and (rollback_slot == RUNTIME_SLOT_NONE or not rollback_count))
    ):
        raise UpdateError("estado runtime possui rollback inconsistente")
    if rollback_available:
        if sum(
            current[index] != rollback[index]
            for index in range(len(RUNTIME_CATALOG))
        ) != rollback_count:
            raise UpdateError("contagem de rollback runtime diverge")
    elif any(state.present for state in rollback):
        raise UpdateError("estado runtime sem rollback possui backups")
    return RuntimeStoredState(
        sequence,
        installed,
        installed_epoch,
        bool(rollback_available),
        rollback_slot,
        entry_count,
        rollback_count,
        previous,
        previous_epoch,
        current,
        rollback,
    )


def decode_runtime_journal_record(data: bytes) -> RuntimeStoredJournal:
    """Decodifica e valida um controle ZRTJ de 4096 bytes."""
    if len(data) != RUNTIME_CONTROL_SIZE or data[:4] != b"ZRTJ":
        raise UpdateError("journal runtime possui magic ou tamanho invalido")
    version, size = struct.unpack_from("<HH", data, 4)
    if (
        version != RUNTIME_CONTROL_VERSION
        or size != RUNTIME_CONTROL_SIZE
        or any(data[40:RUNTIME_JOURNAL_HEADER_SIZE])
        or hashlib.sha256(data[:RUNTIME_CONTROL_HASH_OFFSET]).digest()
        != data[RUNTIME_CONTROL_HASH_OFFSET:]
    ):
        raise UpdateError("journal runtime estruturalmente invalido")
    sequence = struct.unpack_from("<I", data, 8)[0]
    phase, kind, slot, progress = data[12:16]
    entry_count = struct.unpack_from("<H", data, 16)[0]
    base = Version(*struct.unpack_from("<3H", data, 20))
    base_epoch = struct.unpack_from("<I", data, 26)[0]
    target = Version(*struct.unpack_from("<3H", data, 30))
    target_epoch = struct.unpack_from("<I", data, 36)[0]
    entries: list[RuntimeStoredPlanEntry] = []
    for index in range(RUNTIME_MAX_ENTRIES):
        offset = RUNTIME_JOURNAL_ENTRY_OFFSET + index * RUNTIME_JOURNAL_ENTRY_SIZE
        raw = data[offset : offset + RUNTIME_JOURNAL_ENTRY_SIZE]
        old = decode_runtime_file_state(
            raw[RUNTIME_JOURNAL_ENTRY_OLD : RUNTIME_JOURNAL_ENTRY_NEW],
            f"entries[{index}].old",
            journal_old=True,
        )
        new = decode_runtime_file_state(
            raw[RUNTIME_JOURNAL_ENTRY_NEW:], f"entries[{index}].new"
        )
        entries.append(RuntimeStoredPlanEntry(raw[1], raw[2], old, new))
    if phase == RUNTIME_PHASE_NONE:
        if (
            kind != RUNTIME_JOURNAL_NONE
            or slot != RUNTIME_SLOT_NONE
            or progress
            or entry_count
            or any(data[20:40])
            or any(data[RUNTIME_JOURNAL_ENTRY_OFFSET:RUNTIME_CONTROL_HASH_OFFSET])
        ):
            raise UpdateError("journal runtime limpo possui dados pendentes")
        return RuntimeStoredJournal(
            sequence,
            phase,
            kind,
            slot,
            progress,
            entry_count,
            base,
            base_epoch,
            target,
            target_epoch,
            (),
        )
    if (
        phase not in (
            RUNTIME_PHASE_PREPARED,
            RUNTIME_PHASE_STAGING,
            RUNTIME_PHASE_REPLACING,
            RUNTIME_PHASE_COMMITTED,
        )
        or kind not in (RUNTIME_JOURNAL_APPLY, RUNTIME_JOURNAL_ROLLBACK)
        or slot not in (0, 1)
        or not 0 <= entry_count <= len(RUNTIME_CATALOG)
        or progress > entry_count
    ):
        raise UpdateError("journal runtime possui fase ou contadores invalidos")
    for index, entry in enumerate(entries[:entry_count]):
        if (
            entry.catalog_index >= len(RUNTIME_CATALOG)
            or entry.operation == 0
            or entry.operation & ~(
                RUNTIME_OPERATION_REPLACE
                | RUNTIME_OPERATION_CREATE
                | RUNTIME_OPERATION_DELETE
            )
            or (entry.old_state.present and entry.new_state.present and
                entry.operation != RUNTIME_OPERATION_REPLACE)
            or (not entry.old_state.present and entry.new_state.present and
                entry.operation != RUNTIME_OPERATION_CREATE)
            or (entry.old_state.present and not entry.new_state.present and
                entry.operation != RUNTIME_OPERATION_DELETE)
            or (not entry.old_state.present and not entry.new_state.present)
            or any(previous.catalog_index == entry.catalog_index
                   for previous in entries[:index])
        ):
            raise UpdateError("journal runtime possui plano invalido")
    if any(data[
        RUNTIME_JOURNAL_ENTRY_OFFSET + entry_count * RUNTIME_JOURNAL_ENTRY_SIZE :
        RUNTIME_CONTROL_HASH_OFFSET
    ]):
        raise UpdateError("journal runtime possui entradas nao usadas")
    return RuntimeStoredJournal(
        sequence,
        phase,
        kind,
        slot,
        progress,
        entry_count,
        base,
        base_epoch,
        target,
        target_epoch,
        tuple(entries[:entry_count]),
    )


def fat_name(raw: bytes) -> str:
    """Converte os 11 bytes de uma entrada FAT para nome 8.3 legivel."""
    stem = raw[:8].decode("ascii").rstrip()
    extension = raw[8:11].decode("ascii").rstrip()
    return f"{stem}.{extension}" if extension else stem


def inspect_fat12_image(image_path: Path) -> dict[str, tuple[bytes, int]]:
    """Valida FATs, cadeias/alocacoes e retorna os arquivos da raiz."""
    try:
        from packager import PackageError, fat12_geometry, fat12_get
    except ImportError:
        from tools.packager import PackageError, fat12_geometry, fat12_get
    try:
        image = bytearray(image_path.read_bytes())
        bps, spc, reserved, fat_count, spf, root_start, clusters = (
            fat12_geometry(image)
        )
    except (OSError, PackageError) as error:
        raise UpdateError(f"imagem FAT12 invalida: {image_path}") from error
    fat_size = spf * bps
    fats = [
        image[(reserved + copy * spf) * bps:
              (reserved + copy * spf) * bps + fat_size]
        for copy in range(fat_count)
    ]
    if any(fat != fats[0] for fat in fats[1:]):
        raise UpdateError("copias da FAT12 divergem")
    root_entries = struct.unpack_from("<H", image, 17)[0]
    root_offset = root_start * bps
    root_sectors = (root_entries * 32 + bps - 1) // bps
    data_start = root_start + root_sectors
    cluster_size = bps * spc
    claimed: set[int] = set()
    root_files: dict[str, tuple[bytes, int]] = {}

    def chain(first: int, label: str) -> list[int]:
        if first == 0:
            return []
        if first < 2 or first >= clusters + 2:
            raise UpdateError(f"primeiro cluster FAT12 invalido: {label}")
        result: list[int] = []
        cluster = first
        while 2 <= cluster < 0xFF8:
            if cluster >= clusters + 2 or cluster in claimed:
                raise UpdateError(f"cadeia FAT12 invalida ou cruzada: {label}")
            claimed.add(cluster)
            result.append(cluster)
            cluster = fat12_get(fats[0], cluster)
        if cluster < 0xFF8:
            raise UpdateError(f"cadeia FAT12 sem terminador: {label}")
        return result

    def chain_data(items: list[int]) -> bytes:
        content = bytearray()
        for cluster in items:
            start = (data_start + (cluster - 2) * spc) * bps
            content.extend(image[start:start + cluster_size])
        return bytes(content)

    def walk(entries: bytes, count: int, prefix: str, root: bool) -> None:
        for index in range(count):
            entry = entries[index * 32 : (index + 1) * 32]
            if len(entry) < 32 or entry[0] == 0:
                break
            attributes = entry[11]
            if entry[0] == 0xE5 or attributes == 0x0F or attributes & 0x08:
                continue
            if entry[0] == ord("."):
                continue
            try:
                name = fat_name(entry[:11])
            except UnicodeDecodeError as error:
                raise UpdateError("nome FAT12 nao usa ASCII") from error
            label = f"{prefix}/{name}" if prefix else name
            size = struct.unpack_from("<I", entry, 28)[0]
            items = chain(struct.unpack_from("<H", entry, 26)[0], label)
            content = chain_data(items)
            if attributes & 0x10:
                if not items:
                    raise UpdateError(f"diretorio FAT12 sem cluster: {label}")
                walk(content, len(content) // 32, label, False)
            else:
                if size > len(content) or (
                    size > 0 and len(content) - cluster_size >= size
                ):
                    raise UpdateError(f"cadeia FAT12 com tamanho invalido: {label}")
                if root:
                    if name in root_files:
                        raise UpdateError(f"arquivo raiz FAT12 duplicado: {name}")
                    root_files[name] = (content[:size], attributes)

    walk(
        bytes(image[root_offset:root_offset + root_entries * 32]),
        root_entries,
        "",
        True,
    )
    allocated = {
        cluster
        for cluster in range(2, clusters + 2)
        if fat12_get(fats[0], cluster) != 0
    }
    if allocated != claimed:
        raise UpdateError("FAT12 contem clusters orfaos ou nao rastreados")
    return root_files


def audit_image(
    image_path: Path,
    expected_version: Version | None,
    expected_rollback: str,
    allow_pending: bool,
    expected_history_count: int | None = None,
    expected_last_event: str | None = None,
    expected_remote_cache: str = "any",
    expected_remote_alias: str | None = None,
    expected_remote_pending: str = "clean",
    expected_runtime_cache: str = "any",
    expected_runtime_alias: str | None = None,
    expected_runtime_pending: str = "clean",
) -> None:
    """Audita persistencia U3/U4/U5/EP6.3 e arquivos transacionais."""
    root = inspect_fat12_image(image_path)
    state = select_redundant_record(
        tuple(root.get(name, (None, 0))[0] for name in UPDATE_STATE_ALIASES),
        decode_state_record,
        "estado U3",
    )
    journal = select_redundant_record(
        tuple(root.get(name, (None, 0))[0] for name in UPDATE_JOURNAL_ALIASES),
        decode_journal_record,
        "journal U3",
    )
    history = select_redundant_record(
        tuple(root.get(name, (None, 0))[0] for name in UPDATE_HISTORY_ALIASES),
        decode_history_record,
        "historico U4",
    )
    remote = select_redundant_record(
        tuple(
            root.get(name, (None, 0))[0]
            for name in REMOTE_RECORD_ALIASES
        ),
        decode_remote_record,
        "cache remoto U5",
    )
    runtime_remote = select_redundant_record(
        tuple(
            root.get(name, (None, 0))[0]
            for name in RUNTIME_RECORD_ALIASES
        ),
        decode_runtime_remote_record,
        "cache remoto runtime v2",
    )
    runtime_local_state = select_redundant_record(
        tuple(
            root.get(name, (None, 0))[0]
            for name in RUNTIME_STATE_ALIASES
        ),
        decode_runtime_state_record,
        "estado local runtime v2",
    )
    runtime_local_journal = select_redundant_record(
        tuple(
            root.get(name, (None, 0))[0]
            for name in RUNTIME_JOURNAL_ALIASES
        ),
        decode_runtime_journal_record,
        "journal local runtime v2",
    )
    installed = state.installed_version if state else Version(0, 1, 0)
    rollback_available = state.rollback_available if state else False
    pending = journal is not None and journal.kind != UPDATE_JOURNAL_NONE
    history_count = history.count if history else 0
    last_event = history_newest(history, 0) if history_count else None
    remote_pending = bool(
        remote and remote.phase == REMOTE_PHASE_DOWNLOADING
    )
    remote_alias = (
        REMOTE_PACKAGE_ALIASES[remote.active_slot]
        if remote and remote.active_slot in (0, 1)
        else None
    )
    runtime_pending = bool(
        runtime_remote and (
            runtime_remote.phase == 1
            or runtime_remote.pending_slot in (0, 1)
        )
    )
    runtime_local_pending = bool(
        runtime_local_journal and
        runtime_local_journal.phase != RUNTIME_PHASE_NONE
    )
    runtime_alias = (
        RUNTIME_MANIFEST_ALIASES[runtime_remote.active_slot]
        if runtime_remote and runtime_remote.active_slot in (0, 1)
        else None
    )
    internal_aliases = (
        UPDATE_STATE_ALIASES
        + UPDATE_JOURNAL_ALIASES
        + UPDATE_HISTORY_ALIASES
        + tuple(alias for slot in UPDATE_BACKUP_ALIASES for alias in slot)
        + tuple(alias for slot in UPDATE_STAGE_ALIASES for alias in slot)
        + REMOTE_RECORD_ALIASES
        + REMOTE_PACKAGE_ALIASES
        + RUNTIME_RECORD_ALIASES
        + RUNTIME_MANIFEST_ALIASES
        + RUNTIME_PACKAGE_ALIASES
        + tuple(
            f"ZRV{slot}.{index:02d}"
            for slot in range(2)
            for index in range(RUNTIME_MAX_ENTRIES)
        )
        + RUNTIME_STATE_ALIASES
        + RUNTIME_JOURNAL_ALIASES
        + tuple(
            f"{prefix}{index:02d}"
            for prefix in (*RUNTIME_STAGE_PREFIXES, *RUNTIME_BACKUP_PREFIXES)
            for index in range(RUNTIME_MAX_ENTRIES)
        )
    )
    for alias in internal_aliases:
        if alias in root and root[alias][1] != 0x26:
            raise UpdateError(f"atributos do arquivo interno divergem: {alias}")
    if expected_version is not None and installed != expected_version:
        raise UpdateError(
            f"versao instalada {installed}, esperada {expected_version}"
        )
    if not allow_pending and pending:
        raise UpdateError("imagem possui transacao U3 pendente")
    if expected_rollback == "available" and not rollback_available:
        raise UpdateError("rollback esperado nao esta disponivel")
    if expected_rollback == "unavailable" and rollback_available:
        raise UpdateError("rollback inesperado esta disponivel")
    if (
        expected_history_count is not None
        and history_count != expected_history_count
    ):
        raise UpdateError(
            f"historico possui {history_count} evento(s), "
            f"esperados {expected_history_count}"
        )
    if expected_last_event is not None:
        actual = (
            UPDATE_HISTORY_OPERATION_NAMES[last_event.operation]
            if last_event is not None
            else "none"
        )
        if actual != expected_last_event:
            raise UpdateError(
                f"ultimo evento do historico e {actual}, "
                f"esperado {expected_last_event}"
            )
        if (
            expected_last_event.startswith("recovery-")
            and last_event.outcome != 4
        ):
            raise UpdateError(
                "ultimo evento de recuperacao nao possui resultado RECOVERED"
            )
    if expected_remote_pending == "clean" and remote_pending:
        raise UpdateError("imagem possui transferencia U5 pendente")
    if expected_remote_pending == "pending" and not remote_pending:
        raise UpdateError("transferencia U5 pendente era esperada")
    if expected_remote_cache == "empty" and remote_alias is not None:
        raise UpdateError("cache remoto deveria estar vazio")
    if expected_remote_cache == "valid" and remote_alias is None:
        raise UpdateError("cache remoto valido esperado nao existe")
    if expected_remote_alias is not None and remote_alias != expected_remote_alias:
        raise UpdateError(
            f"alias remoto e {remote_alias or 'none'}, "
            f"esperado {expected_remote_alias}"
        )
    if expected_runtime_pending == "clean" and runtime_pending:
        raise UpdateError("imagem possui transferencia runtime v2 pendente")
    if expected_runtime_pending == "pending" and not runtime_pending:
        raise UpdateError("transferencia runtime v2 pendente era esperada")
    if expected_runtime_cache == "empty" and runtime_alias is not None:
        raise UpdateError("cache runtime v2 deveria estar vazio")
    if expected_runtime_cache == "valid" and runtime_alias is None:
        raise UpdateError("cache runtime v2 valido esperado nao existe")
    if expected_runtime_alias is not None and runtime_alias != expected_runtime_alias:
        raise UpdateError(
            f"alias runtime e {runtime_alias or 'none'}, "
            f"esperado {expected_runtime_alias}"
        )
    if not allow_pending and runtime_local_pending:
        raise UpdateError("imagem possui journal local runtime v2 pendente")
    if remote_alias is not None:
        if remote_alias not in root:
            raise UpdateError("slot ativo do cache remoto esta ausente")
        remote_data = root[remote_alias][0]
        if (
            len(remote_data) != remote.package_size
            or hashlib.sha256(remote_data).digest()
            != remote.package_sha256
        ):
            raise UpdateError("hash ou tamanho do cache remoto diverge")
        trusted = load_public_json(RELEASE_PUBLIC)
        verify_artifact(
            remote_data, trusted, remote.base_version, remote.base_epoch
        )
    if not remote_pending:
        for alias in REMOTE_PACKAGE_ALIASES:
            if (alias == remote_alias) != (alias in root):
                raise UpdateError(f"slot remoto inativo permaneceu: {alias}")
    if runtime_alias is not None:
        if runtime_alias not in root:
            raise UpdateError("slot ativo runtime v2 esta ausente")
        trusted = load_public_json(RELEASE_PUBLIC)
        runtime_manifest = parse_runtime_manifest(root[runtime_alias][0], trusted)
        if runtime_remote is None:
            raise UpdateError("manifesto runtime existe sem registro ativo")
        if (
            len(root[runtime_alias][0]) != RUNTIME_MANIFEST_SIZE
            or hashlib.sha256(root[runtime_alias][0]).digest()
            != runtime_remote.manifest_sha256
        ):
            raise UpdateError("hash do manifesto runtime diverge do registro")
        if runtime_remote.mode == 1:
            package_alias = RUNTIME_PACKAGE_ALIASES[runtime_remote.active_slot]
            if package_alias not in root:
                raise UpdateError("pacote runtime completo esta ausente")
            package_data = root[package_alias][0]
            if (
                len(package_data) != runtime_remote.package_size
                or hashlib.sha256(package_data).digest()
                != runtime_remote.package_sha256
            ):
                raise UpdateError("hash do pacote runtime diverge do registro")
            parse_runtime_package(package_data, trusted, runtime_manifest)
        for index, entry in enumerate(runtime_manifest.entries):
            asset_alias = f"ZRV{runtime_remote.active_slot}.{index:02d}"
            if not entry.target_present or asset_alias not in root:
                continue
            asset_data = root[asset_alias][0]
            if (
                len(asset_data) != entry.asset_size
                or hashlib.sha256(asset_data).digest() != entry.asset_sha256
            ):
                raise UpdateError(f"asset runtime diverge: {asset_alias}")
    if not runtime_pending:
        for alias in RUNTIME_MANIFEST_ALIASES + RUNTIME_PACKAGE_ALIASES:
            expected_present = bool(
                alias == runtime_alias or (
                    runtime_remote is not None and
                    runtime_remote.mode == 1 and
                    alias == RUNTIME_PACKAGE_ALIASES[runtime_remote.active_slot]
                )
            )
            if expected_present != (alias in root):
                raise UpdateError(f"slot runtime inativo permaneceu: {alias}")
        for slot in range(2):
            for index in range(RUNTIME_MAX_ENTRIES):
                alias = f"ZRV{slot}.{index:02d}"
                active_asset = (
                    runtime_remote is not None
                    and runtime_remote.active_slot == slot
                    and runtime_remote.mode == 0
                    and bool(runtime_remote.asset_mask & (1 << index))
                )
                if (alias in root) != active_asset:
                    raise UpdateError(f"asset runtime inativo permaneceu: {alias}")
    if state:
        for index, name in enumerate(UPDATE_TARGETS):
            expected = state.current[index]
            if not expected.present:
                if name in root:
                    raise UpdateError(f"arquivo U3 removido permaneceu: {name}")
                continue
            if name not in root:
                raise UpdateError(f"arquivo instalado ausente: {name}")
            data = root[name][0]
            if len(data) != expected.size or hashlib.sha256(data).digest() != expected.sha256:
                raise UpdateError(f"arquivo instalado diverge do estado: {name}")
    if runtime_local_state and not runtime_local_pending:
        for index, name in enumerate(RUNTIME_CATALOG):
            expected = runtime_local_state.current[index]
            if expected.present:
                if name not in root:
                    raise UpdateError(f"arquivo runtime ausente: {name}")
                data = root[name][0]
                if len(data) != expected.size or hashlib.sha256(data).digest() != expected.sha256:
                    raise UpdateError(f"arquivo runtime diverge do estado: {name}")
            elif name in root:
                raise UpdateError(f"arquivo runtime removido permaneceu: {name}")
        for slot in range(2):
            for index in range(RUNTIME_MAX_ENTRIES):
                stage_alias = f"{RUNTIME_STAGE_PREFIXES[slot]}{index:02d}"
                if stage_alias in root:
                    raise UpdateError(f"staging runtime permaneceu: {stage_alias}")
                backup_alias = f"{RUNTIME_BACKUP_PREFIXES[slot]}{index:02d}"
                should_exist = bool(
                    runtime_local_state.rollback_available
                    and runtime_local_state.rollback_slot == slot
                    and runtime_local_state.rollback[index].present
                )
                if should_exist != (backup_alias in root):
                    raise UpdateError(f"estado local runtime diverge: {backup_alias}")
                if should_exist:
                    data = root[backup_alias][0]
                    expected = runtime_local_state.rollback[index]
                    if (
                        len(data) != expected.size
                        or hashlib.sha256(data).digest() != expected.sha256
                    ):
                        raise UpdateError(f"backup runtime diverge: {backup_alias}")
    elif not runtime_local_pending:
        for prefix in (*RUNTIME_STAGE_PREFIXES, *RUNTIME_BACKUP_PREFIXES):
            for index in range(RUNTIME_MAX_ENTRIES):
                alias = f"{prefix}{index:02d}"
                if alias in root:
                    raise UpdateError(f"artefato local runtime sem estado: {alias}")
    if not allow_pending:
        for aliases in UPDATE_STAGE_ALIASES:
            if any(alias in root for alias in aliases):
                raise UpdateError("staging U3 permaneceu apos a transacao")
        for slot, aliases in enumerate(UPDATE_BACKUP_ALIASES):
            for index, alias in enumerate(aliases):
                should_exist = bool(
                    state
                    and state.rollback_available
                    and state.rollback_slot == slot
                    and state.rollback[index].present
                )
                if should_exist != (alias in root):
                    raise UpdateError(f"estado do backup U3 diverge: {alias}")
                if should_exist:
                    data = root[alias][0]
                    expected = state.rollback[index]
                    if len(data) != expected.size or hashlib.sha256(data).digest() != expected.sha256:
                        raise UpdateError(f"backup U3 diverge: {alias}")
    print("Audit image: OK")
    print(f"  installed={installed}")
    print(f"  rollback={'READY' if rollback_available else 'DISABLED'}")
    print(f"  journal={'PENDING' if pending else 'CLEAN'}")
    if last_event is None:
        print("  history=EMPTY count=0")
    else:
        operation = UPDATE_HISTORY_OPERATION_NAMES[last_event.operation]
        outcome = UPDATE_HISTORY_OUTCOME_NAMES[last_event.outcome]
        print(
            f"  history=READY count={history_count} "
            f"last={operation}/{outcome}"
        )
    print(
        "  remote="
        + ("READY" if remote_alias else "EMPTY")
        + f" alias={remote_alias or 'none'} "
        + ("pending=YES" if remote_pending else "pending=NO")
    )
    print(
        "  runtime="
        + ("READY" if runtime_alias else "EMPTY")
        + f" alias={runtime_alias or 'none'} "
        + ("pending=YES" if runtime_pending else "pending=NO")
    )
    print(
        "  runtime_local="
        + ("READY" if runtime_local_state else "EMPTY")
        + f" journal={'PENDING' if runtime_local_pending else 'CLEAN'}"
    )


def selftest_u1() -> None:
    """Valida os quatro vetores imutaveis publicados na U1."""
    fixture_manifest = json.loads(
        (U1_FIXTURES / "fixtures.json").read_text(encoding="utf-8")
    )
    trusted = public_key_info(
        bytes.fromhex(fixture_manifest["trusted_test_key"]["public_key_hex"])
    )
    for name, expected in (
        ("valid.zephyrosupd", REASON_NONE),
        ("corrupted.zephyrosupd", REASON_HASH),
        ("unknown-key.zephyrosupd", REASON_UNKNOWN_KEY),
        ("incompatible-version.zephyrosupd", REASON_BASE_VERSION),
    ):
        actual = expected_reason(
            (U1_FIXTURES / name).read_bytes(), trusted, Version(0, 1, 0), 0
        )
        if actual != expected:
            raise UpdateError(f"fixture U1 {name}: esperado {expected}, obtido {actual}")


def selftest_u2_public() -> None:
    """Confere hashes publicados e resultados dos sete fixtures da U2."""
    fixture_manifest = json.loads(
        (U2_FIXTURES / "fixtures.json").read_text(encoding="utf-8")
    )
    trusted = load_public_json(RELEASE_PUBLIC)
    published_key = fixture_manifest["release_public_key"]
    if (
        published_key["public_key_hex"] != trusted.public_key.hex()
        or published_key["key_id_hex"] != trusted.key_id.hex()
    ):
        raise UpdateError("manifesto U2 nao corresponde a raiz publica")
    for name, metadata in fixture_manifest["fixtures"].items():
        data = (U2_FIXTURES / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        expected = metadata["expected_reason"].removeprefix("ZUPD_REASON_")
        actual = expected_reason(data, trusted, Version(0, 1, 0), 0)
        if digest != metadata["sha256"] or len(data) != metadata["size"]:
            raise UpdateError(f"hash ou tamanho publicado diverge em {name}")
        if actual != expected:
            raise UpdateError(
                f"fixture U2 {name}: esperado {expected}, obtido {actual}"
            )


def selftest_u3_public() -> bool:
    """Valida APPLY.ZUP e payloads publicados; retorna falso no checkpoint."""
    manifest_path = U3_FIXTURES / "fixtures.json"
    if not manifest_path.is_file():
        return False
    try:
        fixture_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError("manifesto publico U3 invalido") from error
    trusted = load_public_json(RELEASE_PUBLIC)
    published_key = fixture_manifest["release_public_key"]
    if (
        published_key["public_key_hex"] != trusted.public_key.hex()
        or published_key["key_id_hex"] != trusted.key_id.hex()
    ):
        raise UpdateError("manifesto U3 nao corresponde a raiz publica")
    artifact_path = U3_FIXTURES / fixture_manifest["artifact"]["name"]
    artifact = artifact_path.read_bytes()
    artifact_meta = fixture_manifest["artifact"]
    if (
        len(artifact) != artifact_meta["size"]
        or hashlib.sha256(artifact).hexdigest() != artifact_meta["sha256"]
    ):
        raise UpdateError("hash ou tamanho publicado diverge em APPLY.ZUP")
    info = verify_artifact(artifact, trusted, Version(0, 1, 0), 0)
    if (
        info.target_version != Version(0, 1, 1)
        or info.target_epoch != 0
        or tuple(entry.path for entry in info.entries) != UPDATE_TARGETS
    ):
        raise UpdateError("metadados autenticados de APPLY.ZUP divergem")
    for entry in info.entries:
        metadata = fixture_manifest["payloads"][entry.path]
        payload = (U3_FIXTURES / entry.path).read_bytes()
        transformed = invert_bmp_pixels(UPDATE_ASSETS[entry.path].read_bytes())
        artifact_payload = artifact[
            entry.payload_offset : entry.payload_offset + entry.payload_size
        ]
        if payload != transformed or payload != artifact_payload:
            raise UpdateError(f"payload U3 diverge: {entry.path}")
        if (
            len(payload) != metadata["size"]
            or hashlib.sha256(payload).hexdigest() != metadata["sha256"]
        ):
            raise UpdateError(f"hash publicado U3 diverge: {entry.path}")
    return True


def encode_test_file(
    record: bytearray, offset: int, data: bytes, present: bool
) -> None:
    """Codifica um descritor usado apenas pelos autotestes de metadados."""
    if not present:
        return
    struct.pack_into("<I", record, offset, len(data))
    record[offset + 4 : offset + 36] = hashlib.sha256(data).digest()
    record[offset + 36] = 1


def encode_test_state(sequence: int, rollback: bool) -> bytes:
    """Cria um estado canônico sintético para testar o decoder host."""
    record = bytearray(UPDATE_CONTROL_SIZE)
    record[:4] = b"ZUST"
    struct.pack_into("<HHI", record, 4, 1, UPDATE_CONTROL_SIZE, sequence)
    version = (0, 1, 1) if rollback else (0, 1, 0)
    struct.pack_into("<3H", record, 12, *version)
    if rollback:
        record[24:27] = bytes((1, 0, UPDATE_TARGET_COUNT))
        struct.pack_into("<3H", record, 28, 0, 1, 0)
    for index, name in enumerate(UPDATE_TARGETS):
        current = f"current-{name}".encode("ascii")
        previous = f"previous-{name}".encode("ascii")
        encode_test_file(
            record,
            UPDATE_STATE_CURRENT_OFFSET + index * UPDATE_STATE_FILE_SIZE,
            current,
            True,
        )
        encode_test_file(
            record,
            UPDATE_STATE_ROLLBACK_OFFSET + index * UPDATE_STATE_FILE_SIZE,
            previous,
            rollback,
        )
    record[UPDATE_CONTROL_HASH_OFFSET:] = hashlib.sha256(
        record[:UPDATE_CONTROL_HASH_OFFSET]
    ).digest()
    return bytes(record)


def encode_test_clean_journal(sequence: int) -> bytes:
    """Cria um journal limpo canônico usado nos autotestes host."""
    record = bytearray(UPDATE_CONTROL_SIZE)
    record[:4] = b"ZUJ1"
    struct.pack_into("<HHI", record, 4, 1, UPDATE_CONTROL_SIZE, sequence)
    record[UPDATE_CONTROL_HASH_OFFSET:] = hashlib.sha256(
        record[:UPDATE_CONTROL_HASH_OFFSET]
    ).digest()
    return bytes(record)


def encode_test_history_entry(
    record: bytearray,
    offset: int,
    sequence: int,
    operation: int,
    outcome: int,
) -> None:
    """Codifica uma entrada canonica usada pelos autotestes U4."""
    struct.pack_into("<I4B", record, offset, sequence, operation, outcome, 0, 0)
    struct.pack_into("<3H", record, offset + 8, 0, 1, 0)
    struct.pack_into("<3H", record, offset + 14, 0, 1, 1)
    struct.pack_into("<IIHH", record, offset + 20, 0, 0, 3, 3)
    record[offset + 32] = UPDATE_HISTORY_FLAG_REBOOT
    alias = b"APPLY.ZUP\0"
    record[offset + 33 : offset + 33 + len(alias)] = alias


def encode_test_history(
    sequence: int,
    event_sequences: tuple[int, ...],
    next_index: int,
) -> bytes:
    """Cria um ring buffer ZUH1 sintetico no layout usado pelo kernel."""
    if len(event_sequences) > UPDATE_HISTORY_MAX_ENTRIES:
        raise UpdateError("autoteste U4 excedeu o ring buffer")
    record = bytearray(UPDATE_CONTROL_SIZE)
    record[:4] = b"ZUH1"
    struct.pack_into("<HHI", record, 4, 1, UPDATE_CONTROL_SIZE, sequence)
    record[12] = len(event_sequences)
    record[13] = next_index
    for index, event_sequence in enumerate(event_sequences):
        offset = UPDATE_HISTORY_ENTRY_OFFSET + index * UPDATE_HISTORY_ENTRY_SIZE
        operation = (event_sequence % 4) + 1
        outcome = (event_sequence % 4) + 1
        encode_test_history_entry(
            record, offset, event_sequence, operation, outcome
        )
    record[UPDATE_CONTROL_HASH_OFFSET:] = hashlib.sha256(
        record[:UPDATE_CONTROL_HASH_OFFSET]
    ).digest()
    return bytes(record)


def selftest_u4_history() -> None:
    """Exercita ring, wrap, redundancia, empate, reservados e SHA-256."""
    partial = decode_history_record(encode_test_history(3, (1, 2, 3), 3))
    if partial.count != 3 or history_newest(partial, 0).sequence != 3:
        raise UpdateError("ordem do historico U4 parcial divergiu")
    wrapped_record = encode_test_history(10, (9, 10, 3, 4, 5, 6, 7, 8), 2)
    wrapped = decode_history_record(wrapped_record)
    if [history_newest(wrapped, index).sequence for index in range(8)] != [
        10, 9, 8, 7, 6, 5, 4, 3
    ]:
        raise UpdateError("wrap apos oito eventos U4 divergiu")
    older = encode_test_history(9, (9, 2, 3, 4, 5, 6, 7, 8), 1)
    selected = select_redundant_record(
        (older, wrapped_record), decode_history_record, "historico U4 de teste"
    )
    if selected.sequence != 10:
        raise UpdateError("redundancia do historico U4 escolheu sequencia errada")
    corrupted = bytearray(wrapped_record)
    corrupted[100] ^= 1
    try:
        decode_history_record(bytes(corrupted))
    except UpdateError:
        pass
    else:
        raise UpdateError("SHA-256 corrompido do historico U4 foi aceito")
    selected = select_redundant_record(
        (older, bytes(corrupted)),
        decode_history_record,
        "historico U4 com uma copia corrompida",
    )
    if selected.sequence != 9:
        raise UpdateError("fallback redundante do historico U4 divergiu")
    reserved = bytearray(wrapped_record)
    reserved[14] = 1
    reserved[UPDATE_CONTROL_HASH_OFFSET:] = hashlib.sha256(
        reserved[:UPDATE_CONTROL_HASH_OFFSET]
    ).digest()
    try:
        decode_history_record(bytes(reserved))
    except UpdateError:
        pass
    else:
        raise UpdateError("reservado nao zero do historico U4 foi aceito")
    entry_reserved = bytearray(wrapped_record)
    entry_reserved[UPDATE_HISTORY_ENTRY_OFFSET + 46] = 1
    entry_reserved[UPDATE_CONTROL_HASH_OFFSET:] = hashlib.sha256(
        entry_reserved[:UPDATE_CONTROL_HASH_OFFSET]
    ).digest()
    try:
        decode_history_record(bytes(entry_reserved))
    except UpdateError:
        pass
    else:
        raise UpdateError("reservado de entrada do historico U4 foi aceito")
    divergent = bytearray(wrapped_record)
    divergent[UPDATE_HISTORY_ENTRY_OFFSET + 33] = ord("B")
    divergent[UPDATE_CONTROL_HASH_OFFSET:] = hashlib.sha256(
        divergent[:UPDATE_CONTROL_HASH_OFFSET]
    ).digest()
    try:
        select_redundant_record(
            (wrapped_record, bytes(divergent)),
            decode_history_record,
            "historico U4 empatado",
        )
    except UpdateError:
        pass
    else:
        raise UpdateError("empate divergente do historico U4 foi aceito")


def modeled_apply_clusters(
    old_sizes: tuple[int, ...],
    new_sizes: tuple[int, ...],
    cluster_size: int,
) -> int:
    """Replica a reserva conservadora calculada pelo preflight do kernel."""
    clusters = lambda size: (size + cluster_size - 1) // cluster_size
    return (
        8
        + sum(clusters(size) for size in old_sizes)
        + sum(clusters(size) for size in new_sizes)
        + clusters(max(old_sizes))
        + clusters(max(new_sizes))
    )


def modeled_apply_has_space(
    free_clusters: int,
    old_sizes: tuple[int, ...],
    new_sizes: tuple[int, ...],
    cluster_size: int,
) -> bool:
    """Indica se o modelo U3 aceita a quantidade informada de clusters."""
    return free_clusters >= modeled_apply_clusters(
        old_sizes, new_sizes, cluster_size
    )


def selftest_u3_metadata() -> None:
    """Exercita redundancia, corrupcao, espaco e transicoes da persistencia."""
    baseline = encode_test_state(1, False)
    applied = encode_test_state(2, True)
    baseline_state = decode_state_record(baseline)
    applied_state = decode_state_record(applied)
    if (
        baseline_state.rollback_available
        or not applied_state.rollback_available
        or applied_state.previous_version != Version(0, 1, 0)
    ):
        raise UpdateError("transicao sintetica de estado U3 divergiu")
    selected = select_redundant_record(
        (baseline, applied), decode_state_record, "estado U3 de teste"
    )
    if selected.sequence != 2:
        raise UpdateError("selecao de sequencia redundante U3 divergiu")
    corrupted = bytearray(applied)
    corrupted[100] ^= 1
    selected = select_redundant_record(
        (baseline, bytes(corrupted)),
        decode_state_record,
        "estado U3 corrompido",
    )
    if selected.sequence != 1:
        raise UpdateError("fallback redundante U3 nao escolheu copia valida")
    try:
        select_redundant_record(
            (bytes(corrupted), bytes(corrupted)),
            decode_state_record,
            "estado U3 corrompido",
        )
    except UpdateError:
        pass
    else:
        raise UpdateError("perda das duas copias U3 foi aceita")
    journal = decode_journal_record(encode_test_clean_journal(7))
    if journal.kind != UPDATE_JOURNAL_NONE or journal.sequence != 7:
        raise UpdateError("journal limpo sintetico divergiu")
    required = modeled_apply_clusters(
        (3126, 3126, 3126), (3126, 3126, 3126), 512
    )
    if (
        modeled_apply_has_space(
            required - 1, (3126, 3126, 3126), (3126, 3126, 3126), 512
        )
        or not modeled_apply_has_space(
            required, (3126, 3126, 3126), (3126, 3126, 3126), 512
        )
    ):
        raise UpdateError("modelo de falta de espaco U3 divergiu")


def selftest_manifest_rejections(private_key: Any) -> None:
    """Exercita politicas de manifesto e os limites totais do container."""
    duplicate = {
        "base_version": "0.1.0",
        "target_version": "0.1.1",
        "base_epoch": 0,
        "target_epoch": 0,
        "files": [
            {"path": "SHELL.BMP", "source": "a"},
            {"path": "SHELL.BMP", "source": "b"},
        ],
    }
    unauthorized = dict(duplicate)
    unauthorized["files"] = [{"path": "KERNEL.BIN", "source": "a"}]
    compressed = dict(duplicate)
    compressed["files"] = [
        {"path": "SHELL.BMP", "source": "a", "compression": "lzss"}
    ]
    for label, manifest in (
        ("duplicado", duplicate),
        ("nao autorizado", unauthorized),
        ("com compressao", compressed),
    ):
        try:
            validate_manifest(manifest, REPO_ROOT)
        except UpdateError:
            pass
        else:
            raise UpdateError(f"manifesto {label} foi aceito")
    large_payload = bytes(ZUPD_MAX_PAYLOAD_SIZE)
    try:
        build_from_parts(
            private_key,
            Version(0, 1, 0),
            Version(0, 1, 1),
            0,
            0,
            [(path, large_payload) for path in ZUPD_ALLOWLIST],
        )
    except UpdateError:
        pass
    else:
        raise UpdateError("overflow do artefato total foi aceito")


def selftest_generated() -> None:
    """Exercita chaves efemeras, matriz U2 e recusas do empacotador."""
    ed25519, serialization, _ = crypto_modules()
    private_key = ed25519.Ed25519PrivateKey.generate()
    trusted = private_public_info(private_key)
    blobs = fixture_blobs(private_key)
    for name, (data, expected) in blobs.items():
        actual = expected_reason(data, trusted, Version(0, 1, 0), 0)
        if actual != expected:
            raise UpdateError(f"fixture efemera {name}: esperado {expected}, obtido {actual}")
    wrong = private_public_info(ed25519.Ed25519PrivateKey.generate())
    valid = blobs["VALID.ZUP"][0]
    if expected_reason(valid, wrong, Version(0, 1, 0), 0) != REASON_UNKNOWN_KEY:
        raise UpdateError("chave desconhecida nao foi recusada")
    with tempfile.TemporaryDirectory(prefix="zephyros-updater-") as temp_name:
        temp = Path(temp_name)
        private_path = temp / "release.pem"
        public_path = temp / "release.json"
        password = b"senha-de-teste-1234"
        pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.BestAvailableEncryption(password),
        )
        write_new_bytes(private_path, pem)
        write_new_text(public_path, public_json(trusted))
        try:
            require_new_file(private_path, "chave privada")
        except UpdateError:
            pass
        else:
            raise UpdateError("sobrescrita de chave privada foi aceita")
        if private_public_info(load_private_key(private_path, password)) != trusted:
            raise UpdateError("recarga da chave privada divergiu")
        try:
            load_private_key(private_path, b"senha-incorreta-123")
        except UpdateError:
            pass
        else:
            raise UpdateError("senha incorreta foi aceita")
        header_path = temp / "update_trust.h"
        sync_trust(public_path, header_path)
        check_trust(public_path, header_path)
        payload_path = temp / "SHELL.BMP"
        write_new_bytes(payload_path, SHELL_BMP.read_bytes())
        manifest_path = temp / "manifest.json"
        write_new_text(
            manifest_path,
            json.dumps(
                {
                    "format": "ZUPD v1",
                    "architecture": "i386",
                    "base_version": "0.1.0",
                    "target_version": "0.1.1",
                    "base_epoch": 0,
                    "target_epoch": 0,
                    "files": [{"path": "SHELL.BMP", "source": "SHELL.BMP"}],
                },
                indent=2,
            ) + "\n",
        )
        descriptor_path = build_release_bundle(
            "selftest-release", manifest_path, private_key, trusted, 1,
            "HEAD", None, temp / "release-output",
        )
        bundle = verify_release_bundle(descriptor_path, trusted)
        if bundle.target_version != Version(0, 1, 1) or bundle.tag is not None:
            raise UpdateError("round-trip da Release EP5 divergiu")
    try:
        validate_private_output(REPO_ROOT / "release-test.pem")
    except UpdateError:
        pass
    else:
        raise UpdateError("caminho privado dentro do repositorio foi aceito")
    payload = SHELL_BMP.read_bytes()
    selftest_manifest_rejections(private_key)
    if len(payload) > ZUPD_MAX_PAYLOAD_SIZE:
        raise UpdateError("asset congelado excede o limite da fixture")


def selftest_u5_remote() -> None:
    """Exercita ZUM1, assinatura, padding e controles A/B."""
    ed25519, _, _ = crypto_modules()
    private_key = ed25519.Ed25519PrivateKey.generate()
    other_key = ed25519.Ed25519PrivateKey.generate()
    public = private_public_info(private_key)
    package = (U3_FIXTURES / "APPLY.ZUP").read_bytes()
    manifest = build_remote_manifest(
        private_key,
        public,
        7,
        Version(0, 1, 0),
        Version(0, 1, 1),
        0,
        0,
        package,
        "/zephyros/APPLY.ZUP",
    )
    decoded = parse_remote_manifest(manifest, public)
    if (
        decoded.generation != 7
        or decoded.package_size != len(package)
        or decoded.package_sha256 != hashlib.sha256(package).digest()
    ):
        raise UpdateError("round-trip do manifesto remoto divergiu")
    corrupted = bytearray(manifest)
    corrupted[40] ^= 1
    try:
        parse_remote_manifest(bytes(corrupted), public)
    except UpdateError:
        pass
    else:
        raise UpdateError("manifesto remoto adulterado foi aceito")
    try:
        parse_remote_manifest(manifest, private_public_info(other_key))
    except UpdateError:
        pass
    else:
        raise UpdateError("chave remota desconhecida foi aceita")
    invalid_padding = bytearray(manifest)
    invalid_padding[191] = 1
    invalid_padding[192:256] = private_key.sign(
        REMOTE_DOMAIN + bytes(invalid_padding[:192])
    )
    try:
        parse_remote_manifest(bytes(invalid_padding), public)
    except UpdateError:
        pass
    else:
        raise UpdateError("padding remoto nao zero foi aceito")
    try:
        parse_remote_manifest(manifest[:-1], public)
    except UpdateError:
        pass
    else:
        raise UpdateError("manifesto remoto truncado foi aceito")
    try:
        encode_remote_path("/zephyros/../APPLY.ZUP")
    except UpdateError:
        pass
    else:
        raise UpdateError("travessia no caminho remoto foi aceita")
    stream_hash = hashlib.sha256()
    received = 0
    for offset in range(0, len(package), 509):
        chunk = package[offset : offset + 509]
        stream_hash.update(chunk)
        received += len(chunk)
    if (
        received != len(package)
        or stream_hash.digest() != hashlib.sha256(package).digest()
    ):
        raise UpdateError("streaming incremental remoto divergiu")
    active_cache = b"cache-anterior"
    pending = bytearray()
    pending.extend(package[:509])
    pending.clear()
    if active_cache != b"cache-anterior" or pending:
        raise UpdateError("cancelamento remoto nao preservou o cache ativo")
    should_retry = lambda error, cancelled, attempt: (
        not cancelled and error in ("TIMEOUT", "STATE", "NOT_FOUND")
        and attempt == 0
    )
    if (
        not should_retry("TIMEOUT", False, 0)
        or should_retry("TIMEOUT", True, 0)
        or should_retry("SIGNATURE", False, 0)
        or should_retry("TIMEOUT", False, 1)
    ):
        raise UpdateError("politica de retry/cancelamento remoto divergiu")
    empty_hash = bytes(32)
    record = RemoteRecord(
        3,
        REMOTE_PHASE_CLEAN,
        0,
        REMOTE_SLOT_NONE,
        decoded.generation,
        decoded.package_size,
        decoded.package_sha256,
        hashlib.sha256(manifest).digest(),
        decoded.base_version,
        decoded.target_version,
        decoded.base_epoch,
        decoded.target_epoch,
    )
    if decode_remote_record(encode_remote_record(record)) != record:
        raise UpdateError("round-trip do registro remoto divergiu")
    older = RemoteRecord(
        2, record.phase, record.active_slot, record.pending_slot,
        record.manifest_generation, record.package_size,
        record.package_sha256, record.manifest_sha256,
        record.base_version, record.target_version,
        record.base_epoch, record.target_epoch,
    )
    selected = select_redundant_record(
        (encode_remote_record(older), encode_remote_record(record)),
        decode_remote_record, "cache remoto U5 de teste",
    )
    if selected.sequence != 3:
        raise UpdateError("selecao A/B do cache remoto divergiu")
    corrupted_record = bytearray(encode_remote_record(record))
    corrupted_record[24] ^= 1
    selected = select_redundant_record(
        (encode_remote_record(older), bytes(corrupted_record)),
        decode_remote_record, "cache remoto U5 corrompido",
    )
    if selected.sequence != 2:
        raise UpdateError("fallback A/B do cache remoto divergiu")
    divergent = RemoteRecord(
        record.sequence, record.phase, record.active_slot,
        record.pending_slot, record.manifest_generation + 1,
        record.package_size, record.package_sha256,
        record.manifest_sha256, record.base_version,
        record.target_version, record.base_epoch, record.target_epoch,
    )
    try:
        select_redundant_record(
            (encode_remote_record(record), encode_remote_record(divergent)),
            decode_remote_record, "cache remoto U5 empatado",
        )
    except UpdateError:
        pass
    else:
        raise UpdateError("empate divergente do cache remoto foi aceito")
    pending = RemoteRecord(
        4,
        REMOTE_PHASE_DOWNLOADING,
        0,
        1,
        decoded.generation,
        decoded.package_size,
        decoded.package_sha256,
        empty_hash,
        decoded.base_version,
        decoded.target_version,
        0,
        0,
    )
    raw = bytearray(encode_remote_record(pending))
    raw[REMOTE_RECORD_RESERVED_OFFSET] = 1
    raw[REMOTE_RECORD_HASH_OFFSET:] = hashlib.sha256(
        raw[:REMOTE_RECORD_HASH_OFFSET]
    ).digest()
    try:
        decode_remote_record(bytes(raw))
    except UpdateError:
        pass
    else:
        raise UpdateError("reservado remoto nao zero foi aceito")
    recovered = RemoteRecord(
        pending.sequence + 1, REMOTE_PHASE_CLEAN,
        pending.active_slot, REMOTE_SLOT_NONE,
        pending.manifest_generation, pending.package_size,
        pending.package_sha256, pending.manifest_sha256,
        pending.base_version, pending.target_version,
        pending.base_epoch, pending.target_epoch,
    )
    if (
        decode_remote_record(encode_remote_record(recovered)).phase
        != REMOTE_PHASE_CLEAN
    ):
        raise UpdateError("recuperacao do estado remoto divergiu")
    check_remote_config(
        REMOTE_CONFIG,
        REPO_ROOT / "src" / "include" / "core" /
        "update_remote_config.h",
    )


def selftest_u5_public() -> None:
    """Valida hashes, assinaturas e resultados dos fixtures publicos U5."""
    try:
        published = json.loads(
            (U5_FIXTURES / "fixtures.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError("manifesto publico U5 ausente ou invalido") from error
    if published.get("format") != "zephyros-update-remote-fixtures-v1":
        raise UpdateError("formato do manifesto publico U5 divergiu")
    trusted = load_public_json(RELEASE_PUBLIC)
    public = published.get("release_public_key", {})
    if (
        public.get("public_key_hex") != trusted.public_key.hex()
        or public.get("key_id_hex") != trusted.key_id.hex()
    ):
        raise UpdateError("raiz publica dos fixtures U5 divergiu")
    packages = published.get("packages", {})
    package_bytes: dict[str, bytes] = {}
    for alias in ("APPLY.ZUP", "BADHASH.ZUP"):
        metadata = packages.get(alias, {})
        source = (U5_FIXTURES / str(metadata.get("source", ""))).resolve()
        try:
            data = source.read_bytes()
        except OSError as error:
            raise UpdateError(f"pacote referenciado U5 ausente: {alias}") from error
        if (
            len(data) != metadata.get("size")
            or hashlib.sha256(data).hexdigest() != metadata.get("sha256")
        ):
            raise UpdateError(f"hash publicado do pacote U5 divergiu: {alias}")
        package_bytes[alias] = data
    fixtures = published.get("fixtures", {})
    expected_names = {
        "stable.zum",
        "stable2.zum",
        "tampered.zum",
        "badpkg.zum",
        "truncated.zum",
    }
    if set(fixtures) != expected_names:
        raise UpdateError("conjunto de fixtures publicos U5 divergiu")
    for name, metadata in fixtures.items():
        data = (U5_FIXTURES / name).read_bytes()
        if (
            len(data) != metadata.get("size")
            or hashlib.sha256(data).hexdigest() != metadata.get("sha256")
        ):
            raise UpdateError(f"hash publicado do fixture U5 divergiu: {name}")
    stable = parse_remote_manifest(
        (U5_FIXTURES / "stable.zum").read_bytes(), trusted
    )
    stable2 = parse_remote_manifest(
        (U5_FIXTURES / "stable2.zum").read_bytes(), trusted
    )
    if (
        stable.generation != 1
        or stable2.generation != 2
        or stable.package_path != "/zephyros/APPLY.ZUP"
        or stable2.package_path != stable.package_path
        or stable.package_sha256
        != hashlib.sha256(package_bytes["APPLY.ZUP"]).digest()
        or stable2.package_sha256 != stable.package_sha256
    ):
        raise UpdateError("manifestos validos U5 divergiram do pacote publicado")
    try:
        parse_remote_manifest(
            (U5_FIXTURES / "tampered.zum").read_bytes(), trusted
        )
    except UpdateError:
        pass
    else:
        raise UpdateError("fixture U5 adulterado foi aceito")
    badpkg = parse_remote_manifest(
        (U5_FIXTURES / "badpkg.zum").read_bytes(), trusted
    )
    if (
        badpkg.package_path != "/zephyros/BADHASH.ZUP"
        or badpkg.package_sha256
        != hashlib.sha256(package_bytes["BADHASH.ZUP"]).digest()
        or expected_reason(
            package_bytes["BADHASH.ZUP"], trusted, Version(0, 1, 0), 0
        )
        != REASON_HASH
    ):
        raise UpdateError("fixture U5 de pacote invalido nao resulta em HASH")
    if len((U5_FIXTURES / "truncated.zum").read_bytes()) == REMOTE_MANIFEST_SIZE:
        raise UpdateError("fixture U5 truncado possui tamanho completo")


def selftest_ep6_public() -> None:
    """Valida as duas Releases e os descritores EP6.0 determinísticos."""
    try:
        published = json.loads(
            (U5_FIXTURES / "fixtures.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise UpdateError("manifesto publico EP6 ausente ou invalido") from error
    expected = {
        "ep6-stable.json": ("ep6-stable", "1" * 40, "stable.zum"),
        "ep6-alt.json": ("ep6-alt", "2" * 40, "stable2.zum"),
    }
    releases = published.get("releases", {})
    if set(releases) != set(expected):
        raise UpdateError("conjunto de Releases EP6 divergiu")
    package = (U3_FIXTURES / "APPLY.ZUP").read_bytes()
    trusted = load_public_json(RELEASE_PUBLIC)
    artifact = verify_artifact(package, trusted, Version(0, 1, 0), 0)
    for name, (tag, source_commit, manifest_name) in expected.items():
        path = U5_FIXTURES / name
        data = path.read_bytes()
        metadata = releases[name]
        if (
            len(data) != metadata.get("size")
            or hashlib.sha256(data).hexdigest() != metadata.get("sha256")
        ):
            raise UpdateError(f"hash publicado da Release EP6 divergiu: {name}")
        descriptor = load_release_descriptor(path)
        if (
            descriptor["release_id"] != tag
            or descriptor["tag"] != tag
            or descriptor["source_commit"] != source_commit
        ):
            raise UpdateError(f"identidade da Release EP6 divergiu: {name}")
        lock = descriptor["version_lock"]
        if lock != {
            "minimum_version": "0.1.0",
            "target_version": "0.1.1",
            "base_epoch": 0,
            "target_epoch": 0,
        }:
            raise UpdateError(f"version_lock EP6 divergiu: {name}")
        expected_package = release_asset_metadata("APPLY.ZUP", package)
        expected_manifest = release_asset_metadata(
            manifest_name, (U5_FIXTURES / manifest_name).read_bytes()
        )
        if descriptor["assets"] != {
            "package": expected_package,
            "manifest": expected_manifest,
        }:
            raise UpdateError(f"assets EP6 divergiram: {name}")
        if (
            artifact.base_version != Version(0, 1, 0)
            or artifact.target_version != Version(0, 1, 1)
            or artifact.base_epoch != 0
            or artifact.target_epoch != 0
        ):
            raise UpdateError("artefato base das Releases EP6 divergiu")
    invalid = published.get("invalid_releases", {})
    expected_invalid = {
        "ep6-invalid-json.json",
        "ep6-no-tag.json",
        "ep6-missing-asset.json",
        "ep6-tampered-manifest.json",
        "ep6-invalid-package.json",
        "ep6-divergent-hash.json",
        "ep6-divergent-lock.json",
        "ep6-divergent-tag.json",
    }
    if set(invalid) != expected_invalid:
        raise UpdateError("conjunto de descritores invalidos EP6 divergiu")
    for name, metadata in invalid.items():
        data = (U5_FIXTURES / name).read_bytes()
        if (
            len(data) != metadata.get("size")
            or hashlib.sha256(data).hexdigest() != metadata.get("sha256")
        ):
            raise UpdateError(f"hash publicado do descritor EP6 divergiu: {name}")


def selftest_ep63_runtime() -> None:
    """Exercita assinatura, cache seletivo e bases multiplas do ZUM2."""
    ed25519, _, _ = crypto_modules()
    private_key = ed25519.Ed25519PrivateKey.generate()
    trusted = private_public_info(private_key)
    with tempfile.TemporaryDirectory(prefix="zephyros-ep63-") as temp_name:
        root = Path(temp_name)
        input_dir = root / "input"
        input_dir.mkdir()
        for name, source in UPDATE_ASSETS.items():
            write_new_bytes(input_dir / name, source.read_bytes())
        manifest_path = input_dir / "runtime.json"
        write_new_text(
            manifest_path,
            json.dumps(
                {
                    "format": "ZUM2 v2",
                    "generation": 6301,
                    "release_tag": "selftest-ep63",
                    "release_id": "selftest-ep63",
                    "target_version": "0.1.1",
                    "target_epoch": 0,
                    "base_versions": [
                        {"version": "0.1.0", "epoch": 0},
                        {"version": "0.0.9", "epoch": 0},
                    ],
                    "files": [
                        {"path": "EXPLORER.BMP", "operation": "replace_or_create", "source": "EXPLORER.BMP"},
                        {"path": "SHELL.BMP", "operation": "create", "source": "SHELL.BMP"},
                        {"path": "TASKMGR.BMP", "operation": "delete", "source": None},
                    ],
                },
                indent=2,
            )
            + "\n",
        )
        output = build_runtime_bundle(
            manifest_path, private_key, trusted, root / "release"
        )
        manifest = parse_runtime_manifest(
            (output / "runtime.zum2").read_bytes(), trusted
        )
        package_data = (output / "runtime.zephyrosupd").read_bytes()
        package = parse_runtime_package(package_data, trusted, manifest)
        if (
            manifest.release_tag != "selftest-ep63"
            or len(manifest.base_versions) != 2
            or package.target_version != Version(0, 1, 1)
            or any(entry.path == "TASKMGR.BMP" and entry.present for entry in package.entries)
        ):
            raise UpdateError("round-trip do runtime v2 divergiu")
        for base, epoch in manifest.base_versions:
            parse_runtime_package(
                package_data, trusted, manifest, base, epoch
            )
        corrupted_manifest = bytearray((output / "runtime.zum2").read_bytes())
        corrupted_manifest[16] ^= 1
        try:
            parse_runtime_manifest(bytes(corrupted_manifest), trusted)
        except Rejection as error:
            if error.reason != REASON_SIGNATURE:
                raise UpdateError("manifesto ZUM2 corrompido produziu motivo errado")
        else:
            raise UpdateError("manifesto ZUM2 corrompido foi aceito")
        corrupted_package = bytearray(package_data)
        corrupted_package[-RUNTIME_SIGNATURE_SIZE - 1] ^= 1
        try:
            parse_runtime_package(bytes(corrupted_package), trusted, manifest)
        except Rejection as error:
            if error.reason not in (REASON_HASH, REASON_SIGNATURE):
                raise UpdateError("pacote ZUPD v2 corrompido produziu motivo errado")
        else:
            raise UpdateError("pacote ZUPD v2 corrompido foi aceito")


def selftest_ep5_release() -> None:
    """Exercita Releases com trava assinada e tag apenas auxiliar."""
    trusted = load_public_json(RELEASE_PUBLIC)
    package = (U3_FIXTURES / "APPLY.ZUP").read_bytes()
    manifest = (U5_FIXTURES / "stable.zum").read_bytes()
    artifact = verify_artifact(package, trusted, Version(0, 1, 0), 0)
    commit = "1" * 40

    def resolver(reference: str, _repository: Path) -> str:
        if reference in (commit, "refs/tags/ep5-fixture"):
            return commit
        raise UpdateError(f"referencia Git inexistente: {reference}")

    with tempfile.TemporaryDirectory(prefix="zephyros-ep5-") as temp_name:
        root = Path(temp_name)
        (root / "APPLY.ZUP").write_bytes(package)
        (root / "stable.zum").write_bytes(manifest)

        def write_fixture(name: str, value: dict[str, Any]) -> Path:
            path = root / name
            path.write_text(
                json.dumps(value, indent=2, ensure_ascii=True) + "\n",
                encoding="utf-8",
            )
            return path

        descriptor = json.loads(
            release_descriptor(
                "ep5-fixture", "EP5 fixture", commit, None, artifact,
                "APPLY.ZUP", package, "stable.zum", manifest,
            )
        )
        valid_without_tag = write_fixture("valid-without-tag.json", descriptor)
        verify_release_bundle(valid_without_tag, trusted, resolver=resolver)

        with_tag = json.loads(json.dumps(descriptor))
        with_tag["tag"] = "ep5-fixture"
        verify_release_bundle(
            write_fixture("valid-with-tag.json", with_tag),
            trusted,
            resolver=resolver,
        )

        rejected: list[tuple[str, dict[str, Any]]] = []
        missing = json.loads(json.dumps(descriptor))
        missing["assets"]["manifest"]["name"] = "missing.zum"
        rejected.append(("missing-asset.json", missing))

        tampered_data = (U5_FIXTURES / "tampered.zum").read_bytes()
        (root / "tampered.zum").write_bytes(tampered_data)
        tampered = json.loads(json.dumps(descriptor))
        tampered["assets"]["manifest"] = release_asset_metadata(
            "tampered.zum", tampered_data
        )
        rejected.append(("tampered-manifest.json", tampered))

        bad_package_data = (U2_FIXTURES / "BADHASH.ZUP").read_bytes()
        bad_manifest_data = (U5_FIXTURES / "badpkg.zum").read_bytes()
        (root / "BADHASH.ZUP").write_bytes(bad_package_data)
        (root / "badpkg.zum").write_bytes(bad_manifest_data)
        invalid_package = json.loads(json.dumps(descriptor))
        invalid_package["assets"]["package"] = release_asset_metadata(
            "BADHASH.ZUP", bad_package_data
        )
        invalid_package["assets"]["manifest"] = release_asset_metadata(
            "badpkg.zum", bad_manifest_data
        )
        rejected.append(("invalid-package.json", invalid_package))

        divergent_lock = json.loads(json.dumps(descriptor))
        divergent_lock["version_lock"]["target_version"] = "0.1.2"
        rejected.append(("divergent-version-lock.json", divergent_lock))

        divergent_tag = json.loads(json.dumps(descriptor))
        divergent_tag["tag"] = "wrong-tag"
        rejected.append(("divergent-tag.json", divergent_tag))

        divergent_commit = json.loads(json.dumps(descriptor))
        divergent_commit["source_commit"] = "2" * 40
        rejected.append(("divergent-commit.json", divergent_commit))

        for name, value in rejected:
            try:
                verify_release_bundle(
                    write_fixture(name, value), trusted, resolver=resolver
                )
            except UpdateError:
                continue
            raise UpdateError(f"fixture EP5 invalido foi aceito: {name}")


def selftest_ep90a_system() -> None:
    """Exercita ZSYS v1 e a Release combinada v2."""
    ed25519, _, invalid_signature = crypto_modules()
    private_key = ed25519.Ed25519PrivateKey.generate()
    trusted = private_public_info(private_key)
    commit = "9" * 40

    def resolver(reference: str, _repository: Path) -> str:
        if reference in (commit, "refs/tags/selftest-ep90a"):
            return commit
        raise UpdateError(f"referencia Git inexistente: {reference}")

    with tempfile.TemporaryDirectory(prefix="zephyros-ep90a-") as temp_name:
        root = Path(temp_name)
        boot = bytes((index * 3) & 0xFF for index in range(512))
        stage2 = bytes((index * 5 + 1) & 0xFF for index in range(512))
        kernel = bytes((index * 7 + 2) & 0xFF for index in range(1536))
        image = boot + stage2 + kernel + bytes(512 * 2877)
        write_new_bytes(root / "boot.bin", boot)
        write_new_bytes(root / "stage2.bin", stage2)
        write_new_bytes(root / "kernel.bin", kernel)
        write_new_bytes(root / "zephyros.img", image)
        manifest_path = root / "system.json"
        write_new_text(
            manifest_path,
            json.dumps(
                {
                    "format": "ZSYS v1",
                    "release_id": "selftest-ep90a",
                    "release_tag": "selftest-ep90a",
                    "target_version": "0.1.1",
                    "target_epoch": 0,
                    "supported_from": [{"version": "0.1.0", "epoch": 0}],
                    "min_updater": {"version": "0.1.0", "epoch": 0},
                    "boot_abi": 1,
                    "data_schema_from": 1,
                    "data_schema_to": 1,
                    "requires_reboot": True,
                    "channel": "stable",
                    "upgrade_route": {
                        "kind": "direct",
                        "checkpoints": [],
                        "bridge_required": False,
                    },
                },
                indent=2,
            ) + "\n",
        )
        package = system_build_package(
            system_manifest_spec(manifest_path), image, boot, stage2, kernel,
            private_key, trusted,
        )
        parsed = parse_system_package(
            package, trusted, Version(0, 1, 0), 0
        )
        if parsed.image_size != len(image) or parsed.release_id != "selftest-ep90a":
            raise UpdateError("round-trip ZSYS divergiu")
        try:
            parse_system_package(package, private_public_info(ed25519.Ed25519PrivateKey.generate()))
        except Rejection as error:
            if error.reason != REASON_UNKNOWN_KEY:
                raise UpdateError("chave ZSYS desconhecida produziu motivo errado")
        else:
            raise UpdateError("chave ZSYS desconhecida foi aceita")
        for label, mutate in (
            ("header", SYSTEM_HEADER_TARGET_VERSION_OFFSET),
            ("payload", SYSTEM_HEADER_SIZE + 3),
            ("signature", SYSTEM_HEADER_SIGNATURE_OFFSET),
        ):
            corrupted = bytearray(package)
            corrupted[mutate] ^= 1
            try:
                parse_system_package(corrupted, trusted)
            except (Rejection, UpdateError) as error:
                if isinstance(error, Rejection) and error.reason not in (REASON_HASH, REASON_SIGNATURE, REASON_FORMAT, REASON_DOWNGRADE):
                    raise UpdateError(f"corrupcao ZSYS {label} produziu motivo errado")
            else:
                raise UpdateError(f"corrupcao ZSYS {label} foi aceita")
        try:
            parse_system_package(package[:-1], trusted)
        except Rejection as error:
            if error.reason != REASON_SIZE:
                raise UpdateError("ZSYS truncado produziu motivo errado")
        else:
            raise UpdateError("ZSYS truncado foi aceito")

        oversized = bytearray(package)
        struct.pack_into("<I", oversized, 24, SYSTEM_MAX_IMAGE_SIZE + 512)
        try:
            parse_system_package(oversized, trusted)
        except Rejection as error:
            if error.reason != REASON_SIZE:
                raise UpdateError("ZSYS excessivo produziu motivo errado")
        else:
            raise UpdateError("ZSYS excessivo foi aceito")
        misaligned = bytearray(package)
        struct.pack_into("<I", misaligned, 24, len(image) - 1)
        try:
            parse_system_package(misaligned, trusted)
        except Rejection as error:
            if error.reason != REASON_SIZE:
                raise UpdateError("ZSYS desalinhado produziu motivo errado")
        else:
            raise UpdateError("ZSYS desalinhado foi aceito")

        try:
            parse_system_package(package, trusted, Version(0, 1, 0), 1)
        except Rejection as error:
            if error.reason != REASON_BASE_VERSION:
                raise UpdateError("epoch-base ZSYS incompativel produziu motivo errado")
        else:
            raise UpdateError("epoch-base ZSYS incompativel foi aceita")

        for label, field_offset, value, expected_reason in (
            ("abi", SYSTEM_HEADER_BOOT_ABI_OFFSET, 2, REASON_UNSUPPORTED),
            ("schema", SYSTEM_HEADER_SCHEMA_FROM_OFFSET, 2, REASON_UNSUPPORTED),
        ):
            incompatible = bytearray(package)
            struct.pack_into("<I", incompatible, field_offset, value)
            try:
                parse_system_package(
                    resign_system_package(bytes(incompatible), private_key),
                    trusted,
                )
            except Rejection as error:
                if error.reason != expected_reason:
                    raise UpdateError(
                        f"incompatibilidade {label} ZSYS produziu motivo errado"
                    )
            else:
                raise UpdateError(f"incompatibilidade {label} ZSYS foi aceita")

        for label, offset in (
            ("imagem", SYSTEM_HEADER_IMAGE_HASH_OFFSET),
            (
                "componente",
                SYSTEM_HEADER_COMPONENTS_OFFSET + SYSTEM_COMPONENT_HASH_OFFSET,
            ),
        ):
            divergent = bytearray(package)
            divergent[offset] ^= 1
            try:
                parse_system_package(
                    resign_system_package(bytes(divergent), private_key),
                    trusted,
                )
            except Rejection as error:
                if error.reason != REASON_HASH:
                    raise UpdateError(
                        f"divergencia de hash {label} produziu motivo errado"
                    )
            else:
                raise UpdateError(f"divergencia de hash {label} foi aceita")
        try:
            parse_system_package(package, trusted, Version(0, 2, 0), 0)
        except Rejection as error:
            if error.reason != REASON_BASE_VERSION:
                raise UpdateError("versao-base ZSYS incompativel produziu motivo errado")
        else:
            raise UpdateError("versao-base ZSYS incompativel foi aceita")

        legacy_dir = root / "legacy"
        runtime_dir = root / "runtime"
        system_dir = root / "system"
        for directory in (legacy_dir, runtime_dir, system_dir):
            directory.mkdir()
        legacy_package = build_from_parts(
            private_key, Version(0, 1, 0), Version(0, 1, 1), 0, 0,
            [("EXPLORER.BMP", b"legacy-ep90a")],
        )
        legacy_manifest = build_remote_manifest(
            private_key, trusted, 9001, Version(0, 1, 0), Version(0, 1, 1),
            0, 0, legacy_package, "/zephyros/APPLY.ZUP",
        )
        write_new_bytes(legacy_dir / "APPLY.ZUP", legacy_package)
        write_new_bytes(legacy_dir / "release.zum", legacy_manifest)
        runtime_input = runtime_dir / "input"
        runtime_input.mkdir()
        for name, source in UPDATE_ASSETS.items():
            write_new_bytes(runtime_input / name, source.read_bytes())
        runtime_manifest_path = runtime_input / "runtime.json"
        write_new_text(
            runtime_manifest_path,
            json.dumps(
                {
                    "format": "ZUM2 v2",
                    "generation": 9001,
                    "release_tag": "selftest-ep90a",
                    "release_id": "selftest-ep90a",
                    "target_version": "0.1.1",
                    "target_epoch": 0,
                    "base_versions": [{"version": "0.1.0", "epoch": 0}],
                    "files": [
                        {"path": name, "operation": "replace", "source": name}
                        for name in UPDATE_ASSETS
                    ],
                },
                indent=2,
            ) + "\n",
        )
        runtime_output = build_runtime_bundle(
            runtime_manifest_path, private_key, trusted, runtime_dir / "built"
        )
        for source in runtime_output.iterdir():
            write_new_bytes(runtime_dir / source.name, source.read_bytes())
        write_new_bytes(system_dir / "system.zsys", package)
        legacy = release_v2_legacy_namespace(legacy_dir, trusted)
        runtime = release_v2_runtime_namespace(runtime_dir, trusted)
        system = {"zsys": release_asset_metadata("system.zsys", package)}
        descriptor = json.loads(
            release_v2_descriptor(
                "selftest-ep90a", "EP9.0A fixture", commit,
                "selftest-ep90a", parsed.compatibility, legacy, runtime, system,
            )
        )
        descriptor_path = root / "release.json"
        for source in (
            legacy_dir / legacy["package"]["name"],
            legacy_dir / legacy["manifest"]["name"],
        ):
            write_new_bytes(root / source.name, source.read_bytes())
        for source in runtime_dir.iterdir():
            if source.is_file() and source.name != "release.json":
                write_new_bytes(root / source.name, source.read_bytes())
        write_new_bytes(root / "system.zsys", package)
        write_new_text(descriptor_path, json.dumps(descriptor, indent=2) + "\n")
        checked = verify_release_v2_bundle(
            descriptor_path, trusted, resolver=resolver
        )
        if checked.release_id != "selftest-ep90a":
            raise UpdateError("Release v2 nao correlacionou o ZSYS")
        bad = json.loads(json.dumps(descriptor))
        bad["compatibility"]["boot_abi"] = 2
        write_new_text(root / "bad-release.json", json.dumps(bad) + "\n")
        try:
            verify_release_v2_bundle(root / "bad-release.json", trusted, resolver=resolver)
        except UpdateError:
            pass
        else:
            raise UpdateError("Release v2 divergente foi aceita")


def run_selftest() -> None:
    """Executa a suite host sem gravar no repositorio."""
    selftest_u1()
    selftest_u2_public()
    u3_ready = selftest_u3_public()
    selftest_u3_metadata()
    selftest_u4_history()
    selftest_generated()
    selftest_u5_remote()
    selftest_u5_public()
    selftest_ep6_public()
    selftest_ep63_runtime()
    selftest_ep5_release()
    selftest_ep90a_system()
    print(
        "Updater selftest: OK"
        if u3_ready
        else "Updater selftest: OK (fixture U3 aguarda checkpoint)"
    )


def print_artifact(info: ArtifactInfo) -> None:
    """Exibe metadados autenticados de forma compacta."""
    print("ZUPD valido")
    print(f"  base={info.base_version} epoch={info.base_epoch}")
    print(f"  target={info.target_version} epoch={info.target_epoch}")
    print(f"  entries={len(info.entries)} total_size={info.total_size}")
    for entry in info.entries:
        print(f"  {entry.path} size={entry.payload_size}")
    print("  reason=NONE")


def command_keygen(args: argparse.Namespace) -> None:
    """Executa o fluxo interativo de provisionamento."""
    info = generate_key(Path(args.private), Path(args.public), prompt_new_password())
    print("Chave Ed25519 criada com sucesso.")
    print(f"key_id={info.key_id.hex()}")
    print("A chave privada nao foi adicionada ao repositorio.")


def command_build(args: argparse.Namespace) -> None:
    """Constroi um artefato assinado e recusa sobrescrita."""
    if Path(args.output).suffix.lower() != ".zephyrosupd":
        raise UpdateError("saida host deve usar a extensao .zephyrosupd")
    output = require_new_file(Path(args.output), "artefato")
    key = load_private_key(Path(args.private), prompt_password())
    artifact = build_artifact(Path(args.manifest), key)
    write_new_bytes(output, artifact)
    print(f"ZUPD criado: {output}")
    print(f"sha256={hashlib.sha256(artifact).hexdigest()}")


def command_verify(args: argparse.Namespace) -> None:
    """Valida um arquivo local com a raiz publica selecionada."""
    trusted = load_public_json(Path(args.public))
    version, epoch = current_system_version()
    if args.system_version is not None:
        version = Version.parse(args.system_version)
    if args.system_epoch is not None:
        epoch = args.system_epoch
    try:
        data = Path(args.artifact).read_bytes()
    except OSError as error:
        raise UpdateError(f"nao foi possivel ler {args.artifact}") from error
    try:
        info = verify_artifact(data, trusted, version, epoch)
    except Rejection as error:
        print(f"ZUPD recusado: {error.reason} ({REASON_VALUES[error.reason]})")
        print(str(error))
        raise
    print_artifact(info)


def command_release_build(args: argparse.Namespace) -> None:
    """Gera o conjunto local que sera anexado manualmente a uma Release."""
    output = Path(args.output_dir).expanduser().resolve()
    if output.exists():
        raise UpdateError(f"diretorio da Release ja existe: {output}")
    key = load_private_key(
        validate_private_input(Path(args.private)), prompt_password()
    )
    descriptor = build_release_bundle(
        args.release,
        Path(args.manifest),
        key,
        load_public_json(Path(args.public)),
        args.generation,
        args.source_commit,
        args.tag,
        output,
    )
    bundle = verify_release_bundle(descriptor, load_public_json(Path(args.public)))
    print(f"Release local criada: {descriptor.parent}")
    print(f"Versao oficial: {bundle.target_version}")
    print(f"Versao minima: {bundle.minimum_version}")
    print("Publique manualmente somente os assets deste diretorio.")


def command_release_check(args: argparse.Namespace) -> None:
    """Confere uma Release local sem publicar ou acessar a rede."""
    bundle = verify_release_bundle(
        Path(args.release), load_public_json(Path(args.public))
    )
    print(f"Release: {bundle.release_id}")
    print(f"Versao oficial: {bundle.target_version}")
    print(f"Versao minima: {bundle.minimum_version}")
    print(f"Epoch: {bundle.base_epoch} -> {bundle.target_epoch}")
    print(f"Commit: {bundle.source_commit}")
    print(f"Tag auxiliar: {bundle.tag or 'ausente'}")
    print("Integridade: OK")


def command_runtime_build(args: argparse.Namespace) -> None:
    """Gera o manifesto assinado e o pacote completo do runtime v2."""
    key = load_private_key(
        validate_private_input(Path(args.private)), prompt_password()
    )
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    output = build_runtime_bundle(
        Path(args.manifest), key, public, Path(args.output_dir).resolve()
    )
    print(f"Runtime v2 criado: {output}")
    print(f"manifest_sha256={hashlib.sha256((output / 'runtime.zum2').read_bytes()).hexdigest()}")
    print(f"package_sha256={hashlib.sha256((output / 'runtime.zephyrosupd').read_bytes()).hexdigest()}")
    print("Publique release.json e somente os assets deste diretorio.")


def command_runtime_verify(args: argparse.Namespace) -> None:
    """Valida ZUM2 e ZUPD v2 localmente, sem instalar nada."""
    trusted = load_public_json(Path(args.public))
    manifest_data = Path(args.manifest).read_bytes()
    manifest = parse_runtime_manifest(manifest_data, trusted)
    package_data = Path(args.package).read_bytes()
    version, epoch = current_system_version()
    if args.system_version is not None:
        version = Version.parse(args.system_version)
    if args.system_epoch is not None:
        epoch = args.system_epoch
    package = parse_runtime_package(
        package_data, trusted, manifest, version, epoch
    )
    print("ZUM2/ZUPD v2 validos")
    print(f"  release={manifest.release_id} tag={manifest.release_tag}")
    print(f"  base_profiles={len(manifest.base_versions)}")
    print(f"  target={package.target_version} epoch={package.target_epoch}")
    print(f"  entries={len(package.entries)} total_size={package.total_size}")
    print("  reason=NONE")


def command_system_build(args: argparse.Namespace) -> None:
    """Gera um envelope ZSYS v1 assinado."""
    key = load_private_key(
        validate_private_input(Path(args.private)), prompt_password()
    )
    public = load_public_json(Path(args.public))
    output = build_system_bundle(
        Path(args.manifest), Path(args.image), Path(args.boot),
        Path(args.stage2), Path(args.kernel), key, public, Path(args.output),
    )
    parsed = parse_system_package(output.read_bytes(), public)
    print(f"ZSYS v1 criado: {output}")
    print(f"release={parsed.release_id} tag={parsed.release_tag}")
    print(f"image_size={parsed.image_size}")
    print(f"image_sha256={parsed.image_sha256.hex()}")
    print("Integridade: OK")


def command_system_verify(args: argparse.Namespace) -> None:
    """Valida um ZSYS sem instalar ou escrever qualquer imagem."""
    trusted = load_public_json(Path(args.public))
    version, epoch = current_system_version()
    if args.system_version is not None:
        version = Version.parse(args.system_version)
    if args.system_epoch is not None:
        epoch = args.system_epoch
    try:
        data = Path(args.package).read_bytes()
    except OSError as error:
        raise UpdateError(f"nao foi possivel ler {args.package}") from error
    package = parse_system_package(data, trusted, version, epoch)
    print("ZSYS v1 valido")
    print(f"  release={package.release_id} tag={package.release_tag}")
    print(f"  target={package.target_version} epoch={package.target_epoch}")
    print(f"  image_size={package.image_size}")
    print(f"  boot_abi={package.compatibility.boot_abi}")
    print(f"  schema={package.compatibility.data_schema_from}->{package.compatibility.data_schema_to}")
    print("  requires_reboot=true")
    print("  reason=NONE")


def command_release_v2_build(args: argparse.Namespace) -> None:
    """Combina namespaces legacy/runtime/system numa Release v2."""
    key = load_private_key(
        validate_private_input(Path(args.private)), prompt_password()
    )
    public = load_public_json(Path(args.public))
    descriptor = build_release_v2_bundle(
        args.release, args.release_name, Path(args.legacy_dir),
        Path(args.runtime_dir), Path(args.system_dir), key, public,
        args.source_commit, args.tag, Path(args.output_dir),
    )
    verify_release_v2_bundle(descriptor, public)
    print(f"Release v2 criada: {descriptor.parent}")
    print("Integridade: OK")
    print("Publique manualmente somente os assets deste diretorio.")


def command_release_v2_check(args: argparse.Namespace) -> None:
    """Confere uma Release v2 local antes da publicacao."""
    package = verify_release_v2_bundle(
        Path(args.release), load_public_json(Path(args.public))
    )
    print(f"Release v2: {package.release_id}")
    print(f"Versao do sistema: {package.target_version}")
    print(f"Imagem: {package.image_size} bytes")
    print("Integridade: OK")


def resign_system_package(data: bytes, private_key: Any) -> bytes:
    """Reassina somente para produzir uma fixture estruturalmente adulterada."""
    if len(data) < SYSTEM_HEADER_SIZE:
        raise UpdateError("ZSYS fixture menor que o cabecalho")
    unsigned = bytearray(data[:SYSTEM_HEADER_SIZE])
    unsigned[
        SYSTEM_HEADER_SIGNATURE_OFFSET :
        SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE
    ] = bytes(SYSTEM_SIGNATURE_SIZE)
    signature = private_key.sign(
        SYSTEM_DOMAIN + bytes(unsigned) + data[SYSTEM_PAYLOAD_OFFSET:]
    )
    return bytes(unsigned[:SYSTEM_HEADER_SIGNATURE_OFFSET]) + signature + bytes(
        unsigned[SYSTEM_HEADER_SIGNATURE_OFFSET + SYSTEM_SIGNATURE_SIZE :]
    ) + data[SYSTEM_PAYLOAD_OFFSET:]


def write_system_fixtures(
    private_key: Any,
    public: PublicKeyInfo,
    manifest_path: Path,
    image_path: Path,
    boot_path: Path,
    stage2_path: Path,
    kernel_path: Path,
    output_dir: Path,
) -> None:
    """Gera vetores ZSYS válidos, truncados, adulterados e incompatíveis."""
    output = output_dir.expanduser().resolve()
    if output.exists() and any(output.iterdir()):
        raise UpdateError(f"diretorio de fixtures ZSYS nao esta vazio: {output}")
    output.mkdir(parents=True, exist_ok=True)
    package = system_build_package(
        system_manifest_spec(manifest_path),
        system_load_file(image_path, "imagem"),
        system_load_file(boot_path, "boot"),
        system_load_file(stage2_path, "stage2"),
        system_load_file(kernel_path, "kernel"),
        private_key,
        public,
    )
    write_new_bytes(output / "valid.zsys", package)
    write_new_bytes(output / "truncated.zsys", package[:-1])

    header_tampered = bytearray(package)
    header_tampered[SYSTEM_HEADER_TARGET_VERSION_OFFSET] ^= 1
    write_new_bytes(output / "tampered-header.zsys", bytes(header_tampered))
    payload_tampered = bytearray(package)
    payload_tampered[SYSTEM_HEADER_SIZE + 7] ^= 1
    write_new_bytes(output / "tampered-payload.zsys", bytes(payload_tampered))
    signature_tampered = bytearray(package)
    signature_tampered[SYSTEM_HEADER_SIGNATURE_OFFSET] ^= 1
    write_new_bytes(output / "tampered-signature.zsys", bytes(signature_tampered))

    oversized = bytearray(package)
    struct.pack_into("<I", oversized, 24, SYSTEM_MAX_IMAGE_SIZE + 512)
    write_new_bytes(output / "oversized.zsys", bytes(oversized))
    misaligned = bytearray(package)
    struct.pack_into("<I", misaligned, 24, len(package) - SYSTEM_HEADER_SIZE - 1)
    write_new_bytes(output / "misaligned.zsys", bytes(misaligned))

    incompatible_version = bytearray(package)
    struct.pack_into(
        "<HHH", incompatible_version, SYSTEM_HEADER_TARGET_VERSION_OFFSET,
        0, 1, 0,
    )
    write_new_bytes(
        output / "incompatible-version.zsys",
        resign_system_package(bytes(incompatible_version), private_key),
    )
    incompatible_epoch = bytearray(package)
    struct.pack_into(
        "<I", incompatible_epoch, SYSTEM_HEADER_BASES_OFFSET + 6, 1
    )
    write_new_bytes(
        output / "incompatible-epoch.zsys",
        resign_system_package(bytes(incompatible_epoch), private_key),
    )

    incompatible = bytearray(package)
    struct.pack_into("<I", incompatible, SYSTEM_HEADER_BOOT_ABI_OFFSET, 2)
    write_new_bytes(
        output / "incompatible-abi.zsys",
        resign_system_package(bytes(incompatible), private_key),
    )
    schema_incompatible = bytearray(package)
    struct.pack_into(
        "<I", schema_incompatible, SYSTEM_HEADER_SCHEMA_FROM_OFFSET, 2
    )
    write_new_bytes(
        output / "incompatible-schema.zsys",
        resign_system_package(bytes(schema_incompatible), private_key),
    )
    image_hash_divergent = bytearray(package)
    image_hash_divergent[SYSTEM_HEADER_IMAGE_HASH_OFFSET] ^= 1
    write_new_bytes(
        output / "hash-divergent-image.zsys",
        resign_system_package(bytes(image_hash_divergent), private_key),
    )
    component_hash_divergent = bytearray(package)
    component_hash_divergent[
        SYSTEM_HEADER_COMPONENTS_OFFSET + SYSTEM_COMPONENT_HASH_OFFSET
    ] ^= 1
    write_new_bytes(
        output / "hash-divergent-component.zsys",
        resign_system_package(bytes(component_hash_divergent), private_key),
    )

    metadata = {
        "format": "zephyros-system-fixtures-v1",
        "valid": "valid.zsys",
        "invalid": [
            path.name
            for path in sorted(output.iterdir())
            if path.name != "valid.zsys"
        ],
    }
    write_new_text(output / "fixtures.json", json.dumps(metadata, indent=2) + "\n")


def command_fixtures_system(args: argparse.Namespace) -> None:
    """Gera fixtures EP9.0A para verificacao offline."""
    key = load_private_key(
        validate_private_input(Path(args.private)), prompt_password()
    )
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_system_fixtures(
        key,
        public,
        Path(args.manifest),
        Path(args.image),
        Path(args.boot),
        Path(args.stage2),
        Path(args.kernel),
        Path(args.output_dir),
    )
    print(f"Fixtures ZSYS criadas: {Path(args.output_dir).resolve()}")


def write_runtime_fixtures(
    private_key: Any,
    public: PublicKeyInfo,
    output_dir: Path,
    changed_assets: bool = False,
) -> None:
    """Gera fixtures v2 validas e adulteradas para EP6.3."""
    output = output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise UpdateError(f"diretorio de fixtures runtime nao esta vazio: {output}")
    output.mkdir(parents=True, exist_ok=True)
    input_dir = output / "input"
    input_dir.mkdir()
    for name, source in UPDATE_ASSETS.items():
        data = source.read_bytes()
        if changed_assets:
            data = invert_bmp_pixels(data)
        write_new_bytes(input_dir / name, data)
    manifest_path = input_dir / "runtime.json"
    manifest_spec = {
        "format": "ZUM2 v2",
        "generation": 63,
        "release_tag": "ep63-runtime",
        "release_id": "ep63-runtime",
        "target_version": "0.1.1",
        "target_epoch": 0,
        "base_versions": [
            {"version": "0.1.0", "epoch": 0},
            {"version": "0.0.9", "epoch": 0},
        ],
        "files": [
            {"path": name, "operation": "replace_or_create", "source": name}
            for name in sorted(RUNTIME_CATALOG)
        ],
    }
    write_new_text(
        manifest_path,
        json.dumps(manifest_spec, indent=2, ensure_ascii=True) + "\n",
    )
    valid_dir = output / "valid"
    build_runtime_bundle(manifest_path, private_key, public, valid_dir)
    manifest = (valid_dir / "runtime.zum2").read_bytes()
    package = (valid_dir / "runtime.zephyrosupd").read_bytes()
    tampered_manifest = bytearray(manifest)
    tampered_manifest[16] ^= 1
    write_new_bytes(output / "tampered.zum2", bytes(tampered_manifest))
    tampered_package = bytearray(package)
    tampered_package[-RUNTIME_SIGNATURE_SIZE - 1] ^= 1
    write_new_bytes(output / "tampered.zephyrosupd", bytes(tampered_package))
    missing_descriptor = json.loads((valid_dir / "release.json").read_text())
    missing_descriptor["runtime"]["assets"].pop()
    write_new_text(
        output / "missing-asset.json",
        json.dumps(missing_descriptor, indent=2, ensure_ascii=True) + "\n",
    )
    published = {
        "format": "zephyros-runtime-fixtures-v2",
        "release_public_key": {
            "public_key_hex": public.public_key.hex(),
            "key_id_hex": public.key_id.hex(),
        },
        "valid": {
            name: {
                "size": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for name, path in (
                ("runtime.zum2", valid_dir / "runtime.zum2"),
                ("runtime.zephyrosupd", valid_dir / "runtime.zephyrosupd"),
                ("release.json", valid_dir / "release.json"),
            )
        },
        "invalid": {
            name: {
                "size": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for name, path in (
                ("tampered.zum2", output / "tampered.zum2"),
                ("tampered.zephyrosupd", output / "tampered.zephyrosupd"),
                ("missing-asset.json", output / "missing-asset.json"),
            )
        },
    }
    write_new_text(
        output / "fixtures.json",
        json.dumps(published, indent=2, ensure_ascii=True) + "\n",
    )


def command_fixtures_runtime(args: argparse.Namespace) -> None:
    """Gera a matriz EP6.3 fora do repositorio de codigo."""
    key = load_private_key(Path(args.private), prompt_password())
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_runtime_fixtures(
        key, public, Path(args.output_dir), args.changed_assets
    )
    print(f"Fixtures runtime v2 criadas em {Path(args.output_dir).resolve()}")
    print("Somente artefatos publicos foram gravados no diretorio indicado.")


def runtime_server_files(root: Path, variant: str) -> dict[str, bytes]:
    """Carrega os assets runtime publicados por um servidor de fixtures."""
    valid = root / "valid" if (root / "valid").is_dir() else root
    required = {
        "runtime.zum2": valid / "runtime.zum2",
        "runtime.zephyrosupd": valid / "runtime.zephyrosupd",
        "release.json": valid / "release.json",
    }
    optional_assets = {
        name: valid / name for name in RUNTIME_CATALOG if (valid / name).is_file()
    }
    missing = [path for path in required.values() if not path.is_file()]
    if missing:
        raise UpdateError(f"fixture runtime ausente: {missing[0]}")
    files = {
        name: path.read_bytes() for name, path in {**required, **optional_assets}.items()
    }
    if variant == "tampered-manifest":
        data = bytearray(files["runtime.zum2"])
        data[16] ^= 1
        files["runtime.zum2"] = bytes(data)
    elif variant == "tampered-package":
        data = bytearray(files["runtime.zephyrosupd"])
        data[-RUNTIME_SIGNATURE_SIZE - 1] ^= 1
        files["runtime.zephyrosupd"] = bytes(data)
    elif variant == "missing-asset":
        descriptor = json.loads(files["release.json"].decode("utf-8"))
        descriptor["runtime"]["assets"].pop()
        files["release.json"] = (
            json.dumps(descriptor, indent=2, ensure_ascii=True) + "\n"
        ).encode("utf-8")
    elif variant == "tag-divergent":
        descriptor = json.loads(files["release.json"].decode("utf-8"))
        descriptor["tag"] = f"{descriptor['tag']}-other"
        files["release.json"] = (
            json.dumps(descriptor, indent=2, ensure_ascii=True) + "\n"
        ).encode("utf-8")
    elif variant == "invalid-json":
        files["release.json"] = b'{"format":"zephyros-runtime-release-v2"'
    elif variant == "bad-digest":
        descriptor = json.loads(files["release.json"].decode("utf-8"))
        descriptor["runtime"]["zum2"]["sha256"] = "0" * 64
        files["release.json"] = (
            json.dumps(descriptor, indent=2, ensure_ascii=True) + "\n"
        ).encode("utf-8")
    return files


def command_serve_runtime(args: argparse.Namespace) -> None:
    """Serve a Release HTTP U5 do runtime v2 para o QEMU."""
    files = runtime_server_files(Path(args.root).resolve(), args.variant)
    tag = args.tag
    descriptor_name = f"runtime-{tag}.json"
    routes = {
        "/zephyros/runtime.zum2": "runtime.zum2",
        "/zephyros/runtime.zephyrosupd": "runtime.zephyrosupd",
        f"/zephyros/{descriptor_name}": "release.json",
        **{
            f"/zephyros/{name}": name
            for name in RUNTIME_CATALOG
            if name in files
        },
    }

    class RuntimeHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == "/zephyros/error-runtime":
                self.send_error(500, "fixture runtime HTTP")
                return
            name = routes.get(self.path)
            if name is None:
                self.send_error(404, "fixture runtime inexistente")
                return
            data = files[name]
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, format: str, *values: object) -> None:
            print(f"Runtime HTTP {self.address_string()} - {format % values}")

    server = http.server.ThreadingHTTPServer((args.bind, args.port), RuntimeHandler)
    print(f"Servidor runtime U5 em http://{args.bind}:{args.port}/zephyros/runtime.zum2")
    print(f"Descriptor por tag: /zephyros/{descriptor_name}")
    print(f"Variante da fixture: {args.variant}")
    print("Pressione Ctrl+C para encerrar.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def command_serve_github_runtime(args: argparse.Namespace) -> None:
    """Serve Release API e assets runtime v2 por HTTPS com TLS externo."""
    files = runtime_server_files(Path(args.root).resolve(), args.variant)
    cert = Path(args.cert).resolve()
    key = Path(args.key).resolve()
    if not cert.is_file() or not key.is_file():
        raise UpdateError("certificado ou chave TLS runtime ausente")
    api_path = f"/repos/{args.owner}/{args.repository}/releases/tags/{args.tag}"
    asset_prefix = f"/releases/download/{args.tag}"
    public_base = f"https://{args.public_host}:{args.port}"
    names = (
        "runtime.zum2",
        "runtime.zephyrosupd",
        *(name for name in RUNTIME_CATALOG if name in files),
    )

    def api_payload() -> dict[str, Any]:
        assets = [
            {
                "name": name,
                "size": len(files[name]),
                "browser_download_url": f"{public_base}{asset_prefix}/{name}",
                "state": "uploaded",
                "digest": f"sha256:{hashlib.sha256(files[name]).hexdigest()}",
            }
            for name in names
        ]
        payload: dict[str, Any] = {
            "id": 63063,
            "tag_name": args.tag,
            "name": "EP6.3 Runtime fixture",
            "draft": False,
            "prerelease": False,
            "published_at": "2026-08-22T12:00:00Z",
            "assets": assets,
        }
        if args.variant == "missing-asset":
            payload["assets"] = assets[:-1]
        elif args.variant == "tag-divergent":
            payload["tag_name"] = f"{args.tag}-other"
        elif args.variant == "draft":
            payload["draft"] = True
        elif args.variant == "prerelease":
            payload["prerelease"] = True
        elif args.variant == "bad-digest":
            payload["assets"][0]["digest"] = f"sha256:{'0' * 64}"
        return payload

    class RuntimeGitHubHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == api_path:
                if args.variant == "invalid-json":
                    data = b'{"tag_name":'
                else:
                    data = json.dumps(api_payload(), separators=(",", ":")).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/vnd.github+json")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data)
                return
            for name in names:
                if self.path == f"{asset_prefix}/{name}":
                    data = files[name]
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(data)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(data)
                    return
            self.send_error(404, "fixture GitHub runtime inexistente")

        def log_message(self, format: str, *values: object) -> None:
            print(f"GitHub Runtime HTTPS {self.address_string()} - {format % values}")

    server = http.server.ThreadingHTTPServer((args.bind, args.port), RuntimeGitHubHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    try:
        context.load_cert_chain(certfile=cert, keyfile=key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    except (OSError, ssl.SSLError) as error:
        server.server_close()
        raise UpdateError("certificado ou chave TLS runtime invalido") from error
    print(f"Servidor GitHub runtime HTTPS em {public_base}{api_path}")
    print(f"Variante da API: {args.variant}")
    print("Pressione Ctrl+C para encerrar.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def command_fixtures(args: argparse.Namespace) -> None:
    """Gera a matriz U2 usando a chave privada mantida pelo usuario."""
    key = load_private_key(Path(args.private), prompt_password())
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_fixtures(key, Path(args.output_dir))
    print(f"Fixtures U2 criadas em {Path(args.output_dir).resolve()}")


def command_fixtures_u3(args: argparse.Namespace) -> None:
    """Gera APPLY.ZUP e os BMPs publicos usando a chave externa."""
    key = load_private_key(Path(args.private), prompt_password())
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_u3_fixtures(key, Path(args.output_dir))
    print(f"Fixtures U3 criadas em {Path(args.output_dir).resolve()}")
    print("Somente artefatos publicos foram gravados no repositorio.")


def command_fixtures_u5(args: argparse.Namespace) -> None:
    """Gera os manifestos assinados e adulterados da U5."""
    key = load_private_key(Path(args.private), prompt_password())
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_u5_fixtures(key, Path(args.output_dir))
    print(f"Fixtures U5 criadas em {Path(args.output_dir).resolve()}")
    print("Somente manifestos e hashes publicos foram gravados.")


def command_serve_u5(args: argparse.Namespace) -> None:
    """Serve fixtures U5 e pacotes conhecidos para o QEMU SLIRP."""
    root = Path(args.root).resolve()
    routes = {
        "/zephyros/stable.zum": root / "stable.zum",
        "/zephyros/stable2.zum": root / "stable2.zum",
        "/zephyros/tampered.zum": root / "tampered.zum",
        "/zephyros/badpkg.zum": root / "badpkg.zum",
        "/zephyros/truncated.zum": root / "truncated.zum",
        "/zephyros/APPLY.ZUP": U3_FIXTURES / "APPLY.ZUP",
        "/zephyros/BADHASH.ZUP": U2_FIXTURES / "BADHASH.ZUP",
    }
    for name in (
        "ep6-stable.json",
        "ep6-alt.json",
        "ep6-invalid-json.json",
        "ep6-no-tag.json",
        "ep6-missing-asset.json",
        "ep6-tampered-manifest.json",
        "ep6-invalid-package.json",
        "ep6-divergent-hash.json",
        "ep6-divergent-lock.json",
        "ep6-divergent-tag.json",
    ):
        routes[f"/zephyros/{name}"] = root / name
    missing = [path for path in routes.values() if not path.is_file()]
    if missing:
        raise UpdateError(f"fixture U5 ausente: {missing[0]}")

    class U5Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == "/zephyros/error.zum":
                self.send_error(500, "fixture HTTP")
                return
            if self.path == "/zephyros/slow.zum":
                time.sleep(55)
                target = routes["/zephyros/stable.zum"]
            else:
                target = routes.get(self.path)
            if target is None:
                self.send_error(404, "fixture inexistente")
                return
            data = target.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, format: str, *values: object) -> None:
            print(f"U5 {self.address_string()} - {format % values}")

    server = http.server.ThreadingHTTPServer(
        (args.bind, args.port), U5Handler
    )
    print(
        f"Servidor U5 em http://{args.bind}:{args.port}/zephyros/stable.zum"
    )
    print("Fixtures EP6: ep6-stable.json e ep6-alt.json (tag exata).")
    print("Pressione Ctrl+C para encerrar.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def command_audit_image(args: argparse.Namespace) -> None:
    """Executa a auditoria offline da imagem FAT12 apos encerrar o QEMU."""
    expected_version = (
        Version.parse(args.expect_version)
        if args.expect_version is not None
        else None
    )
    audit_image(
        Path(args.image),
        expected_version,
        args.expect_rollback,
        args.allow_pending,
        args.expect_history_count,
        args.expect_last_event,
        args.expect_remote_cache,
        args.expect_remote_alias,
        args.expect_remote_pending,
        args.expect_runtime_cache,
        args.expect_runtime_alias,
        args.expect_runtime_pending,
    )


def build_parser() -> argparse.ArgumentParser:
    """Monta a interface CLI publica da ferramenta."""
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    keygen = subparsers.add_parser("keygen", help="gera a chave de release")
    keygen.add_argument("--private", required=True)
    keygen.add_argument("--public", required=True)
    keygen.set_defaults(handler=command_keygen)

    build = subparsers.add_parser("build", help="constroi e assina um ZUPD")
    build.add_argument("--manifest", required=True)
    build.add_argument("--private", required=True)
    build.add_argument("--output", required=True)
    build.set_defaults(handler=command_build)

    verify = subparsers.add_parser("verify", help="verifica um ZUPD")
    verify.add_argument("--artifact", required=True)
    verify.add_argument("--public", required=True)
    verify.add_argument("--system-version")
    verify.add_argument("--system-epoch", type=int)
    verify.set_defaults(handler=command_verify)

    runtime_build = subparsers.add_parser(
        "runtime-build", help="constroi ZUM2 e o pacote completo runtime v2"
    )
    runtime_build.add_argument("--manifest", required=True)
    runtime_build.add_argument("--private", required=True)
    runtime_build.add_argument("--public", required=True)
    runtime_build.add_argument("--output-dir", required=True)
    runtime_build.set_defaults(handler=command_runtime_build)

    runtime_verify = subparsers.add_parser(
        "runtime-verify", help="verifica manifesto e pacote runtime v2"
    )
    runtime_verify.add_argument("--manifest", required=True)
    runtime_verify.add_argument("--package", required=True)
    runtime_verify.add_argument("--public", required=True)
    runtime_verify.add_argument("--system-version")
    runtime_verify.add_argument("--system-epoch", type=int)
    runtime_verify.set_defaults(handler=command_runtime_verify)

    system_build = subparsers.add_parser(
        "system-build", help="constroi um envelope ZSYS v1 assinado"
    )
    system_build.add_argument("--manifest", required=True)
    system_build.add_argument("--image", required=True)
    system_build.add_argument("--boot", required=True)
    system_build.add_argument("--stage2", required=True)
    system_build.add_argument("--kernel", required=True)
    system_build.add_argument("--private", required=True)
    system_build.add_argument("--public", required=True)
    system_build.add_argument("--output", required=True)
    system_build.set_defaults(handler=command_system_build)

    system_verify = subparsers.add_parser(
        "system-verify", help="verifica um envelope ZSYS v1"
    )
    system_verify.add_argument("--package", required=True)
    system_verify.add_argument("--public", required=True)
    system_verify.add_argument("--system-version")
    system_verify.add_argument("--system-epoch", type=int)
    system_verify.set_defaults(handler=command_system_verify)

    release_build = subparsers.add_parser(
        "release-build", help="gera os assets locais de uma Release EP5"
    )
    release_build.add_argument("--release", required=True)
    release_build.add_argument("--manifest", required=True)
    release_build.add_argument("--private", required=True)
    release_build.add_argument("--public", required=True)
    release_build.add_argument("--generation", type=int, required=True)
    release_build.add_argument("--source-commit", required=True)
    release_build.add_argument("--tag")
    release_build.add_argument("--output-dir", required=True)
    release_build.set_defaults(handler=command_release_build)

    release_check = subparsers.add_parser(
        "release-check", help="confere uma Release EP5 antes da publicacao"
    )
    release_check.add_argument("--release", required=True)
    release_check.add_argument("--public", required=True)
    release_check.set_defaults(handler=command_release_check)

    release_v2_build = subparsers.add_parser(
        "release-v2-build", help="gera uma Release combinada v2"
    )
    release_v2_build.add_argument("--release", required=True)
    release_v2_build.add_argument("--release-name", required=True)
    release_v2_build.add_argument("--legacy-dir", required=True)
    release_v2_build.add_argument("--runtime-dir", required=True)
    release_v2_build.add_argument("--system-dir", required=True)
    release_v2_build.add_argument("--private", required=True)
    release_v2_build.add_argument("--public", required=True)
    release_v2_build.add_argument("--source-commit", required=True)
    release_v2_build.add_argument("--tag", required=True)
    release_v2_build.add_argument("--output-dir", required=True)
    release_v2_build.set_defaults(handler=command_release_v2_build)

    release_v2_check = subparsers.add_parser(
        "release-v2-check", help="confere uma Release combinada v2"
    )
    release_v2_check.add_argument("--release", required=True)
    release_v2_check.add_argument("--public", required=True)
    release_v2_check.set_defaults(handler=command_release_v2_check)

    fixtures = subparsers.add_parser("fixtures", help="gera os vetores U2")
    fixtures.add_argument("--private", required=True)
    fixtures.add_argument("--public", required=True)
    fixtures.add_argument("--output-dir", required=True)
    fixtures.set_defaults(handler=command_fixtures)

    fixtures_u3 = subparsers.add_parser(
        "fixtures-u3", help="gera APPLY.ZUP e payloads da U3"
    )
    fixtures_u3.add_argument("--private", required=True)
    fixtures_u3.add_argument("--public", required=True)
    fixtures_u3.add_argument("--output-dir", required=True)
    fixtures_u3.set_defaults(handler=command_fixtures_u3)

    fixtures_u5 = subparsers.add_parser(
        "fixtures-u5", help="gera manifestos remotos da U5"
    )
    fixtures_u5.add_argument("--private", required=True)
    fixtures_u5.add_argument("--public", required=True)
    fixtures_u5.add_argument("--output-dir", required=True)
    fixtures_u5.set_defaults(handler=command_fixtures_u5)

    fixtures_runtime = subparsers.add_parser(
        "fixtures-runtime", help="gera fixtures EP6.3 do runtime v2"
    )
    fixtures_runtime.add_argument("--private", required=True)
    fixtures_runtime.add_argument("--public", required=True)
    fixtures_runtime.add_argument("--output-dir", required=True)
    fixtures_runtime.add_argument(
        "--changed-assets",
        action="store_true",
        help="gera payloads BMP diferentes dos arquivos da imagem base",
    )
    fixtures_runtime.set_defaults(handler=command_fixtures_runtime)

    fixtures_system = subparsers.add_parser(
        "fixtures-system", help="gera fixtures de verificacao ZSYS v1"
    )
    fixtures_system.add_argument("--manifest", required=True)
    fixtures_system.add_argument("--image", required=True)
    fixtures_system.add_argument("--boot", required=True)
    fixtures_system.add_argument("--stage2", required=True)
    fixtures_system.add_argument("--kernel", required=True)
    fixtures_system.add_argument("--private", required=True)
    fixtures_system.add_argument("--public", required=True)
    fixtures_system.add_argument("--output-dir", required=True)
    fixtures_system.set_defaults(handler=command_fixtures_system)

    serve_runtime = subparsers.add_parser(
        "serve-runtime", help="serve fixtures HTTP do runtime v2"
    )
    serve_runtime.add_argument("--root", required=True)
    serve_runtime.add_argument("--tag", default="ep63-runtime")
    serve_runtime.add_argument("--bind", default="0.0.0.0")
    serve_runtime.add_argument("--port", type=int, default=8000)
    serve_runtime.add_argument(
        "--variant",
        choices=(
            "valid",
            "missing-asset",
            "tag-divergent",
            "invalid-json",
            "bad-digest",
            "tampered-manifest",
            "tampered-package",
        ),
        default="valid",
    )
    serve_runtime.set_defaults(handler=command_serve_runtime)

    serve_github_runtime = subparsers.add_parser(
        "serve-github-runtime", help="serve API/HTTPS GitHub do runtime v2"
    )
    serve_github_runtime.add_argument("--root", required=True)
    serve_github_runtime.add_argument("--cert", required=True)
    serve_github_runtime.add_argument("--key", required=True)
    serve_github_runtime.add_argument("--tag", required=True)
    serve_github_runtime.add_argument("--bind", default="0.0.0.0")
    serve_github_runtime.add_argument("--public-host", default="10.0.2.2")
    serve_github_runtime.add_argument("--port", type=int, default=8443)
    serve_github_runtime.add_argument("--owner", default="BadTonho")
    serve_github_runtime.add_argument("--repository", default="ZephyrOS")
    serve_github_runtime.add_argument(
        "--variant",
        choices=(
            "valid",
            "missing-asset",
            "tag-divergent",
            "invalid-json",
            "bad-digest",
            "draft",
            "prerelease",
        ),
        default="valid",
    )
    serve_github_runtime.set_defaults(handler=command_serve_github_runtime)

    fixtures_github = subparsers.add_parser(
        "fixtures-github", help="gera assets para o servidor HTTPS GitHub"
    )
    fixtures_github.add_argument("--private", required=True)
    fixtures_github.add_argument("--public", required=True)
    fixtures_github.add_argument("--tag", required=True)
    fixtures_github.add_argument("--output-dir", required=True)
    fixtures_github.set_defaults(handler=command_fixtures_github)

    serve_u5 = subparsers.add_parser(
        "serve-u5", help="serve fixtures U5 para o QEMU"
    )
    serve_u5.add_argument("--root", required=True)
    serve_u5.add_argument("--bind", default="0.0.0.0")
    serve_u5.add_argument("--port", type=int, default=8000)
    serve_u5.set_defaults(handler=command_serve_u5)

    serve_github = subparsers.add_parser(
        "serve-github", help="serve API e assets GitHub por HTTPS"
    )
    serve_github.add_argument("--root", required=True)
    serve_github.add_argument("--cert", required=True)
    serve_github.add_argument("--key", required=True)
    serve_github.add_argument("--tag", required=True)
    serve_github.add_argument("--bind", default="0.0.0.0")
    serve_github.add_argument("--public-host", default="10.0.2.2")
    serve_github.add_argument("--port", type=int, default=8443)
    serve_github.add_argument("--owner", default="BadTonho")
    serve_github.add_argument("--repository", default="ZephyrOS")
    serve_github.add_argument(
        "--variant",
        choices=(
            "valid",
            "missing-asset",
            "tag-divergent",
            "invalid-json",
            "bad-digest",
            "draft",
            "prerelease",
        ),
        default="valid",
    )
    serve_github.set_defaults(handler=command_serve_github)

    audit = subparsers.add_parser(
        "audit-image", help="audita estado U3/U4 em uma imagem FAT12"
    )
    audit.add_argument("--image", required=True)
    audit.add_argument("--expect-version")
    audit.add_argument(
        "--expect-rollback",
        choices=("any", "available", "unavailable"),
        default="any",
    )
    audit.add_argument("--allow-pending", action="store_true")
    audit.add_argument("--expect-history-count", type=int)
    audit.add_argument(
        "--expect-last-event",
        choices=tuple(UPDATE_HISTORY_OPERATION_NAMES.values()),
    )
    audit.add_argument(
        "--expect-remote-cache",
        choices=("any", "empty", "valid"),
        default="any",
    )
    audit.add_argument("--expect-remote-alias")
    audit.add_argument(
        "--expect-remote-pending",
        choices=("any", "clean", "pending"),
        default="clean",
    )
    audit.add_argument(
        "--expect-runtime-cache",
        choices=("any", "empty", "valid"),
        default="any",
    )
    audit.add_argument("--expect-runtime-alias")
    audit.add_argument(
        "--expect-runtime-pending",
        choices=("any", "clean", "pending"),
        default="clean",
    )
    audit.set_defaults(handler=command_audit_image)

    sync = subparsers.add_parser("sync-trust", help="gera o header publico")
    sync.add_argument("--public", required=True)
    sync.add_argument("--output", required=True)
    sync.set_defaults(
        handler=lambda args: sync_trust(Path(args.public), Path(args.output))
    )

    check = subparsers.add_parser("check-trust", help="confere JSON e header")
    check.add_argument("--public", required=True)
    check.add_argument("--header", required=True)
    check.set_defaults(
        handler=lambda args: check_trust(Path(args.public), Path(args.header))
    )

    sync_remote = subparsers.add_parser(
        "sync-remote", help="gera o header do canal remoto"
    )
    sync_remote.add_argument("--config", required=True)
    sync_remote.add_argument("--output", required=True)
    sync_remote.set_defaults(
        handler=lambda args: sync_remote_config(
            Path(args.config), Path(args.output)
        )
    )

    check_remote = subparsers.add_parser(
        "check-remote", help="confere JSON e header do canal"
    )
    check_remote.add_argument("--config", required=True)
    check_remote.add_argument("--header", required=True)
    check_remote.set_defaults(
        handler=lambda args: check_remote_config(
            Path(args.config), Path(args.header)
        )
    )

    selftest = subparsers.add_parser("selftest", help="executa testes host")
    selftest.set_defaults(handler=lambda _args: run_selftest())
    return parser


def main() -> int:
    """Executa um comando e converte falhas para codigos previsiveis."""
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.handler(args)
        return 0
    except Rejection:
        return 1
    except UpdateError as error:
        print(f"Erro: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
