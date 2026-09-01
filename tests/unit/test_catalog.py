import unittest
from unittest.mock import patch

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
