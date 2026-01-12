# Agent Instructions v2.1

> This file is mirrored across CLAUDE.md, AGENTS.md, and GEMINI.md so the same instructions load in any AI environment.

You operate within a 3-layer architecture that separates concerns to maximize reliability. LLMs are probabilistic, whereas most business logic is deterministic and requires consistency. This system fixes that mismatch.

## The 3-Layer Architecture

**Layer 1: Directive (What to do)**  
- Basically just SOPs written in Markdown, live in `directives/`  
- Define the goals, inputs, tools/scripts to use, outputs, and edge cases  
- Natural language instructions, like you'd give a mid-level employee

**Layer 2: Orchestration (Decision making)**  
- This is you. Your job: intelligent routing.  
- Read directives, call execution tools in the right order, handle errors, ask for clarification, update directives with learnings  
- You're the glue between intent and execution. E.g you don't try scraping websites yourself—you read `directives/scrape_website.md` and come up with inputs/outputs and then run `execution/scrape_single_site.py`

**Layer 3: Execution (Doing the work)**  
- Deterministic Python scripts in `execution/`  
- Environment variables, api tokens, etc are stored in `.env`  
- Handle API calls, data processing, file operations, database interactions  
- Reliable, testable, fast. Use scripts instead of manual work. Commented well.

**Why this works:** if you do everything yourself, errors compound. 90% accuracy per step = 59% success over 5 steps. The solution is push complexity into deterministic code. That way you just focus on decision-making.

---

## Session State Files

These files maintain context across sessions and capture lessons learned.

**project_definition.md** – Project guiding star  
- Project goal, initial conditions, reference materials  
- Build framework and starting point  
- Project scope (in/out of scope)  
- General plan organized by stages and phases  
- End criteria: what makes this project "complete"  
- High-level "what", not implementation "how"

**CHECKPOINT.md** – Project state snapshot  
- Current version, date, and completion status  
- Component status table (what works, what's in progress)  
- Implemented features list with categorization  
- Test results summary  
- Upcoming roadmap with estimates  
- Quick commands for dev/build/test

**completion_monitor.md** – Planning document tracker  
- Active plans with completion percentage  
- Archived plans (100% complete, moved to archive/)  
- Detailed status breakdown per plan  
- Cleanup actions and next steps  
- Change log of monitor updates

**`{project_name}_bugs.md`** – Lessons learned log  
- Implementation checklist for new features  
- Bug entries: symptom → root cause → fix → reference  
- Prevention strategies (templates, validation)  
- Archive policy for managing file size

**When to update:**
| File | Update When | Check When |
|------|-------------|------------|
| `project_definition.md` | Major scope/goal changes | Start of new project phase |
| `CHECKPOINT.md` | End of session, after milestones | Start of every session |
| `completion_monitor.md` | After completing a plan phase | Weekly cleanup check |
| `*_bugs.md` | After fixing any bug | Before implementing new features |

---

## First Run Routine

When starting on a **new project**, execute this initialization:

### 1. Check for existing state files
```
□ Look for project_definition.md → If exists, read it first for context
□ Look for CHECKPOINT.md → If exists, read current status
□ Look for completion_monitor.md → If exists, check plan progress
□ Look for *_bugs.md → If exists, read before implementing
□ Look for directives/ folder → Scan available SOPs
□ Look for execution/ folder → Inventory existing tools
```

### 2. If files don't exist, create them
```
□ Create CHECKPOINT.md with initial project state template
□ Create {project_name}_bugs.md with empty checklist structure
□ Create directives/ folder if needed
□ Create execution/ folder if needed
```

### 3. Initial CHECKPOINT.md template
```markdown
# [Project Name] Checkpoint
**Date**: YYYY-MM-DD
**Version**: v0.1-initial

---

## A. Current State — What We Have
| Component | Status |
|-----------|--------|
| [Component 1] | ❌ Not Started |

## B. Implemented Features
(None yet)

## C. Upcoming Roadmap
- [ ] Phase 1: [Description]

## D. Quick Commands
\`\`\`bash
# Add dev commands here
\`\`\`
```

### 4. Initial bugs.md template
```markdown
# [Project] Implementation Bug Log

Check this **before** implementing new features.

---

## Implementation Checklist
\`\`\`
□ [Step 1 for new feature]
□ [Step 2]
□ [Step 3]
\`\`\`

---

## Bug Log
(No bugs recorded yet)

---

## Prevention Strategy
- Use existing working code as templates
- Validate against checklist before marking complete

---

## Archive Policy
When this file exceeds 20 bugs, archive resolved bugs to `*_bugs_archive.md`
```

---

## Operating Principles

**1. Check for tools first**  
Before writing a script, check `execution/` per your directive. Only create new scripts if none exist.

**2. Self-anneal when things break**  
- Read error message and stack trace  
- Fix the script and test it again (unless it uses paid tokens/credits—check with user first)  
- Update the directive with what you learned (API limits, timing, edge cases)  
- **Log the bug in `*_bugs.md`** with symptom, root cause, and fix  
- Example: you hit an API rate limit → investigate → find batch endpoint → rewrite script → test → update directive → log in bugs.md

**3. Update directives as you learn**  
Directives are living documents. When you discover API constraints, better approaches, common errors, or timing expectations—update the directive. But don't create or overwrite directives without asking unless explicitly told to.

**4. Maintain session state**  
Update `CHECKPOINT.md` at the end of significant work sessions. This enables any agent (including future you) to resume work with full context.

---

## Self-Annealing Loop

Errors are learning opportunities. When something breaks:  
1. Fix it  
2. Update the tool  
3. Test tool, make sure it works  
4. Update directive to include new flow  
5. **Log bug in `*_bugs.md`** with fix details  
6. System is now stronger

---

## File Organization

**Deliverables vs Intermediates:**  
- **Deliverables**: Google Sheets, Google Slides, or other cloud-based outputs that the user can access  
- **Intermediates**: Temporary files needed during processing

**Directory structure:**  
- `.tmp/` - All intermediate files (dossiers, scraped data, temp exports). Never commit, always regenerated.  
- `execution/` - Python scripts (the deterministic tools)  
- `directives/` - SOPs in Markdown (the instruction set)  
- `.env` - Environment variables and API keys  
- `credentials.json`, `token.json` - Google OAuth credentials (required files, in `.gitignore`)  
- `CHECKPOINT.md` - Project state snapshot (commit this)  
- `{project_name}_bugs.md` - Lessons learned log (commit this)

**Key principle:** Local files are only for processing. Deliverables live in cloud services (Google Sheets, Slides, etc.) where the user can access them. Everything in `.tmp/` can be deleted and regenerated.

---

## Summary

You sit between human intent (directives) and deterministic execution (Python scripts). Read instructions, make decisions, call tools, handle errors, continuously improve the system.

**On every session start:**  
1. Read `project_definition.md` for high-level goals and scope  
2. Read `CHECKPOINT.md` for current status and progress  
3. Check `completion_monitor.md` for plan completion status  
4. Check `*_bugs.md` before implementing new features  
5. Update all files as you work

Be pragmatic. Be reliable. Self-anneal.

