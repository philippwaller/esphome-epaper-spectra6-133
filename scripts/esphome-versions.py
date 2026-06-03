#!/usr/bin/env python3
"""Resolve ESPHome package versions and keep support documentation in sync.

The script powers CI matrix generation from PyPI releases and updates managed
README/CONTRIBUTING snippets from the effective requirements.txt specifier.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

from packaging.requirements import Requirement
from packaging.specifiers import InvalidSpecifier, SpecifierSet
from packaging.version import InvalidVersion, Version

ROOT_DIR = Path(__file__).resolve().parents[1]
REQUIREMENTS_FILE = ROOT_DIR / "requirements.txt"
README_FILE = ROOT_DIR / "README.md"
CONTRIBUTING_FILE = ROOT_DIR / "CONTRIBUTING.md"
CONFIGS_DIR = ROOT_DIR / "configs"
DEFAULT_PACKAGE_NAME = "esphome"
PYPI_JSON_URL = "https://pypi.org/pypi/{package_name}/json"
DEFAULT_USER_AGENT = "python-package-version-resolver/1.0"

STANDALONE_CONFIG_EXTENSIONS = frozenset({".yaml", ".yml"})
EXCLUDED_STANDALONE_CONFIG_NAMES = frozenset(
    {
        "secrets.yaml",
        "secrets.yml",
        "secrets.example.yaml",
        "secrets.example.yml",
    }
)

MATRIX_MODES = (
    "validate",
    "validate-compatibility-window",
    "validate-all-supported-versions",
    "compile",
    "compile-compatibility-window",
    "compile-all-supported-versions",
)

VERSION_SPECIFIER_BLOCK_START = "<!-- x-esphome-version-specifier-start -->"
VERSION_SPECIFIER_BLOCK_END = "<!-- x-esphome-version-specifier-end -->"
VERSION_SPECIFIER_LINE_MARKER = "<!-- x-esphome-version-specifier -->"
SPECIFIER_OPERATORS = ("===", ">=", "<=", "~=", "!=", "==", ">", "<")
PLAIN_SPECIFIER_VERSION_PATTERN = r"[^\s;,)]+"
# URL-encoded specifier literals may contain almost any non-space character for
# arbitrary equality (===). Keep the match lazy and require a URL/text boundary
# so badge suffixes such as "-000000?logo=..." are not consumed as version text.
ENCODED_SPECIFIER_VERSION_PATTERN = (
    r"(?:[A-Za-z0-9._~-]|%(?!(?:09|0A|0a|0B|0b|0C|0c|0D|0d|20|29|2C|2c|3B|3b))"
    r"[0-9A-Fa-f]{2})+?"
)
ENCODED_SPECIFIER_BOUNDARY_PATTERN = (
    r"(?=$|[\s\])}>]|[?&#]|-(?:[0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})(?:$|[/?#&]))"
)


@dataclass(frozen=True)
class VersionQuery:
    """Effective package query shared by version, matrix, and sync commands."""

    package_name: str
    specifier: str
    specifier_display: str
    include_prereleases: bool = False


def normalize_specifier_display(specifier: str) -> str:
    """Render a PEP 440 specifier string with readable operator spacing."""
    formatted_parts: list[str] = []
    for part in specifier.split(","):
        stripped = part.strip()
        if not stripped:
            continue

        for operator in ("===", ">=", "<=", "~=", "!=", "==", ">", "<"):
            if stripped.startswith(operator):
                formatted_parts.append(
                    f"{operator} {stripped[len(operator) :].strip()}"
                )
                break
        else:
            formatted_parts.append(stripped)

    return ", ".join(formatted_parts)


def sort_versions(versions: list[str]) -> list[str]:
    """Sort PEP 440 version strings from oldest to newest."""
    return sorted(versions, key=Version)


def unique_sorted_versions(versions: list[str]) -> list[str]:
    """Normalize, deduplicate, and sort PEP 440 version strings."""
    return sorted({str(Version(version)) for version in versions}, key=Version)


def discover_standalone_configs(
    configs_dir: Path = CONFIGS_DIR,
    *,
    root_dir: Path = ROOT_DIR,
) -> list[str]:
    """Return standalone ESPHome example configs as sorted repo-relative paths."""
    if not configs_dir.is_dir():
        return []

    configs: list[str] = []
    for path in configs_dir.iterdir():
        if not path.is_file():
            continue
        if path.suffix not in STANDALONE_CONFIG_EXTENSIONS:
            continue
        if path.name in EXCLUDED_STANDALONE_CONFIG_NAMES:
            continue
        configs.append(path.relative_to(root_dir).as_posix())

    return sorted(configs)


def parse_requirement(
    package_name: str,
    path: Path = REQUIREMENTS_FILE,
) -> tuple[str, str]:
    """Read one constrained package requirement from a requirements-style file."""
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        candidate = stripped.split("#", maxsplit=1)[0].strip()
        if not candidate:
            continue

        requirement = Requirement(candidate)
        if requirement.name != package_name:
            continue

        specifier = candidate.partition(requirement.name)[2].strip() or str(
            requirement.specifier
        )
        if not specifier:
            raise ValueError(f"{path} must constrain the {package_name} dependency")

        return specifier, normalize_specifier_display(specifier)

    raise ValueError(f"{path} does not define a requirement for {package_name}")


def resolve_query(
    *,
    package_name: str | None,
    specifier: str | None,
    include_prereleases: bool,
    requirements_file: Path,
) -> VersionQuery:
    """Resolve CLI package/specifier input, falling back to requirements.txt."""
    effective_package_name = package_name or DEFAULT_PACKAGE_NAME
    if specifier is None:
        specifier, specifier_display = parse_requirement(
            effective_package_name,
            requirements_file,
        )
    else:
        try:
            SpecifierSet(specifier)
        except InvalidSpecifier as exc:
            raise ValueError(f"Invalid version specifier {specifier!r}: {exc}") from exc
        specifier_display = normalize_specifier_display(specifier)

    return VersionQuery(
        package_name=effective_package_name,
        specifier=specifier,
        specifier_display=specifier_display,
        include_prereleases=include_prereleases,
    )


def fetch_package_versions(
    package_name: str,
    *,
    include_prereleases: bool,
) -> list[Version]:
    """Fetch valid PyPI release versions for one package, optionally prereleases."""
    request = Request(
        PYPI_JSON_URL.format(package_name=quote(package_name, safe="")),
        headers={"Accept": "application/json", "User-Agent": DEFAULT_USER_AGENT},
    )
    try:
        with urlopen(request, timeout=30) as response:
            data = json.load(response)
    except HTTPError as exc:
        if exc.code == 404:
            raise RuntimeError(
                f"Package {package_name!r} was not found on PyPI"
            ) from exc
        raise RuntimeError(
            f"Failed to fetch {package_name!r} releases from PyPI: HTTP {exc.code}"
        ) from exc
    except URLError as exc:
        raise RuntimeError(
            f"Failed to fetch {package_name!r} releases from PyPI: {exc.reason}"
        ) from exc

    versions: list[Version] = []
    for raw_version in data.get("releases", {}):
        try:
            version = Version(raw_version)
        except InvalidVersion:
            continue

        if version.is_prerelease and not include_prereleases:
            continue
        versions.append(version)

    return sorted(set(versions))


def matching_versions(
    package_name: str,
    specifier: str,
    include_prereleases: bool = False,
) -> list[str]:
    """Return sorted PyPI releases that satisfy one package specifier."""
    spec = SpecifierSet(specifier)
    versions: list[Version] = []
    for version in fetch_package_versions(
        package_name,
        include_prereleases=include_prereleases,
    ):
        if spec.contains(version, prereleases=include_prereleases):
            versions.append(version)

    return [str(version) for version in sorted(set(versions))]


def latest_version(versions: list[str]) -> str:
    """Return the newest version from a non-empty list of PEP 440 versions."""
    if not versions:
        raise ValueError("No matching versions were resolved")

    return sort_versions(versions)[-1]


def version_set_hash(versions: list[str]) -> str:
    """Return a deterministic hash for the exact resolved version set."""
    normalized_versions = unique_sorted_versions(versions)
    payload = "\n".join(normalized_versions)
    if payload:
        payload = f"{payload}\n"
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def version_set_snapshot(query: VersionQuery, versions: list[str]) -> dict[str, object]:
    """Build metadata for the exact resolved package version set."""
    resolved = unique_sorted_versions(versions)
    if not resolved:
        raise ValueError("No matching versions were resolved")

    return {
        "package": query.package_name,
        "specifier": query.specifier,
        "specifier_display": query.specifier_display,
        "include_prereleases": query.include_prereleases,
        "latest_version": resolved[-1],
        "version_set_hash": version_set_hash(resolved),
        "versions": resolved,
    }


def extract_specifier_versions(specifier: str) -> list[str]:
    """Extract version literals from comma-separated specifier clauses."""
    versions: list[str] = []
    for part in specifier.split(","):
        stripped = part.strip()
        if not stripped:
            continue

        for operator in SPECIFIER_OPERATORS:
            if stripped.startswith(operator):
                version = stripped[len(operator) :].strip()
                if not version:
                    raise ValueError(
                        f"Invalid version specifier clause {stripped!r}: missing version"
                    )
                versions.append(version)
                break
        else:
            raise ValueError(
                f"Invalid version specifier clause {stripped!r}: missing operator"
            )

    if not versions:
        raise ValueError(f"Invalid version specifier {specifier!r}: no clauses found")

    return versions


def pattern_version_sort_key(version: str) -> tuple[int, Version | str, str]:
    """Place valid versions before opaque specifier literals such as wildcards."""
    try:
        return (0, Version(version), version)
    except InvalidVersion:
        return (1, version, version)


def sort_pattern_versions(versions: list[str]) -> list[str]:
    """Sort version literals longest-first for deterministic regex alternation."""
    unique_versions = sorted({*versions}, key=pattern_version_sort_key)
    return sorted(
        unique_versions,
        key=lambda version: (-len(version), pattern_version_sort_key(version)),
    )


def build_specifier_patterns(
    package_name: str,
    current_specifier: str,
) -> tuple[re.Pattern[str], re.Pattern[str]]:
    """Build plain-text and URL-encoded specifier regexes for managed docs."""
    operator_alternation = "|".join(
        re.escape(operator) for operator in SPECIFIER_OPERATORS
    )
    encoded_operator_alternation = "|".join(
        re.escape(quote(operator, safe="")) for operator in SPECIFIER_OPERATORS
    )

    plain_pattern = re.compile(
        rf"(?:{operator_alternation})\s*(?:{PLAIN_SPECIFIER_VERSION_PATTERN})"
        rf"(?:\s*,\s*(?:{operator_alternation})"
        rf"\s*(?:{PLAIN_SPECIFIER_VERSION_PATTERN}))*"
    )
    encoded_pattern = re.compile(
        rf"(?:{encoded_operator_alternation})(?:%20)*"
        rf"(?:{ENCODED_SPECIFIER_VERSION_PATTERN})"
        rf"(?:(?:%20)*%2C(?:%20)*(?:{encoded_operator_alternation})"
        rf"(?:%20)*(?:{ENCODED_SPECIFIER_VERSION_PATTERN}))*"
        rf"{ENCODED_SPECIFIER_BOUNDARY_PATTERN}"
    )
    return plain_pattern, encoded_pattern


def validate_versions_compatibility_window(versions: list[str]) -> list[str]:
    """Select first, latest per minor, and latest versions for validation."""
    if not versions:
        raise ValueError("No matching versions were resolved")

    resolved = sort_versions(versions)
    latest_per_minor: dict[tuple[int, int], str] = {}
    for version in resolved:
        parsed_version = Version(version)
        latest_per_minor[(parsed_version.major, parsed_version.minor)] = version

    return unique_sorted_versions(
        [resolved[0], *latest_per_minor.values(), resolved[-1]]
    )


def validate_versions_all_supported_versions(versions: list[str]) -> list[str]:
    """Select every resolved version for validation."""
    return unique_sorted_versions(versions)


def compile_versions_compatibility_window(versions: list[str]) -> list[str]:
    """Select oldest, newest, and latest stable when newest is a prerelease."""
    if not versions:
        raise ValueError("No matching versions were resolved")

    resolved = sort_versions(versions)
    selected = [resolved[0], resolved[-1]]
    if Version(resolved[-1]).is_prerelease:
        stable_versions = [
            version for version in resolved if not Version(version).is_prerelease
        ]
        if stable_versions:
            selected.append(stable_versions[-1])

    return unique_sorted_versions(selected)


def compile_versions_all_supported_versions(versions: list[str]) -> list[str]:
    """Select every resolved version for compile smoke tests."""
    return unique_sorted_versions(versions)


def versions_for_mode(versions: list[str], mode: str) -> list[str]:
    """Resolve a named matrix mode to the versions it should exercise."""
    if mode in {"validate", "validate-compatibility-window"}:
        return validate_versions_compatibility_window(versions)
    if mode == "validate-all-supported-versions":
        resolved = validate_versions_all_supported_versions(versions)
        if not resolved:
            raise ValueError(f"No versions resolved for mode {mode}")
        return resolved
    if mode in {"compile", "compile-compatibility-window"}:
        return compile_versions_compatibility_window(versions)
    if mode == "compile-all-supported-versions":
        resolved = compile_versions_all_supported_versions(versions)
        if not resolved:
            raise ValueError(f"No versions resolved for mode {mode}")
        return resolved
    raise ValueError(f"Unsupported matrix mode: {mode}")


def is_compile_mode(mode: str) -> bool:
    """Return whether a matrix mode runs compile smoke tests."""
    return mode in {
        "compile",
        "compile-compatibility-window",
        "compile-all-supported-versions",
    }


def matrix_for_versions(versions: list[str]) -> str:
    """Return compact GitHub Actions matrix JSON for validation jobs."""
    matrix = {"include": [{"esphome": version} for version in versions]}
    return json.dumps(matrix, separators=(",", ":"))


def compile_matrix_for_versions(
    versions: list[str],
    configs: list[str] | None = None,
) -> str:
    """Return compact GitHub Actions matrix JSON for compile smoke tests."""
    if configs is None:
        configs = discover_standalone_configs()
    if not configs:
        raise ValueError(f"No standalone configs found under {CONFIGS_DIR.name}/")

    matrix = {
        "include": [
            {"esphome": version, "config": config}
            for version in versions
            for config in configs
        ]
    }
    return json.dumps(matrix, separators=(",", ":"))


def replace_marked_block(content: str, transform: Callable[[str], str]) -> str:
    """Transform each managed block while preserving its marker comments."""
    parts = content.split(VERSION_SPECIFIER_BLOCK_START)
    if len(parts) == 1:
        raise ValueError(
            "No managed version-specifier blocks found using "
            f"{VERSION_SPECIFIER_BLOCK_START}"
        )

    rebuilt = [parts[0]]
    for part in parts[1:]:
        if VERSION_SPECIFIER_BLOCK_END not in part:
            raise ValueError(
                "Managed version-specifier block starting with "
                f"{VERSION_SPECIFIER_BLOCK_START} is missing "
                f"{VERSION_SPECIFIER_BLOCK_END}"
            )
        block_content, remainder = part.split(VERSION_SPECIFIER_BLOCK_END, maxsplit=1)
        updated_block = transform(block_content)
        rebuilt.append(
            f"{VERSION_SPECIFIER_BLOCK_START}{updated_block}"
            f"{VERSION_SPECIFIER_BLOCK_END}{remainder}"
        )

    if content.count(VERSION_SPECIFIER_BLOCK_START) != content.count(
        VERSION_SPECIFIER_BLOCK_END
    ):
        raise ValueError(
            "Managed version-specifier markers must be balanced: "
            f"{VERSION_SPECIFIER_BLOCK_START} / {VERSION_SPECIFIER_BLOCK_END}"
        )

    return "".join(rebuilt)


def replace_marked_line(content: str, transform: Callable[[str], str]) -> str:
    """Transform the line following each single-line managed marker."""
    lines = content.splitlines(keepends=True)
    updated_lines: list[str] = []
    found_marker = False
    index = 0

    while index < len(lines):
        line = lines[index]
        updated_lines.append(line)
        if line.strip() != VERSION_SPECIFIER_LINE_MARKER:
            index += 1
            continue

        found_marker = True
        index += 1
        if index >= len(lines):
            raise ValueError(
                f"{VERSION_SPECIFIER_LINE_MARKER} must be followed by a managed line"
            )

        original_line = lines[index]
        line_ending = "\n" if original_line.endswith("\n") else ""
        line_content = original_line[:-1] if line_ending else original_line
        updated_lines.append(f"{transform(line_content)}{line_ending}")
        index += 1

    if not found_marker:
        raise ValueError(
            "No managed version-specifier line found using "
            f"{VERSION_SPECIFIER_LINE_MARKER}"
        )

    return "".join(updated_lines)


def replace_specifier_in_managed_content(
    managed_content: str,
    *,
    plain_pattern: re.Pattern[str],
    encoded_pattern: re.Pattern[str],
    specifier_display: str,
) -> str:
    """Replace plain or URL-encoded specifier strings inside one managed region."""
    updated_content = managed_content
    replacement_count = 0
    encoded_specifier = quote(specifier_display, safe="")

    updated_content, encoded_replacements = encoded_pattern.subn(
        encoded_specifier,
        updated_content,
    )
    replacement_count += encoded_replacements

    updated_content, plain_replacements = plain_pattern.subn(
        specifier_display,
        updated_content,
    )
    replacement_count += plain_replacements

    if replacement_count == 0:
        raise ValueError(
            "Managed version-specifier content does not contain a replaceable specifier"
        )

    return updated_content


def apply_managed_specifier_sync(
    content: str,
    *,
    plain_pattern: re.Pattern[str],
    encoded_pattern: re.Pattern[str],
    specifier_display: str,
) -> str:
    """Apply specifier replacement to every managed line or block in one file."""
    has_line_marker = VERSION_SPECIFIER_LINE_MARKER in content
    has_block_marker = (
        VERSION_SPECIFIER_BLOCK_START in content
        or VERSION_SPECIFIER_BLOCK_END in content
    )
    if not has_line_marker and not has_block_marker:
        raise ValueError("No managed version-specifier markers found")

    def transform(managed_content: str) -> str:
        """Replace the specifier within one managed region."""
        return replace_specifier_in_managed_content(
            managed_content,
            plain_pattern=plain_pattern,
            encoded_pattern=encoded_pattern,
            specifier_display=specifier_display,
        )

    updated = content
    if has_line_marker:
        updated = replace_marked_line(updated, transform)
    if has_block_marker:
        updated = replace_marked_block(updated, transform)
    return updated


def update_file(path: Path, rendered: str, *, check: bool) -> bool:
    """Write rendered content, or report drift without writing in check mode."""
    current = path.read_text(encoding="utf-8")
    if current == rendered:
        return False
    if check:
        return True
    path.write_text(rendered, encoding="utf-8")
    return True


def add_query_arguments(parser: argparse.ArgumentParser) -> None:
    """Add package/specifier options shared by version-resolving subcommands."""
    parser.add_argument(
        "--package",
        default=None,
        help=(
            "Package to resolve from PyPI. Defaults to the repository package "
            f"{DEFAULT_PACKAGE_NAME!r}."
        ),
    )
    parser.add_argument(
        "--specifier",
        default=None,
        help=(
            "Version specifier to apply. When omitted, the package requirement is "
            f"read from {REQUIREMENTS_FILE.relative_to(ROOT_DIR)}."
        ),
    )
    parser.add_argument(
        "--include-prereleases",
        action="store_true",
        default=False,
        help="Include prerelease versions when filtering PyPI releases.",
    )
    parser.add_argument(
        "--requirements-file",
        type=Path,
        default=REQUIREMENTS_FILE,
        help="Requirements file used for default package/specifier lookup.",
    )


def emit_resolved_versions(args: argparse.Namespace) -> None:
    """Resolve and print all matching package versions, one per line."""
    query = resolve_query(
        package_name=args.package,
        specifier=args.specifier,
        include_prereleases=args.include_prereleases,
        requirements_file=args.requirements_file,
    )
    print(
        "\n".join(
            matching_versions(
                query.package_name,
                query.specifier,
                include_prereleases=query.include_prereleases,
            )
        )
    )


def emit_matrix(args: argparse.Namespace) -> None:
    """Resolve versions and emit the requested GitHub Actions matrix."""
    query = resolve_query(
        package_name=args.package,
        specifier=args.specifier,
        include_prereleases=args.include_prereleases,
        requirements_file=args.requirements_file,
    )
    versions = matching_versions(
        query.package_name,
        query.specifier,
        include_prereleases=query.include_prereleases,
    )
    matrix_versions = versions_for_mode(versions, args.mode)
    if is_compile_mode(args.mode):
        print(compile_matrix_for_versions(matrix_versions))
        return

    print(matrix_for_versions(matrix_versions))


def emit_latest_version(args: argparse.Namespace) -> None:
    """Resolve and print the latest matching package version."""
    query = resolve_query(
        package_name=args.package,
        specifier=args.specifier,
        include_prereleases=args.include_prereleases,
        requirements_file=args.requirements_file,
    )
    version = latest_version(
        matching_versions(
            query.package_name,
            query.specifier,
            include_prereleases=query.include_prereleases,
        )
    )

    if args.format == "json":
        print(
            json.dumps(
                {
                    "package": query.package_name,
                    "specifier": query.specifier,
                    "specifier_display": query.specifier_display,
                    "include_prereleases": query.include_prereleases,
                    "version": version,
                },
                sort_keys=True,
            )
        )
        return

    print(version)


def emit_version_set_snapshot(args: argparse.Namespace) -> None:
    """Resolve and print JSON metadata for the exact matching version set."""
    query = resolve_query(
        package_name=args.package,
        specifier=args.specifier,
        include_prereleases=args.include_prereleases,
        requirements_file=args.requirements_file,
    )
    print(
        json.dumps(
            version_set_snapshot(
                query,
                matching_versions(
                    query.package_name,
                    query.specifier,
                    include_prereleases=query.include_prereleases,
                ),
            ),
            sort_keys=True,
            separators=(",", ":"),
        )
    )


def emit_compile_configs(_: argparse.Namespace) -> None:
    """Emit discovered standalone compile smoke-test configs, one per line."""
    configs = discover_standalone_configs()
    if not configs:
        raise ValueError(f"No standalone configs found under {CONFIGS_DIR.name}/")
    print("\n".join(configs))


def sync_support_files(args: argparse.Namespace) -> None:
    """Synchronize README and CONTRIBUTING managed snippets with the specifier."""
    query = resolve_query(
        package_name=args.package,
        specifier=args.specifier,
        include_prereleases=False,
        requirements_file=args.requirements_file,
    )
    plain_pattern, encoded_pattern = build_specifier_patterns(
        query.package_name,
        query.specifier,
    )
    rendered_files = {
        path: apply_managed_specifier_sync(
            path.read_text(encoding="utf-8"),
            plain_pattern=plain_pattern,
            encoded_pattern=encoded_pattern,
            specifier_display=query.specifier_display,
        )
        for path in (README_FILE, CONTRIBUTING_FILE)
    }
    changed_paths = [
        path
        for path, rendered in rendered_files.items()
        if update_file(path, rendered, check=args.check)
    ]

    if args.check and changed_paths:
        changed = ", ".join(str(path.relative_to(ROOT_DIR)) for path in changed_paths)
        raise ValueError(
            "Support-derived files are out of date. Run "
            "python scripts/esphome-versions.py sync. "
            f"Changed: {changed}"
        )

    if args.check:
        print("Support-derived files are up to date.")
        return

    if changed_paths:
        changed = ", ".join(str(path.relative_to(ROOT_DIR)) for path in changed_paths)
        print(f"Updated support-derived files: {changed}")
        return

    print("Support-derived files are already up to date.")


def build_parser() -> argparse.ArgumentParser:
    """Build the command-line parser and attach subcommand handlers."""
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    resolve_parser = subparsers.add_parser(
        "resolve",
        aliases=["versions"],
        help="Resolve matching package versions from PyPI.",
    )
    add_query_arguments(resolve_parser)
    resolve_parser.set_defaults(func=emit_resolved_versions)

    matrix_parser = subparsers.add_parser(
        "matrix",
        help="Emit a GitHub Actions matrix resolved from PyPI.",
    )
    add_query_arguments(matrix_parser)
    matrix_parser.add_argument(
        "--mode",
        choices=MATRIX_MODES,
        required=True,
        help="Matrix selection strategy.",
    )
    matrix_parser.set_defaults(func=emit_matrix)

    latest_parser = subparsers.add_parser(
        "latest",
        help="Emit the latest matching package version from PyPI.",
    )
    add_query_arguments(latest_parser)
    latest_parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format for the latest version metadata.",
    )
    latest_parser.set_defaults(func=emit_latest_version)

    snapshot_parser = subparsers.add_parser(
        "snapshot",
        help="Emit JSON metadata for the resolved package version set.",
    )
    add_query_arguments(snapshot_parser)
    snapshot_parser.set_defaults(func=emit_version_set_snapshot)

    compile_configs_parser = subparsers.add_parser(
        "compile-configs",
        help="Print discovered standalone compile smoke-test configs.",
    )
    compile_configs_parser.set_defaults(func=emit_compile_configs)

    sync_parser = subparsers.add_parser(
        "sync",
        help="Update managed README and CONTRIBUTING support snippets.",
    )
    sync_parser.add_argument(
        "--package",
        default=None,
        help=(
            "Package used for default requirement lookup. Defaults to the repository "
            f"package {DEFAULT_PACKAGE_NAME!r}."
        ),
    )
    sync_parser.add_argument(
        "--specifier",
        default=None,
        help="Version specifier to render into the managed documentation snippets.",
    )
    sync_parser.add_argument(
        "--requirements-file",
        type=Path,
        default=REQUIREMENTS_FILE,
        help="Requirements file used for default package/specifier lookup.",
    )
    sync_parser.add_argument(
        "--check",
        action="store_true",
        default=False,
        help="Fail if the managed documentation snippets are out of date.",
    )
    sync_parser.set_defaults(func=sync_support_files)

    return parser


def main() -> None:
    """Run the CLI and render expected failures as user-facing errors."""
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.func(args)
    except (RuntimeError, ValueError) as exc:
        raise SystemExit(f"Error: {exc}") from exc


if __name__ == "__main__":
    main()
