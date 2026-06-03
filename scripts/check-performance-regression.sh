#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${ROOT_DIR}/build/bencher-local"
LOG_DIR="${OUTPUT_DIR}/logs"
REPORT_DIR="${OUTPUT_DIR}/reports"

BASELINE_BRANCH="main"
BASELINE_ITERATIONS=5
FEATURE_ITERATIONS=1
MIN_TIME="0.2s"
BENCHMARK_FILTER=".*"
PROJECT_PREFIX="esphome-epaper-spectra6-local"
PROJECT_SLUG=""
TESTBED="$(hostname)-local"
VERBOSE=0
DRY_RUN=0

FEATURE_BRANCH=""
FEATURE_SHA=""
BASELINE_SHA=""
STASH_CREATED=0
STASH_RESTORED=0
STASH_REF=""
STASH_MESSAGE=""
CURRENT_LOG=""

usage() {
  cat <<'EOF'
Usage: scripts/check-performance-regression.sh [options]

Run a local Bencher regression check from a feature branch:
  1. auto-stash local changes
  2. switch to main and seed baseline benchmark runs
  3. switch back to the feature branch
  4. restore the stash
  5. run one feature benchmark and compare against main

Options:
  --project-slug SLUG        Reuse a specific Bencher scratch project
  --project-prefix NAME      Prefix for generated project slugs
                             (default: esphome-epaper-spectra6-local)
  --baseline-branch BRANCH   Branch used as local baseline (default: main)
  --baseline-iterations N    Number of baseline runs (default: 5)
  --feature-iterations N     Number of feature runs (default: 1)
  --min-time VALUE           Google Benchmark min time (default: 0.2s)
  --filter REGEX             Google Benchmark filter (default: .*)
  --testbed NAME             Bencher testbed name (default: $(hostname)-local)
  --verbose                  Stream command output instead of writing logs only
  --dry-run                  Pass --dry-run to bencher run; backend data is not stored
  -h, --help                 Show this help

Examples:
  scripts/check-performance-regression.sh
  scripts/check-performance-regression.sh --project-slug esphome-epaper-spectra6-local-a1b2c3
  scripts/check-performance-regression.sh --baseline-iterations 1 --feature-iterations 1 --filter 'ColorMapping/.*'
EOF
}

die() {
  echo "error: $*" >&2
  if [[ -n "${CURRENT_LOG}" ]]; then
    echo "last log: ${CURRENT_LOG}" >&2
  fi
  exit 1
}

info() {
  printf '[bencher] %s\n' "$*"
}

sanitize_slug_part() {
  printf '%s' "$1" |
    tr '[:upper:]' '[:lower:]' |
    sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//; s/-+/-/g'
}

random_suffix() {
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -hex 3
    return
  fi

  date '+%s' | shasum | awk '{print substr($1, 1, 6)}'
}

