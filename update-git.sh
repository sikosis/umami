#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if ! command -v git >/dev/null 2>&1; then
    echo "error: git is not installed or is not in PATH" >&2
    exit 1
fi

if ! command -v gum >/dev/null 2>&1; then
    echo "error: gum is not installed or is not in PATH" >&2
    echo "Install Gum, then run this script again." >&2
    exit 1
fi

if [ "$(git -C "$script_directory" rev-parse --is-inside-work-tree 2>/dev/null)" != "true" ]; then
    echo "error: $script_directory is not a Git working tree" >&2
    exit 1
fi

gum style --bold --foreground 105 "Updating umami's Git repository"

# Stage every addition, modification, and deletion in this repository.
git -C "$script_directory" add -A

if git -C "$script_directory" diff --cached --quiet; then
    gum style --foreground 214 "There are no changes to commit."
    exit 0
fi

gum style --bold "Staged changes"
git -C "$script_directory" status --short
printf '\n'

commit_title=$(gum input \
    --prompt "Commit title: " \
    --placeholder "Briefly describe the change")

if [ -z "$commit_title" ]; then
    gum style --foreground 196 "A commit title is required. The changes remain staged."
    exit 1
fi

commit_details=$(gum write \
    --header "Commit details (optional; Ctrl+D when finished)" \
    --placeholder "Explain what changed and why")

if [ -n "$commit_details" ]; then
    git -C "$script_directory" commit -m "$commit_title" -m "$commit_details"
else
    git -C "$script_directory" commit -m "$commit_title"
fi

branch=$(git -C "$script_directory" branch --show-current)
if [ -z "$branch" ]; then
    gum style --foreground 214 "Commit created in detached HEAD state; it was not pushed."
    exit 0
fi

if ! gum confirm --default=false "Push '$branch' now?"; then
    gum style --foreground 214 "Commit created locally and not pushed."
    exit 0
fi

if git -C "$script_directory" rev-parse --abbrev-ref '@{upstream}' >/dev/null 2>&1; then
    git -C "$script_directory" push
elif git -C "$script_directory" remote get-url origin >/dev/null 2>&1; then
    git -C "$script_directory" push --set-upstream origin "$branch"
else
    gum style --foreground 196 "No upstream branch or 'origin' remote is configured."
    exit 1
fi

gum style --bold --foreground 42 "Commit pushed successfully."
