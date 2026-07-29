#!/usr/bin/env python3
"""Empacotador e verificador host para o contrato ZUPD v1."""

from __future__ import annotations

import argparse
import getpass
import hashlib
import json
import re
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = REPO_ROOT / "docs" / "14-atualizacoes" / "contrato-zupd-v1.md"
U1_FIXTURES = REPO_ROOT / "docs" / "fixtures" / "updates" / "v1"
U2_FIXTURES = REPO_ROOT / "docs" / "fixtures" / "updates" / "u2"
RELEASE_PUBLIC = REPO_ROOT / "config" / "update-release-public.json"
VERSION_HEADER = REPO_ROOT / "src" / "include" / "core" / "version.h"
SHELL_BMP = REPO_ROOT / "assets" / "icons" / "SHELL.BMP"

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
FAT_PATH_RE = re.compile(r"^[A-Z0-9_]{1,8}\.[A-Z0-9]{1,3}$")
DEFINE_RE = re.compile(r"^#define\s+(ZEPHYROS_VERSION_[A-Z]+)\s+([0-9]+)U?\s*$")

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


def run_selftest() -> None:
    """Executa a suite host sem gravar no repositorio."""
    selftest_u1()
    selftest_u2_public()
    selftest_generated()
    print("Updater selftest: OK")


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


def command_fixtures(args: argparse.Namespace) -> None:
    """Gera a matriz U2 usando a chave privada mantida pelo usuario."""
    key = load_private_key(Path(args.private), prompt_password())
    public = load_public_json(Path(args.public))
    if private_public_info(key) != public:
        raise UpdateError("chave privada nao corresponde ao JSON publico")
    write_fixtures(key, Path(args.output_dir))
    print(f"Fixtures U2 criadas em {Path(args.output_dir).resolve()}")


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

    fixtures = subparsers.add_parser("fixtures", help="gera os vetores U2")
    fixtures.add_argument("--private", required=True)
    fixtures.add_argument("--public", required=True)
    fixtures.add_argument("--output-dir", required=True)
    fixtures.set_defaults(handler=command_fixtures)

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