positive_integer() {
  [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

run_logged() {
  local label="$1"
  local log_file="$2"
  shift 2

  CURRENT_LOG="${log_file}"
  mkdir -p "$(dirname "${log_file}")"

  if [[ "${VERBOSE}" -eq 1 ]]; then
    info "${label}"
    "$@" 2>&1 | tee "${log_file}"
  else
    info "${label} ..."
    if "$@" >"${log_file}" 2>&1; then
      info "${label} done"
    else
      die "${label} failed"
    fi
  fi

  CURRENT_LOG=""
}

bencher_run() {
  local label="$1"
  local report_path="$2"
  shift 2

  local args=(
    run
    --project "${PROJECT_SLUG}"
    --testbed "${TESTBED}"
    --adapter cpp_google
    --format json
    --quiet
  )

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    args+=(--dry-run)
  fi

  args+=("$@")

  CURRENT_LOG="${report_path}"
  mkdir -p "$(dirname "${report_path}")"

  if [[ "${VERBOSE}" -eq 1 ]]; then
    info "${label}"
  else
    info "${label} ..."
  fi

  # Local comparison runs should not reuse credentials by accident. Running
  # unauthenticated keeps samples in a scratch/unclaimed Bencher project instead
  # of mutating the official repository history.
  if env -u BENCHER_PROJECT -u BENCHER_API_KEY -u BENCHER_API_TOKEN bencher "${args[@]}" >"${report_path}" 2>&1; then
    info "${label} done"
  else
    die "${label} failed"
  fi

  if [[ "${VERBOSE}" -eq 1 ]]; then
    cat "${report_path}"
  fi

  CURRENT_LOG=""
}

create_stash_if_needed() {
  if git diff --quiet --ignore-submodules -- && git diff --cached --quiet --ignore-submodules -- &&
    [[ -z "$(git ls-files --others --exclude-standard)" ]]; then
    return
  fi

  STASH_MESSAGE="scripts/check-performance-regression.sh ${FEATURE_BRANCH} $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  info "dirty worktree detected; creating stash"
  git stash push --include-untracked -m "${STASH_MESSAGE}" >/dev/null
  STASH_CREATED=1
  STASH_REF="stash@{0}"
  info "stash created: ${STASH_REF} (${STASH_MESSAGE})"
}

restore_stash_if_needed() {
  if [[ "${STASH_CREATED}" -eq 0 || "${STASH_RESTORED}" -eq 1 ]]; then
    return
  fi

  info "restoring stashed changes"
  CURRENT_LOG="${LOG_DIR}/stash-pop.log"
  mkdir -p "${LOG_DIR}"
  if git stash pop "${STASH_REF}" >"${CURRENT_LOG}" 2>&1; then
    STASH_RESTORED=1
    CURRENT_LOG=""
    info "stash restored"
  else
    cat "${CURRENT_LOG}" >&2
    cat >&2 <<EOF
error: restoring the auto-stash produced conflicts.
The stash was kept by git. Resolve the conflicts manually, or inspect it with:
  git stash list
  git stash show -p '${STASH_REF}'
EOF
    exit 1
  fi
}

cleanup() {
  local status=$?

  set +e
  if [[ -n "${FEATURE_BRANCH}" ]]; then
    local current_branch
    current_branch="$(git branch --show-current 2>/dev/null)"
    if [[ "${current_branch}" != "${FEATURE_BRANCH}" ]]; then
      git switch "${FEATURE_BRANCH}" >/dev/null 2>&1
    fi
  fi

  if [[ "${STASH_CREATED}" -eq 1 && "${STASH_RESTORED}" -eq 0 ]]; then
    restore_stash_if_needed
    status=$?
  fi

  exit "${status}"
}

extract_urls() {
  local report_path="$1"
  python3 - "$report_path" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
try:
    data = json.loads(path.read_text())
except Exception:
    sys.exit(0)

urls = []

def walk(value):
    if isinstance(value, str) and value.startswith(("http://", "https://")):
        urls.append(value)
    elif isinstance(value, dict):
        for item in value.values():
            walk(item)
    elif isinstance(value, list):
        for item in value:
            walk(item)

walk(data)
for url in dict.fromkeys(urls):
    print(url)
PY
}

print_summary() {
  echo
  echo "Local Bencher run complete"
  echo "Project slug: ${PROJECT_SLUG}"
  echo "Testbed:      ${TESTBED}"
  echo "Baseline:     ${BASELINE_BRANCH} @ ${BASELINE_SHA}"
  echo "Feature:      ${FEATURE_BRANCH} @ ${FEATURE_SHA}"
  echo "Artifacts:    ${OUTPUT_DIR}"
  echo "Logs:         ${LOG_DIR}"
  echo

  local urls_file="${OUTPUT_DIR}/bencher-urls.txt"
  : >"${urls_file}"

  while IFS= read -r report; do
    extract_urls "${report}" >>"${urls_file}"
  done < <(find "${REPORT_DIR}" -type f -name '*.json' | sort)

  local any_url=0
  while IFS= read -r url; do
    [[ -n "${url}" ]] || continue
    if [[ "${any_url}" -eq 0 ]]; then
      echo "Bencher URLs:"
      any_url=1
    fi
    printf '  %s\n' "${url}"
  done < <(sort -u "${urls_file}")

  if [[ "${any_url}" -eq 0 ]]; then
    echo "Bencher did not expose URLs in JSON output."
    echo "Saved Bencher reports are in: ${REPORT_DIR}"
  fi
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --project-slug)
        PROJECT_SLUG="$2"
        shift 2
        ;;
      --project-prefix)
        PROJECT_PREFIX="$2"
        shift 2
        ;;
      --baseline-branch)
        BASELINE_BRANCH="$2"
        shift 2
        ;;
      --baseline-iterations)
        BASELINE_ITERATIONS="$2"
        shift 2
        ;;
      --feature-iterations)
        FEATURE_ITERATIONS="$2"
        shift 2
        ;;
      --min-time)
        MIN_TIME="$2"
        shift 2
        ;;
      --filter)
        BENCHMARK_FILTER="$2"
        shift 2
        ;;
      --testbed)
        TESTBED="$2"
        shift 2
        ;;
      --verbose)
        VERBOSE=1
        shift
        ;;
      --dry-run)
        DRY_RUN=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "unknown option: $1"
        ;;
    esac
  done
}

