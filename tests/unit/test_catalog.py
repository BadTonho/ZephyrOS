import unittest
import json
import tempfile
from unittest.mock import patch
from pathlib import Path

from tools import test_catalog


def sample_case():
    return {
        "id": "host:coverage:sample",
        "scenario": "sample",
        "owner": "quality",
        "layer": "host",
        "executor": "host",
        "profile": "fixture",
        "timeout_seconds": 5,
        "heartbeat_timeout_seconds": 1,
        "isolation": "fixture",
        "parameters": {},
        "preconditions": "ready",
        "action": "run",
        "expected": "pass",
        "errors": "failure",
        "effects": "none",
        "cleanup": "none",
        "status": "AUTOMATED",
        "surface_ids": ["c:src/core/sample.c:sample"],
    }


def sample_surface(case_ids=None):
    return {
        "id": "c:src/core/sample.c:sample",
        "kind": "c_function",
        "source": "src/core/sample.c",
        "symbol": "sample",
        "linkage": "external",
        "owner": "core",
        "layer": "kernel-source",
        "preconditions": "ready",
        "action": "run",
        "expected": "pass",
        "errors": "failure",
        "effects": "none",
        "cleanup": "none",
        "status": "COVERED",
        "coverage_mode": "direct",
        "case_ids": case_ids if case_ids is not None else ["host:coverage:sample"],
        "tags": [],
    }


def sample_catalog(case_ids=None, surface_ids=None):
    case = sample_case()
    if surface_ids is not None:
        case["surface_ids"] = surface_ids
    return {
        "schema": test_catalog.SCHEMA,
        "metadata": {
            "source_root": "src",
            "vendor_policy": "excluded",
            "assembly_policy": "entries",
            "identity_policy": "stable",
            "strict_policy": "PENDING_and_BLOCKED_are_blocking",
            "surface_statuses": sorted(test_catalog.SURFACE_STATUSES),
            "case_statuses": sorted(test_catalog.CASE_STATUSES),
        },
        "surfaces": [sample_surface(case_ids)],
        "cases": [case],
        "retired": [],
    }


