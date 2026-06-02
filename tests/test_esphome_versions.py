"""Tests for scripts/esphome-versions.py."""

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
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


class EsphomeVersionsScriptTests(unittest.TestCase):
    """Coverage for the PyPI-backed version helper script."""

    def mock_pypi(self, releases: dict[str, list[object]]) -> FakeResponse:
        payload = json.dumps({"releases": releases}).encode("utf-8")
        return FakeResponse(payload)

    def test_parse_requirement_preserves_full_specifier_string(self) -> None:
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

    def test_apply_managed_specifier_sync_replaces_plain_and_encoded_matches(
        self,
    ) -> None:
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
        with self.assertRaisesRegex(ValueError, "missing"):
            MODULE.replace_marked_block(
                f"{MODULE.VERSION_SPECIFIER_BLOCK_START}\n| **ESPHome** | >= 2025.9.4 |\n",
                lambda value: value,
            )

    def test_replace_marked_line_requires_following_line(self) -> None:
        with self.assertRaisesRegex(ValueError, "must be followed"):
            MODULE.replace_marked_line(
                MODULE.VERSION_SPECIFIER_LINE_MARKER,
                lambda value: value,
            )

    def test_apply_managed_specifier_sync_requires_replaceable_content(self) -> None:
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
            stdout.getvalue().strip(),
            '{"include":[{"esphome":"2026.1.0"},{"esphome":"2026.2.1"}]}',
        )

    def test_cli_resolve_uses_default_requirement(self) -> None:
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