validate_environment() {
  git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not inside a git repository"
  [[ "$(git rev-parse --show-toplevel)" == "${ROOT_DIR}" ]] || die "run this script from this repository"
  command -v bencher >/dev/null 2>&1 || die "bencher CLI not found in PATH"
  command -v python3 >/dev/null 2>&1 || die "python3 not found in PATH"
  [[ -f "${ROOT_DIR}/scripts/run-benchmarks.sh" ]] || die "scripts/run-benchmarks.sh not found"
  positive_integer "${BASELINE_ITERATIONS}" || die "--baseline-iterations must be a positive integer"
  positive_integer "${FEATURE_ITERATIONS}" || die "--feature-iterations must be a positive integer"

  FEATURE_BRANCH="$(git branch --show-current)"
  [[ -n "${FEATURE_BRANCH}" ]] || die "detached HEAD is not supported; switch to a feature branch first"
  [[ "${FEATURE_BRANCH}" != "${BASELINE_BRANCH}" ]] || die "run this from a feature branch, not ${BASELINE_BRANCH}"

  git rev-parse --verify "${BASELINE_BRANCH}" >/dev/null 2>&1 ||
    die "baseline branch not found: ${BASELINE_BRANCH}"

  FEATURE_SHA="$(git rev-parse HEAD)"

  if [[ -z "${PROJECT_SLUG}" ]]; then
    local prefix
    prefix="$(sanitize_slug_part "${PROJECT_PREFIX}")"
    [[ -n "${prefix}" ]] || die "--project-prefix must contain at least one alphanumeric character"
    PROJECT_SLUG="${prefix}-$(random_suffix)"
  else
    PROJECT_SLUG="$(sanitize_slug_part "${PROJECT_SLUG}")"
    [[ -n "${PROJECT_SLUG}" ]] || die "--project-slug must contain at least one alphanumeric character"
  fi
}

main() {
  parse_args "$@"
  cd "${ROOT_DIR}"
  validate_environment

  mkdir -p "${OUTPUT_DIR}" "${LOG_DIR}" "${REPORT_DIR}"
  trap cleanup EXIT INT TERM

  info "feature branch: ${FEATURE_BRANCH}"
  info "project slug: ${PROJECT_SLUG}"
  info "testbed: ${TESTBED}"

  create_stash_if_needed

  info "switching to ${BASELINE_BRANCH}"
  git switch "${BASELINE_BRANCH}" >/dev/null 2>&1
  BASELINE_SHA="$(git rev-parse HEAD)"

  for ((i = 1; i <= BASELINE_ITERATIONS; i++)); do
    local_benchmark_json="${OUTPUT_DIR}/main-${i}.json"
    local_benchmark_log="${LOG_DIR}/main-${i}.log"
    local_report_json="${REPORT_DIR}/main-${i}.json"

    run_logged "baseline ${i}/${BASELINE_ITERATIONS}: benchmark" "${local_benchmark_log}" \
      bash "${ROOT_DIR}/scripts/run-benchmarks.sh" \
        --min-time "${MIN_TIME}" \
        --filter "${BENCHMARK_FILTER}" \
        --output "${local_benchmark_json}"

    bencher_run "baseline ${i}/${BASELINE_ITERATIONS}: bencher upload" "${local_report_json}" \
      --branch "${BASELINE_BRANCH}" \
      --hash "${BASELINE_SHA}" \
      --threshold-measure latency \
      --threshold-test percentage \
      --threshold-min-sample-size 5 \
      --threshold-max-sample-size 30 \
      --threshold-upper-boundary 0.30 \
      --thresholds-reset \
      --file "${local_benchmark_json}"
  done

  info "switching back to ${FEATURE_BRANCH}"
  git switch "${FEATURE_BRANCH}" >/dev/null 2>&1
  restore_stash_if_needed
  FEATURE_SHA="$(git rev-parse HEAD)"

  for ((i = 1; i <= FEATURE_ITERATIONS; i++)); do
    local_benchmark_json="${OUTPUT_DIR}/feature-${i}.json"
    local_benchmark_log="${LOG_DIR}/feature-${i}.log"
    local_report_json="${REPORT_DIR}/feature-${i}.json"

    run_logged "feature ${i}/${FEATURE_ITERATIONS}: benchmark" "${local_benchmark_log}" \
      bash "${ROOT_DIR}/scripts/run-benchmarks.sh" \
        --min-time "${MIN_TIME}" \
        --filter "${BENCHMARK_FILTER}" \
        --output "${local_benchmark_json}"

    bencher_run "feature ${i}/${FEATURE_ITERATIONS}: bencher comparison" "${local_report_json}" \
      --branch "${FEATURE_BRANCH}" \
      --hash "${FEATURE_SHA}" \
      --start-point "${BASELINE_BRANCH}" \
      --start-point-hash "${BASELINE_SHA}" \
      --start-point-clone-thresholds \
      --error-on-alert \
      --file "${local_benchmark_json}"
  done

  trap - EXIT INT TERM
  print_summary
}

main "$@"
