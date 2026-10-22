# Issue tracker: GitHub

Issues and specs live in GitHub Issues for TonyinBio/sts2-cpp.
Use the gh CLI, inferring the repository from the current git remote.

## Operations

- Create: gh issue create --title "..." --body-file <file>
- Read: gh issue view <number> --json number,title,body,labels,comments
- List: gh issue list --state open --json number,title,body,labels
- Comment: gh issue comment <number> --body-file <file>
- Label: gh issue edit <number> --add-label "..." or --remove-label "..."
- Close: gh issue close <number> --comment "..."

For multiline bodies, write the exact text to a temporary file and use
--body-file.

When a skill says "publish to the issue tracker", create a GitHub issue.
When it says "fetch the relevant ticket", read the GitHub issue.

## Pull requests as a triage surface

**PRs as a request surface: no.**

## Wayfinding

Represent a map as an issue labelled wayfinder:map. Link child tickets
as GitHub sub-issues; if unavailable, use a task list in the map and
"Part of #<map>" in each child.

Label children wayfinder:research, wayfinder:prototype,
wayfinder:grilling, or wayfinder:task.

Use native GitHub issue dependencies for blockers. If unavailable,
record "Blocked by: #<number>" in the child. A child is unblocked
when every blocker is closed.

Select the first open, unassigned, unblocked child in map order.
Claim it with gh issue edit <number> --add-assignee @me.
Resolve it by commenting with the result, closing it, and adding
a result summary and link to the map's Decisions-so-far.
