#!/usr/bin/env bash
set -euo pipefail

MODEL="${MODEL:-claude-opus-5-5}"
EFFORT="${EFFORT:-medium}"

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "Commit or stash your changes first."
  exit 1
fi

COMMIT=$(git rev-parse HEAD)

# Store findings per commit; each run overwrites the previous review
REVIEW_DIR="$(git rev-parse --git-path claude-reviews)"
mkdir -p "$REVIEW_DIR"
REVIEW_FILE="$REVIEW_DIR/$COMMIT.txt"

echo "Reviewing commit $COMMIT..."

# Read-only review: only inspection tools, no edits, no test runs.
if ! claude -p \
  --model "$MODEL" \
  --effort "$EFFORT" \
  --permission-mode default \
  --allowedTools "Read" "Grep" "Glob" \
    "Bash(git show:*)" "Bash(git diff:*)" "Bash(git log:*)" "Bash(git status:*)" \
  --disallowedTools "Edit" "Write" "NotebookEdit" \
  > "$REVIEW_FILE" <<EOF
Review commit $COMMIT for genuine bugs and correctness issues.

Compare with the parent commit.
Follow AGENTS.md instructions.
Report only actionable findings with file locations.
Prioritize bugs affecting functionality or Java parity.
Do not modify any files.
Do not fix anything.
Do not run the full test suite.
EOF
then
  echo "Review failed."
  rm -f "$REVIEW_FILE"
  exit 1
fi

echo
echo "Review findings:"
cat "$REVIEW_FILE"

if [[ ! -s "$REVIEW_FILE" ]]; then
  echo "No review output generated."
  exit 1
fi

echo
read -r -p "Fix the review findings? [y/N] " answer

if [[ ! "$answer" =~ ^[Yy]$ ]]; then
  exit 0
fi

echo "Applying review fixes..."

{
  cat <<EOF
You are fixing findings from a previous review
of commit $COMMIT.

Do not repeat the general code review.

Instructions:
- Verify each finding before making changes.
- Fix genuine correctness issues.
- Ignore false positives and stylistic suggestions.
- Keep changes minimal.
- The Java project is the source of truth.
- Run only focused tests with a 120-second timeout.
- Never run the entire test suite.
- Do not commit or push.

ORIGINAL REVIEW FINDINGS:
EOF

  cat "$REVIEW_FILE"

} | claude -p \
  --model "$MODEL" \
  --effort "$EFFORT" \
  --permission-mode acceptEdits \
  --allowedTools "Read" "Grep" "Glob" "Edit" "Write" \
    "Bash(git show:*)" "Bash(git diff:*)" "Bash(git log:*)" "Bash(git status:*)" \
    "Bash(timeout 120 bun test:*)" \
  --disallowedTools "Bash(git commit:*)" "Bash(git push:*)" "Bash(bun run verify:*)"

echo
echo "Finished. Inspect changes with:"
echo "  git diff"
echo "  git status --short"
