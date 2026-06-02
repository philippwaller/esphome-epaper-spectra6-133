"""Regression tests for the ESPHome version helper CLI.

The module is imported by path because the script filename contains a hyphen,
and tests mock PyPI responses so version filtering stays deterministic.
"""

from __future__ import annotations

import argparse
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch
from urllib.error import HTTPError


ROOT_DIR = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT_DIR / "scripts" / "esphome-versions.py"

SPEC = importlib.util.spec_from_file_location("esphome_versions", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {SCRIPT_PATH}")

MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeResponse(io.BytesIO):
    """Minimal context-managed response object for mocked urlopen calls."""

    def __enter__(self) -> FakeResponse:
        """Return the in-memory response for use in a with-statement."""
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        """Close the in-memory response when the with-statement exits."""
        self.close()


class EsphomeVersionsScriptTests(unittest.TestCase):
    """Coverage for the PyPI-backed version helper script."""

    def mock_pypi(self, releases: dict[str, list[object]]) -> FakeResponse:
        """Build a fake PyPI JSON response containing the supplied releases."""
        payload = json.dumps({"releases": releases}).encode("utf-8")
        return FakeResponse(payload)

    def test_parse_requirement_preserves_full_specifier_string(self) -> None:
        """Preserve compound specifiers exactly while formatting display text."""
        with tempfile.TemporaryDirectory() as temp_dir:
            requirements_file = Path(temp_dir) / "requirements.txt"
            requirements_file.write_text(
                textwrap.dedent(
                    """\
                    demo>=1.2,<2,!=1.5
                    other==3.0
                    """
                ),
                encoding="utf-8",
            )

            specifier, display = MODULE.parse_requirement("demo", requirements_file)

        self.assertEqual(specifier, ">=1.2,<2,!=1.5")
        self.assertEqual(display, ">= 1.2, < 2, != 1.5")

    def test_matching_versions_filters_and_sorts_stable_releases(self) -> None:
        """Ignore invalid and prerelease versions unless explicitly requested."""
        releases = {
            "2026.2.0": [],
            "2026.1.1": [],
            "invalid": [],
            "2026.2.0rc1": [],
            "2026.1.0": [],
            "2027.0.0": [],
        }
        with patch.object(MODULE, "urlopen", return_value=self.mock_pypi(releases)):
            versions = MODULE.matching_versions("esphome", ">=2026.1.0,<2027.0.0")

        self.assertEqual(versions, ["2026.1.0", "2026.1.1", "2026.2.0"])

    def test_matching_versions_can_include_prereleases(self) -> None:
        """Allow prerelease matches when the query opts into prereleases."""
        releases = {
            "2026.2.0rc1": [],
            "2026.2.0": [],
        }
        with patch.object(MODULE, "urlopen", return_value=self.mock_pypi(releases)):
            versions = MODULE.matching_versions(
                "esphome",
                ">=2026.2.0rc1,<2026.3.0",
                include_prereleases=True,
            )

        self.assertEqual(versions, ["2026.2.0rc1", "2026.2.0"])

    def test_matching_versions_reports_missing_package(self) -> None:
        """Convert a PyPI 404 into a clear missing-package runtime error."""
        error = HTTPError(
            url="https://pypi.org/pypi/missing/json",
            code=404,
            msg="Not Found",
            hdrs=None,
            fp=None,
        )

        with patch.object(MODULE, "urlopen", side_effect=error):
            with self.assertRaisesRegex(RuntimeError, "not found on PyPI"):
                MODULE.matching_versions("missing", ">=1")

    def test_validate_versions_default_uses_latest_patch_per_minor(self) -> None:
        """Keep the validation matrix lean while covering every minor release."""
        versions = MODULE.validate_versions_default(
            [
                "2026.1.0",
                "2026.1.4",
                "2026.1.5",
                "2026.2.0",
                "2026.2.3",
                "2026.3.1",
            ]
        )

        self.assertEqual(versions, ["2026.1.0", "2026.1.5", "2026.2.3", "2026.3.1"])

    def test_versions_for_mode_rejects_empty_full_modes(self) -> None:
        """Ensure versions_for_mode rejects empty full mode results."""
        with self.assertRaisesRegex(
            ValueError, "No versions resolved for mode validate-full"
        ):
            MODULE.versions_for_mode([], "validate-full")

        with self.assertRaisesRegex(
            ValueError, "No versions resolved for mode compile-full"
        ):
            MODULE.versions_for_mode([], "compile-full")

    def test_apply_managed_specifier_sync_replaces_plain_and_encoded_matches(
        self,
    ) -> None:
        """Update badges and table rows inside marked managed regions."""
        readme = textwrap.dedent(
            f"""\
            Intro
            {MODULE.VERSION_SPECIFIER_LINE_MARKER}
            [![ESPHome](https://img.shields.io/badge/ESPHome-%3E%3D%202025.9.4-000000?logo=esphome&logoColor=white)](https://esphome.io)

            {MODULE.VERSION_SPECIFIER_BLOCK_START}
            | What | Details |
            | ------ | --------- |
            | **ESPHome** | >= 2025.9.4 |
            | **Board** | ESP32 with PSRAM |
            {MODULE.VERSION_SPECIFIER_BLOCK_END}
            """
        )

        with patch.object(
            MODULE,
            "fetch_package_versions",
            side_effect=AssertionError("unexpected PyPI fetch"),
        ):
            plain_pattern, encoded_pattern = MODULE.build_specifier_patterns(
                "esphome",
                ">=2026.1.0,<2026.3.0",
            )

        rendered = MODULE.apply_managed_specifier_sync(
            readme,
            plain_pattern=plain_pattern,
            encoded_pattern=encoded_pattern,
            specifier_display=">= 2026.1.0, < 2026.3.0",
        )

        self.assertIn(">= 2026.1.0, < 2026.3.0", rendered)
        self.assertIn(
            "[![ESPHome](https://img.shields.io/badge/ESPHome-%3E%3D%202026.1.0%2C%20%3C%202026.3.0-000000?logo=esphome&logoColor=white)]",
            rendered,
        )
        self.assertIn("| **ESPHome** | >= 2026.1.0, < 2026.3.0 |", rendered)
        self.assertIn("| **Board** | ESP32 with PSRAM |", rendered)
        self.assertNotIn("2025.9.4", rendered)

    def test_apply_managed_specifier_sync_can_replace_old_shape(self) -> None:
        """Support older managed block layouts that only contain plain text."""
        contributing = textwrap.dedent(
            f"""\
            Intro
            {MODULE.VERSION_SPECIFIER_BLOCK_START}
            | **ESPHome** | >= 2025.9.4, < 2026.1.0 |
            | **Shell** | Bash-compatible shell |
            {MODULE.VERSION_SPECIFIER_BLOCK_END}
            """
        )

        with patch.object(
            MODULE,
            "fetch_package_versions",
            side_effect=AssertionError("unexpected PyPI fetch"),
        ):
            plain_pattern, encoded_pattern = MODULE.build_specifier_patterns(
                "esphome",
                ">=2026.1.0",
            )

        rendered = MODULE.apply_managed_specifier_sync(
            contributing,
            plain_pattern=plain_pattern,
            encoded_pattern=encoded_pattern,
            specifier_display=">= 2026.1.0",
        )

        self.assertIn("| **ESPHome** | >= 2026.1.0 |", rendered)
        self.assertIn("| **Shell** | Bash-compatible shell |", rendered)
        self.assertNotIn(">= 2025.9.4, < 2026.1.0", rendered)

    def test_apply_managed_specifier_sync_replaces_arbitrary_equality_literals(
        self,
    ) -> None:
        """Replace arbitrary equality literals without overmatching badge suffixes."""
        readme = textwrap.dedent(
            f"""\
            Intro
            {MODULE.VERSION_SPECIFIER_LINE_MARKER}
            [![ESPHome](https://img.shields.io/badge/ESPHome-%3D%3D%3D%20foo-bar%2Frc%5B1%5D%40local%3Atag-000000?logo=esphome&logoColor=white)](https://esphome.io)

            {MODULE.VERSION_SPECIFIER_BLOCK_START}
            ESPHome === foo-bar/rc[1]@local:tag
            {MODULE.VERSION_SPECIFIER_BLOCK_END}
            """
        )

        with patch.object(
            MODULE,
            "fetch_package_versions",
            side_effect=AssertionError("unexpected PyPI fetch"),
        ):
            plain_pattern, encoded_pattern = MODULE.build_specifier_patterns(
                "esphome",
                "===foo-baz/rc[2]@local:tag",
            )

        rendered = MODULE.apply_managed_specifier_sync(
            readme,
            plain_pattern=plain_pattern,
            encoded_pattern=encoded_pattern,
            specifier_display="=== foo-baz/rc[2]@local:tag",
        )

        self.assertIn("ESPHome === foo-baz/rc[2]@local:tag", rendered)
        self.assertIn(
            "ESPHome-%3D%3D%3D%20foo-baz%2Frc%5B2%5D%40local%3Atag-000000?",
            rendered,
        )
        self.assertNotIn("foo-bar", rendered)

    def test_replace_marked_block_requires_balanced_markers(self) -> None:
        """Reject managed blocks that are missing their closing marker."""
        with self.assertRaisesRegex(ValueError, "missing"):
            MODULE.replace_marked_block(
                f"{MODULE.VERSION_SPECIFIER_BLOCK_START}\n| **ESPHome** | >= 2025.9.4 |\n",
                lambda value: value,
            )

    def test_replace_marked_line_requires_following_line(self) -> None:
        """Reject a single-line marker that has no managed line to replace."""
        with self.assertRaisesRegex(ValueError, "must be followed"):
            MODULE.replace_marked_line(
                MODULE.VERSION_SPECIFIER_LINE_MARKER,
                lambda value: value,
            )

    def test_apply_managed_specifier_sync_requires_replaceable_content(self) -> None:
        """Require managed regions to contain a recognizable specifier."""
        content = textwrap.dedent(
            f"""\
            Intro
            {MODULE.VERSION_SPECIFIER_BLOCK_START}
            | **ESPHome** | latest |
            {MODULE.VERSION_SPECIFIER_BLOCK_END}
            """
        )

        with patch.object(
            MODULE,
            "fetch_package_versions",
            side_effect=AssertionError("unexpected PyPI fetch"),
        ):
            plain_pattern, encoded_pattern = MODULE.build_specifier_patterns(
                "esphome",
                ">=2026.1.0",
            )

        with self.assertRaisesRegex(ValueError, "replaceable specifier"):
            MODULE.apply_managed_specifier_sync(
                content,
                plain_pattern=plain_pattern,
                encoded_pattern=encoded_pattern,
                specifier_display=">= 2026.1.0",
            )

    def test_resolve_query_uses_requirement_default(self) -> None:
        """Read the default package and specifier from a requirements file."""
        with tempfile.TemporaryDirectory() as temp_dir:
            requirements_file = Path(temp_dir) / "requirements.txt"
            requirements_file.write_text(
                textwrap.dedent(
                    """\
                    esphome>=2026.1.0,<2026.3.0
                    Pillow>=10.4.0,<13
                    """
                ),
                encoding="utf-8",
            )
            query = MODULE.resolve_query(
                package_name=None,
                specifier=None,
                include_prereleases=False,
                requirements_file=requirements_file,
            )

        self.assertEqual(query.package_name, "esphome")
        self.assertEqual(query.specifier, ">=2026.1.0,<2026.3.0")
        self.assertEqual(query.specifier_display, ">= 2026.1.0, < 2026.3.0")

    def test_emit_matrix_uses_resolved_versions(self) -> None:
        """Build compile matrices from resolved versions and smoke-test configs."""
        args = argparse.Namespace(
            package=None,
            specifier=">=2026.1.0,<2026.3.0",
            include_prereleases=False,
            requirements_file=ROOT_DIR / "requirements.txt",
            mode="compile-default",
        )
        with patch.object(
            MODULE, "matching_versions", return_value=["2026.1.0", "2026.2.1"]
        ):
            with patch("sys.stdout", new_callable=io.StringIO) as stdout:
                MODULE.emit_matrix(args)

        self.assertEqual(
            json.loads(stdout.getvalue()),
            {
                "include": [
                    {"esphome": "2026.1.0", "config": "configs/hello-world.yaml"},
                    {"esphome": "2026.1.0", "config": "configs/image.yaml"},
                    {"esphome": "2026.1.0", "config": "configs/test-sheet.yaml"},
                    {"esphome": "2026.2.1", "config": "configs/hello-world.yaml"},
                    {"esphome": "2026.2.1", "config": "configs/image.yaml"},
                    {"esphome": "2026.2.1", "config": "configs/test-sheet.yaml"},
                ]
            },
        )

    def test_validate_matrix_only_includes_esphome_versions(self) -> None:
        """Ensure validate matrices contain only ESPHome versions."""
        matrix = json.loads(MODULE.matrix_for_versions(["2026.1.0", "2026.2.1"]))

        self.assertEqual(
            matrix,
            {"include": [{"esphome": "2026.1.0"}, {"esphome": "2026.2.1"}]},
        )

    def test_compile_matrix_includes_each_smoke_test_config(self) -> None:
        """Ensure compile matrices include every smoke test config."""
        matrix = json.loads(
            MODULE.compile_matrix_for_versions(["2026.1.0", "2026.2.1"])
        )

        self.assertEqual(
            matrix,
            {
                "include": [
                    {"esphome": "2026.1.0", "config": "configs/hello-world.yaml"},
                    {"esphome": "2026.1.0", "config": "configs/image.yaml"},
                    {"esphome": "2026.1.0", "config": "configs/test-sheet.yaml"},
                    {"esphome": "2026.2.1", "config": "configs/hello-world.yaml"},
                    {"esphome": "2026.2.1", "config": "configs/image.yaml"},
                    {"esphome": "2026.2.1", "config": "configs/test-sheet.yaml"},
                ]
            },
        )

    def test_cli_resolve_uses_default_requirement(self) -> None:
        """Exercise the resolve subcommand with the default requirement lookup."""
        with tempfile.TemporaryDirectory() as temp_dir:
            requirements_file = Path(temp_dir) / "requirements.txt"
            requirements_file.write_text(
                "esphome>=2026.1.0,<2026.3.0\n",
                encoding="utf-8",
            )

            with patch.object(MODULE, "matching_versions", return_value=["2026.1.0"]):
                with patch.object(
                    sys,
                    "argv",
                    [
                        "esphome-versions.py",
                        "resolve",
                        "--requirements-file",
                        str(requirements_file),
                    ],
                ):
                    with patch("sys.stdout", new_callable=io.StringIO) as stdout:
                        MODULE.main()

        self.assertEqual(stdout.getvalue().strip(), "2026.1.0")


if __name__ == "__main__":
    unittest.main()
