#!/usr/bin/env python3
"""Empacotador host para o formato local .zephyrosapp do ZephyrOS."""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.server
import json
import re
import socketserver
import struct
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PACKAGE_MAGIC = b"ZPKG"
PACKAGE_VERSION = 1
PACKAGE_ARCH_I386 = 1
PACKAGE_HEADER = struct.Struct("<4sHHIIIIII")
PACKAGE_HEADER_SIZE = PACKAGE_HEADER.size
PACKAGE_MAX_MANIFEST = 512
ZAPP_MAGIC = b"ZAPP"
ZAPP_VERSION = 1
ZAPP_ARCH_I386 = 1
ZAPP_STACK_SIZE = 4096
ZAPP_MAX_CODE = 4096
ZAPP_MAX_DATA = 4096
ZAPP_HEADER = struct.Struct("<4sIIIIIIIIII")
ZAPP_MAX_SIZE = ZAPP_HEADER.size + ZAPP_MAX_CODE + ZAPP_MAX_DATA
FAT12_EOF = 0xFFF
FAT12_FREE = 0x000
FAT12_ATTR_ARCHIVE = 0x20
FAT32_EOF = 0x0FFFFFFF
FAT32_FREE = 0x00000000
FAT32_MIN_CLUSTERS = 4086
FAT32_STANDARD_MIN_CLUSTERS = 65525
FAT32_ATTR_DIRECTORY = 0x10
FAT32_ATTR_ARCHIVE = 0x20
FAT32_ATTR_LFN = 0x0F
FAT32_DIR_ENTRY_SIZE = 32
FAT32_SECTOR_SIZE = 512
HYBRID_DISK_BYTES = 256 * 1024 * 1024
HYBRID_FAT32_START_LBA = 4096
HYBRID_FAT32_LABEL = "ZEPHYROS"
ID_RE = re.compile(r"^[A-Z0-9_]{1,8}$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
REQUIRED_MANIFEST_KEYS = ("id", "name", "version", "api", "entry", "dependencies")
SUPPORTED_APP_APIS = ("0.3", "0.4", "0.5")
CURRENT_APP_API = "0.5"
STORE_FIXTURE_FORMAT = "zephyros-app-store-fixtures-v1"
STORE_FIXTURE_ALIASES = (
    "VALID.ZPK",
    "BADCRC.ZPK",
    "BADAPI.ZPK",
    "BADALIAS.ZPK",
    "NEEDSDEP.ZPK",
    "SAMEVER.ZPK",
)
STORE_AS2_FIXTURE_FORMAT = "zephyros-app-store-as2-fixtures-v1"
STORE_AS2_FIXTURE_ALIASES = (
    "WAITAPP.ZPK",
    "BASE.ZPK",
    "DEPEND.ZPK",
)
STORE_AS4_FIXTURE_FORMAT = "zephyros-app-store-as4-fixtures-v1"
STORE_AS4_FIXTURE_ALIASES = (
    "UPTARGET.ZPK",
    "UPDEPA.ZPK",
    "UPDEPB.ZPK",
    "BROKEN.ZPK",
    "CYCLEA.ZPK",
    "CYCLEB.ZPK",
)
STORE_AS5_FIXTURE_FORMAT = "zephyros-app-store-as5-fixtures-v1"
STORE_AS5_PUBLIC_FORMAT = "zephyros-app-store-ed25519-public-v1"
STORE_AS5_DOMAIN = b"ZEPHYROS-APP-CATALOG-V1\0"
STORE_AS5_HEADER_SIZE = 128
STORE_AS5_ENTRY_SIZE = 256
STORE_AS5_SIGNATURE_SIZE = 64
STORE_AS5_MAX_ENTRIES = 16
STORE_AS5_CHANNEL = 1
STORE_AS5_HASH_SHA256 = 1
STORE_AS5_SIGNATURE_ED25519 = 1
STORE_AS5_PACKAGE_LIMIT = 16384
STORE_AS5_PROFILES = ("seed", "update")
STORE_AS5_IDS = ("RMDEPA", "RMDEPB", "RMTARGET")


class PackageError(ValueError):
    """Erro controlado para entrada, pacote ou imagem FAT invalidos."""


@dataclass(frozen=True)
class PackageInfo:
    """Conteudo validado de um pacote ZPKG."""

    manifest: dict[str, str]
    payload: bytes


def ensure_ascii(value: str, label: str, limit: int) -> str:
    """Valida texto ASCII curto usado pelo manifesto interno."""
    if not isinstance(value, str) or not value or len(value) > limit:
        raise PackageError(f"{label} invalido")
    try:
        value.encode("ascii")
    except UnicodeEncodeError as error:
        raise PackageError(f"{label} deve usar ASCII") from error
    if any(ord(char) < 0x20 or char in "=\r\n" for char in value):
        raise PackageError(f"{label} contem caractere nao permitido")
    return value


def validate_id(value: str, label: str = "id") -> str:
    """Valida identificador compativel com diretorio e alias FAT 8.3."""
    if not isinstance(value, str) or not ID_RE.fullmatch(value):
        raise PackageError(f"{label} deve ter 1-8 caracteres A-Z, 0-9 ou _")
    return value


def validate_dependencies(value: object, package_id: str) -> list[str]:
    """Valida a lista curta de dependencias sem resolver pacotes."""
    if value is None:
        return []
    if not isinstance(value, list) or len(value) > 4:
        raise PackageError("dependencies deve conter no maximo quatro IDs")
    dependencies = [validate_id(item, "dependencia") for item in value]
    if package_id in dependencies or len(set(dependencies)) != len(dependencies):
        raise PackageError("dependencies possui ID duplicado ou auto-dependencia")
    return dependencies


def manifest_from_json(path: Path) -> dict[str, str]:
    """Le o manifesto JSON do host e converte para o contrato do pacote."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError(f"nao foi possivel ler {path}") from error
    if not isinstance(data, dict):
        raise PackageError("app.json deve conter um objeto")
    package_id = validate_id(data.get("id"))
    name = ensure_ascii(data.get("name"), "name", 31)
    version = ensure_ascii(data.get("version"), "version", 15)
    if not VERSION_RE.fullmatch(version):
        raise PackageError("version deve usar MAJOR.MINOR.PATCH")
    api = ensure_ascii(data.get("api", CURRENT_APP_API), "api", 7)
    if api not in SUPPORTED_APP_APIS:
        raise PackageError("api deve ser 0.3, 0.4 ou 0.5")
    dependencies = validate_dependencies(data.get("dependencies", []), package_id)
    return {
        "id": package_id,
        "name": name,
        "version": version,
        "api": api,
        "entry": "APP.ZAP",
        "dependencies": ",".join(dependencies),
    }


def encode_manifest(manifest: dict[str, str]) -> bytes:
    """Serializa o manifesto chave=valor para o payload legivel pelo kernel."""
    if tuple(manifest.keys()) != REQUIRED_MANIFEST_KEYS:
        raise PackageError("campos do manifesto invalidos")
    text = "".join(f"{key}={manifest[key]}\n" for key in REQUIRED_MANIFEST_KEYS)
    encoded = text.encode("ascii")
    if len(encoded) == 0 or len(encoded) > PACKAGE_MAX_MANIFEST:
        raise PackageError("manifesto excede o limite")
    return encoded


def parse_manifest(raw: bytes) -> dict[str, str]:
    """Interpreta e valida o manifesto ASCII gravado no container."""
    if not raw or len(raw) > PACKAGE_MAX_MANIFEST:
        raise PackageError("tamanho do manifesto invalido")
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as error:
        raise PackageError("manifesto nao usa ASCII") from error
    values: dict[str, str] = {}
    for line in text.splitlines():
        if line.count("=") != 1:
            raise PackageError("linha de manifesto invalida")
        key, value = line.split("=", 1)
        if key not in REQUIRED_MANIFEST_KEYS or key in values:
            raise PackageError("chave de manifesto invalida")
        values[key] = value
    if tuple(values.keys()) != REQUIRED_MANIFEST_KEYS:
        raise PackageError("manifesto incompleto ou fora de ordem")
    package_id = validate_id(values["id"])
    values["name"] = ensure_ascii(values["name"], "name", 31)
    values["version"] = ensure_ascii(values["version"], "version", 15)
    if not VERSION_RE.fullmatch(values["version"]):
        raise PackageError("version invalida")
    if values["api"] not in SUPPORTED_APP_APIS or values["entry"] != "APP.ZAP":
        raise PackageError("API ou entry do manifesto invalida")
    dependencies = [] if not values["dependencies"] else values["dependencies"].split(",")
    validate_dependencies(dependencies, package_id)
    return values


def validate_zapp(image: bytes) -> None:
    """Replica os limites estruturais da imagem ZAPP aceitos pelo loader."""
    if len(image) < ZAPP_HEADER.size or len(image) > ZAPP_MAX_SIZE:
        raise PackageError("tamanho da imagem ZAPP invalido")
    fields = ZAPP_HEADER.unpack_from(image)
    magic, version, arch, header_size, code_offset, code_size, data_offset, data_size, entry, stack_size, flags = fields
    if magic != ZAPP_MAGIC or version != ZAPP_VERSION or arch != ZAPP_ARCH_I386:
        raise PackageError("cabecalho ZAPP invalido")
    if header_size != ZAPP_HEADER.size or code_offset != header_size:
        raise PackageError("offset de codigo ZAPP invalido")
    if code_size == 0 or code_size > ZAPP_MAX_CODE or data_size > ZAPP_MAX_DATA:
        raise PackageError("tamanho de secoes ZAPP invalido")
    if data_offset != code_offset + code_size or entry >= code_size:
        raise PackageError("layout ZAPP invalido")
    if stack_size != ZAPP_STACK_SIZE or flags != 0:
        raise PackageError("stack ou flags ZAPP invalidas")
    if len(image) != data_offset + data_size:
        raise PackageError("tamanho total ZAPP diverge do cabecalho")


def build_package(manifest: dict[str, str], zapp: bytes) -> bytes:
    """Monta o container ZPKG v1 a partir do manifesto e da imagem ZAPP."""
    validate_zapp(zapp)
    manifest_raw = encode_manifest(manifest)
    content = manifest_raw + zapp
    checksum = zlib.crc32(content) & 0xFFFFFFFF
    header = PACKAGE_HEADER.pack(
        PACKAGE_MAGIC,
        PACKAGE_VERSION,
        PACKAGE_HEADER_SIZE,
        PACKAGE_ARCH_I386,
        len(manifest_raw),
        len(zapp),
        checksum,
        0,
        0,
    )
    return header + content


def parse_package(data: bytes) -> PackageInfo:
    """Valida o container completo e retorna manifesto e payload confiaveis."""
    if len(data) < PACKAGE_HEADER_SIZE:
        raise PackageError("pacote menor que o cabecalho")
    magic, version, header_size, arch, manifest_size, payload_size, checksum, flags, reserved = PACKAGE_HEADER.unpack_from(data)
    if magic != PACKAGE_MAGIC or version != PACKAGE_VERSION:
        raise PackageError("magic ou versao do pacote invalida")
    if header_size != PACKAGE_HEADER_SIZE or arch != PACKAGE_ARCH_I386:
        raise PackageError("header ou arquitetura do pacote invalida")
    if flags != 0 or reserved != 0:
        raise PackageError("flags reservadas do pacote invalidas")
    if manifest_size == 0 or manifest_size > PACKAGE_MAX_MANIFEST:
        raise PackageError("tamanho do manifesto invalido")
    if payload_size == 0 or payload_size > ZAPP_MAX_SIZE:
        raise PackageError("tamanho do payload invalido")
    expected_size = header_size + manifest_size + payload_size
    if len(data) != expected_size:
        raise PackageError("tamanho do pacote diverge do cabecalho")
    content = data[header_size:]
    if zlib.crc32(content) & 0xFFFFFFFF != checksum:
        raise PackageError("CRC32 do pacote diverge")
    manifest_end = header_size + manifest_size
    manifest = parse_manifest(data[header_size:manifest_end])
    payload = data[manifest_end:]
    validate_zapp(payload)
    return PackageInfo(manifest=manifest, payload=payload)


def alias_name(package_id: str) -> str:
    """Retorna o nome 8.3 usado para transportar o pacote no FAT12."""
    return f"{validate_id(package_id)}.ZPK"


def fat12_geometry(image: bytearray) -> tuple[int, int, int, int, int, int, int]:
    """Le a geometria FAT12 sem depender de ferramentas externas."""
    if len(image) < 512 or image[510:512] != b"\x55\xAA":
        raise PackageError("imagem FAT12 sem boot sector valido")
    bytes_per_sector = struct.unpack_from("<H", image, 11)[0]
    sectors_per_cluster = image[13]
    reserved = struct.unpack_from("<H", image, 14)[0]
    fat_count = image[16]
    root_entries = struct.unpack_from("<H", image, 17)[0]
    total_sectors = struct.unpack_from("<H", image, 19)[0] or struct.unpack_from("<I", image, 32)[0]
    sectors_per_fat = struct.unpack_from("<H", image, 22)[0]
    if bytes_per_sector != 512 or sectors_per_cluster == 0 or reserved == 0:
        raise PackageError("BPB FAT12 invalido")
    if fat_count == 0 or root_entries == 0 or sectors_per_fat == 0:
        raise PackageError("geometria FAT12 invalida")
    if total_sectors == 0 or total_sectors * bytes_per_sector > len(image):
        raise PackageError("imagem menor que o volume FAT12")
    root_start = reserved + fat_count * sectors_per_fat
    root_sectors = (root_entries * 32 + bytes_per_sector - 1) // bytes_per_sector
    data_start = root_start + root_sectors
    if data_start >= total_sectors:
        raise PackageError("area de dados FAT12 invalida")
    clusters = (total_sectors - data_start) // sectors_per_cluster
    if clusters == 0 or clusters >= 4085:
        raise PackageError("volume nao e FAT12 compativel")
    return bytes_per_sector, sectors_per_cluster, reserved, fat_count, sectors_per_fat, root_start, clusters


def prepare_boot_image(image_path: Path, disk_bytes: int) -> int:
    """Reserva o payload de boot no BPB e expande a imagem sem truncar dados."""
    try:
        payload = bytearray(image_path.read_bytes())
    except OSError as error:
        raise PackageError("nao foi possivel ler a imagem de boot") from error

    if len(payload) < 512 or payload[510:512] != b"\x55\xAA":
        raise PackageError("payload sem boot sector valido")
    bytes_per_sector = struct.unpack_from("<H", payload, 11)[0]
    total_sectors = (struct.unpack_from("<H", payload, 19)[0] or
                     struct.unpack_from("<I", payload, 32)[0])
    if bytes_per_sector != 512 or disk_bytes <= 0:
        raise PackageError("tamanho de setor ou disco invalido")
    if disk_bytes % bytes_per_sector != 0:
        raise PackageError("tamanho do disco nao esta alinhado a setores")
    if total_sectors == 0 or total_sectors * bytes_per_sector != disk_bytes:
        raise PackageError("BPB e tamanho final do disco divergem")
    if len(payload) > disk_bytes:
        raise PackageError("boot, stage2 e kernel excedem a imagem FAT12")

    reserved = (len(payload) + bytes_per_sector - 1) // bytes_per_sector
    if reserved == 0 or reserved > 0xFFFF:
        raise PackageError("quantidade de setores reservados invalida")

    prepared = payload + bytearray(disk_bytes - len(payload))
    struct.pack_into("<H", prepared, 14, reserved)
    fat12_geometry(prepared)
    try:
        image_path.write_bytes(prepared)
    except OSError as error:
        raise PackageError("nao foi possivel gravar a imagem FAT12") from error
    return reserved


def _fat32_u32(image: bytearray, offset: int) -> int:
    return struct.unpack_from("<I", image, offset)[0]


def _fat32_put_u32(image: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", image, offset, value & 0x0FFFFFFF)


def fat32_geometry(image: bytearray, start_lba: int) -> tuple[int, int, int, int, int, int, int]:
    """Valida a geometria FAT32 de uma particao dentro de uma imagem hibrida."""
    base = start_lba * FAT32_SECTOR_SIZE
    if start_lba < 1 or base + FAT32_SECTOR_SIZE > len(image):
        raise PackageError("inicio da particao FAT32 fora da imagem")
    sector = image[base:base + FAT32_SECTOR_SIZE]
    if sector[510:512] != b"\x55\xAA":
        raise PackageError("boot sector FAT32 sem assinatura valida")
    bps = struct.unpack_from("<H", sector, 11)[0]
    spc = sector[13]
    reserved = struct.unpack_from("<H", sector, 14)[0]
    fat_count = sector[16]
    total = struct.unpack_from("<I", sector, 32)[0]
    spf = struct.unpack_from("<I", sector, 36)[0]
    root_cluster = struct.unpack_from("<I", sector, 44)[0]
    if bps != FAT32_SECTOR_SIZE or spc == 0 or reserved == 0:
        raise PackageError("BPB FAT32 invalido")
    if fat_count == 0 or spf == 0 or total == 0:
        raise PackageError("geometria FAT32 invalida")
    if start_lba + total > len(image) // FAT32_SECTOR_SIZE:
        raise PackageError("particao FAT32 excede a imagem")
    data_start = reserved + fat_count * spf
    if data_start >= total:
        raise PackageError("area de dados FAT32 invalida")
    clusters = (total - data_start) // spc
    if clusters < FAT32_MIN_CLUSTERS or root_cluster < 2 or root_cluster >= clusters + 2:
        raise PackageError("volume nao e FAT32 compativel")
    if spf * (bps // 4) < clusters + 2:
        raise PackageError("FAT32 nao comporta a quantidade de clusters")
    return bps, spc, reserved, fat_count, spf, data_start, clusters


def _fat32_get(fat: bytearray, cluster: int) -> int:
    return struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF


def _fat32_set(fat: bytearray, cluster: int, value: int) -> None:
    struct.pack_into("<I", fat, cluster * 4, value & 0x0FFFFFFF)


def _fat32_allocate(fat: bytearray, count: int, clusters: int) -> list[int]:
    selected = [
        index for index in range(2, clusters + 2)
        if _fat32_get(fat, index) == FAT32_FREE
    ]
    if len(selected) < count:
        raise PackageError("espaco insuficiente na particao FAT32")
    return selected[:count]


def _fat32_chain(fat: bytearray, first: int, clusters: int) -> list[int]:
    if first < 2:
        return []
    result: list[int] = []
    cluster = first
    while 2 <= cluster < FAT32_EOF:
        if cluster >= clusters + 2 or cluster in result:
            raise PackageError("cadeia FAT32 invalida")
        result.append(cluster)
        cluster = _fat32_get(fat, cluster)
    if cluster != 0 and cluster < FAT32_EOF:
        raise PackageError("fim de cadeia FAT32 invalido")
    return result


def _fat32_cluster_offset(data_start: int, spc: int, cluster: int) -> int:
    return (data_start + (cluster - 2) * spc) * FAT32_SECTOR_SIZE


def _fat32_write_fats(image: bytearray, start_lba: int, reserved: int,
                      fat_count: int, spf: int, fat: bytearray) -> None:
    for index in range(fat_count):
        offset = (start_lba + reserved + index * spf) * FAT32_SECTOR_SIZE
        image[offset:offset + len(fat)] = fat


def _fat32_format_partition(image: bytearray, start_lba: int,
                            total_sectors: int, label: str) -> None:
    reserved = 32
    fat_count = 2
    spc = 4
    spf = 1
    for _ in range(8):
        clusters = (total_sectors - reserved - fat_count * spf) // spc
        next_spf = (4 * (clusters + 2) + FAT32_SECTOR_SIZE - 1) // FAT32_SECTOR_SIZE
        if next_spf == spf:
            break
        spf = next_spf
    data_start = reserved + fat_count * spf
    clusters = (total_sectors - data_start) // spc
    if clusters < FAT32_STANDARD_MIN_CLUSTERS:
        raise PackageError("particao FAT32 pequena demais")
    if len(label) > 11 or not label or any(ord(c) < 0x20 or ord(c) > 0x7E for c in label):
        raise PackageError("rotulo FAT32 invalido")

    base = start_lba * FAT32_SECTOR_SIZE
    boot = bytearray(FAT32_SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"ZEPHYR2 "
    struct.pack_into("<H", boot, 11, FAT32_SECTOR_SIZE)
    boot[13] = spc
    struct.pack_into("<H", boot, 14, reserved)
    boot[16] = fat_count
    boot[21] = 0xF8
    struct.pack_into("<I", boot, 28, start_lba)
    struct.pack_into("<I", boot, 32, total_sectors)
    struct.pack_into("<I", boot, 36, spf)
    struct.pack_into("<I", boot, 44, 2)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x45503934)
    boot[71:82] = label.upper().ljust(11).encode("ascii")
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    image[base:base + FAT32_SECTOR_SIZE] = boot
    image[(start_lba + 6) * FAT32_SECTOR_SIZE:(start_lba + 7) * FAT32_SECTOR_SIZE] = boot

    fsinfo = bytearray(FAT32_SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, clusters - 1)
    struct.pack_into("<I", fsinfo, 492, 3)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    image[(start_lba + 1) * FAT32_SECTOR_SIZE:(start_lba + 2) * FAT32_SECTOR_SIZE] = fsinfo
    image[(start_lba + 7) * FAT32_SECTOR_SIZE:(start_lba + 8) * FAT32_SECTOR_SIZE] = fsinfo

    fat = bytearray(spf * FAT32_SECTOR_SIZE)
    _fat32_set(fat, 0, 0x0FFFFFF8)
    _fat32_set(fat, 1, 0x0FFFFFFF)
    _fat32_set(fat, 2, FAT32_EOF)
    _fat32_write_fats(image, start_lba, reserved, fat_count, spf, fat)
    root_offset = (start_lba + data_start) * FAT32_SECTOR_SIZE
    root = bytearray(FAT32_SECTOR_SIZE)
    root[0:11] = label.upper().ljust(11).encode("ascii")
    root[11] = 0x08
    image[root_offset:root_offset + FAT32_SECTOR_SIZE] = root


def _fat32_alias(name: str, existing: set[bytes]) -> bytes:
    """Gera alias 8.3 deterministico para um nome LFN."""
    upper = name.upper()
    if all(char in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_." for char in upper):
        try:
            alias = name_to_83(upper)
            if alias not in existing:
                return alias
        except PackageError:
            pass
    stem, dot, extension = upper.rpartition(".")
    if not dot:
        stem, extension = upper, ""
    stem = "".join(char for char in stem if char in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") or "FILE"
    extension = "".join(char for char in extension if char in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")[:3]
    for number in range(1, 10000):
        suffix = "~" + str(number)
        prefix = (stem or "FILE")[:8 - len(suffix)]
        alias = (prefix + suffix).ljust(8)[:8]
        alias = (alias + extension.ljust(3)).encode("ascii")
        if alias not in existing:
            return alias
    raise PackageError("nao foi possivel gerar alias 8.3")


def _fat32_lfn_checksum(alias: bytes) -> int:
    checksum = 0
    for value in alias:
        checksum = ((checksum & 1) << 7) + (checksum >> 1) + value
        checksum &= 0xFF
    return checksum


def _fat32_lfn_units(name: str) -> list[int]:
    if not name or "/" in name or "\\" in name or len(name.encode("utf-8")) > 255:
        raise PackageError("nome LFN invalido")
    encoded = name.encode("utf-16le")
    units = list(struct.unpack("<%dH" % (len(encoded) // 2), encoded))
    if len(units) > 255:
        raise PackageError("nome LFN excede 255 caracteres")
    return units


def _fat32_lfn_entry(sequence: int, units: list[int], checksum: int) -> bytes:
    entry = bytearray(32)
    entry[0] = sequence
    entry[11] = FAT32_ATTR_LFN
    entry[13] = checksum
    positions = (1, 14, 28)
    unit_index = (sequence & 0x1F) - 1
    start = unit_index * 13
    values = units[start:start + 13]
    if len(values) < 13:
        values.append(0)
    while len(values) < 13:
        values.append(0xFFFF)
    for position, count in zip(positions, (5, 6, 2)):
        for index in range(count):
            struct.pack_into("<H", entry, position + index * 2,
                             values.pop(0))
    return bytes(entry)


def _fat32_decode_lfn(entry: bytes) -> list[int]:
    values: list[int] = []
    for position, count in ((1, 5), (14, 6), (28, 2)):
        for index in range(count):
            values.append(struct.unpack_from("<H", entry, position + index * 2)[0])
    return values


def _fat32_short_name(alias: bytes) -> str:
    stem = alias[:8].decode("ascii", errors="replace").rstrip(" ")
    extension = alias[8:11].decode("ascii", errors="replace").rstrip(" ")
    return stem + (("." + extension) if extension else "")


def _fat32_name_equal(left: str, right: str) -> bool:
    """Compara ASCII sem caixa e preserva igualdade exata para UTF-8."""
    if len(left) != len(right):
        return False
    for left_value, right_value in zip(left, right):
        if ord(left_value) < 0x80 and ord(right_value) < 0x80:
            if left_value.lower() != right_value.lower():
                return False
        elif left_value != right_value:
            return False
    return True


def _fat32_directory_chain(image: bytearray, start_lba: int, fat: bytearray,
                            data_start: int, spc: int, clusters: int,
                            first_cluster: int) -> list[int]:
    del start_lba
    chain = _fat32_chain(fat, first_cluster, clusters)
    if not chain:
        raise PackageError("diretorio FAT32 sem cadeia")
    return chain


def _fat32_directory_slots(image: bytearray, start_lba: int, bps: int,
                            data_start: int, spc: int, fat: bytearray,
                            clusters: int, chain: list[int],
                            required: int) -> list[int]:
    """Reserva slots contiguos, atravessando a extensao do diretorio."""
    slots: list[int] = []
    for cluster in chain:
        base = start_lba * bps + _fat32_cluster_offset(data_start, spc, cluster)
        for offset in range(0, spc * bps, FAT32_DIR_ENTRY_SIZE):
            slot = base + offset
            if image[slot] in (0x00, 0xE5):
                slots.append(slot)
            else:
                slots = []
            if len(slots) >= required:
                return slots[:required]

    while len(slots) < required:
        previous = chain[-1]
        new_cluster = _fat32_allocate(fat, 1, clusters)[0]
        _fat32_set(fat, previous, new_cluster)
        _fat32_set(fat, new_cluster, FAT32_EOF)
        new_base = start_lba * bps + _fat32_cluster_offset(
            data_start, spc, new_cluster
        )
        image[new_base:new_base + spc * bps] = bytes(spc * bps)
        chain.append(new_cluster)
        for offset in range(0, spc * bps, FAT32_DIR_ENTRY_SIZE):
            slots.append(new_base + offset)
            if len(slots) >= required:
                return slots[:required]
    return slots[:required]


def _fat32_records(image: bytearray, start_lba: int, fat: bytearray,
                   data_start: int, spc: int, clusters: int,
                   first_cluster: int) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    lfn_units: list[int] = []
    lfn_checksum: int | None = None
    lfn_start: int | None = None
    chain = _fat32_directory_chain(image, start_lba, fat, data_start, spc,
                                    clusters, first_cluster)
    for cluster in chain:
        base = start_lba * FAT32_SECTOR_SIZE + _fat32_cluster_offset(
            data_start, spc, cluster
        )
        for offset in range(0, spc * FAT32_SECTOR_SIZE, 32):
            entry_offset = base + offset
            raw = bytes(image[entry_offset:entry_offset + 32])
            first = raw[0]
            if first == 0:
                return records
            if first == 0xE5:
                lfn_units = []
                lfn_checksum = None
                lfn_start = None
                continue
            if raw[11] == FAT32_ATTR_LFN:
                sequence = raw[0] & 0x1F
                if raw[0] & 0x40:
                    lfn_units = _fat32_decode_lfn(raw)
                    lfn_checksum = raw[13]
                    lfn_start = entry_offset
                elif lfn_units and sequence:
                    lfn_units = _fat32_decode_lfn(raw) + lfn_units
                continue
            if raw[11] & 0x08:
                lfn_units = []
                lfn_checksum = None
                lfn_start = None
                continue
            alias = raw[:11]
            units = lfn_units
            if lfn_checksum != _fat32_lfn_checksum(alias):
                units = []
            while units and units[-1] in (0, 0xFFFF):
                units.pop()
            try:
                long_name = bytes(struct.pack("<%dH" % len(units), *units)).decode("utf-16le") if units else _fat32_short_name(alias)
            except (UnicodeDecodeError, struct.error):
                long_name = _fat32_short_name(alias)
                units = []
            high = struct.unpack_from("<H", raw, 20)[0]
            low = struct.unpack_from("<H", raw, 26)[0]
            records.append({
                "name": long_name,
                "alias": alias,
                "offset": entry_offset,
                "lfn_start": lfn_start if units else entry_offset,
                "raw": raw,
                "cluster": (high << 16) | low,
                "size": struct.unpack_from("<I", raw, 28)[0],
                "attributes": raw[11],
            })
            lfn_units = []
            lfn_checksum = None
            lfn_start = None
    return records


def _fat32_resolve_directory(image: bytearray, start_lba: int, fat: bytearray,
                              data_start: int, spc: int, clusters: int,
                              path: str) -> int:
    cluster = 2
    normalized = path.replace("\\", "/").strip("/")
    if not normalized:
        return cluster
    for component in normalized.split("/"):
        if not component or component in (".", ".."):
            raise PackageError("caminho FAT32 invalido")
        record = next((item for item in _fat32_records(
            image, start_lba, fat, data_start, spc, clusters, cluster)
            if _fat32_name_equal(item["name"], component) or
            _fat32_name_equal(_fat32_short_name(item["alias"]), component)), None)
        if not record or not record["attributes"] & FAT32_ATTR_DIRECTORY:
            raise PackageError("diretorio FAT32 nao encontrado")
        cluster = record["cluster"]
    return cluster


def _fat32_write_fat_chain(image: bytearray, start_lba: int, reserved: int,
                            fat_count: int, spf: int, fat: bytearray) -> None:
    _fat32_write_fats(image, start_lba, reserved, fat_count, spf, fat)


def inject_fat32_bytes(data: bytes, image: bytearray, path: str,
                       replace: bool = False,
                       fat32_start_lba: int = HYBRID_FAT32_START_LBA,
                       fat_name: str | None = None) -> None:
    if not path or path.endswith(("/", "\\")):
        raise PackageError("caminho de arquivo FAT32 invalido")
    bps, spc, reserved, fat_count, spf, data_start, clusters = fat32_geometry(
        image, fat32_start_lba
    )
    fat_offset = (fat32_start_lba + reserved) * bps
    fat = bytearray(image[fat_offset:fat_offset + spf * bps])
    normalized = path.replace("\\", "/").strip("/")
    components = normalized.split("/")
    filename = components[-1]
    directory_path = "/".join(components[:-1])
    directory_cluster = _fat32_resolve_directory(
        image, fat32_start_lba, fat, data_start, spc, clusters, directory_path
    )
    records = _fat32_records(image, fat32_start_lba, fat, data_start, spc,
                             clusters, directory_cluster)
    existing_aliases = {record["alias"] for record in records}
    requested_alias = name_to_83(fat_name) if fat_name else None
    existing = next((record for record in records
                     if (requested_alias is not None and
                         record["alias"] == requested_alias) or
                     _fat32_name_equal(record["name"], filename)), None)
    if existing and not replace:
        raise PackageError("arquivo FAT32 ja existe; use --replace")
    alias = (existing["alias"] if existing else
             requested_alias if requested_alias is not None else
             _fat32_alias(filename, existing_aliases))
    if existing:
        old_chain = _fat32_chain(fat, existing["cluster"], clusters)
        for cluster in old_chain:
            _fat32_set(fat, cluster, FAT32_FREE)
        image[existing["lfn_start"]:existing["offset"] + 32] = bytes(
            [0xE5] * (existing["offset"] + 32 - existing["lfn_start"])
        )
    units = _fat32_lfn_units(filename)
    lfn_count = (len(units) + 12) // 13
    required_slots = lfn_count + 1
    chain = _fat32_directory_chain(image, fat32_start_lba, fat, data_start,
                                    spc, clusters, directory_cluster)
    slots = _fat32_directory_slots(
        image, fat32_start_lba, bps, data_start, spc, fat, clusters, chain,
        required_slots,
    )
    needed = (len(data) + bps * spc - 1) // (bps * spc)
    data_clusters = _fat32_allocate(fat, needed, clusters) if needed else []
    for index, cluster in enumerate(data_clusters):
        _fat32_set(fat, cluster, data_clusters[index + 1] if index + 1 < len(data_clusters) else FAT32_EOF)
        offset = fat32_start_lba * bps + _fat32_cluster_offset(data_start, spc, cluster)
        block = data[index * bps * spc:(index + 1) * bps * spc]
        image[offset:offset + bps * spc] = block.ljust(bps * spc, b"\0")
    checksum = _fat32_lfn_checksum(alias)
    lfn_entries = [
        _fat32_lfn_entry(
            index | (0x40 if index == lfn_count else 0), units, checksum
        )
        for index in range(lfn_count, 0, -1)
    ]
    short = bytearray(32)
    short[:11] = alias
    short[11] = FAT32_ATTR_ARCHIVE
    if data_clusters:
        struct.pack_into("<H", short, 20, (data_clusters[0] >> 16) & 0xFFFF)
        struct.pack_into("<H", short, 26, data_clusters[0] & 0xFFFF)
    struct.pack_into("<I", short, 28, len(data))
    for slot, entry in zip(slots, lfn_entries + [bytes(short)]):
        image[slot:slot + 32] = entry
    _fat32_write_fat_chain(image, fat32_start_lba, reserved, fat_count, spf, fat)


def inject_fat32_file(data: bytes, image_path: Path, path: str,
                      replace: bool = False,
                      fat32_start_lba: int = HYBRID_FAT32_START_LBA,
                      fat_name: str | None = None) -> None:
    """Injeta arquivo FAT32 com LFN e alias 8.3, usando escrita host atomica."""
    try:
        image = bytearray(image_path.read_bytes())
    except OSError as error:
        raise PackageError("nao foi possivel ler imagem hibrida") from error
    inject_fat32_bytes(data, image, path, replace, fat32_start_lba, fat_name)
    try:
        image_path.write_bytes(image)
    except OSError as error:
        raise PackageError("nao foi possivel gravar arquivo FAT32") from error


def read_fat32_file(image_path: Path, path: str,
                    fat32_start_lba: int = HYBRID_FAT32_START_LBA) -> bytes:
    """Le um arquivo FAT32 para auditorias host sem depender do kernel."""
    try:
        image = bytearray(image_path.read_bytes())
    except OSError as error:
        raise PackageError("nao foi possivel ler imagem FAT32") from error
    bps, spc, reserved, _, spf, data_start, clusters = fat32_geometry(
        image, fat32_start_lba
    )
    fat_offset = (fat32_start_lba + reserved) * bps
    fat = bytearray(image[fat_offset:fat_offset + spf * bps])
    normalized = path.replace("\\", "/").strip("/")
    components = normalized.split("/")
    directory = "/".join(components[:-1])
    cluster = _fat32_resolve_directory(
        image, fat32_start_lba, fat, data_start, spc, clusters, directory
    )
    records = _fat32_records(image, fat32_start_lba, fat, data_start, spc,
                             clusters, cluster)
    record = next((item for item in records
                   if _fat32_name_equal(item["name"], components[-1]) or
                   _fat32_name_equal(_fat32_short_name(item["alias"]),
                                     components[-1])), None)
    if not record or record["attributes"] & FAT32_ATTR_DIRECTORY:
        raise PackageError("arquivo FAT32 nao encontrado")
    result = bytearray()
    for cluster in _fat32_chain(fat, record["cluster"], clusters):
        offset = fat32_start_lba * bps + _fat32_cluster_offset(
            data_start, spc, cluster)
        result.extend(image[offset:offset + bps * spc])
    if len(result) < record["size"]:
        raise PackageError("cadeia FAT32 do arquivo esta incompleta")
    return bytes(result[:record["size"]])


def read_image_file(image_path: Path, name: str) -> bytes:
    """Le tanto imagens FAT12 legadas quanto imagens hibridas."""
    image = bytearray(image_path.read_bytes())
    try:
        fat32_geometry(image, HYBRID_FAT32_START_LBA)
    except PackageError:
        return read_root_file(image_path, name)
    return read_fat32_file(image_path, name)


def prepare_hybrid_image(image_path: Path, disk_bytes: int,
                         fat32_start_lba: int, label: str) -> None:
    """Cria a imagem hibrida sem alterar o codigo do boot gerado."""
    try:
        payload = bytearray(image_path.read_bytes())
    except OSError as error:
        raise PackageError("nao foi possivel ler o payload de boot") from error
    if len(payload) < FAT32_SECTOR_SIZE or payload[510:512] != b"\x55\xAA":
        raise PackageError("payload de boot sem assinatura valida")
    if disk_bytes <= 0 or disk_bytes % FAT32_SECTOR_SIZE:
        raise PackageError("tamanho da imagem hibrida invalido")
    if (fat32_start_lba < 4096 or fat32_start_lba % 4096 or
            fat32_start_lba * FAT32_SECTOR_SIZE >= disk_bytes):
        raise PackageError("inicio FAT32 invalido")
    if len(payload) > fat32_start_lba * FAT32_SECTOR_SIZE:
        raise PackageError("boot legado invadiria a particao FAT32")
    legacy_total = (struct.unpack_from("<H", payload, 19)[0] or
                    struct.unpack_from("<I", payload, 32)[0])
    reserved = struct.unpack_from("<H", payload, 14)[0]
    if legacy_total == 0 or reserved == 0 or reserved >= legacy_total:
        raise PackageError("reserva FAT12 legada invalida")
    image = payload + bytearray(disk_bytes - len(payload))
    fat12_geometry(image)
    format_legacy_fat12(image)
    image[446:510] = b"\0" * 64
    entry = 446
    image[entry + 4] = 0x0C
    struct.pack_into("<I", image, entry + 8, fat32_start_lba)
    struct.pack_into("<I", image, entry + 12,
                     disk_bytes // FAT32_SECTOR_SIZE - fat32_start_lba)
    _fat32_format_partition(
        image, fat32_start_lba,
        disk_bytes // FAT32_SECTOR_SIZE - fat32_start_lba, label,
    )
    try:
        image_path.write_bytes(image)
    except OSError as error:
        raise PackageError("nao foi possivel gravar a imagem hibrida") from error


def fat12_get(fat: bytearray, index: int) -> int:
    """Le uma entrada de 12 bits da FAT."""
    offset = index + index // 2
    if index & 1:
        return ((fat[offset] >> 4) | (fat[offset + 1] << 4)) & 0xFFF
    return (fat[offset] | ((fat[offset + 1] & 0x0F) << 8)) & 0xFFF


def fat12_set(fat: bytearray, index: int, value: int) -> None:
    """Escreve uma entrada de 12 bits preservando o nibble vizinho."""
    offset = index + index // 2
    value &= 0xFFF
    if index & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def format_legacy_fat12(image: bytearray) -> None:
    """Inicializa as cópias FAT12 sem tocar na área reservada do boot."""
    bps, _, reserved, fat_count, spf, _, _ = fat12_geometry(image)
    for index in range(fat_count):
        start = (reserved + index * spf) * bps
        fat = bytearray(spf * bps)
        fat[0:3] = bytes((image[21], 0xFF, 0xFF))
        image[start:start + len(fat)] = fat


def name_to_83(name: str) -> bytes:
    """Converte um alias estrito 8.3 para a entrada FAT."""
    upper = name.upper()
    if upper.count(".") != 1:
        raise PackageError("alias FAT deve usar extensao 8.3")
    stem, extension = upper.split(".", 1)
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
    if not stem or len(stem) > 8 or not extension or len(extension) > 3:
        raise PackageError("alias FAT fora do limite 8.3")
    if any(char not in allowed for char in stem + extension):
        raise PackageError("alias FAT possui caractere invalido")
    return stem.ljust(8).encode("ascii") + extension.ljust(3).encode("ascii")


def root_entry_offset(root_offset: int, index: int) -> int:
    """Retorna a posicao de uma entrada de 32 bytes no diretorio raiz."""
    return root_offset + index * 32


def find_root_entry(image: bytearray, root_offset: int, root_entries: int, name: bytes) -> int | None:
    """Busca uma entrada de arquivo ativa pelo nome FAT normalizado."""
    for index in range(root_entries):
        offset = root_entry_offset(root_offset, index)
        first = image[offset]
        if first == 0x00:
            break
        if first != 0xE5 and not (image[offset + 11] & 0x08) and image[offset:offset + 11] == name:
            return index
    return None


def find_root_slot(image: bytearray, root_offset: int, root_entries: int) -> int:
    """Encontra uma entrada livre ou removida no diretorio raiz FAT12."""
    for index in range(root_entries):
        first = image[root_entry_offset(root_offset, index)]
        if first in (0x00, 0xE5):
            return index
    raise PackageError("diretorio raiz FAT12 cheio")


def prepare_fats(image: bytearray, bps: int, reserved: int, fat_count: int, spf: int) -> list[bytearray]:
    """Carrega FATs e inicializa somente o marcador reservado de volume vazio."""
    fats: list[bytearray] = []
    fat_bytes = spf * bps
    for index in range(fat_count):
        start = (reserved + index * spf) * bps
        fat = bytearray(image[start:start + fat_bytes])
        if not any(fat):
            fat[0:3] = bytes((image[21], 0xFF, 0xFF))
        if fat[0] != image[21] or fat12_get(fat, 1) < 0xFF8:
            raise PackageError("FAT12 nao esta formatada de forma compativel")
        fats.append(fat)
    return fats


def allocate_clusters(fat: bytearray, count: int, clusters: int) -> list[int]:
    """Reserva a quantidade exata de clusters livres sem alterar a imagem."""
    selected = [index for index in range(2, clusters + 2) if fat12_get(fat, index) == FAT12_FREE]
    if len(selected) < count:
        raise PackageError("espaco insuficiente na imagem FAT12")
    return selected[:count]


def release_clusters(fat: bytearray, first_cluster: int, clusters: int) -> None:
    """Libera uma cadeia FAT12 existente antes de uma substituicao explicita."""
    if first_cluster == 0:
        return
    if first_cluster < 2 or first_cluster >= clusters + 2:
        raise PackageError("cadeia FAT12 existente invalida")
    cluster = first_cluster
    visited: set[int] = set()
    while 2 <= cluster < 0xFF8:
        if cluster in visited or cluster >= clusters + 2:
            raise PackageError("cadeia FAT12 existente corrompida")
        visited.add(cluster)
        next_cluster = fat12_get(fat, cluster)
        fat12_set(fat, cluster, FAT12_FREE)
        cluster = next_cluster
    if cluster < 0xFF8:
        raise PackageError("cadeia FAT12 existente corrompida")


def inject_root_file(data: bytes, image_path: Path, fat_name: str,
                     replace: bool = False) -> None:
    """Grava um arquivo nao vazio no diretorio raiz de uma imagem FAT12."""
    if not data:
        raise PackageError("arquivo vazio nao pode ser injetado")
    try:
        image = bytearray(image_path.read_bytes())
    except OSError as error:
        raise PackageError(f"nao foi possivel ler imagem {image_path}") from error
    bps, spc, reserved, fat_count, spf, root_start, clusters = fat12_geometry(image)
    root_entries = struct.unpack_from("<H", image, 17)[0]
    root_offset = root_start * bps
    name = name_to_83(fat_name)
    existing = find_root_entry(image, root_offset, root_entries, name)
    if existing is not None and not replace:
        raise PackageError("arquivo FAT ja existe; use --replace para substitui-lo")
    slot = existing if existing is not None else find_root_slot(image, root_offset, root_entries)
    fats = prepare_fats(image, bps, reserved, fat_count, spf)
    if existing is not None:
        entry = root_entry_offset(root_offset, existing)
        old_cluster = struct.unpack_from("<H", image, entry + 26)[0]
        for fat in fats:
            release_clusters(fat, old_cluster, clusters)
    cluster_bytes = bps * spc
    needed = (len(data) + cluster_bytes - 1) // cluster_bytes
    allocated = allocate_clusters(fats[0], needed, clusters)
    for fat in fats:
        for index, cluster in enumerate(allocated):
            fat12_set(fat, cluster, allocated[index + 1] if index + 1 < len(allocated) else FAT12_EOF)
    data_start = root_start + (root_entries * 32 + bps - 1) // bps
    for index, cluster in enumerate(allocated):
        start = (data_start + (cluster - 2) * spc) * bps
        block = data[index * cluster_bytes:(index + 1) * cluster_bytes]
        image[start:start + cluster_bytes] = block.ljust(cluster_bytes, b"\0")
    for index, fat in enumerate(fats):
        start = (reserved + index * spf) * bps
        image[start:start + len(fat)] = fat
    entry = root_entry_offset(root_offset, slot)
    image[entry:entry + 32] = b"\0" * 32
    image[entry:entry + 11] = name
    image[entry + 11] = FAT12_ATTR_ARCHIVE
    struct.pack_into("<H", image, entry + 26, allocated[0])
    struct.pack_into("<I", image, entry + 28, len(data))
    try:
        image_path.write_bytes(image)
    except OSError as error:
        raise PackageError(f"nao foi possivel gravar imagem {image_path}") from error


def inject_package(package: bytes, image_path: Path, fat_name: str,
                   replace: bool = False) -> None:
    """Grava um pacote validado no diretorio raiz de uma imagem FAT12."""
    info = parse_package(package)
    if fat_name.upper() != alias_name(info.manifest["id"]):
        raise PackageError("alias FAT deve corresponder ao ID do pacote")
    inject_root_file(package, image_path, fat_name, replace)


def read_root_file(image_path: Path, fat_name: str) -> bytes:
    """Le um arquivo raiz FAT12, usado apenas pelo auto-teste do host."""
    image = bytearray(image_path.read_bytes())
    bps, spc, reserved, fat_count, spf, root_start, clusters = fat12_geometry(image)
    del fat_count, spf
    root_entries = struct.unpack_from("<H", image, 17)[0]
    root_offset = root_start * bps
    entry_index = find_root_entry(image, root_offset, root_entries, name_to_83(fat_name))
    if entry_index is None:
        raise PackageError("arquivo injetado nao encontrado")
    entry = root_entry_offset(root_offset, entry_index)
    size = struct.unpack_from("<I", image, entry + 28)[0]
    cluster = struct.unpack_from("<H", image, entry + 26)[0]
    fat_start = reserved * bps
    fat = image[fat_start:fat_start + struct.unpack_from("<H", image, 22)[0] * bps]
    data_start = root_start + (root_entries * 32 + bps - 1) // bps
    result = bytearray()
    steps = 0
    while cluster >= 2 and cluster < 0xFF8 and steps <= clusters:
        start = (data_start + (cluster - 2) * spc) * bps
        result.extend(image[start:start + bps * spc])
        cluster = fat12_get(fat, cluster)
        steps += 1
    if len(result) < size:
        raise PackageError("cadeia FAT12 do arquivo esta incompleta")
    return bytes(result[:size])


def build_demo_zapp() -> bytes:
    """Cria uma imagem ZAPP minima que apenas executa process_exit(0)."""
    code = b"\xB8\x00\x00\x00\x00\x31\xDB\xCD\x80\xEB\xFE"
    header = ZAPP_HEADER.pack(
        ZAPP_MAGIC, ZAPP_VERSION, ZAPP_ARCH_I386, ZAPP_HEADER.size,
        ZAPP_HEADER.size, len(code), ZAPP_HEADER.size + len(code), 0,
        0, ZAPP_STACK_SIZE, 0,
    )
    return header + code


def build_wait_zapp() -> bytes:
    """Cria um ZAPP minimo que permanece ativo ate o cancelamento por F12."""
    code = b"\xEB\xFE"
    header = ZAPP_HEADER.pack(
        ZAPP_MAGIC, ZAPP_VERSION, ZAPP_ARCH_I386, ZAPP_HEADER.size,
        ZAPP_HEADER.size, len(code), ZAPP_HEADER.size + len(code), 0,
        0, ZAPP_STACK_SIZE, 0,
    )
    return header + code


def store_fixture_manifest(
    package_id: str,
    name: str,
    version: str = "1.0.0",
    api: str = CURRENT_APP_API,
    dependencies: str = "",
) -> dict[str, str]:
    """Monta um manifesto na ordem canonica para os fixtures da App Store."""
    return {
        "id": package_id,
        "name": name,
        "version": version,
        "api": api,
        "entry": "APP.ZAP",
        "dependencies": dependencies,
    }


def build_store_fixtures() -> dict[str, bytes]:
    """Gera deterministicamente a matriz publica do catalogo AS1."""
    zapp = build_demo_zapp()
    fixtures = {
        "VALID.ZPK": build_package(
            store_fixture_manifest("VALID", "Store Valid"), zapp
        ),
        "BADAPI.ZPK": build_package(
            store_fixture_manifest("BADAPI", "Bad API", api="9.9"), zapp
        ),
        "BADALIAS.ZPK": build_package(
            store_fixture_manifest("ALIASOK", "Bad Alias"), zapp
        ),
        "NEEDSDEP.ZPK": build_package(
            store_fixture_manifest(
                "NEEDSDEP", "Needs Dependency", dependencies="MISSING"
            ),
            zapp,
        ),
        "SAMEVER.ZPK": build_package(
            store_fixture_manifest("SAMEVER", "Same Version"), zapp
        ),
    }
    bad_crc = bytearray(
        build_package(store_fixture_manifest("BADCRC", "Bad CRC"), zapp)
    )
    bad_crc[-1] ^= 0xFF
    fixtures["BADCRC.ZPK"] = bytes(bad_crc)
    return {alias: fixtures[alias] for alias in STORE_FIXTURE_ALIASES}


def store_fixture_expectations() -> dict[str, dict[str, str]]:
    """Descreve os resultados publicos esperados pelo catalogo AS1."""
    return {
        "VALID.ZPK": {
            "id": "VALID",
            "state": "AVAILABLE",
            "reason": "NONE",
        },
        "BADCRC.ZPK": {
            "id": "BADCRC",
            "state": "INVALID",
            "reason": "PACKAGE_INVALID",
        },
        "BADAPI.ZPK": {
            "id": "BADAPI",
            "state": "INVALID",
            "reason": "PACKAGE_INVALID",
        },
        "BADALIAS.ZPK": {
            "id": "ALIASOK",
            "state": "INVALID",
            "reason": "ALIAS_MISMATCH",
        },
        "NEEDSDEP.ZPK": {
            "id": "NEEDSDEP",
            "state": "BLOCKED",
            "reason": "DEPENDENCY_MISSING",
        },
        "SAMEVER.ZPK": {
            "id": "SAMEVER",
            "state": "AVAILABLE_THEN_SAME_VERSION",
            "reason": "NONE",
        },
    }


def write_store_fixtures(output_dir: Path) -> None:
    """Grava somente os artefatos publicos conhecidos e seu manifesto."""
    fixtures = build_store_fixtures()
    expectations = store_fixture_expectations()
    output_dir.mkdir(parents=True, exist_ok=True)
    published: dict[str, object] = {
        "format": STORE_FIXTURE_FORMAT,
        "fixtures": {},
    }
    fixture_metadata = published["fixtures"]
    if not isinstance(fixture_metadata, dict):
        raise PackageError("estrutura interna dos fixtures invalida")
    for alias, data in fixtures.items():
        (output_dir / alias).write_bytes(data)
        fixture_metadata[alias] = {
            **expectations[alias],
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    (output_dir / "fixtures.json").write_text(
        json.dumps(published, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def load_store_fixture_manifest(fixtures_dir: Path) -> dict[str, object]:
    """Le e valida a forma basica do manifesto publico AS1."""
    try:
        published = json.loads(
            (fixtures_dir / "fixtures.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError("manifesto dos fixtures da App Store invalido") from error
    if not isinstance(published, dict) or (
        published.get("format") != STORE_FIXTURE_FORMAT
    ):
        raise PackageError("formato dos fixtures da App Store divergiu")
    metadata = published.get("fixtures")
    if not isinstance(metadata, dict) or set(metadata) != set(STORE_FIXTURE_ALIASES):
        raise PackageError("conjunto dos fixtures da App Store divergiu")
    return published


def audit_store_fixture_semantics(fixtures: dict[str, bytes]) -> None:
    """Confere que cada vetor cobre exatamente o caso publicado."""
    valid = parse_package(fixtures["VALID.ZPK"])
    bad_alias = parse_package(fixtures["BADALIAS.ZPK"])
    needs_dep = parse_package(fixtures["NEEDSDEP.ZPK"])
    same_version = parse_package(fixtures["SAMEVER.ZPK"])
    if valid.manifest["id"] != "VALID":
        raise PackageError("fixture VALID divergiu")
    if (
        bad_alias.manifest["id"] != "ALIASOK"
        or alias_name(bad_alias.manifest["id"]) == "BADALIAS.ZPK"
    ):
        raise PackageError("fixture BADALIAS divergiu")
    if needs_dep.manifest["dependencies"] != "MISSING":
        raise PackageError("fixture NEEDSDEP divergiu")
    if same_version.manifest["version"] != "1.0.0":
        raise PackageError("fixture SAMEVER divergiu")
    for alias in ("BADCRC.ZPK", "BADAPI.ZPK"):
        try:
            parse_package(fixtures[alias])
        except PackageError:
            continue
        raise PackageError(f"fixture invalido foi aceito: {alias}")


def audit_store_fixtures(
    fixtures_dir: Path, image_path: Path | None = None
) -> None:
    """Audita hashes, semantica e, opcionalmente, aliases na imagem FAT12."""
    published = load_store_fixture_manifest(fixtures_dir)
    metadata = published["fixtures"]
    expected = build_store_fixtures()
    expectations = store_fixture_expectations()
    fixtures: dict[str, bytes] = {}
    if not isinstance(metadata, dict):
        raise PackageError("metadados dos fixtures da App Store invalidos")
    for alias in STORE_FIXTURE_ALIASES:
        try:
            data = (fixtures_dir / alias).read_bytes()
        except OSError as error:
            raise PackageError(f"fixture ausente: {alias}") from error
        entry = metadata.get(alias)
        if not isinstance(entry, dict):
            raise PackageError(f"metadado ausente: {alias}")
        if (
            data != expected[alias]
            or entry.get("size") != len(data)
            or entry.get("sha256") != hashlib.sha256(data).hexdigest()
            or any(
                entry.get(field) != value
                for field, value in expectations[alias].items()
            )
        ):
            raise PackageError(f"fixture dessincronizado: {alias}")
        if image_path is not None and read_image_file(image_path, alias) != data:
            raise PackageError(f"fixture divergiu na imagem: {alias}")
        fixtures[alias] = data
        print(f"store_fixture_{alias} OK")
    audit_store_fixture_semantics(fixtures)
    print("App Store fixtures: OK")


def build_store_as2_fixtures() -> dict[str, bytes]:
    """Gera a matriz separada de ciclo de vida local do AS2."""
    demo = build_demo_zapp()
    wait = build_wait_zapp()
    fixtures = {
        "WAITAPP.ZPK": build_package(
            store_fixture_manifest("WAITAPP", "Wait for F12"), wait
        ),
        "BASE.ZPK": build_package(
            store_fixture_manifest("BASE", "Dependency Base"), demo
        ),
        "DEPEND.ZPK": build_package(
            store_fixture_manifest(
                "DEPEND", "Reverse Dependent", dependencies="BASE"
            ),
            demo,
        ),
    }
    return {
        alias: fixtures[alias] for alias in STORE_AS2_FIXTURE_ALIASES
    }


def store_as2_fixture_expectations() -> dict[str, dict[str, str]]:
    """Descreve os estados e comportamentos esperados na matriz AS2."""
    return {
        "WAITAPP.ZPK": {
            "id": "WAITAPP",
            "state": "AVAILABLE",
            "reason": "NONE",
            "behavior": "WAIT_FOR_F12",
        },
        "BASE.ZPK": {
            "id": "BASE",
            "state": "AVAILABLE",
            "reason": "NONE",
            "behavior": "EXIT_SUCCESS",
        },
        "DEPEND.ZPK": {
            "id": "DEPEND",
            "state": "BLOCKED_THEN_AVAILABLE",
            "reason": "DEPENDENCY_MISSING",
            "behavior": "DEPENDS_ON_BASE",
        },
    }


def write_store_as2_fixtures(output_dir: Path) -> None:
    """Grava os artefatos AS2 sem alterar o conjunto canonico do AS1."""
    fixtures = build_store_as2_fixtures()
    expectations = store_as2_fixture_expectations()
    output_dir.mkdir(parents=True, exist_ok=True)
    published: dict[str, object] = {
        "format": STORE_AS2_FIXTURE_FORMAT,
        "fixtures": {},
    }
    metadata = published["fixtures"]
    if not isinstance(metadata, dict):
        raise PackageError("estrutura interna dos fixtures AS2 invalida")
    for alias, data in fixtures.items():
        (output_dir / alias).write_bytes(data)
        metadata[alias] = {
            **expectations[alias],
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    (output_dir / "fixtures.json").write_text(
        json.dumps(published, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def load_store_as2_manifest(fixtures_dir: Path) -> dict[str, object]:
    """Le e valida o manifesto da matriz AS2."""
    try:
        published = json.loads(
            (fixtures_dir / "fixtures.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError("manifesto dos fixtures AS2 invalido") from error
    if not isinstance(published, dict):
        raise PackageError("estrutura do manifesto AS2 invalida")
    metadata = published.get("fixtures")
    if (
        published.get("format") != STORE_AS2_FIXTURE_FORMAT
        or not isinstance(metadata, dict)
        or set(metadata) != set(STORE_AS2_FIXTURE_ALIASES)
    ):
        raise PackageError("conjunto dos fixtures AS2 divergiu")
    return published


def audit_store_as2_semantics(fixtures: dict[str, bytes]) -> None:
    """Confere espera por F12 e o par de dependencia reversa."""
    wait = parse_package(fixtures["WAITAPP.ZPK"])
    base = parse_package(fixtures["BASE.ZPK"])
    dependent = parse_package(fixtures["DEPEND.ZPK"])
    if wait.manifest["id"] != "WAITAPP" or wait.payload != build_wait_zapp():
        raise PackageError("fixture WAITAPP divergiu")
    if base.manifest["id"] != "BASE" or base.manifest["dependencies"]:
        raise PackageError("fixture BASE divergiu")
    if (
        dependent.manifest["id"] != "DEPEND"
        or dependent.manifest["dependencies"] != "BASE"
    ):
        raise PackageError("fixture DEPEND divergiu")


def audit_store_as2_fixtures(
    fixtures_dir: Path, image_path: Path | None = None
) -> None:
    """Audita hashes, semantica e bytes FAT12 da matriz AS2."""
    published = load_store_as2_manifest(fixtures_dir)
    metadata = published["fixtures"]
    expected = build_store_as2_fixtures()
    expectations = store_as2_fixture_expectations()
    fixtures: dict[str, bytes] = {}
    if not isinstance(metadata, dict):
        raise PackageError("metadados dos fixtures AS2 invalidos")
    for alias in STORE_AS2_FIXTURE_ALIASES:
        try:
            data = (fixtures_dir / alias).read_bytes()
        except OSError as error:
            raise PackageError(f"fixture AS2 ausente: {alias}") from error
        entry = metadata.get(alias)
        if not isinstance(entry, dict):
            raise PackageError(f"metadado AS2 ausente: {alias}")
        if (
            data != expected[alias]
            or entry.get("size") != len(data)
            or entry.get("sha256") != hashlib.sha256(data).hexdigest()
            or any(
                entry.get(field) != value
                for field, value in expectations[alias].items()
            )
        ):
            raise PackageError(f"fixture AS2 dessincronizado: {alias}")
        if image_path is not None and read_image_file(image_path, alias) != data:
            raise PackageError(f"fixture AS2 divergiu na imagem: {alias}")
        fixtures[alias] = data
        print(f"store_as2_fixture_{alias} OK")
    audit_store_as2_semantics(fixtures)
    print("App Store AS2 fixtures: OK")


def build_store_as4_fixtures(profile: str) -> dict[str, bytes]:
    """Gera as fontes locais de update e planejamento do AS4."""
    if profile not in ("seed", "update"):
        raise PackageError("perfil AS4 invalido")
    demo = build_demo_zapp()
    target_version = "1.0.0" if profile == "seed" else "1.1.0"
    target_dependencies = "" if profile == "seed" else "UPDEPA"
    fixtures = {
        "UPTARGET.ZPK": build_package(
            store_fixture_manifest("UPTARGET", "Update Target", target_version,
                                   dependencies=target_dependencies), demo
        ),
        "UPDEPA.ZPK": build_package(
            store_fixture_manifest("UPDEPA", "Update Dependency A",
                                   dependencies="UPDEPB"), demo
        ),
        "UPDEPB.ZPK": build_package(
            store_fixture_manifest("UPDEPB", "Update Dependency B"), demo
        ),
        "BROKEN.ZPK": build_package(
            store_fixture_manifest("BROKEN", "Broken Plan",
                                   dependencies="NOFONTE"), demo
        ),
        "CYCLEA.ZPK": build_package(
            store_fixture_manifest("CYCLEA", "Cycle A", dependencies="CYCLEB"),
            demo,
        ),
        "CYCLEB.ZPK": build_package(
            store_fixture_manifest("CYCLEB", "Cycle B", dependencies="CYCLEA"),
            demo,
        ),
    }
    return {alias: fixtures[alias] for alias in STORE_AS4_FIXTURE_ALIASES}


def store_as4_fixture_expectations(profile: str) -> dict[str, dict[str, str]]:
    """Publica os casos de planejamento AS4 sem depender do runtime."""
    return {
        "UPTARGET.ZPK": {
            "id": "UPTARGET", "version": "1.0.0" if profile == "seed" else "1.1.0",
            "dependencies": "" if profile == "seed" else "UPDEPA",
        },
        "UPDEPA.ZPK": {"id": "UPDEPA", "version": "1.0.0", "dependencies": "UPDEPB"},
        "UPDEPB.ZPK": {"id": "UPDEPB", "version": "1.0.0", "dependencies": ""},
        "BROKEN.ZPK": {"id": "BROKEN", "version": "1.0.0", "dependencies": "NOFONTE"},
        "CYCLEA.ZPK": {"id": "CYCLEA", "version": "1.0.0", "dependencies": "CYCLEB"},
        "CYCLEB.ZPK": {"id": "CYCLEB", "version": "1.0.0", "dependencies": "CYCLEA"},
    }


def write_store_as4_fixtures(output_dir: Path, profile: str) -> None:
    """Grava seed ou update deterministico, com metadados auditaveis."""
    fixtures = build_store_as4_fixtures(profile)
    expectations = store_as4_fixture_expectations(profile)
    output_dir.mkdir(parents=True, exist_ok=True)
    published: dict[str, object] = {
        "format": STORE_AS4_FIXTURE_FORMAT, "profile": profile, "fixtures": {}
    }
    metadata = published["fixtures"]
    if not isinstance(metadata, dict):
        raise PackageError("estrutura interna dos fixtures AS4 invalida")
    for alias, data in fixtures.items():
        (output_dir / alias).write_bytes(data)
        metadata[alias] = {
            **expectations[alias], "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    (output_dir / "fixtures.json").write_text(
        json.dumps(published, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def audit_store_as4_fixtures(
    fixtures_dir: Path, image_path: Path | None = None
) -> None:
    """Audita pacote, perfil e aliases FAT12 da matriz AS4."""
    try:
        published = json.loads(
            (fixtures_dir / "fixtures.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError("manifesto AS4 invalido") from error
    profile = published.get("profile") if isinstance(published, dict) else None
    metadata = published.get("fixtures") if isinstance(published, dict) else None
    if (
        not isinstance(published, dict)
        or published.get("format") != STORE_AS4_FIXTURE_FORMAT
        or profile not in ("seed", "update") or not isinstance(metadata, dict)
        or set(metadata) != set(STORE_AS4_FIXTURE_ALIASES)
    ):
        raise PackageError("conjunto dos fixtures AS4 divergiu")
    fixtures = build_store_as4_fixtures(profile)
    expectations = store_as4_fixture_expectations(profile)
    for alias, expected in fixtures.items():
        try:
            data = (fixtures_dir / alias).read_bytes()
        except OSError as error:
            raise PackageError(f"fixture AS4 ausente: {alias}") from error
        entry = metadata.get(alias)
        if (
            not isinstance(entry, dict) or data != expected
            or entry.get("size") != len(data)
            or entry.get("sha256") != hashlib.sha256(data).hexdigest()
            or any(entry.get(field) != value
                   for field, value in expectations[alias].items())
        ):
            raise PackageError(f"fixture AS4 dessincronizado: {alias}")
        parsed = parse_package(data)
        if any(parsed.manifest.get(field) != value
               for field, value in expectations[alias].items()):
            raise PackageError(f"semantica AS4 divergiu: {alias}")
        if image_path is not None and read_image_file(image_path, alias) != data:
            raise PackageError(f"fixture AS4 divergiu na imagem: {alias}")
        print(f"store_as4_fixture_{profile}_{alias} OK")
    print(f"App Store AS4 fixtures ({profile}): OK")


def store_as5_crypto_modules() -> tuple[Any, Any, Any]:
    """Carrega Ed25519 somente nos comandos AS5 que dependem dela."""
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ed25519
    except ImportError as error:
        raise PackageError(
            "AS5 requer tools/requirements-updater.txt"
        ) from error
    return ed25519, serialization, InvalidSignature


def store_as5_public_config(path: Path) -> dict[str, object]:
    """Le a raiz publica exclusiva da App Store de teste."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError("configuracao publica AS5 invalida") from error
    if (
        not isinstance(data, dict)
        or data.get("format") != STORE_AS5_PUBLIC_FORMAT
        or data.get("algorithm") != "Ed25519"
        or data.get("trust") != "test-only"
    ):
        raise PackageError("contrato da chave publica AS5 divergiu")
    try:
        public = bytes.fromhex(str(data["public_key_hex"]))
        key_id = bytes.fromhex(str(data["key_id_hex"]))
        revoked = [bytes.fromhex(str(item)) for item in data["revoked_key_ids"]]
    except (KeyError, TypeError, ValueError) as error:
        raise PackageError("material publico AS5 invalido") from error
    if len(public) != 32 or len(key_id) != 16 or any(
        len(item) != 16 for item in revoked
    ):
        raise PackageError("tamanho da chave publica AS5 invalido")
    if hashlib.sha256(public).digest()[:16] != key_id:
        raise PackageError("key ID AS5 nao corresponde a chave publica")
    if key_id in revoked or len(set(revoked)) != len(revoked):
        raise PackageError("tabela de revogacao AS5 e conflitante")
    return {**data, "public": public, "key_id": key_id, "revoked": revoked}


def store_as5_fixed(value: str, size: int, label: str) -> bytes:
    """Codifica um campo ASCII NUL-padded do ZAC1."""
    encoded = ensure_ascii(value, label, size - 1).encode("ascii")
    return encoded + bytes(size - len(encoded))


def build_store_as5_packages(profile: str) -> dict[str, bytes]:
    """Gera os tres pacotes remotos deterministas de instalacao/update."""
    if profile not in STORE_AS5_PROFILES:
        raise PackageError("perfil AS5 invalido")
    demo = build_demo_zapp()
    target_version = "1.0.0" if profile == "seed" else "1.1.0"
    packages = {
        "RMDEPA": build_package(
            store_fixture_manifest("RMDEPA", "Remote Dependency A",
                                   dependencies="RMDEPB"), demo
        ),
        "RMDEPB": build_package(
            store_fixture_manifest("RMDEPB", "Remote Dependency B"), demo
        ),
        "RMTARGET": build_package(
            store_fixture_manifest("RMTARGET", "Remote Target",
                                   target_version,
                                   dependencies="RMDEPA"), demo
        ),
    }
    return packages


def build_store_as5_catalog(
    packages: dict[str, bytes], generation: int, key_id: bytes,
    private_key: Any, overrides: dict[str, dict[str, object]] | None = None,
) -> bytes:
    """Monta e assina um catalogo ZAC1 ordenado por ID."""
    if not 1 <= len(packages) <= STORE_AS5_MAX_ENTRIES or generation <= 0:
        raise PackageError("quantidade ou geracao ZAC1 invalida")
    entries = bytearray()
    overrides = overrides or {}
    for package_id in sorted(packages):
        data = packages[package_id]
        parsed = parse_package(data)
        override = overrides.get(package_id, {})
        catalog_id = str(override.get("id", parsed.manifest["id"]))
        name = str(override.get("name", parsed.manifest["name"]))
        version = str(override.get("version", parsed.manifest["version"]))
        dependencies = str(
            override.get("dependencies", parsed.manifest["dependencies"])
        )
        dependency_ids = [] if not dependencies else dependencies.split(",")
        package_hash = override.get("hash", hashlib.sha256(data).digest())
        path = str(override.get("path", f"/zephyros/apps/{package_id}.ZPK"))
        if not isinstance(package_hash, bytes) or len(package_hash) != 32:
            raise PackageError("hash ZAC1 invalido")
        entry = bytearray(STORE_AS5_ENTRY_SIZE)
        entry[0:9] = store_as5_fixed(catalog_id, 9, "ID remoto")
        entry[9:41] = store_as5_fixed(name, 32, "nome remoto")
        entry[41:57] = store_as5_fixed(version, 16, "versao remota")
        if len(dependency_ids) > 4:
            raise PackageError("dependencias ZAC1 excederam o limite")
        entry[57] = len(dependency_ids)
        for index, dependency in enumerate(dependency_ids):
            entry[58 + index * 9:67 + index * 9] = store_as5_fixed(
                dependency, 9, "dependencia remota"
            )
        struct.pack_into("<I", entry, 96, len(data))
        entry[100:132] = package_hash
        entry[132:232] = store_as5_fixed(path, 100, "caminho remoto")
        entries.extend(entry)
    header = bytearray(STORE_AS5_HEADER_SIZE)
    header[0:4] = b"ZAC1"
    struct.pack_into(
        "<HHHHIHHHH", header, 4, 1, STORE_AS5_HEADER_SIZE,
        STORE_AS5_ENTRY_SIZE, len(packages), generation,
        STORE_AS5_CHANNEL, STORE_AS5_HASH_SHA256,
        STORE_AS5_SIGNATURE_ED25519, 0,
    )
    header[24:40] = key_id
    header[40:72] = hashlib.sha256(entries).digest()
    signed = bytes(header + entries)
    return signed + private_key.sign(STORE_AS5_DOMAIN + signed)


def parse_store_as5_catalog(
    raw: bytes, public_config: dict[str, object], minimum_generation: int = 0
) -> tuple[dict[str, object], str]:
    """Autentica ZAC1 no host e devolve a razao estavel de recusa."""
    if len(raw) < STORE_AS5_HEADER_SIZE + STORE_AS5_SIGNATURE_SIZE:
        return {}, "CATALOG_FORMAT"
    if raw[:4] != b"ZAC1":
        return {}, "CATALOG_FORMAT"
    version, header_size, entry_size, count = struct.unpack_from("<HHHH", raw, 4)
    generation = struct.unpack_from("<I", raw, 12)[0]
    channel, hash_alg, signature_alg, flags = struct.unpack_from("<HHHH", raw, 16)
    signed_size = STORE_AS5_HEADER_SIZE + count * STORE_AS5_ENTRY_SIZE
    if (
        version != 1 or header_size != STORE_AS5_HEADER_SIZE
        or entry_size != STORE_AS5_ENTRY_SIZE
        or not 1 <= count <= STORE_AS5_MAX_ENTRIES
        or len(raw) != signed_size + STORE_AS5_SIGNATURE_SIZE
        or generation <= 0 or channel != STORE_AS5_CHANNEL
        or hash_alg != STORE_AS5_HASH_SHA256
        or signature_alg != STORE_AS5_SIGNATURE_ED25519 or flags != 0
        or any(raw[72:STORE_AS5_HEADER_SIZE])
    ):
        return {}, "CATALOG_FORMAT"
    key_id = raw[24:40]
    if key_id in public_config["revoked"]:
        return {}, "REVOKED_KEY"
    if key_id != public_config["key_id"]:
        return {}, "UNKNOWN_KEY"
    ed25519, _, invalid_signature = store_as5_crypto_modules()
    try:
        ed25519.Ed25519PublicKey.from_public_bytes(
            public_config["public"]
        ).verify(raw[signed_size:], STORE_AS5_DOMAIN + raw[:signed_size])
    except invalid_signature:
        return {}, "SIGNATURE"
    if generation < minimum_generation:
        return {}, "REPLAY"
    entries_raw = raw[STORE_AS5_HEADER_SIZE:signed_size]
    if hashlib.sha256(entries_raw).digest() != raw[40:72]:
        return {}, "CATALOG_FORMAT"
    entries: list[dict[str, object]] = []
    previous = ""

    def fixed_text(field: bytes) -> str:
        end = field.find(b"\0")
        if end <= 0 or any(field[end + 1:]):
            raise ValueError("campo fixo ZAC1 invalido")
        value = field[:end].decode("ascii")
        if any(ord(character) < 0x20 or ord(character) > 0x7E
               for character in value):
            raise ValueError("campo fixo ZAC1 nao imprimivel")
        return value

    for index in range(count):
        entry = entries_raw[index * STORE_AS5_ENTRY_SIZE:(index + 1) * STORE_AS5_ENTRY_SIZE]
        try:
            package_id = fixed_text(entry[0:9])
            name = fixed_text(entry[9:41])
            version_text = fixed_text(entry[41:57])
            dependency_count = entry[57]
            if dependency_count > 4:
                return {}, "CATALOG_FORMAT"
            dependencies = [
                fixed_text(entry[58 + item * 9:67 + item * 9])
                for item in range(dependency_count)
            ]
            path = fixed_text(entry[132:232])
        except (UnicodeDecodeError, IndexError, ValueError):
            return {}, "CATALOG_FORMAT"
        package_size = struct.unpack_from("<I", entry, 96)[0]
        if (
            not ID_RE.fullmatch(package_id) or previous >= package_id
            or not VERSION_RE.fullmatch(version_text)
            or any(not ID_RE.fullmatch(item) for item in dependencies)
            or len(set(dependencies)) != len(dependencies)
            or any(
                any(entry[58 + item * 9:67 + item * 9])
                for item in range(dependency_count, 4)
            )
            or any(entry[94:96])
            or not 1 <= package_size <= STORE_AS5_PACKAGE_LIMIT
            or path != f"/zephyros/apps/{package_id}.ZPK"
            or any(entry[236:]) or struct.unpack_from("<I", entry, 232)[0] != 0
        ):
            if previous >= package_id:
                return {}, "DUPLICATE"
            if len(set(dependencies)) != len(dependencies):
                return {}, "PLAN_CONFLICT"
            return {}, "PATH" if path != f"/zephyros/apps/{package_id}.ZPK" else "CATALOG_FORMAT"
        previous = package_id
        entries.append({
            "id": package_id, "name": name, "version": version_text,
            "dependencies": dependencies,
            "size": package_size,
            "sha256": entry[100:132], "path": path,
        })
    return {"generation": generation, "entries": entries}, "NONE"


def store_as5_read_blob(path: Path) -> bytes:
    """Le artefato binario ou sua representacao base64 versionavel."""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise PackageError(f"artefato AS5 ausente: {path}") from error
    if path.suffix.lower() == ".b64":
        try:
            return base64.b64decode(data.strip(), validate=True)
        except ValueError as error:
            raise PackageError(f"base64 AS5 invalido: {path}") from error
    return data


def store_as5_catalog_semantics(
    catalog: dict[str, object], package_dir: Path | None
) -> str:
    """Replica planejamento e vinculacao pacote/catalogo para a auditoria."""
    entries = catalog.get("entries")
    if not isinstance(entries, list):
        return "CATALOG_FORMAT"
    by_id = {str(entry["id"]): entry for entry in entries}
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(package_id: str) -> str:
        if package_id in visiting:
            return "PLAN_CYCLE"
        if package_id in visited:
            return "NONE"
        entry = by_id.get(package_id)
        if not entry:
            return "PLAN_INCOMPLETE"
        visiting.add(package_id)
        for dependency in entry["dependencies"]:
            reason = visit(str(dependency))
            if reason != "NONE":
                return reason
        visiting.remove(package_id)
        visited.add(package_id)
        return "NONE"

    plan_reason = visit("RMTARGET") if "RMTARGET" in by_id else "NONE"
    if plan_reason != "NONE":
        return plan_reason
    if package_dir is None:
        return "NONE"
    for entry in entries:
        path = package_dir / f"{entry['id']}.ZPK.b64"
        package = store_as5_read_blob(path)
        if len(package) != entry["size"] or hashlib.sha256(package).digest() != entry["sha256"]:
            return "PACKAGE_HASH"
        parsed = parse_package(package)
        dependencies = [] if not parsed.manifest["dependencies"] else parsed.manifest["dependencies"].split(",")
        if (
            parsed.manifest["id"] != entry["id"]
            or parsed.manifest["name"] != entry["name"]
            or parsed.manifest["version"] != entry["version"]
            or dependencies != entry["dependencies"]
        ):
            return "PACKAGE_MISMATCH"
    return "NONE"


def store_as5_header_bytes(text: str, symbol: str) -> bytes:
    """Extrai uma tabela hexadecimal pequena do header de confianca."""
    match = re.search(
        rf"{re.escape(symbol)}[^=]*=\s*\{{(.*?)\}};", text, re.DOTALL
    )
    if not match:
        raise PackageError(f"simbolo AS5 ausente no header: {symbol}")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(1)))


