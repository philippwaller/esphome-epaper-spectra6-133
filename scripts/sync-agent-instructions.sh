#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
source_file="${repo_root}/AGENTS.md"
copilot_file="${repo_root}/.github/copilot-instructions.md"
claude_file="${repo_root}/CLAUDE.md"
gemini_file="${repo_root}/GEMINI.md"
mode="${1:-write}"

if [[ "${mode}" != "write" && "${mode}" != "--check" ]]; then
  echo "usage: $0 [write|--check]" >&2
  exit 2
fi

if [[ ! -f "${source_file}" ]]; then
  echo "missing source instruction file: ${source_file}" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

generated_copilot="${tmp_dir}/copilot-instructions.md"
generated_claude="${tmp_dir}/CLAUDE.md"
generated_gemini="${tmp_dir}/GEMINI.md"

{
  printf -- "---\n"
  printf "applyTo: \"**\"\n"
  printf -- "---\n\n"
  printf "<!-- Generated from AGENTS.md. Run scripts/sync-agent-instructions.sh after editing AGENTS.md. -->\n\n"
  cat "${source_file}"
} >"${generated_copilot}"

{
  printf "<!-- Generated from AGENTS.md. Run scripts/sync-agent-instructions.sh after editing AGENTS.md. -->\n\n"
  printf "@AGENTS.md\n"
} >"${generated_claude}"

{
  printf "<!-- Generated from AGENTS.md. Run scripts/sync-agent-instructions.sh after editing AGENTS.md. -->\n\n"
  printf "@AGENTS.md\n"
} >"${generated_gemini}"

if [[ "${mode}" == "--check" ]]; then
  ok=0
  cmp -s "${generated_copilot}" "${copilot_file}" || {
    echo ".github/copilot-instructions.md is out of sync with AGENTS.md" >&2
    ok=1
  }
  cmp -s "${generated_claude}" "${claude_file}" || {
    echo "CLAUDE.md is out of sync with AGENTS.md" >&2
    ok=1
  }
  cmp -s "${generated_gemini}" "${gemini_file}" || {
    echo "GEMINI.md is out of sync with AGENTS.md" >&2
    ok=1
  }
  exit "${ok}"
fi

cp "${generated_copilot}" "${copilot_file}"
cp "${generated_claude}" "${claude_file}"
cp "${generated_gemini}" "${gemini_file}"
