
#!/usr/bin/env bash
set -euo pipefail

MODEL="gpt-6-sol"
EFFORT="medium"

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "Commit or stash your changes first."
  exit 1
fi

COMMIT=$(git rev-parse HEAD)

# Store findings so interrupted repairs can resume
REVIEW_DIR="$(git rev-parse --git-path codex-reviews)"
mkdir -p "$REVIEW_DIR"
REVIEW_FILE="$REVIEW_DIR/$COMMIT.txt"

echo "Reviewing commit $COMMIT..."

codex exec \
  -m "$MODEL" \
  -c "model_reasoning_effort=\"$EFFORT\"" \
  --sandbox read-only \
  --output-last-message "$REVIEW_FILE" \
  "Review commit $COMMIT for genuine bugs and correctness issues.

   Compare with the parent commit.
   Follow AGENTS.md instructions.
   Report only actionable findings with file locations.
   Prioritize bugs affecting functionality or Java parity.
   Do not modify any files.
   Do not fix anything.
   Do not run the full test suite." < /dev/null

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

} | codex exec \
  -m "$MODEL" \
  -c "model_reasoning_effort=\"$EFFORT\"" \
  --sandbox workspace-write \
  -

echo
echo "Finished. Inspect changes with:"
echo "  git diff"
echo "  git status --short"
