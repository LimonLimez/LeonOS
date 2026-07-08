# LeonOS test guide

Run tests from the repo root with PowerShell 7 (`pwsh`) on Linux/macOS or Windows
PowerShell 5+.

## Quick smoke

```powershell
pwsh -File tools/build.ps1
pwsh -File tools/build32-image.ps1
pwsh -File tools/test-qemu.ps1
pwsh -File tools/test32-qemu.ps1
```

## Prerequisites by test class

| Test group | Needs | Typical runtime |
|------------|-------|-----------------|
| `test-qemu.ps1`, `test32-qemu.ps1` | floppy image only | ~30s |
| `test32-shell-qemu.ps1` | HDD image (`build32-hdd.ps1`) | ~45s |
| `test32-visual*.ps1` | VBE 1080p/720p build | ~20s each |
| `test32-net-qemu.ps1` | QEMU user networking (`-nic user`) | ~2 min |
| `test32-browser-*-qemu.ps1` | HDD + network | ~30s–3 min |
| `test32-hdd-*-qemu.ps1` | HDD image | ~15s–10 min |
| `test32-hdd-netsurf-qemu.ps1` | NetSurf probe build + network | 5–9 min |

## User-app hotkeys (32-bit HDD)

These kernel dev shortcuts use Ctrl modifiers so they do not collide with shell
window-management keys:

| App | Hotkey |
|-----|--------|
| UCDEMO | `Ctrl+C` |
| UWEB | `Ctrl+X` |
| USTREAM | `Ctrl+S` |
| NetSurf probe | `M` (in shell) or Programs where listed |

## Network tests

Tests that open real HTTPS URLs require QEMU's user-mode network
(`-nic user,model=rtl8139`). They may fail offline or when remote sites change.
Increase timeouts for slow links:

```powershell
pwsh -File tools/test32-hdd-netsurf-qemu.ps1 -TimeoutSeconds 520
```

## Line endings

HDD payload tests under `fs32/` compare file sizes from the working tree. Do not
rewrite those files to LF-only without updating the tests or pinning CRLF in
`.gitattributes`.

## CI

GitHub Actions workflow `.github/workflows/ci.yml` runs build + core boot, shell,
browser selftests, and one visual regression on Ubuntu.
