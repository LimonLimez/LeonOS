# LeonOS

LeonOS is currently a minimal hobby OS milestone with two bootable QEMU images:
a stable BIOS-loaded 16-bit real-mode/unreal-mode graphical file browser, and
an experimental staged 32-bit protected-mode image.

This is not a Windows or Linux replacement. It does not have long mode, a real
process model, a general writable filesystem, or a full browser.

## What Works Now

- Builds a 1.44 MB bootable FAT12 floppy image at `dist/leonos.img`.
- Boots under legacy BIOS in QEMU.
- Loads `KERNEL.SYS` from the FAT12 data area.
- Parses the FAT12 root directory and follows FAT cluster chains to open files.
- Finds and enters a native VBE `1920x1080x32` linear-framebuffer mode in QEMU.
- Shows a native-pixel Windows-style desktop with a movable file-browser window,
  file list, viewer panel, taskbar, and readable BIOS 8x16 text rendering.
- Handles keyboard navigation through an IRQ1 PS/2 keyboard handler.
- Handles mouse movement/clicks through an IRQ12 PS/2 mouse handler.
- Can be tested headlessly by checking serial output and QEMU-injected input.
- Can be visually regression-tested from a QEMU framebuffer screenshot.

## Requirements

- Windows PowerShell 5+.
- NASM for building.
- QEMU for testing.

NASM and QEMU are not vendored in this repo. If they are not installed:

```powershell
winget install --id NASM.NASM --exact --accept-source-agreements --accept-package-agreements
winget install --id SoftwareFreedomConservancy.QEMU --exact --accept-source-agreements --accept-package-agreements
```

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
```

## 32-bit Protected-Mode Build

LeonOS also has an experimental protected-mode image at `dist32\leonos32.img`.
It keeps the 16-bit image as the stable baseline, but the 32-bit path now boots
through stage 2, enters protected mode, and runs the C framebuffer shell.

Check whether the optional C toolchain is installed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check-toolchain.ps1
```

Build the preview 32-bit kernel objects:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build32.ps1
```

This requires either LLVM (`clang`, `ld.lld`, `llvm-objcopy`) or an
`i686-elf-gcc` cross toolchain. If the toolchain is missing, the script exits
with install instructions. The current bootable OS image still builds with
`tools\build.ps1`.

Build the preview stage-2 loader artifact:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-stage2.ps1
```

`stage2.bin` is not wired into `dist\leonos.img` yet. It exists to establish
the next loader boundary without breaking the current bootable baseline.

Build and test the experimental 32-bit boot image:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-qemu.ps1
```

This produces `dist32\leonos32.img`. The test verifies stage 2, VBE
`1920x1080x32`, E820 handoff, protected-mode C entry, IDT/PIC setup, PS/2
keyboard input, PS/2 mouse input, FAT12 root scanning, read-only file open,
physical page allocation, paging, a page-backed heap smoke path, and timer/event
delivery through cooperative kernel tasks.

The 32-bit visual regression test captures and validates the native framebuffer:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-visual.ps1
```

It writes (default 1080p build):

- `dist32\leonos32-visual-test-1080p.ppm`
- `dist32\leonos32-visual-test-1080p-scaled.png`

The 32-bit shell uses a saved-background software cursor so mouse movement does
not repaint the full desktop on every packet.

### Resolution-aware GUI (32-bit)

The 32-bit desktop layout is driven by the actual VBE framebuffer width and
height passed through BootInfo, not hardcoded 1920x1080 coordinates. Window chrome,
file list, preview pane, status line, taskbar, cursor bounds, and mouse hit
testing all scale from the active mode.

Stage2 selects a 32-bit linear VBE mode at boot time. The build script chooses
the preferred size; if QEMU does not expose that exact mode, stage2 falls back
to another supported mode and prints the chosen size on serial, for example
`LeonOS stage2 VBE 1280x720x32 ready`. The kernel then prints
`LeonOS 32-bit framebuffer WxHx32`.