class CatalogContractTests(unittest.TestCase):
    def test_registry_accepts_explicit_direct_coverage(self):
        catalog = sample_catalog()
        registry = {
            "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
            "entries": [{
                "id": "sample",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_ids": ["c:src/core/sample.c:sample"],
                "evidence": "unit",
            }],
        }
        self.assertEqual(test_catalog.validate_coverage_registry(
            registry, catalog, strict=True), [])

    def test_registry_rejects_uncovered_surface(self):
        catalog = sample_catalog()
        catalog["surfaces"][0]["status"] = "PENDING"
        registry = {
            "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
            "entries": [{
                "id": "sample",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_ids": ["c:src/core/sample.c:sample"],
                "evidence": "unit",
            }],
        }
        errors = test_catalog.validate_coverage_registry(
            registry, catalog, strict=True)
        self.assertTrue(any("nao coberta" in error for error in errors))

    def test_bidirectional_links_are_required(self):
        catalog = sample_catalog(case_ids=[])
        with patch.object(test_catalog, "discover_surfaces", return_value=[
                {"id": "c:src/core/sample.c:sample"}]):
            errors = test_catalog.validate_catalog(catalog, test_catalog.ROOT)
        self.assertTrue(any("vinculo reverso ausente" in error for error in errors))

    def test_sync_repairs_explicit_case_links(self):
        catalog = sample_catalog(case_ids=[])
        test_catalog.synchronize_coverage_links(catalog)
        self.assertEqual(
            catalog["surfaces"][0]["case_ids"], ["host:coverage:sample"])

    def test_reset_coverage_links_discards_stale_associations(self):
        catalog = sample_catalog()
        test_catalog.reset_coverage_links(catalog)
        self.assertEqual(catalog["cases"][0]["surface_ids"], [])
        self.assertEqual(catalog["surfaces"][0]["case_ids"], [])
        self.assertEqual(catalog["surfaces"][0]["status"], "PENDING")
        self.assertNotIn("coverage_mode", catalog["surfaces"][0])

    def test_registry_case_definition_is_added_to_catalog(self):
        catalog = sample_catalog()
        definition = sample_case()
        definition["id"] = "host:coverage:defined"
        definition["surface_ids"] = ["c:src/core/sample.c:sample"]
        registry = {
            "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
            "entries": [{
                "id": "sample-definition",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:defined"],
                "surface_ids": ["c:src/core/sample.c:sample"],
                "case_definition": definition,
                "evidence": "unit",
            }],
        }
        test_catalog.apply_registry_case_definitions(catalog, registry)
        self.assertEqual(catalog["cases"][-1], definition)

    def test_strict_rejects_pending_surface(self):
        catalog = sample_catalog()
        catalog["surfaces"][0]["status"] = "PENDING"
        with patch.object(test_catalog, "discover_surfaces", return_value=[
                {"id": "c:src/core/sample.c:sample"}]):
            errors = test_catalog.validate_catalog(
                catalog, test_catalog.ROOT, strict=True)
        self.assertTrue(any("cobertura pendente" in error for error in errors))

    def test_registry_selector_expands_case_surfaces(self):
        catalog = sample_catalog()
        registry = {
            "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
            "entries": [{
                "id": "sample-selector",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_selector": "case_surface_ids",
                "evidence": "unit",
            }],
        }
        self.assertEqual(test_catalog.validate_coverage_registry(
            registry, catalog, strict=True), [])

    def test_registry_accepts_real_direct_and_integration_evidence(self):
        catalog = sample_catalog()
        registry = {
            "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
            "entries": [
                {
                    "id": "sample-integration",
                    "domain": "core",
                    "owner": "quality",
                    "executor": "host",
                    "coverage_mode": "integration",
                    "case_ids": ["host:coverage:sample"],
                    "surface_ids": ["c:src/core/sample.c:sample"],
                    "evidence": "integration",
                },
                {
                    "id": "sample-direct",
                    "domain": "core",
                    "owner": "quality",
                    "executor": "host",
                    "coverage_mode": "direct",
                    "case_ids": ["host:coverage:sample"],
                    "surface_ids": ["c:src/core/sample.c:sample"],
                    "evidence": "direct",
                },
            ],
        }
        self.assertEqual(test_catalog.validate_coverage_registry(
            registry, catalog, strict=True), [])

    def test_registry_coverage_report_selects_only_observed_sources(self):
        catalog = sample_catalog()
        second = sample_surface(["host:coverage:sample"])
        second["id"] = "c:src/core/other.c:other"
        second["source"] = "src/core/other.c"
        second["symbol"] = "other"
        catalog["surfaces"].append(second)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "coverage.json"
            report.write_text(json.dumps({
                "status": "PASS",
                "covered_surface_ids": [
                    "c:src/core/sample.c:sample",
                    "c:src/core/other.c:other",
                ],
                "unknown_addresses": [],
                "ambiguous_addresses": [],
            }), encoding="utf-8")
            entry = {
                "id": "sample-report",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_selector": "coverage_report",
                "coverage_report": "coverage.json",
                "coverage_sources": ["src/core/sample.c"],
                "evidence": "coverage.json",
            }
            registry = {
                "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
                "entries": [entry],
            }
            self.assertEqual(test_catalog.registry_surface_ids(
                entry, {"host:coverage:sample": catalog["cases"][0]},
                {item["id"]: item for item in catalog["surfaces"]}, root),
                ["c:src/core/sample.c:sample"])

    def test_registry_coverage_report_uses_report_when_surface_list_is_empty(self):
        catalog = sample_catalog()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "coverage.json"
            report.write_text(json.dumps({
                "status": "PASS",
                "covered_surface_ids": ["c:src/core/sample.c:sample"],
                "unknown_addresses": [],
                "ambiguous_addresses": [],
            }), encoding="utf-8")
            entry = {
                "id": "sample-report-empty",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_ids": [],
                "surface_selector": "coverage_report",
                "coverage_report": "coverage.json",
                "evidence": "coverage.json",
            }
            self.assertEqual(test_catalog.registry_surface_ids(
                entry, {"host:coverage:sample": catalog["cases"][0]},
                {item["id"]: item for item in catalog["surfaces"]}, root),
                ["c:src/core/sample.c:sample"])

    def test_registry_coverage_report_links_public_api_by_observed_symbol(self):
        catalog = sample_catalog()
        api = sample_surface(["host:coverage:sample"])
        api["id"] = "api:src/include/apps/sample.h:sample"
        api["kind"] = "api_function"
        api["source"] = "src/include/apps/sample.h"
        api["linkage"] = "public"
        catalog["surfaces"].append(api)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "coverage.json"
            report.write_text(json.dumps({
                "status": "PASS",
                "covered_surface_ids": ["c:src/core/sample.c:sample"],
                "unknown_addresses": [],
                "ambiguous_addresses": [],
            }), encoding="utf-8")
            entry = {
                "id": "sample-public-api-report",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_selector": "coverage_report",
                "coverage_report": "coverage.json",
                "include_public_apis": True,
                "evidence": "coverage.json",
            }
            self.assertEqual(test_catalog.registry_surface_ids(
                entry, {"host:coverage:sample": catalog["cases"][0]},
                {item["id"]: item for item in catalog["surfaces"]}, root), [
                    "api:src/include/apps/sample.h:sample",
                "c:src/core/sample.c:sample",
            ])

    def test_discover_c_functions_includes_pointer_return_definition(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "pointer.c"
            source.write_text(
                "const char* sample_match(const char* value) {\n"
                "    return value;\n"
                "}\n", encoding="utf-8")
            surfaces = test_catalog.discover_c_functions(source, root)
        self.assertEqual([surface["symbol"] for surface in surfaces],
                         ["sample_match"])

    def test_discover_header_apis_includes_pointer_return_declaration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "pointer.h"
            header.write_text(
                "const char* sample_match(const char* value);\n",
                encoding="utf-8")
            surfaces = test_catalog.discover_header_apis(header, root)
        self.assertEqual([surface["symbol"] for surface in surfaces],
                         ["sample_match"])

    def test_registry_checks_each_explicit_surface_link(self):
        catalog = sample_catalog()
        second_id = "c:src/core/second.c:second"
        second_surface = sample_surface(["host:coverage:sample"])
        second_surface["id"] = second_id
        catalog["surfaces"].append(second_surface)
        catalog["cases"][0]["surface_ids"].append(second_id)
        registry = {
            "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
            "entries": [{
                "id": "sample-explicit",
                "domain": "core",
                "owner": "quality",
                "executor": "host",
                "coverage_mode": "direct",
                "case_ids": ["host:coverage:sample"],
                "surface_ids": [
                    "c:src/core/sample.c:sample", second_id],
                "evidence": "unit",
            }],
        }
        self.assertEqual(test_catalog.validate_coverage_registry(
            registry, catalog, strict=True), [])
        catalog["cases"][0]["surface_ids"].remove(second_id)
        errors = test_catalog.validate_coverage_registry(
            registry, catalog, strict=True)
        self.assertTrue(any(second_id in error and
                            "registro sem vinculo" in error
                            for error in errors))

    def test_registry_allows_multiple_reports_for_one_case(self):
        catalog = sample_catalog()
        second_id = "c:src/core/second.c:second"
        second_surface = sample_surface(["host:coverage:sample"])
        second_surface["id"] = second_id
        second_surface["source"] = "src/core/second.c"
        second_surface["symbol"] = "second"
        catalog["surfaces"].append(second_surface)
        catalog["cases"][0]["surface_ids"].append(second_id)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, surface_id in (("one.json", "c:src/core/sample.c:sample"),
                                     ("two.json", second_id)):
                (root / name).write_text(json.dumps({
                    "status": "PASS",
                    "covered_surface_ids": [surface_id],
                    "unknown_addresses": [],
                    "ambiguous_addresses": [],
                }), encoding="utf-8")
            entries = []
            for name, source in (("one.json", "src/core/sample.c"),
                                 ("two.json", "src/core/second.c")):
                entries.append({
                    "id": name,
                    "domain": "core",
                    "owner": "quality",
                    "executor": "host",
                    "coverage_mode": "direct",
                    "case_ids": ["host:coverage:sample"],
                    "surface_selector": "coverage_report",
                    "coverage_report": name,
                    "coverage_sources": [source],
                    "evidence": "coverage",
                })
            registry = {
                "schema": test_catalog.COVERAGE_REGISTRY_SCHEMA,
                "entries": entries,
            }
            self.assertEqual(test_catalog.validate_coverage_registry(
                registry, catalog, strict=True, root=root), [])

    def test_malformed_link_ids_are_reported_without_validator_crash(self):
        catalog = sample_catalog()
        catalog["cases"][0]["surface_ids"] = [[]]
        catalog["surfaces"][0]["case_ids"] = [[]]
        with patch.object(test_catalog, "discover_surfaces", return_value=[
                {"id": "c:src/core/sample.c:sample"}]):
            errors = test_catalog.validate_catalog(catalog, test_catalog.ROOT)
        self.assertTrue(any("id invalido" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
