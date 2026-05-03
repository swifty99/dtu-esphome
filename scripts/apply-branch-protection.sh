#!/usr/bin/env bash
set -euo pipefail

repo="${1:-swifty99/dtu-esphome}"
branch="${2:-main}"

gh api \
  --method PUT \
  "repos/${repo}/branches/${branch}/protection" \
  --field required_linear_history=true \
  --field allow_force_pushes=false \
  --field allow_deletions=false \
  --input - <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "lint",
      "pytest",
      "compile"
    ]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": null,
  "restrictions": null
}
JSON
