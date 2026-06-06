#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${ROOT_DIR}/.venv"
VENV_PYTHON="${VENV_DIR}/bin/python"
PRE_COMMIT_BIN="${VENV_DIR}/bin/pre-commit"
REQUIREMENTS_DEV="${ROOT_DIR}/requirements-dev.txt"
PYTHON_VERSION_FILE="${ROOT_DIR}/.python-version"
SECRETS_EXAMPLE="${ROOT_DIR}/configs/secrets.example.yaml"
SECRETS_FILE="${ROOT_DIR}/configs/secrets.yaml"
REQUIRED_PYTHON_VERSION="$(tr -d '[:space:]' <"${PYTHON_VERSION_FILE}")"
readonly ROOT_DIR VENV_DIR VENV_PYTHON PRE_COMMIT_BIN REQUIREMENTS_DEV PYTHON_VERSION_FILE REQUIRED_PYTHON_VERSION SECRETS_EXAMPLE SECRETS_FILE

if [[ -t 1 ]]; then
  BOLD="$(printf '\033[1m')"
  GREEN="$(printf '\033[32m')"
  RED="$(printf '\033[31m')"
  RESET="$(printf '\033[0m')"
else
  BOLD=""
  GREEN=""
  RED=""
  RESET=""
fi
readonly BOLD GREEN RED RESET

step() {
  printf '\n%s==>%s %s\n' "${BOLD}" "${RESET}" "$*"
}

ok() {
  printf '%s[ok]%s %s\n' "${GREEN}" "${RESET}" "$*"
}

die() {
  printf '%s[error]%s %s\n' "${RED}" "${RESET}" "$*" >&2
  exit 1
}

run_quietly() {
  local log_file
  log_file="$(mktemp "${TMPDIR:-/tmp}/esphome-setup.XXXXXX")"

  if "$@" >"${log_file}" 2>&1; then
    rm -f "${log_file}"
    return 0
  fi

  cat "${log_file}" >&2
  rm -f "${log_file}"
  return 1
}

python_version_matches() {
  local python_bin="$1"
  local actual_version

  if ! actual_version="$("${python_bin}" -c 'import sys; print(".".join(str(part) for part in sys.version_info[:3]))')"; then
    return 1
  fi

  case "${REQUIRED_PYTHON_VERSION}" in
    *.*.*)
      [[ "${actual_version}" == "${REQUIRED_PYTHON_VERSION}" ]]
      ;;
    *)
      [[ "${actual_version}" == "${REQUIRED_PYTHON_VERSION}."* ]]
      ;;
  esac
}

find_required_python() {
  local candidate
  local python_bin
  local candidates=()

  if [[ -n "${PYTHON:-}" ]]; then
    candidates+=("${PYTHON}")
  fi

  candidates+=("python${REQUIRED_PYTHON_VERSION}" "python3" "python")

  for candidate in "${candidates[@]}"; do
    if ! python_bin="$(command -v "${candidate}" 2>/dev/null)"; then
      continue
    fi

    if python_version_matches "${python_bin}"; then
      printf '%s\n' "${python_bin}"
      return 0
    fi
  done

  if command -v uv >/dev/null 2>&1; then
    uv python install "${REQUIRED_PYTHON_VERSION}" >/dev/null
    if python_bin="$(uv python find "${REQUIRED_PYTHON_VERSION}" 2>/dev/null)" &&
      python_version_matches "${python_bin}"; then
      printf '%s\n' "${python_bin}"
      return 0
    fi
  fi

  return 1
}

prepare_python_environment() {
  local python_bin

  if [[ ! "${REQUIRED_PYTHON_VERSION}" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    echo ".python-version must contain an exact Python minor or patch version such as 3.14 or 3.14.1." >&2
    return 1
  fi

  if ! python_bin="$(find_required_python)"; then
    cat >&2 <<EOF
Python ${REQUIRED_PYTHON_VERSION} was not found.
Install it with your Python version manager, or install uv and rerun ./scripts/setup.sh so the setup can provision it automatically.
EOF
    return 1
  fi

  if [[ -x "${VENV_PYTHON}" ]] && ! python_version_matches "${VENV_PYTHON}"; then
    rm -rf "${VENV_DIR}"
  fi

  if [[ ! -x "${VENV_PYTHON}" ]]; then
    if ! "${python_bin}" -m venv "${VENV_DIR}"; then
      echo "Failed to create ${VENV_DIR} with Python ${REQUIRED_PYTHON_VERSION}." >&2
      return 1
    fi
  fi

  "${VENV_PYTHON}" -m pip install --upgrade pip

  if [[ -n "${ESPHOME_VERSION:-}" ]]; then
    if [[ ! "${ESPHOME_VERSION}" =~ ^[0-9]{4}\.[0-9]+\.[0-9]+(a[0-9]+|b[0-9]+|rc[0-9]+)?$ ]]; then
      echo "ESPHOME_VERSION must be an exact version such as 2026.5.1, 2026.5.0b3, or 2026.5.0rc1." >&2
      return 1
    fi

    "${VENV_PYTHON}" -m pip install -r "${REQUIREMENTS_DEV}" "esphome==${ESPHOME_VERSION}"
  else
    "${VENV_PYTHON}" -m pip install -r "${REQUIREMENTS_DEV}"
  fi
}

cd "${ROOT_DIR}"

printf '%sESPHome Spectra 6 local setup%s\n' "${BOLD}" "${RESET}"

if ! git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  die "This setup must run from a Git checkout so required hooks can be installed."
fi

step "Prepare Python environment"
if ! run_quietly prepare_python_environment; then
  die "Python environment setup failed."
fi
ok "Python environment is ready"

if [[ ! -x "${PRE_COMMIT_BIN}" ]]; then
  die "pre-commit was not installed in ${VENV_DIR}. Check requirements-dev.txt, then rerun ./scripts/setup.sh."
fi

step "Prepare local secrets"
if [[ ! -f "${SECRETS_FILE}" ]]; then
  if [[ ! -f "${SECRETS_EXAMPLE}" ]]; then
    die "Missing ${SECRETS_EXAMPLE}; cannot create local secrets."
  fi

  cp "${SECRETS_EXAMPLE}" "${SECRETS_FILE}"
  ok "Created configs/secrets.yaml from configs/secrets.example.yaml"
else
  ok "Keeping existing configs/secrets.yaml"
fi

install_hook() {
  local name="$1"
  shift

  if ! "${PRE_COMMIT_BIN}" install "$@"; then
    die "Failed to install ${name}. If an existing hook is in the way, move it aside and rerun ./scripts/setup.sh."
  fi

  ok "${name} is installed"
}

step "Install required Git hooks"
install_hook "pre-commit hook"
install_hook "pre-push hook" --hook-type pre-push

cat <<'EOF'

Setup complete.

Your virtual environment is ready at .venv.
To activate it for the current terminal session, run:

  source .venv/bin/activate
EOF
