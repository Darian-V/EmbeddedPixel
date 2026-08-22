---
name: git-merge-and-tag
description: >-
  Use this skill to guide and execute branch merging into main and semantic version tagging.
  Enforces ground rules: prompts whether to merge into main, mandates new release tags on main,
  and determines semantic version bump levels (breaking=1st octet, large feature=2nd octet,
  bugfix/minor update=3rd octet).
---

# Git Merge and Semantic Tagging Runbook

This skill defines the standardized workflow and ground rules for merging feature branches into `main` and publishing semantic version tags.

---

## 1. Ground Rules & Prerequisites

Whenever a feature, refactor, or fix is ready for release:

1. **Explicit Merge Confirmation**:
   - **Always ask the user** if the current feature/topic branch should be merged into `main`.
   - If the user declines, remain on the feature branch without creating release tags.

2. **Mandatory Tagging on Main**:
   - **Every merge into `main` must produce a new release tag**.

3. **Semantic Versioning Level Rules**:
   Always evaluate or ask the user to classify the level of change to determine the appropriate version increment:

   | Version Level | Octet Incremented | Trigger Conditions | Example Bump |
   |---|---|---|---|
   | **Major** | **1st Octet** (`vX.0.0`) | **Breaking / Incompatible Changes**: Breaking protocol wire format, incompatible HAL/API redesign, major architectural overhaul. | `v1.2.1` $\rightarrow$ `v2.0.0` |
   | **Minor / Feature** | **2nd Octet** (`v1.X.0`) | **Large New Feature**: Adding substantial new functionality, new network services (OTA, multi-stream telemetry, CLI engine), or new hardware target support. | `v1.2.1` $\rightarrow$ `v1.3.0` |
   | **Patch / Revision** | **3rd Octet** (`v1.2.X`) | **Bugs & Minor Updates**: Bug fixes, performance optimizations, documentation consolidation, or minor tweaks to existing features. | `v1.2.0` $\rightarrow$ `v1.2.1` |

---

## 2. Step-by-Step Workflow

### Step 1: Inspect Current Tags & Branch State
```bash
# Check current branch and ensure working tree is clean
git status

# Inspect existing tags to find the latest version
git tag -l -n --sort=-v:refname
```

### Step 2: Determine Next Semantic Version
1. Identify current version (e.g. `v1.2.1`).
2. Classify change level with the user (Major vs Minor vs Patch).
3. Compute target version (e.g. `v1.3.0` for a new feature, `v1.2.2` for a patch).

### Step 3: Switch to Main & Merge
```bash
# Switch to main
git checkout main

# Merge feature branch
git merge <feature_branch_name>
```

### Step 4: Create Annotated Release Tag
Create an annotated tag on the merge commit on `main`:
```bash
git tag -a v<Major>.<Minor>.<Patch> -m "Release v<Major>.<Minor>.<Patch>: <Summary of feature or changes>"
```

### Step 5: Verify Tag Placement
```bash
# Verify tag points to HEAD of main
git show v<Major>.<Minor>.<Patch> --stat
```

### Step 6: Push to Remote
Prompt and provide the push commands for the user:
```bash
# Push main branch and newly created release tag
git push origin main
git push origin v<Major>.<Minor>.<Patch>
```

---

## 3. Safety Rules
- Never merge into `main` with uncommitted changes in the working tree.
- Never create lightweight tags for releases; always use annotated tags (`git tag -a ... -m "..."`).
- Never delete or move existing release tags without explicit user approval.
