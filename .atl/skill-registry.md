# Skill Registry — ibarra-iot-gateway

**Generated**: 2026-05-25

## User Skills (opencode)

| Skill | Path | Trigger |
|-------|------|---------|
| sdd-init | `~/.config/opencode/skills/sdd-init/SKILL.md` | SDD initialization |
| sdd-explore | `~/.config/opencode/skills/sdd-explore/SKILL.md` | SDD explore |
| sdd-propose | `~/.config/opencode/skills/sdd-propose/SKILL.md` | SDD proposal |
| sdd-spec | `~/.config/opencode/skills/sdd-spec/SKILL.md` | SDD specs |
| sdd-design | `~/.config/opencode/skills/sdd-design/SKILL.md` | SDD design |
| sdd-tasks | `~/.config/opencode/skills/sdd-tasks/SKILL.md` | SDD tasks |
| sdd-apply | `~/.config/opencode/skills/sdd-apply/SKILL.md` | SDD apply |
| sdd-verify | `~/.config/opencode/skills/sdd-verify/SKILL.md` | SDD verify |
| sdd-archive | `~/.config/opencode/skills/sdd-archive/SKILL.md` | SDD archive |
| sdd-onboard | `~/.config/opencode/skills/sdd-onboard/SKILL.md` | SDD onboarding |
| issue-creation | `~/.config/opencode/skills/issue-creation/SKILL.md` | GitHub issue creation |
| branch-pr | `~/.config/opencode/skills/branch-pr/SKILL.md` | Pull request creation |
| go-testing | `~/.config/opencode/skills/go-testing/SKILL.md` | Go/Bubbletea testing |
| judgment-day | `~/.config/opencode/skills/judgment-day/SKILL.md` | Parallel adversarial review |
| skill-creator | `~/.config/opencode/skills/skill-creator/SKILL.md` | New skill creation |
| skill-registry | `~/.config/opencode/skills/skill-registry/SKILL.md` | Skill registry update |

## Project Conventions

| File | Status |
|------|--------|
| AGENTS.md | ❌ Not found |
| CLAUDE.md | ❌ Not found |
| .cursorrules | ❌ Not found |
| GEMINI.md | ❌ Not found |

## Project-Specific Notes

**Stack**: C++17 with CMake + GTest (primary), Python 3 (calibration tools), Node.js (legacy edge services)
**Target**: Rockchip RV1106 (armhf, uClibc, 128MB RAM)
**Testing**: GTest with MockEngine/MockOcrEngine/MockFlashTrigger for SIL; Dockerfile.test for CI; 57 total tests
**Architecture**: RAII + DI + Producer-Consumer threading; OTA agent with Ed25519-signed state machine
**SDD**: Hybrid mode (engram + openspec files); current change: `vision-pipeline-contracts` (archived)