def audit_store_as5_header(header_path: Path,
                           public: dict[str, object]) -> None:
    """Confere que o header derivado usa exatamente a raiz publica JSON."""
    try:
        text = header_path.read_text(encoding="utf-8")
    except OSError as error:
        raise PackageError("header de confianca AS5 ausente") from error
    if (
        store_as5_header_bytes(text, "APP_REMOTE_TRUST_PUBLIC_KEY") != public["public"]
        or store_as5_header_bytes(text, "APP_REMOTE_TRUST_KEY_ID") != public["key_id"]
        or store_as5_header_bytes(text, "APP_REMOTE_REVOKED_KEY_IDS") != b"".join(public["revoked"])
    ):
        raise PackageError("header e raiz publica AS5 estao dessincronizados")


def audit_store_as5_fixtures(
    fixtures_dir: Path, public_path: Path, header_path: Path | None = None
) -> None:
    """Audita chave publica, assinaturas, hashes e matriz negativa AS5."""
    public = store_as5_public_config(public_path)
    if header_path is not None:
        audit_store_as5_header(header_path, public)
    try:
        manifest = json.loads(
            (fixtures_dir / "fixtures.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PackageError("manifesto dos fixtures AS5 invalido") from error
    artifacts = manifest.get("artifacts") if isinstance(manifest, dict) else None
    if (
        not isinstance(manifest, dict)
        or manifest.get("format") != STORE_AS5_FIXTURE_FORMAT
        or not isinstance(artifacts, list) or not artifacts
    ):
        raise PackageError("conjunto dos fixtures AS5 divergiu")
    for item in artifacts:
        if not isinstance(item, dict):
            raise PackageError("entrada de fixture AS5 invalida")
        path = fixtures_dir / str(item.get("file", ""))
        blob = store_as5_read_blob(path)
        if hashlib.sha256(blob).hexdigest() != item.get("sha256"):
            raise PackageError(f"hash publicado AS5 divergiu: {path}")
        if item.get("type") == "package":
            parse_package(blob)
            print(f"store_as5_{path.name} OK")
            continue
        if item.get("type") != "catalog":
            raise PackageError("tipo de fixture AS5 desconhecido")
        minimum = int(item.get("minimum_generation", 0))
        catalog, parser_reason = parse_store_as5_catalog(blob, public, minimum)
        expected_parser = item.get("parser_expected", item.get("expected"))
        if parser_reason != expected_parser:
            raise PackageError(
                f"catalogo AS5 {path.name}: esperado parser {expected_parser}, obteve {parser_reason}"
            )
        reason = parser_reason
        if parser_reason == "NONE":
            package_dir_value = item.get("package_dir")
            package_dir = fixtures_dir / str(package_dir_value) if package_dir_value else None
            reason = store_as5_catalog_semantics(catalog, package_dir)
        if reason != item.get("expected"):
            raise PackageError(
                f"catalogo AS5 {path.name}: esperado {item.get('expected')}, obteve {reason}"
            )
        if parser_reason == "NONE" and not catalog.get("entries"):
            raise PackageError("catalogo AS5 valido ficou vazio")
        print(f"store_as5_{path.name} {reason} OK")
    print("App Store AS5 fixtures: OK")


class StoreAs5Handler(http.server.BaseHTTPRequestHandler):
    """Serve perfil selecionado e catalogos negativos, decodificando Base64."""
    fixture_root: Path
    fixture_base: Path

    def do_GET(self) -> None:  # noqa: N802 - contrato de BaseHTTPRequestHandler
        name = "stable.zac.b64" if self.path == "/zephyros/apps/stable.zac" else ""
        root = self.fixture_root
        catalog_routes = {
            "/zephyros/apps/seed/stable.zac": self.fixture_base / "seed" / "stable.zac.b64",
            "/zephyros/apps/update/stable.zac": self.fixture_base / "update" / "stable.zac.b64",
        }
        if self.path in catalog_routes:
            path = catalog_routes[self.path]
        elif (
            self.path.startswith("/zephyros/apps/invalid/")
            and self.path.endswith(".zac")
        ):
            fixture_name = self.path.rsplit("/", 1)[-1][:-4]
            path = self.fixture_base / "invalid" / f"{fixture_name}.zac.b64"
        else:
            path = None
        if self.path.startswith("/zephyros/apps/") and self.path.endswith(".ZPK"):
            name = self.path.rsplit("/", 1)[-1] + ".b64"
        if name:
            path = root / name
        if not path or not path.is_file():
            self.send_error(404)
            return
        data = store_as5_read_blob(path)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format: str, *args: object) -> None:
        print(f"AS5 HTTP: {format % args}")


def create_fixture_image(path: Path) -> None:
    """Gera uma imagem FAT12 vazia para testar a injecao sem QEMU."""
    image = bytearray(1474560)
    image[0:3] = b"\xEB\x3C\x90"
    image[3:11] = b"ZEPHYROS "
    struct.pack_into("<H", image, 11, 512)
    image[13] = 1
    struct.pack_into("<H", image, 14, 1)
    image[16] = 2
    struct.pack_into("<H", image, 17, 224)
    struct.pack_into("<H", image, 19, 2880)
    image[21] = 0xF0
    struct.pack_into("<H", image, 22, 9)
    image[510:512] = b"\x55\xAA"
    path.write_bytes(image)


def create_fixture_boot_payload(path: Path, size: int) -> None:
    """Gera um payload parcial com BPB valido para testar a reserva dinamica."""
    if size < 512:
        raise PackageError("fixture de boot menor que um setor")
    image = bytearray(size)
    image[0:3] = b"\xEB\x3C\x90"
    image[3:11] = b"ZEPHYROS "
    struct.pack_into("<H", image, 11, 512)
    image[13] = 1
    struct.pack_into("<H", image, 14, 1)
    image[16] = 2
    struct.pack_into("<H", image, 17, 224)
    struct.pack_into("<H", image, 19, 2880)
    image[21] = 0xF0
    struct.pack_into("<H", image, 22, 9)
    image[510:512] = b"\x55\xAA"
    for index in range(512, size):
        image[index] = index & 0xFF
    path.write_bytes(image)


def run_selftest() -> int:
    """Executa os cenarios host sem modificar arquivos do repositorio."""
    manifest = {
        "id": "DEMO",
        "name": "Demo",
        "version": "1.0.0",
        "api": CURRENT_APP_API,
        "entry": "APP.ZAP",
        "dependencies": "",
    }
    package = build_package(manifest, build_demo_zapp())
    checks = {
        "criar": True,
        "api_legada": False,
        "crc_invalido": False,
        "injecao": False,
        "substituicao": False,
        "arquivo": False,
        "reserva_dinamica": False,
        "bpb_invalido": False,
        "imagem_excedida": False,
        "store_fixtures": False,
        "store_as2_fixtures": False,
        "store_as4_fixtures": False,
        "hybrid_image": False,
        "hybrid_lfn": False,
        "hybrid_alias": False,
        "hybrid_replace": False,
    }
    try:
        parse_package(package)
        legacy_manifest = dict(manifest)
        legacy_manifest["api"] = "0.3"
        parse_package(build_package(legacy_manifest, build_demo_zapp()))
        compat_manifest = dict(manifest)
        compat_manifest["api"] = "0.4"
        parse_package(build_package(compat_manifest, build_demo_zapp()))
        checks["api_legada"] = True
        broken = bytearray(package)
        broken[-1] ^= 0xFF
        try:
            parse_package(bytes(broken))
        except PackageError:
            checks["crc_invalido"] = True
        with tempfile.TemporaryDirectory(prefix="zephyros-package-") as temp_dir:
            image_path = Path(temp_dir) / "fixture.img"
            create_fixture_image(image_path)
            inject_package(package, image_path, "DEMO.ZPK")
            checks["injecao"] = read_root_file(image_path, "DEMO.ZPK") == package
            inject_package(package, image_path, "DEMO.ZPK", replace=True)
            checks["substituicao"] = read_root_file(image_path, "DEMO.ZPK") == package
            inject_root_file(b"desktop icon", image_path, "ICON.BMP")
            checks["arquivo"] = read_root_file(image_path, "ICON.BMP") == b"desktop icon"

            store_dir = Path(temp_dir) / "store"
            write_store_fixtures(store_dir)
            for alias in STORE_FIXTURE_ALIASES:
                inject_root_file(
                    (store_dir / alias).read_bytes(), image_path, alias
                )
            audit_store_fixtures(store_dir, image_path)
            checks["store_fixtures"] = True

            store_as2_dir = Path(temp_dir) / "store-as2"
            write_store_as2_fixtures(store_as2_dir)
            for alias in STORE_AS2_FIXTURE_ALIASES:
                inject_root_file(
                    (store_as2_dir / alias).read_bytes(), image_path, alias
                )
            audit_store_as2_fixtures(store_as2_dir, image_path)
            checks["store_as2_fixtures"] = True

            store_as4_dir = Path(temp_dir) / "store-as4"
            write_store_as4_fixtures(store_as4_dir, "update")
            for alias in STORE_AS4_FIXTURE_ALIASES:
                inject_root_file(
                    (store_as4_dir / alias).read_bytes(), image_path, alias
                )
            audit_store_as4_fixtures(store_as4_dir, image_path)
            checks["store_as4_fixtures"] = True

            hybrid_path = Path(temp_dir) / "hybrid.img"
            create_fixture_boot_payload(hybrid_path, 1300)
            prepare_hybrid_image(
                hybrid_path, HYBRID_DISK_BYTES,
                HYBRID_FAT32_START_LBA, HYBRID_FAT32_LABEL,
            )
            hybrid = bytearray(hybrid_path.read_bytes())
            partition = 446
            checks["hybrid_image"] = (
                len(hybrid) == HYBRID_DISK_BYTES and
                hybrid[partition + 4] == 0x0C and
                struct.unpack_from("<I", hybrid, partition + 8)[0] ==
                HYBRID_FAT32_START_LBA
            )
            long_name = "Dados de Sistema.txt"
            long_data = b"hybrid fat32 payload"
            for index in range(7):
                inject_fat32_file(
                    b"directory boundary",
                    hybrid_path,
                    f"FILL{index}.TXT",
                )
            inject_fat32_file(long_data, hybrid_path, long_name)
            records_image = bytearray(hybrid_path.read_bytes())
            _, spc, reserved, _, spf, data_start, clusters = \
                fat32_geometry(records_image, HYBRID_FAT32_START_LBA)
            fat_offset = (HYBRID_FAT32_START_LBA + reserved) * 512
            fat = records_image[fat_offset:fat_offset + spf * 512]
            records = _fat32_records(
                records_image, HYBRID_FAT32_START_LBA, fat, data_start,
                spc, clusters, 2,
            )
            long_record = next(
                (item for item in records if item["name"] == long_name), None
            )
            checks["hybrid_lfn"] = (
                read_fat32_file(hybrid_path, long_name) == long_data and
                long_record is not None and
                records_image[long_record["lfn_start"]] == 0x42 and
                records_image[long_record["lfn_start"] + 32] == 0x01 and
                records_image[long_record["offset"]] ==
                long_record["alias"][0] and
                len(_fat32_chain(fat, 2, clusters)) > 1
            )
            checks["hybrid_alias"] = any(
                item["name"] == long_name and
                _fat32_short_name(item["alias"]) == "DADOSD~1.TXT"
                for item in records
            )
            replaced_data = b"hybrid fat32 replacement"
            inject_fat32_file(replaced_data, hybrid_path, long_name,
                               replace=True)
            checks["hybrid_replace"] = (
                read_fat32_file(hybrid_path, long_name) == replaced_data
            )

            boot_path = Path(temp_dir) / "boot-payload.img"
            create_fixture_boot_payload(boot_path, 1300)
            original = boot_path.read_bytes()
            reserved = prepare_boot_image(boot_path, 1474560)
            prepared = boot_path.read_bytes()
            preserved = (prepared[:14] == original[:14] and
                         prepared[16:len(original)] == original[16:])
            checks["reserva_dinamica"] = (
                reserved == 3 and len(prepared) == 1474560 and preserved and
                struct.unpack_from("<H", prepared, 14)[0] == 3
            )

            invalid_path = Path(temp_dir) / "invalid-bpb.img"
            create_fixture_boot_payload(invalid_path, 512)
            invalid = bytearray(invalid_path.read_bytes())
            invalid[510:512] = b"\x00\x00"
            invalid_path.write_bytes(invalid)
            try:
                prepare_boot_image(invalid_path, 1474560)
            except PackageError:
                checks["bpb_invalido"] = invalid_path.read_bytes() == invalid

            oversized_path = Path(temp_dir) / "oversized.img"
            create_fixture_boot_payload(oversized_path, 1474561)
            oversized_size = oversized_path.stat().st_size
            try:
                prepare_boot_image(oversized_path, 1474560)
            except PackageError:
                checks["imagem_excedida"] = (
                    oversized_path.stat().st_size == oversized_size
                )
    except (OSError, PackageError):
        checks["criar"] = False
    for label, approved in checks.items():
        print(f"packager_{label} {'OK' if approved else 'ERRO'}")
    result = all(checks.values())
    print(f"Packager self-test {'OK' if result else 'ERRO'}")
    return 0 if result else 1


def command_build(arguments: argparse.Namespace) -> int:
    """Gera um artefato .zephyrosapp a partir dos arquivos do host."""
    manifest = manifest_from_json(Path(arguments.manifest))
    try:
        zapp = Path(arguments.zapp).read_bytes()
        Path(arguments.output).write_bytes(build_package(manifest, zapp))
    except OSError as error:
        raise PackageError("falha ao ler ou gravar arquivo do pacote") from error
    print(f"Pacote criado: {arguments.output}")
    return 0


def command_verify(arguments: argparse.Namespace) -> int:
    """Valida um artefato existente sem alterar seu conteudo."""
    try:
        info = parse_package(Path(arguments.package).read_bytes())
    except OSError as error:
        raise PackageError("nao foi possivel ler pacote") from error
    print(f"Pacote valido: {info.manifest['id']} {info.manifest['version']}")
    return 0


def command_inject(arguments: argparse.Namespace) -> int:
    """Injeta um artefato existente na imagem FAT12 informada."""
    try:
        package = Path(arguments.package).read_bytes()
    except OSError as error:
        raise PackageError("nao foi possivel ler pacote") from error
    info = parse_package(package)
    fat_name = arguments.fat_name or alias_name(info.manifest["id"])
    inject_package(package, Path(arguments.image), fat_name, arguments.replace)
    print(f"Pacote injetado como {fat_name.upper()}")
    return 0


def command_inject_file(arguments: argparse.Namespace) -> int:
    """Injeta um arquivo comum no diretorio raiz da imagem FAT12."""
    try:
        data = Path(arguments.file).read_bytes()
    except OSError as error:
        raise PackageError("nao foi possivel ler arquivo para injecao") from error
    inject_root_file(data, Path(arguments.image), arguments.fat_name,
                     arguments.replace)
    print(f"Arquivo injetado como {arguments.fat_name.upper()}")
    return 0


def command_prepare_image(arguments: argparse.Namespace) -> int:
    """Finaliza a imagem de boot com reserva FAT12 calculada pelo payload."""
    reserved = prepare_boot_image(Path(arguments.image), arguments.disk_bytes)
    print(f"Imagem preparada com {reserved} setores reservados")
    return 0


def command_prepare_hybrid_image(arguments: argparse.Namespace) -> int:
    """Prepara a imagem FAT12 legada e formata a particao FAT32 do sistema."""
    prepare_hybrid_image(
        Path(arguments.image), arguments.disk_bytes,
        arguments.fat32_start_lba, arguments.label,
    )
    print("Imagem hibrida FAT12/FAT32 preparada")
    return 0


def command_inject_file_fat32(arguments: argparse.Namespace) -> int:
    """Injeta um arquivo comum no volume FAT32 com LFN."""
    try:
        data = Path(arguments.file).read_bytes()
    except OSError as error:
        raise PackageError("nao foi possivel ler arquivo para injecao FAT32") from error
    inject_fat32_file(
        data, Path(arguments.image), arguments.path,
        arguments.replace, arguments.fat32_start_lba, arguments.fat_name,
    )
    print(f"Arquivo FAT32 injetado em {arguments.path}")
    return 0


def command_inject_files_fat32(arguments: argparse.Namespace) -> int:
    image_path = Path(arguments.image)
    try:
        image = bytearray(image_path.read_bytes())
    except OSError as error:
        raise PackageError("nao foi possivel ler imagem hibrida") from error
    for value in arguments.entry:
        source, separator, target = value.partition("=")
        if not separator or not source or not target:
            raise PackageError("entrada FAT32 em lote invalida")
        try:
            data = Path(source).read_bytes()
        except OSError as error:
            raise PackageError("nao foi possivel ler entrada FAT32") from error
        inject_fat32_bytes(
            data, image, target, arguments.replace,
            arguments.fat32_start_lba,
        )
    try:
        image_path.write_bytes(image)
    except OSError as error:
        raise PackageError("nao foi possivel gravar imagem FAT32") from error
    print(f"Arquivos FAT32 injetados: {len(arguments.entry)}")
    return 0


def command_demo(arguments: argparse.Namespace) -> int:
    """Cria e injeta o demo usado na validacao manual da Fase 7."""
    manifest = {
        "id": "DEMO",
        "name": "Demo",
        "version": "1.0.0",
        "api": CURRENT_APP_API,
        "entry": "APP.ZAP",
        "dependencies": "",
    }
    package = build_package(manifest, build_demo_zapp())
    output = Path(arguments.output)
    output.write_bytes(package)
    image_path = Path(arguments.image)
    try:
        fat32_geometry(bytearray(image_path.read_bytes()),
                       HYBRID_FAT32_START_LBA)
    except (OSError, PackageError):
        inject_package(package, image_path, "DEMO.ZPK")
    else:
        inject_fat32_file(package, image_path, "DEMO.ZPK", replace=True,
                          fat_name="DEMO.ZPK")
    print(f"Pacote demo criado: {output}")
    print("Pacote demo injetado como DEMO.ZPK")
    return 0


def command_fixtures_store(arguments: argparse.Namespace) -> int:
    """Gera os fixtures publicos e deterministicos do catalogo AS1."""
    output_dir = Path(arguments.output_dir)
    try:
        write_store_fixtures(output_dir)
    except OSError as error:
        raise PackageError("falha ao gravar fixtures da App Store") from error
    print(f"Fixtures da App Store criados em {output_dir.resolve()}")
    return 0


def command_audit_store(arguments: argparse.Namespace) -> int:
    """Audita os fixtures AS1 e seus aliases opcionais na imagem FAT12."""
    image_path = Path(arguments.image) if arguments.image else None
    try:
        audit_store_fixtures(Path(arguments.fixtures_dir), image_path)
    except OSError as error:
        raise PackageError("falha ao auditar fixtures da App Store") from error
    return 0


def command_fixtures_store_as2(arguments: argparse.Namespace) -> int:
    """Gera os fixtures deterministicos de ciclo de vida AS2."""
    output_dir = Path(arguments.output_dir)
    try:
        write_store_as2_fixtures(output_dir)
    except OSError as error:
        raise PackageError("falha ao gravar fixtures AS2") from error
    print(f"Fixtures AS2 criados em {output_dir.resolve()}")
    return 0


def command_audit_store_as2(arguments: argparse.Namespace) -> int:
    """Audita a matriz AS2 e seus aliases opcionais na imagem FAT12."""
    image_path = Path(arguments.image) if arguments.image else None
    try:
        audit_store_as2_fixtures(
            Path(arguments.fixtures_dir), image_path
        )
    except OSError as error:
        raise PackageError("falha ao auditar fixtures AS2") from error
    return 0


def command_fixtures_store_as4(arguments: argparse.Namespace) -> int:
    """Gera os fixtures AS4 para a imagem seed ou update."""
    output_dir = Path(arguments.output_dir)
    try:
        write_store_as4_fixtures(output_dir, arguments.profile)
    except OSError as error:
        raise PackageError("falha ao gravar fixtures AS4") from error
    print(f"Fixtures AS4 ({arguments.profile}) criados em {output_dir.resolve()}")
    return 0


def command_audit_store_as4(arguments: argparse.Namespace) -> int:
    """Audita a matriz de update AS4 e seus aliases FAT12 opcionais."""
    image_path = Path(arguments.image) if arguments.image else None
    try:
        audit_store_as4_fixtures(Path(arguments.fixtures_dir), image_path)
    except OSError as error:
        raise PackageError("falha ao auditar fixtures AS4") from error
    return 0


def command_sign_store_as5(arguments: argparse.Namespace) -> int:
    """Gera um perfil ZAC1 usando uma chave privada mantida fora do repo."""
    ed25519, serialization, _ = store_as5_crypto_modules()
    public = store_as5_public_config(Path(arguments.public))
    try:
        private = serialization.load_pem_private_key(
            Path(arguments.private).read_bytes(), password=None
        )
    except (OSError, ValueError, TypeError) as error:
        raise PackageError("chave privada AS5 nao pode ser carregada") from error
    if not isinstance(private, ed25519.Ed25519PrivateKey):
        raise PackageError("chave privada AS5 nao e Ed25519")
    raw_public = private.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )
    if raw_public != public["public"]:
        raise PackageError("chave privada AS5 nao corresponde a raiz publica")
    packages = build_store_as5_packages(arguments.profile)
    generation = 1 if arguments.profile == "seed" else 2
    catalog = build_store_as5_catalog(
        packages, generation, public["key_id"], private
    )
    output = Path(arguments.output_dir)
    output.mkdir(parents=True, exist_ok=True)
    (output / "stable.zac").write_bytes(catalog)
    for package_id, package in packages.items():
        (output / f"{package_id}.ZPK").write_bytes(package)
    print(f"Catalogo AS5 {arguments.profile} assinado em {output.resolve()}")
    return 0


