---
applyTo: "**"
---

# Documentation Rules for Contributors and Agents

## Purpose

Keep inline documentation clear, accurate, and maintainable for users and contributors.

## Scope by File Type

### C / C++
- Use Doxygen style for public APIs (`/** ... */`, `@brief`, `@param`, `@return`).
- Document public classes, structs, enums, constants, and methods.
- For private helpers, add comments only when behavior is non-obvious, hardware-specific, or timing-sensitive.
- Prefer comments that explain *intent* and *constraints* (BUSY pin semantics, refresh sequencing, ownership, blocking behavior).
- Keep comments stable: avoid implementation history and migration notes.

### Python
- Add module docstrings for each module.
- Add concise docstrings for public functions, classes, and dataclasses.
- In ESPHome codegen modules, document what users configure and how values are validated.
- For helper functions, document only when logic is not obvious from names and types.

### ESPHome YAML Examples
- Write comments from the end-user perspective.
- Explain when to change an option, not just what the key is called.
- Keep comments near relevant keys.
- Avoid decorative comment banners and repeated section dividers.

## Comment Quality Rules

### Good comments
- Explain why a delay exists or why order matters.
- Explain hardware constraints that are not visible in code.
- Explain asynchronous lifecycle, cancellation, and ownership boundaries.

### Bad comments
- Repeat the code in different words.
- Mention "old/new/refactored/temporary/legacy" without a real compatibility reason.
- Add marketing language, superlatives, or historical narratives.

## When Not to Comment
- Do not comment obvious assignments, loop counters, or direct API calls.
- Do not add comments that mirror function names.
- Do not keep comments that became stale after behavior changes.

## Updating Documentation with Code Changes
- If behavior, timing, configuration defaults, or API semantics change, update nearby documentation in the same change.
- Keep C++ headers, Python schema docs, and YAML example comments consistent.
- If a public API changes, update both declaration-side docs and user-facing example comments.

## Naming and Tone
- Use professional, plain English.
- Prefer precise technical terms over informal phrasing.
- Keep comments short and specific.
