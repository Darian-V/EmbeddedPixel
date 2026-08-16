---
name: git-granular-commits
description: >-
  Use this skill to group unstaged and modified git changes into logical, atomic,
  granular commits with structured messages (including project ticket IDs) and push
  safely to the remote repository.
---

# Git Granular Commit & Push Runbook

This skill defines the workflow for organizing workspace changes into clean, atomic, granular Git commits and publishing them to the remote tracking branch.

---

## 1. Inspect Workspace State

Before staging, inspect the modified, untracked, and deleted files:

```bash
git status
```

Review modified diffs per component:
```bash
git diff --stat
```

---

## 2. Logical Granular Grouping Strategy

Group files into discrete atomic commits following strict separation of concerns. Never lump documentation, business logic, headers, and binary artifacts together in a single commit.

### Standard Granular Commit Order

| Commit Type | Scope / Paths | Example Commit Message |
|---|---|---|
| **1. Documentation & Architecture** | `docs/`, `*.md` | `EP-XXX docs: Add architecture specification for ...` |
| **2. Interface & Protocol Types** | `core/hal/`, `components/*/proto/`, API headers | `EP-XXX net: Define binary wire protocol headers and helpers` |
| **3. Implementation & Services** | `components/*/services/`, business logic | `EP-XXX net: Implement UDP discovery and telemetry services` |
| **4. Integration & Configuration** | `boards/`, `apps/*/main.cpp`, `CMakeLists.txt`, config files | `EP-XXX app: Integrate services into main.cpp and tune RTOS heap` |
| **5. Staged Binaries & Artifacts** | `boards/*/apps/*/programming_files/` | `EP-XXX build: Update staged flashable binaries` |

---

## 3. Commit Execution Workflow

Execute granular commits sequentially until the working tree is clean:

### Step 1: Stage Specific Component Files
```bash
git add <path/to/component_files>
```

### Step 2: Verify Staged Changes
```bash
git diff --cached --stat
```

### Step 3: Commit with Conventional Ticket Format
Follow the project's commit prefix convention (e.g. `EP-<ticket_id>` or `TICKET-<id>`):
```bash
git commit -m "EP-XXX <scope>: <Imperative summary of changes>"
```

### Step 4: Repeat for Each Logical Component
Repeat Steps 1–3 for each logical grouping until `git status` reports `nothing to commit, working tree clean`.

---

## 4. Final Review & Push

1. **Review Local Commit History**:
   ```bash
   git log -n 10 --oneline
   ```

2. **Verify Branch & Upstream**:
   Ensure you are on the intended branch (`main` or feature branch):
   ```bash
   git branch -vv
   ```

3. **Push to Remote Server**:
   ```bash
   git push origin <current_branch>
   ```

---

## 5. Safety Rules & Guardrails

- **No Destructive Operations**: Never run `git reset --hard`, `git push --force`, or `git clean -fd` without explicit user request.
- **Atomic Integrity**: Each commit must compile independently whenever possible.
- **Sensitive Files**: Never stage secrets, local credentials, or private `.env` files.
