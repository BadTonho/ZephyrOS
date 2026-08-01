#!/usr/bin/env python3
"""Empacotador host para o formato local .zephyrosapp do ZephyrOS."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path


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
ID_RE = re.compile(r"^[A-Z0-9_]{1,8}$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
REQUIRED_MANIFEST_KEYS = ("id", "name", "version", "api", "entry", "dependencies")
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
    api = ensure_ascii(data.get("api", "0.3"), "api", 7)
    if api != "0.3":
        raise PackageError("api deve ser 0.3")
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
    if values["api"] != "0.3" or values["entry"] != "APP.ZAP":
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
    api: str = "0.3",
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
        if image_path is not None and read_root_file(image_path, alias) != data:
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
        if image_path is not None and read_root_file(image_path, alias) != data:
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
        if image_path is not None and read_root_file(image_path, alias) != data:
            raise PackageError(f"fixture AS4 divergiu na imagem: {alias}")
        print(f"store_as4_fixture_{profile}_{alias} OK")
    print(f"App Store AS4 fixtures ({profile}): OK")


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
    manifest = {"id": "DEMO", "name": "Demo", "version": "1.0.0", "api": "0.3", "entry": "APP.ZAP", "dependencies": ""}
    package = build_package(manifest, build_demo_zapp())
    checks = {
        "criar": True,
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
    }
    try:
        parse_package(package)
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


def command_demo(arguments: argparse.Namespace) -> int:
    """Cria e injeta o demo usado na validacao manual da Fase 7."""
    manifest = {"id": "DEMO", "name": "Demo", "version": "1.0.0", "api": "0.3", "entry": "APP.ZAP", "dependencies": ""}
    package = build_package(manifest, build_demo_zapp())
    output = Path(arguments.output)
    output.write_bytes(package)
    inject_package(package, Path(arguments.image), "DEMO.ZPK")
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
