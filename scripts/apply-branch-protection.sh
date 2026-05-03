#!/usr/bin/env bash
set -euo pipefail

repo="${1:-swifty99/dtu-esphome}"
branch="${2:-main}"

gh api \
  --method PUT \
  "repos/${repo}/branches/${branch}/protection" \
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
  "restrictions": null,
  "required_linear_history": true,
  "allow_force_pushes": false,
  "allow_deletions": false
}
JSON