Supported guest modes (when QEMU's std VGA exposes them):

| Build `-Resolution` | Preferred | Fallback order (720p build) | Fallback (1080p build) |
| --- | --- | --- | --- |
| `1080p` (default) | 1920x1080 | 1366x768, 1280x720, 1024x768 | (same) |
| `720p` | 1280x720 | 1366x768, 1024x768, 1920x1080 | — |
| `768p` | 1024x768 | (1080p fallbacks) | — |
| `1366` | 1366x768 | (1080p fallbacks) | — |

Build for a smaller guest mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build32-image.ps1 -Resolution 720p
```

Visual regression at native 1080p (default build):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-visual.ps1
```

Smaller-mode visual regression (builds with `-Resolution 720p`, asserts 1280x720
capture and scaled UI probe points):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-visual-720p.ps1
```

Captures are named `leonos32-visual-test-<resolution>.ppm`.

**Honest limit:** resizing or maximizing the QEMU GTK window only changes
*host-side* scaling (`zoom-to-fit`). The guest VBE mode does not change unless
you rebuild with a different `-Resolution` preference (or we add runtime mode
switching later). `ScaledMaximize` runner mode uses GTK `zoom-to-fit=on` so a
large host window is usable without changing guest pixels.

Visible runners (FAT32 HDD example; **Small720p** is the default friendly mode):

```powershell
# Default: 1280x720 guest + GTK zoom-to-fit (recommended on laptops)
powershell -ExecutionPolicy Bypass -File .\tools\run32-hdd-qemu.ps1

# Native 1920x1080 guest, 1:1 window (no zoom-to-fit)
powershell -ExecutionPolicy Bypass -File .\tools\run32-hdd-qemu.ps1 -Mode Native1080p

# Same as default; explicit 720p + zoom-to-fit
powershell -ExecutionPolicy Bypass -File .\tools\run32-hdd-qemu.ps1 -Mode Small720p

# Host scales guest framebuffer to fit window (maximize-friendly)
powershell -ExecutionPolicy Bypass -File .\tools\run32-hdd-qemu.ps1 -Mode ScaledMaximize
```

The floppy-only runner `tools\run32-qemu.ps1` accepts the same `-Mode` values.

### Desktop shell and window management (32-bit)

The 32-bit kernel draws a cooperative desktop shell (`src32\kernel\shell_ui.inc.c`),
not a user-space compositor. It is inspired by the design reference
`assets\ui\leonos-desktop-spritesheet-transparent.png`, sliced at build time into
`src32\include\ui_sprites.h` via `tools\generate-ui-sprites.ps1` and
`assets\ui\spritesheet-manifest.json` (no runtime PNG decoder). Legacy bitmask
glyphs remain in `src32\include\ui_icons.h` as fallback only; see
`assets\ui\README.md`, `tools\verify-ui-sprites.ps1`, and `tools\verify-ui-icons.ps1`.

What works today:

- Up to six in-kernel windows (Files, About, Apps, Log) with z-order and focus.
- Draggable title bars, on-screen clamping, and a saved-background software cursor
  (no drag trails).
- Title-bar minimize (yellow), maximize/restore (green), and close (red) buttons
  with simple hover/pressed states where feasible.
- Taskbar with Start button, per-window buttons, and restore-from-minimized.
- Start menu (mouse, **S**, or **F1**) with icons and a visible header (not a debug box).
- Files window **tabs**: **Files** (sidebar list + preview pane) and **Preview**
  (full-width preview). List text is clipped to the sidebar on the Files tab.
- Default **Files** window fills most of the screen on 720p/1080p (not a tiny floating box).
- Larger UI chrome on 720p: taller title bar, tabs, taskbar, and 2x framebuffer text scaling.
- Start button, titlebar min/max/close, and Start menu icons blitted from generated sprites
  (simple per-pixel alpha blend; not a full compositor).
- Live keyboard shortcuts (QEMU often captures F-keys; letter keys are more reliable):
  **S** or **F1** start menu, **T** or **F2** or **Ctrl+Tab** Files tabs, **M** or **Alt+M**
  minimize, **X** or **Alt+X** maximize/restore, **C** or **Alt+C** close, **Alt+arrows**
  nudge window, **F7** serial “moved” proof for tests.

Automated coverage:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-shell-qemu.ps1
```

`tools\test32-visual.ps1` and `tools\test32-visual-720p.ps1` assert scaled UI
regions (taskbar, title bar, close button, sprite-colored Start/close glyphs,
active tab, sidebar/content panes) and, at 720p, an overlap guard so file-list
pixels do not spill into the preview pane.
`tools\test32-hdd-visual.ps1` uses the same probes on the FAT32 HDD boot path.

Honest limits: no GPU, no full alpha compositor, no runtime PNG loader, no real window
compositing, and no preemptive WM — windows are kernel data structures redrawn on
a dirty flag. QEMU runners no longer pass `-window-size` (unsupported on some hosts);
use `-Mode Small720p` / `ScaledMaximize` with GTK `zoom-to-fit` instead.
Headless `test32-qemu.ps1` / `test32-hdd-qemu.ps1` mouse runs only prove PS/2 init
and shell boot (monitor relative moves are not reliable enough to assert file-open
via click); keyboard and `test32-shell-qemu.ps1` cover file open and WM shortcuts.

## 32-bit FAT32 Hard-Disk Support

The 32-bit kernel can also read a FAT32 hard disk. The FAT12 floppy remains the
boot and regression baseline; the FAT32 disk is an additional read-only data
disk that the kernel mounts through its own ATA PIO driver (no BIOS calls).

Build the deterministic FAT32 image at `dist32\leonos32-hdd.img` from the
`fs32\` fixture directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build32-hdd.ps1
```

The image is a real FAT32 volume (over 65525 clusters) inside a single MBR
partition (type `0x0C`, FAT32 LBA) starting at LBA 2048.

Boot the 32-bit floppy with the FAT32 disk attached and verify it over serial:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-qemu.ps1
```

This attaches the floppy (`-boot a`) plus the FAT32 disk as the primary IDE
master and asserts that the kernel:

- detects the ATA primary master,
- parses the MBR and FAT32 BPB and mounts the volume,
- walks the root-directory cluster chain and lists `HELLO32.TXT`,
  `NOTES32.TXT`, and `README32.TXT`,
- follows a FAT32 cluster chain to open a file from the disk.

When a FAT32 disk is present the file browser lists and opens files from FAT32
instead of the FAT12 floppy. With no disk attached the kernel reports
`LeonOS 32-bit ATA no disk` and falls back to the FAT12 floppy path.

The matching visual regression for the FAT32 file browser:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-visual.ps1
```

It writes (default 1080p build):

- `dist32\leonos32-hdd-visual-test-1080p.ppm`
- `dist32\leonos32-hdd-visual-test-1080p-scaled.png`

### FAT32 write-back (one small root file)

The 32-bit kernel has a deliberately tiny FAT32 *write* path. Pressing `W`
while a FAT32 disk is mounted makes the kernel create-or-overwrite a single
short 8.3 root file, `WRITE32.TXT`, then read it back in the same boot. The
write path:

- allocates one free cluster from the FAT when the file does not already exist,
- updates that cluster's entry in **both** FAT copies,
- writes the file data into the cluster's sector (zero-padded),
- creates or overwrites the 8.3 root-directory entry, updating its first
  cluster and size fields,
- issues an ATA `CACHE FLUSH` after each sector write.

Verify it in QEMU:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-write-qemu.ps1
```

The test presses `W`, asserts the kernel's write and read-back serial lines,
quits QEMU, and then **independently re-parses the FAT32 image on the host**
(MBR, BPB, root directory, cluster chain) to confirm `WRITE32.TXT` contains the
expected bytes. It does not rely on kernel serial output alone.

This is the *entire* extent of FAT32 write support. There is no general
writable filesystem: only one small (<= 512-byte) 8.3 root file written through
the fixed `WRITE32.TXT` path is implemented and tested.

### LEO1 ring-0 cooperative app loader

The 32-bit kernel can load and run one external program from the FAT32 disk.
This is **not** user mode: it is a ring-0 cooperative flat binary with no
memory isolation, no address-space separation, and no syscall boundary.

`LEO1` is a tiny flat executable format with a 32-byte little-endian header:

| offset | field | notes |
| --- | --- | --- |
| 0 | magic `"LEO1"` | identifies the format |
| 4 | abi version | currently `1` |
| 8 | entry offset | from the start of the image |
| 12 | image size | bytes the loader copies |
| 16 | bss size | extra zeroed bytes after the image |
| 20..28 | reserved | zero |

The fixture app `src32\app\helloapp.asm` is assembled by NASM into
`HELLOAPP.LEO` and stored on the FAT32 volume by `tools\build32-hdd.ps1`.

Pressing `A` while the FAT32 disk is mounted makes the kernel:

- find `HELLOAPP.LEO` in the FAT32 root directory and read it,
- validate the `LEO1` header (magic, version, entry/image/bss bounds, and that
  the whole image fits in a single 4 KiB page),
- allocate one page-backed physical page, copy the image, and zero the rest,
- call the entry point in ring 0 with a fixed register contract: `EAX` = a
  pointer to an in-kernel `LeoAppApi` table (`serial_print`, `app_print`),
  `EBX` = the load base. The app is reached with `call` and returns with `ret`,
  and it preserves `EBX/ESI/EDI/EBP` like a normal cdecl callee.

The app proves it ran by calling `api->serial_print` (serial line
`HELLOAPP ran`) and `api->app_print`, which shows one line in the file-browser
content area. The kernel then frees the page.

Verify it in QEMU:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-app-qemu.ps1
```

The test presses `A` and asserts the loader and app serial lines, including
`LeonOS LEO1 app HELLOAPP.LEO loaded`, `HELLOAPP ran`, and
`LeonOS LEO1 app returned`.

Limits of the app loader (all intentional in this milestone): one fixed app
name, single-page (<= 4 KiB) flat images only, ring 0 only, no user mode, no
memory protection between the app and the kernel, no process/scheduler model,
no dynamic linking or relocations, and the only services are the two function
pointers in the fixed API table.

### Ring-3 user-mode app with an int 0x80 syscall path

The 32-bit kernel can also run one program in real **ring 3** (user mode). This
is a genuine privilege boundary, not the ring-0 loader above: the user app gets
no kernel function pointers and can only reach the kernel through a syscall
interrupt.

To make this safe the kernel replaces stage2's minimal GDT with its own GDT
that adds ring-3 user code/data descriptors and a TSS:

- kernel code/data stay at selectors `0x08`/`0x10` (unchanged meaning),
- user code is `0x1B` (`0x18` | RPL 3), user data is `0x23` (`0x20` | RPL 3),
- the TSS carries `ss0`/`esp0` so the CPU has a ring-0 stack to switch to when
  an `int 0x80` or hardware IRQ fires while the CPU is in ring 3.

`int 0x80` is installed as an IDT gate with DPL 3 so ring-3 code may invoke it.
The syscall ABI is deliberately tiny:

| `eax` | syscall | arguments |
| --- | --- | --- |
| `0` | `SYS_EXIT` | none; never returns to the app |
| `1` | `SYS_WRITE` | `ebx` = pointer to a NUL-terminated string in the app image or stack |
| `2` | `SYS_FB_INFO` | `ebx` = pointer to four `u32`s: width, height, pitch, bpp |
| `3` | `SYS_FB_FILL` | `ebx` = x, `ecx` = y, `edx` = w, `esi` = h, `edi` = 32-bit RGB color |
| `4` | `SYS_FB_PRESENT` | present the kernel-owned backbuffer to the visible framebuffer |
| `5` | `SYS_EVENT_POLL` | `ebx` = pointer to three `u32`s: type, data0, data1 |

`SYS_WRITE` validates that the pointer lies inside the loaded user image or
stack region and scans length-bounded, so a bad or unterminated pointer cannot
walk into kernel memory. The kernel mirrors the text to serial and one GUI
line.

`UHELLO.LEO` is a `LEO1` **version 2** image (`src32\app\uhello.asm`). It is
position-independent (a `call`/`pop` finds its own load base), prints via
`SYS_WRITE`, then calls `SYS_EXIT`. `UGFX.LEO` (`src32\app\ugfx.asm`) uses the
same ring-3 path, queries framebuffer info, draws through rectangle fill
syscalls, presents the backbuffer, polls one event slot, then exits.
`UCDEMO.LEO` (`src32\user\cdemo.c`) is built from freestanding C through the
LEO1 crt0/linker path in `src32\user\`. It proves C globals/strings and stack
syscall buffers work at the fixed user virtual base. None of these programs
references an in-kernel pointer.

Pressing `U` while the FAT32 disk is mounted makes the kernel:

- read `UHELLO.LEO`, validate the `LEO1` v2 header,
- allocate a contiguous user image region up to 1 MiB, read the whole FAT32
  file into it, zero its declared BSS/trailing pages, and allocate a 16 KiB
  user stack,
- map those physical pages into the fixed user virtual window at `0x40000000`
  with user access while other pages stay supervisor-only,
- `iret` into the entry point at ring 3 with interrupts enabled.

Pressing `G`, or launching **Run UGFX** from Programs, runs the framebuffer
syscall probe instead. Pressing `Ctrl+C`, or launching **Run C Demo** from
Programs, runs the freestanding C user app (plain `C` is the shell's
window-close shortcut; the `UWEB`/`USTREAM` probes likewise use `Ctrl+X` and
`Ctrl+S`).

When the app calls `SYS_EXIT`, the kernel switches back to the saved kernel
stack, unmaps the user virtual window, frees the physical pages, and continues.
Verify it:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-userapp-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-usergfx-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-userc-qemu.ps1
```

The test presses `U` and asserts the boundary serial lines, including
`LeonOS 32-bit GDT/TSS ring3 ready`, `LeonOS user mode enter UHELLO.LEO`,
`UHELLO ran via syscall`, `LeonOS user app exited`, and
`LeonOS user app returned to kernel`. The graphics test asserts
`UGFX framebuffer syscall app`, `LeonOS user fb info`, `LeonOS user fb fill`,
and `LeonOS user fb present`. The C test asserts `UCDEMO C userland app` plus
the framebuffer syscall lines. These tests fail on any loader error, bad
syscall pointer/number, `CPU exception`, or `PANIC`.

Limits of the user-mode path (all intentional in this milestone): fixed app
names only, contiguous flat images up to 1 MiB, one fixed 16 KiB user stack,
ring-3 execution of exactly one app at a time, a blocking cooperative call (not
a scheduled process), and only the tiny syscall set above. There is **no**
multitasking process table, no separate per-process page directory (the app
uses the kernel page directory plus one temporary user virtual mapping), no
per-process file handles, no `fork`/`exec` or shell, no SMP, and no security
hardening. The ring-0 `HELLOAPP.LEO` loader is unchanged and still runs via
`A`.

## 32-bit Browser

The 32-bit QEMU path now has a small Browser window backed by a polled RTL8139
network stack. With `-nic user,model=rtl8139`, LeonOS can:

**Quick launch (FAT32 HDD boot, network enabled):**

| Key / menu | App | What it does |
| --- | --- | --- |
| **F11** / Programs -> **Browser** | shell browser window | Opens the LeonOS-managed browser window with the normal desktop taskbar, mouse, titlebar, URL bar, HTTPS fetch/render path, forms, images, and page controls |

The older browser probes (`UBROWSER.LEO`, `UNETRUN.LEO`, `UWEB.LEO`,
`NETSURF.LEO`, `USTREAM.LEO`, and `UQJS.LEO`) remain on disk for regression
tests and direct developer hotkeys, but they are no longer separate user-facing
Programs entries.

Run with network:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run32-hdd-qemu.ps1 -Mode Native1080p -Network
```

Automated checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-ubrowser-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-unetsurf-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-netsurf-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-uweb-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-ustream-qemu.ps1
```

The 32-bit browser stack can:

- detect and initialize the RTL8139 PCI NIC;
- send and receive Ethernet frames through polled RX/TX rings;
- resolve QEMU's gateway MAC with ARP;
- send one ICMP echo request and receive the reply;
- resolve QEMU's DNS server MAC and send one UDP DNS A-record query for
  `www.google.com`;
- parse the DNS reply and print the returned IPv4 address on serial;
- open one TCP connection to the resolved `www.google.com:443`;
- send a minimal TLS 1.3 ClientHello with SNI `www.google.com` and ALPN
  `http/1.1`;
- derive TLS 1.3 handshake/application keys for `TLS_CHACHA20_POLY1305_SHA256`;
- verify the server Finished message;
- send an encrypted HTTPS
  `GET /search?q=google&gbv=1&hl=en`;
- decrypt Google HTTPS application records, parse the HTTP header boundary even
  when headers and body arrive in separate TLS records, and print Google's
  `HTTP/1.1 200` status on serial;
- strip simple HTML tags/entities into a fixed 8 KiB browser text buffer and
  build a bounded structured preview-block list from the same real HTML stream;
- build a bounded 64-node document tree from real HTML tags with parent/depth
  tracking and visible text-byte counts, giving the browser a real foundation
  for later layout, hit-testing, links, forms, and DOM work;
- compute a bounded 96-box viewport layout table from the structured preview,
  including line, link, and control box counters that change with the actual
  browser viewport size;
- bind preview link layout boxes back to normalized `href` targets and handle
  mouse clicks on those boxes, with a serial proof of the clicked URL;
- promote clicked same-host links into the active browser URL/target, queue a
  fresh HTTPS navigation attempt for that path, and serial-proof the navigation
  SYN without pretending the fetched page replaces the document yet;
- scan real inline `<style>` content into bounded CSS telemetry for rule,
  declaration, color, font, URL, and `display:none` hints, with a visible CSS
  summary block in the preview;
- store up to 16 simple CSS rules from real inline styles and match simple
  `tag`, `.class`, and `#id` selectors to preview flags such as hidden,
  centered, bold, large, and boxed;
- carry bounded render flags from real HTML attributes/tags for simple hidden,
  centered, bold, boxed input, and button-style preview blocks;
- scan real inline `<script>` contents into bounded JavaScript telemetry for
  bytes, tokens, functions, variables, `document`, `window`, and `location`
  references, and expose one tiny literal `document.write("text")` preview
  hook without claiming a DOM runtime;
- tokenize real HTML tag names from the decrypted response and count anchors,
  links, scripts, styles, images, forms, and inputs;
- parse real tag attributes from the decrypted response and count extracted
  `href`, `src`, and form `action` resource references;
- normalize fetchable `href`, `src`, and `action` targets into a bounded
  resource queue for the next multi-connection fetch milestone;
- start a second same-host HTTPS resource attempt for the first queued target
  and report its live phase plus either a real response status/byte count or
  an honest `TMO` timeout;
- draw a Browser window with back/forward placeholders, refresh/home reset,
  a URL bar, a large scrollable structured HTML preview pane, and an info
  drawer for the current network response plus parsed HTML structure/resource
  counters;
- wrap document text on word boundaries and draw a scroll thumb for long pages.
- run the upstream NetSurf monkey frontend/core as `NETSURF.LEO`, route
  NetSurf `https://` fetches through the LeonOS `SYS_NET_STREAM_*` API, pass
  real HTTP status/content-type metadata through `SYS_NET_STREAM_META`, fetch
  real Google homepage HTML plus image/CSS subresources, feed the real response
  bodies into NetSurf callbacks, decode and blit the real Google logo, and keep
  the browser resident for scrolling and typed HTTPS URLs. JavaScript is built
  but disabled by default because Duktape cannot run Google's modern script
  bundles reliably yet; this is an honest static HTML/CSS/image browser mode,
  not a fake Chrome page.
- expose a one-handle ring-3 HTTPS stream API (`SYS_NET_STREAM_OPEN`, `POLL`,
  `READ`, `META`, `CLOSE`) and prove it with `USTREAM.LEO` reading the real
  Google HTTPS body in 1 KiB chunks before closing cleanly. The NetSurf fetcher
  now queues fetch contexts against that one stream instead of immediately
  failing a resource request while the stream is busy, and the build can target
  another HTTPS startup URL with `ports\netsurf\build-leonos-probe.ps1 -StartUrl`.

Verify it:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-net-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-net-qemu.ps1 -Resolution 1080p
powershell -ExecutionPolicy Bypass -File .\tools\capture-browser.ps1 -Resolution 1080p
powershell -ExecutionPolicy Bypass -File .\tools\capture-browser.ps1 -Resolution 1080p -Info
powershell -ExecutionPolicy Bypass -File .\tools\capture-browser.ps1 -Resolution 1080p -Info -ClickFirstLink
```

Still intentionally unsupported: DHCP, routing beyond the QEMU user-net smoke
path, sockets, general-purpose TLS, certificate validation, full CSS
cascade/layout, computed styles, Chrome-class JavaScript compatibility, complete
DOM/Web APIs, event loop/timers, remote PNG/JPEG/GIF/WebP image decoding in the
NetSurf target, reliable clicked-page replacement after navigation,
queued-resource HTTPS GET completion, generalized or multi-host resource
fetching, arbitrary socket reads/writes, packet retransmission, multiple
simultaneous connections, and pixel-accurate web page rendering.
LeonOS now renders a bounded structured preview from real text/tags extracted
from Google HTTPS HTML, builds a bounded document tree, scans and applies a tiny
subset of inline CSS selector rules to preview flags, computes bounded viewport
layout boxes, hit-tests parsed anchor boxes, starts a same-host HTTPS navigation
attempt for clicked anchors, scans inline JavaScript into telemetry, applies
only tiny
preview-level tag/attribute hints, and exposes real parsed
tag/resource/style/script/DOM/layout counts plus a normalized resource queue and
same-host HTTPS resource/navigation attempt phase/result; it is not a full
browser engine yet. The NetSurf bridge streams the real Google Search response
body over HTTPS and renders through real NetSurf code. The current QEMU proof is
still a smoke test: Duktape and generated DOM bindings compile and start, the
fetcher reads via the one-handle LeonOS HTTPS stream API, queues later fetch
contexts until the stream is free, forwards real status/content-type metadata,
and feeds the real Google HTML body to NetSurf.
It does not prove full Google JavaScript execution, external resource loading,
or pixel-accurate layout.

To become a real browser equivalent, LeonOS still needs a much larger stack:
a general socket API, retransmission/timeouts, DHCP or configurable network
settings, multi-host DNS/TCP/TLS, certificate and time validation, a heap-backed
HTML DOM, CSS layout, font shaping, image codecs, input/forms, navigation
history, caching, multiple concurrent connections, and either a JavaScript
runtime port or a deliberately smaller non-JS browser goal. The current code is
building toward that in kernel space for now; a real production-style browser
will eventually need stronger user-space processes and memory isolation.

### Existing Browser Port Track

`ports\netsurf\` is the start of a real existing-browser port track. The target
is NetSurf because it is a C browser with a framebuffer-style frontend and a
smaller dependency surface than Chromium/Firefox/WebKit. The upstream source is
not vendored by default; use:

```powershell
powershell -ExecutionPolicy Bypass -File .\ports\netsurf\fetch-netsurf.ps1
```

The first LeonOS-side prerequisites are now real: ring-3 user code can query the
framebuffer, draw rectangles into the kernel-owned backbuffer, present it, poll
one event slot, allocate from a user heap, call a blocking HTTPS fetch syscall,
and use one active HTTPS stream handle with chunked reads. NetSurf's fetcher now
uses that stream handle, renders into the LeonOS framebuffer, accepts basic
keyboard/mouse events, and keeps a resident browser window alive. That is still
not a complete NetSurf port: the next required OS pieces are polished native
browser chrome, real file/sysclock/POSIX behavior, and a generalized socket/TLS
API instead of one global HTTPS stream.

## Test In QEMU

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test-qemu.ps1
```

The test starts QEMU without graphics, captures COM1 serial output, injects one
keyboard open and one mouse open through QEMU's monitor, and fails if the kernel
does not report both file-open events.

## Visual Regression Test

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test-visual.ps1
```

The visual test starts QEMU headlessly, captures the native 1920x1080
framebuffer through QEMU's monitor, validates key desktop/window colors and text
pixel density, and writes:

- `dist\leonos-visual-test.ppm`
- `dist\leonos-visual-test-scaled.png`

## Run With A Visible QEMU Window

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run-qemu.ps1
```

Close the QEMU window to stop it.

For a fullscreen QEMU window:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run-qemu.ps1 -FullScreen
```

The legacy 16-bit visible runner uses GTK zoom-to-fit by default.

For the 32-bit image, use `tools\run32-qemu.ps1` or `tools\run32-hdd-qemu.ps1`
with `-Mode Native1080p`, `Small720p`, or `ScaledMaximize` (see Resolution-aware
GUI above).

## Current Boot Path

1. BIOS loads `dist/leonos.img` at physical address `0x7C00`.
2. The boot sector reads the first FAT12 data clusters containing `KERNEL.SYS`.
3. The kernel initializes COM1 serial output and VBE `1920x1080x32`.
4. The kernel reads FAT and root-directory sectors from disk.
5. The GUI draws a native-pixel desktop and lists visible FAT12 files.
6. IRQ1 and IRQ12 handlers feed keyboard and mouse actions into the shell.

## Real OS Roadmap

The active architecture target is the staged 32-bit protected-mode OS:

1. Preserve this 16-bit system as the regression baseline.
2. Add a stage 2 loader for BIOS-only work.
3. Add a freestanding 32-bit C kernel.
4. Pass framebuffer, memory-map, and disk metadata through a fixed `BootInfo`
   structure.
5. Continue replacing demo-kernel behavior with real kernel services.

See `docs\REAL_OS_PLAN.md` for the milestone plan and bug policy.

## Current Limits

- Stable-image text is BIOS 8x16 ROM font rendering scaled 2x onto the 1080p
  surface. The 32-bit image uses its own small framebuffer glyph renderer.
- The native framebuffer renderer is slow under QEMU TCG, so the automated test
  gives the first paint time to complete before injecting input.
- FAT12 reads are implemented; FAT12 writes are not.
- The 32-bit kernel can read a FAT32 hard disk through an ATA PIO driver.
  FAT32 writing is limited to exactly one path: create-or-overwrite a single
  small (<= 512-byte) 8.3 root file named `WRITE32.TXT`, triggered by pressing
  `W`. The following FAT32 behavior is NOT supported:
  - writing or overwriting any file other than `WRITE32.TXT`;
  - files larger than one 512-byte cluster sector on the write path;
  - deleting or renaming files;
  - free-space/FSInfo accounting updates on write (FSInfo is left as-is);
  - subdirectories (only the root directory is listed; directory and volume
    entries are skipped);
  - long file names (LFN entries are skipped; only 8.3 short names are listed);
  - files larger than the 8 KiB preview buffer are truncated in the viewer;
  - disks other than the primary IDE master, and partitions other than the
    first MBR FAT32 partition (or a partitionless FAT32 volume at LBA 0);
  - booting LeonOS itself from the FAT32 disk (LeonOS still boots from the
    FAT12 floppy; the FAT32 disk is a data disk only).
- Root-directory files are supported; subdirectories are intentionally skipped.
- The stable image remains 16-bit real/unreal mode; the experimental image is
  32-bit protected mode.
- The GUI is a cooperative in-kernel desktop shell (simple WM, taskbar, start menu,
  tabs in Files), not a user-space compositor or full OS desktop.
- The 32-bit image has paging, kernel memory allocation, timer events, a
  cooperative in-kernel task runner, read-only FAT12, read FAT32 plus the
  single-file FAT32 write path described above, a ring-0 cooperative LEO1
  flat-app loader for one fixed single-page app, and a ring-3 user-mode path
  that runs fixed LEO1 v2 apps (`UHELLO.LEO`, `UGFX.LEO`, and `UCDEMO.LEO`)
  through a fixed user virtual window and an `int 0x80` syscall gate
  (`SYS_EXIT`, `SYS_WRITE`, framebuffer info/fill/present, and event poll).
  It also has a minimal QEMU-tested RTL8139 network/browser smoke path for ARP,
  ICMP, UDP DNS A-record queries for `www.google.com`, TCP port-443 TLS 1.3,
  encrypted HTTPS GETs, decrypted Google `HTTP/1.1 200` responses, a scrollable
  fixed-buffer text extraction of Google's returned HTML, and a NetSurf
  `NETSURF.LEO` smoke that fetches real Google Search body bytes through the
  LeonOS ring-3 HTTPS stream API, streams the real HTML body into NetSurf
  callbacks, reaches NetSurf body parsing, redraws, quits, and returns to the
  kernel. `NETSURF.LEO` now builds Duktape plus generated DOM bindings, but
  external scripts and modern Web APIs are still incomplete.
- The ring-3 path is a real privilege boundary (GDT user descriptors, a TSS,
  ring-0/ring-3 switching via `iret`, and a DPL-3 syscall gate), but it is NOT
  a process model. It still has:
  - no multitasking and no process table (exactly one app runs at a time, as a
    blocking cooperative call from the kernel main loop);
  - no separate per-process address space (the app shares the kernel page
    directory with one temporary user virtual mapping);
  - no per-process file handles, no `fork`/`exec`, and no user shell;
  - no preemptive scheduling of user code, no signals, no SMP, no sockets,
    no general TLS socket API, no Chrome-class JavaScript compatibility, no
    pixel-accurate full browser, and no real security hardening;
  - no general writable filesystem and no dynamic linking.
- LeonOS is a hobby OS, not a Windows or Linux replacement.
