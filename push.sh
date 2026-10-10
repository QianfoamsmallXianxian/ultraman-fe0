#!/data/data/com.termux/files/usr/bin/bash
set -e
R="${1:-ultraman-fe0}"
V="${2:-public}"
command -v git >/dev/null || { echo "pkg install git"; exit 1; }
command -v gh  >/dev/null || { echo "pkg install gh";  exit 1; }
gh auth status >/dev/null 2>&1 || gh auth login
[ -d .git ] || git init -b main
git config user.name  >/dev/null 2>&1 || git config user.name  "$(gh api user -q .login)"
git config user.email >/dev/null 2>&1 || git config user.email "$(gh api user -q .login)@users.noreply.github.com"
git add -A
git diff --cached --quiet || git commit -m "build $(date +%Y%m%d-%H%M)"
if gh repo view "$R" >/dev/null 2>&1; then
    git remote get-url origin >/dev/null 2>&1 || git remote add origin "$(gh repo view "$R" --json sshUrl -q .sshUrl)"
    git push -u origin main
else
    gh repo create "$R" --"$V" --source=. --remote=origin --description "Ultraman FE0" --push
fi
echo ""
echo "done: $(gh repo view "$R" --json url -q .url)"