def command_audit_store_as5(arguments: argparse.Namespace) -> int:
    """Executa a auditoria completa dos artefatos assinados AS5."""
    audit_store_as5_fixtures(
        Path(arguments.fixtures_dir), Path(arguments.public),
        Path(arguments.header) if arguments.header else None,
    )
    return 0


def command_serve_store_as5(arguments: argparse.Namespace) -> int:
    """Serve o perfil AS5 no endpoint HTTP consumido pelo QEMU."""
    root = Path(arguments.fixtures_dir) / arguments.profile
    if not root.is_dir():
        raise PackageError("perfil AS5 para HTTP nao foi encontrado")
    StoreAs5Handler.fixture_root = root
    StoreAs5Handler.fixture_base = Path(arguments.fixtures_dir)
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer((arguments.bind, arguments.port),
                                StoreAs5Handler) as server:
        print(
            f"AS5 HTTP {arguments.profile}: http://{arguments.bind}:{arguments.port}/zephyros/apps/stable.zac"
        )
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("AS5 HTTP encerrado")
    return 0


def build_parser() -> argparse.ArgumentParser:
    """Monta a interface de linha de comando do empacotador."""
    parser = argparse.ArgumentParser(description="Empacotador .zephyrosapp")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build")
    build.add_argument("--manifest", required=True)
    build.add_argument("--zapp", required=True)
    build.add_argument("--output", required=True)
    build.set_defaults(handler=command_build)
    verify = commands.add_parser("verify")
    verify.add_argument("package")
    verify.set_defaults(handler=command_verify)
    inject = commands.add_parser("inject")
    inject.add_argument("--package", required=True)
    inject.add_argument("--image", required=True)
    inject.add_argument("--fat-name")
    inject.add_argument("--replace", action="store_true")
    inject.set_defaults(handler=command_inject)
    inject_file = commands.add_parser("inject-file")
    inject_file.add_argument("--file", required=True)
    inject_file.add_argument("--image", required=True)
    inject_file.add_argument("--fat-name", required=True)
    inject_file.add_argument("--replace", action="store_true")
    inject_file.set_defaults(handler=command_inject_file)
    prepare_image = commands.add_parser("prepare-image")
    prepare_image.add_argument("--image", required=True)
    prepare_image.add_argument("--disk-bytes", required=True, type=int)
    prepare_image.set_defaults(handler=command_prepare_image)
    prepare_hybrid = commands.add_parser("prepare-hybrid-image")
    prepare_hybrid.add_argument("--image", required=True)
    prepare_hybrid.add_argument("--disk-bytes", type=int,
                                default=HYBRID_DISK_BYTES)
    prepare_hybrid.add_argument("--fat32-start-lba", type=int,
                                default=HYBRID_FAT32_START_LBA)
    prepare_hybrid.add_argument("--label", default=HYBRID_FAT32_LABEL)
    prepare_hybrid.set_defaults(handler=command_prepare_hybrid_image)
    inject_file_fat32 = commands.add_parser("inject-file-fat32")
    inject_file_fat32.add_argument("--file", required=True)
    inject_file_fat32.add_argument("--image", required=True)
    inject_file_fat32.add_argument("--path", required=True)
    inject_file_fat32.add_argument("--fat-name")
    inject_file_fat32.add_argument("--fat32-start-lba", type=int,
                                   default=HYBRID_FAT32_START_LBA)
    inject_file_fat32.add_argument("--replace", action="store_true")
    inject_file_fat32.set_defaults(handler=command_inject_file_fat32)
    inject_files_fat32 = commands.add_parser("inject-files-fat32")
    inject_files_fat32.add_argument("--image", required=True)
    inject_files_fat32.add_argument("--entry", action="append", required=True)
    inject_files_fat32.add_argument("--fat32-start-lba", type=int,
                                    default=HYBRID_FAT32_START_LBA)
    inject_files_fat32.add_argument("--replace", action="store_true")
    inject_files_fat32.set_defaults(handler=command_inject_files_fat32)
    demo = commands.add_parser("demo")
    demo.add_argument("--output", required=True)
    demo.add_argument("--image", required=True)
    demo.set_defaults(handler=command_demo)
    fixtures_store = commands.add_parser("fixtures-store")
    fixtures_store.add_argument("--output-dir", required=True)
    fixtures_store.set_defaults(handler=command_fixtures_store)
    audit_store = commands.add_parser("audit-store")
    audit_store.add_argument("--fixtures-dir", required=True)
    audit_store.add_argument("--image")
    audit_store.set_defaults(handler=command_audit_store)
    fixtures_store_as2 = commands.add_parser("fixtures-store-as2")
    fixtures_store_as2.add_argument("--output-dir", required=True)
    fixtures_store_as2.set_defaults(handler=command_fixtures_store_as2)
    audit_store_as2 = commands.add_parser("audit-store-as2")
    audit_store_as2.add_argument("--fixtures-dir", required=True)
    audit_store_as2.add_argument("--image")
    audit_store_as2.set_defaults(handler=command_audit_store_as2)
    fixtures_store_as4 = commands.add_parser("fixtures-store-as4")
    fixtures_store_as4.add_argument("--output-dir", required=True)
    fixtures_store_as4.add_argument("--profile", required=True,
                                    choices=("seed", "update"))
    fixtures_store_as4.set_defaults(handler=command_fixtures_store_as4)
    audit_store_as4 = commands.add_parser("audit-store-as4")
    audit_store_as4.add_argument("--fixtures-dir", required=True)
    audit_store_as4.add_argument("--image")
    audit_store_as4.set_defaults(handler=command_audit_store_as4)
    sign_store_as5 = commands.add_parser("sign-store-as5")
    sign_store_as5.add_argument("--profile", required=True,
                                choices=STORE_AS5_PROFILES)
    sign_store_as5.add_argument("--private", required=True)
    sign_store_as5.add_argument("--public", required=True)
    sign_store_as5.add_argument("--output-dir", required=True)
    sign_store_as5.set_defaults(handler=command_sign_store_as5)
    audit_store_as5 = commands.add_parser("audit-store-as5")
    audit_store_as5.add_argument("--fixtures-dir", required=True)
    audit_store_as5.add_argument("--public", required=True)
    audit_store_as5.add_argument("--header")
    audit_store_as5.set_defaults(handler=command_audit_store_as5)
    serve_store_as5 = commands.add_parser("serve-store-as5")
    serve_store_as5.add_argument("--fixtures-dir", required=True)
    serve_store_as5.add_argument("--profile", choices=STORE_AS5_PROFILES,
                                 default="update")
    serve_store_as5.add_argument("--bind", default="0.0.0.0")
    serve_store_as5.add_argument("--port", type=int, default=8000)
    serve_store_as5.set_defaults(handler=command_serve_store_as5)
    selftest = commands.add_parser("selftest")
    selftest.set_defaults(handler=lambda arguments: run_selftest())
    return parser


def main() -> int:
    """Executa o subcomando solicitado e traduz falhas para erro controlado."""
    arguments = build_parser().parse_args()
    try:
        return arguments.handler(arguments)
    except PackageError as error:
        print(f"Packager: ERRO: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
