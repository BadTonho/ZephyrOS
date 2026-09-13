"""Parsing and aggregation helpers for the PERF1 metric envelope."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import time
from typing import Any, Iterable


MACHINE_PREFIX = "@@ZMETRIC/1 "
REPORT_SCHEMA = "zephyros-perf1-baseline-v1"
MATRIX_SCHEMA = "zephyros-perf1-baseline-matrix-v1"
UINT32_MAX = 0xFFFFFFFF


class MetricsError(ValueError):
    """Raised when the guest metric protocol is not complete or valid."""


def _fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line[len(MACHINE_PREFIX):].split():
        if "=" not in token:
            raise MetricsError(f"token_invalido:{token}")
        key, value = token.split("=", 1)
        if not key or not value:
            raise MetricsError("campo_vazio")
        if key in fields:
            raise MetricsError(f"chave_duplicada:{key}")
        fields[key] = value
    if "record" not in fields:
        raise MetricsError("record_ausente")
    return fields


def _uint32(value: str, field: str) -> int:
    if not value.isdigit():
        raise MetricsError(f"{field}_invalido:{value}")
    parsed = int(value, 10)
    if parsed > UINT32_MAX:
        raise MetricsError(f"{field}_overflow:{value}")
    return parsed


def _validate_metric(fields: dict[str, str]) -> None:
    required = (
        "metric", "value", "unit", "kind", "source", "context",
        "status", "resolution", "overflow",
    )
    for key in required:
        if key not in fields:
            raise MetricsError(f"metric_{key}_ausente")
    if fields["status"] not in {"ok", "unavailable"}:
        raise MetricsError(f"status_invalido:{fields['status']}")
    if fields["kind"] not in {"counter", "gauge", "bytes", "duration", "state"}:
        raise MetricsError(f"kind_invalido:{fields['kind']}")
    if fields["overflow"] not in {"wrap_u32", "none"}:
        raise MetricsError(f"overflow_invalido:{fields['overflow']}")
    if fields["kind"] in {"counter", "bytes"} and fields["overflow"] != "wrap_u32":
        raise MetricsError("overflow_counter_invalido")
    if fields["kind"] not in {"counter", "bytes"} and fields["overflow"] != "none":
        raise MetricsError("overflow_non_counter_invalido")
    if fields["status"] == "unavailable" and fields["value"] != "ND":
        raise MetricsError("nd_ausente")
    if fields["status"] == "ok" and fields["value"] == "ND":
        raise MetricsError("nd_incompativel_com_ok")
    if fields["value"] != "ND":
        _uint32(fields["value"], "value")
    if _uint32(fields["resolution"], "resolution") == 0:
        raise MetricsError("resolution_invalida")


def parse_machine_records(text: str) -> list[dict[str, Any]]:
    if not isinstance(text, str):
        raise MetricsError("entrada_invalida")

    records: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    sequences: set[int] = set()
    for raw_line in text.splitlines():
        if not raw_line.startswith(MACHINE_PREFIX):
            continue
        fields = _fields(raw_line.rstrip("\r"))
        record_type = fields["record"]
        if record_type == "begin":
            if current is not None:
                raise MetricsError("begin_aninhado")
            if "seq" not in fields:
                raise MetricsError("seq_ausente")
            sequence = _uint32(fields["seq"], "seq")
            if sequence in sequences:
                raise MetricsError(f"seq_duplicado:{sequence}")
            sequences.add(sequence)
            current = {
                "seq": sequence,
                "baseline": fields.get("baseline", "ND"),
                "source": fields.get("source", "ND"),
                "metrics": {},
            }
            continue
        if record_type == "metric":
            if current is None:
                raise MetricsError("metric_sem_begin")
            _validate_metric(fields)
            name = fields["metric"]
            if name in current["metrics"]:
                raise MetricsError(f"metric_duplicada:{name}")
            current["metrics"][name] = fields
            continue
        if record_type == "end":
            if current is None:
                raise MetricsError("end_sem_begin")
            if "seq" not in fields:
                raise MetricsError("seq_end_ausente")
            sequence = _uint32(fields["seq"], "seq")
            if sequence != current["seq"]:
                raise MetricsError("seq_desalinhado")
            status = fields.get("status")
            if status not in {"ok", "partial"}:
                raise MetricsError(f"status_end_invalido:{status}")
            current["status"] = status
            records.append(current)
            current = None
            continue
        raise MetricsError(f"record_desconhecido:{record_type}")

    if current is not None:
        raise MetricsError("envelope_incompleto")
    if not records:
        raise MetricsError("envelope_ausente")
    return records


def parse_machine_file(path: Path) -> list[dict[str, Any]]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise MetricsError(f"serial_indisponivel:{error}") from error
    return parse_machine_records(text)


def _numeric_value(metric: dict[str, str]) -> int | None:
    if metric["value"] == "ND":
        return None
    return _uint32(metric["value"], "value")


def aggregate_records(records: Iterable[dict[str, Any]]) -> dict[str, Any]:
    metric_values: dict[str, dict[str, Any]] = {}
    record_list = list(records)
    for record in record_list:
        for name, metric in record.get("metrics", {}).items():
            entry = metric_values.setdefault(
                name,
                {
                    "unit": metric["unit"],
                    "kind": metric["kind"],
                    "source": metric["source"],
                    "context": metric["context"],
                    "values": [],
                    "unavailable": 0,
                },
            )
            if any(entry[key] != metric[key] for key in
                   ("unit", "kind", "source", "context")):
                raise MetricsError(f"metadata_inconsistente:{name}")
            value = _numeric_value(metric)
            if value is None:
                entry["unavailable"] += 1
            else:
                entry["values"].append(value)

    for entry in metric_values.values():
        values = sorted(entry["values"])
        entry["count"] = len(values)
        if values:
            entry["min"] = values[0]
            entry["max"] = values[-1]
            entry["median"] = values[(len(values) - 1) // 2]
        else:
            entry["min"] = "ND"
            entry["max"] = "ND"
            entry["median"] = "ND"

    return {
        "schema": "zephyros-perf1-metrics-aggregate-v1",
        "record_count": len(record_list),
        "metrics": metric_values,
    }


def _file_metadata(image: Path) -> dict[str, Any]:
    if not image.is_file():
        return {"path": str(image), "size_bytes": "ND", "sha256": "ND"}
    digest = hashlib.sha256()
    size = 0
    with image.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return {"path": str(image), "size_bytes": size, "sha256": digest.hexdigest()}


def _latency_summary(aggregate: dict[str, Any]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for name, metric in aggregate["metrics"].items():
        if metric["kind"] == "duration" or "latency" in name or name.endswith("_ticks"):
            output[name] = {
                "unit": metric["unit"],
                "min": metric["min"],
                "max": metric["max"],
                "median": metric["median"],
            }
    return output


def build_report(
    image: Path,
    profile: str,
    mode: str,
    iteration: int,
    records: list[dict[str, Any]],
    host: dict[str, Any],
    guest_result: dict[str, Any] | None = None,
    wall_time_seconds: float | str = "ND",
    sample_interval_seconds: float | str = "ND",
) -> dict[str, Any]:
    aggregate = aggregate_records(records)
    guest = {
        "samples": records,
        "aggregate": aggregate,
        "result": guest_result if guest_result is not None else {"status": "ND"},
    }
    return {
        "schema": REPORT_SCHEMA,
        "version": 1,
        "image": _file_metadata(image),
        "profile": profile,
        "mode": mode,
        "iteration": iteration,
        "guest": guest,
        "observations": {
            "ticks": {
                name: metric
                for name, metric in aggregate["metrics"].items()
                if metric["unit"] == "tick"
            },
            "queues": {
                name: metric
                for name, metric in aggregate["metrics"].items()
                if "queue" in name or "pending" in name
            },
            "memory": {
                name: metric
                for name, metric in aggregate["metrics"].items()
                if name.startswith("memory_") or name.startswith("paging_")
            },
            "latencies": _latency_summary(aggregate),
        },
        "host": {
            "qemu_process": host,
            "sample_count": host.get("sample_count", "ND"),
            "sample_interval_seconds": sample_interval_seconds,
            "wall_time_seconds": wall_time_seconds,
        },
    }


def build_matrix_report(
    image: Path,
    profile: str,
    runs: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema": MATRIX_SCHEMA,
        "version": 1,
        "image": _file_metadata(image),
        "profile": profile,
        "modes": ["simple", "classic"],
        "iterations_per_mode": 3,
        "runs": runs,
    }


def _nd_process_sample(timestamp: float) -> dict[str, Any]:
    return {
        "monotonic_seconds": timestamp,
        "user_seconds": "ND",
        "system_seconds": "ND",
        "rss_bytes": "ND",
        "peak_rss_bytes": "ND",
    }


def _sample_posix(pid: int, timestamp: float) -> dict[str, Any]:
    try:
        stat_text = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        stat_fields = stat_text.rsplit(")", 1)[1].split()
        ticks = os.sysconf("SC_CLK_TCK")
        user_seconds = int(stat_fields[11]) / ticks
        system_seconds = int(stat_fields[12]) / ticks
        status = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
        memory: dict[str, int] = {}
        for line in status.splitlines():
            key, separator, value = line.partition(":")
            if separator and key in {"VmRSS", "VmHWM"}:
                memory[key] = int(value.strip().split()[0]) * 1024
        return {
            "monotonic_seconds": timestamp,
            "user_seconds": user_seconds,
            "system_seconds": system_seconds,
            "rss_bytes": memory.get("VmRSS", "ND"),
            "peak_rss_bytes": memory.get("VmHWM", "ND"),
        }
    except (OSError, ValueError, IndexError, TypeError):
        return _nd_process_sample(timestamp)


def _sample_windows(pid: int, timestamp: float) -> dict[str, Any]:
    try:
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        handle = kernel32.OpenProcess(0x1000 | 0x0010, False, pid)
        if not handle:
            return _nd_process_sample(timestamp)

        class FileTime(ctypes.Structure):
            _fields_ = [("low", wintypes.DWORD), ("high", wintypes.DWORD)]

        class MemoryCounters(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        creation = FileTime()
        exit_time = FileTime()
        kernel_time = FileTime()
        user_time = FileTime()
        counters = MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        valid_times = kernel32.GetProcessTimes(
            handle, ctypes.byref(creation), ctypes.byref(exit_time),
            ctypes.byref(kernel_time), ctypes.byref(user_time),
        )
        valid_memory = psapi.GetProcessMemoryInfo(
            handle, ctypes.byref(counters), ctypes.sizeof(counters),
        )
        kernel32.CloseHandle(handle)
        if not valid_times and not valid_memory:
            return _nd_process_sample(timestamp)

        def file_time_seconds(value: FileTime) -> float:
            return ((value.high << 32) | value.low) / 10_000_000.0

        return {
            "monotonic_seconds": timestamp,
            "user_seconds": file_time_seconds(user_time) if valid_times else "ND",
            "system_seconds": file_time_seconds(kernel_time) if valid_times else "ND",
            "rss_bytes": counters.WorkingSetSize if valid_memory else "ND",
            "peak_rss_bytes": counters.PeakWorkingSetSize if valid_memory else "ND",
        }
    except (AttributeError, OSError, TypeError, ValueError):
        return _nd_process_sample(timestamp)


def sample_process(pid: int) -> dict[str, Any]:
    timestamp = time.monotonic()
    if os.name == "nt":
        return _sample_windows(pid, timestamp)
    return _sample_posix(pid, timestamp)


def summarize_process_samples(
    samples: list[dict[str, Any]],
    wall_time_seconds: float | str = "ND",
) -> dict[str, Any]:
    if not samples:
        return {
            "wall_time_seconds": wall_time_seconds,
            "user_seconds": "ND",
            "system_seconds": "ND",
            "rss_bytes": "ND",
            "peak_rss_bytes": "ND",
            "sample_count": 0,
        }

    def delta(field: str) -> float | str:
        values = [item[field] for item in samples if isinstance(item[field], (int, float))]
        if len(values) < 2:
            return "ND"
        return max(0, values[-1] - values[0])

    rss = [item["rss_bytes"] for item in samples if isinstance(item["rss_bytes"], int)]
    peaks = [item["peak_rss_bytes"] for item in samples if isinstance(item["peak_rss_bytes"], int)]
    return {
        "wall_time_seconds": wall_time_seconds,
        "user_seconds": delta("user_seconds"),
        "system_seconds": delta("system_seconds"),
        "rss_bytes": rss[-1] if rss else "ND",
        "peak_rss_bytes": max(peaks + rss) if peaks or rss else "ND",
        "sample_count": len(samples),
    }


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
