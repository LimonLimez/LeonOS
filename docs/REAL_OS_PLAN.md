# LeonOS Real OS Plan

LeonOS is moving from a 16-bit graphical demo kernel toward a staged 32-bit
hobby operating system. The current working BIOS/FAT12/QEMU path stays as the
regression baseline until the protected-mode kernel can replace it.

## Milestone 0: Stable Baseline

- Keep the current NASM boot sector and 16-bit kernel bootable.
- Keep native VBE `1920x1080x32` framebuffer output working in QEMU.
- Keep FAT12 read-only root-file browsing working.
- Keep IRQ1 keyboard navigation and IRQ12 PS/2 mouse input working.
- Keep cursor redraw clean, with no visible trails.
- Keep a readable Windows-style desktop shell.
- Update documentation whenever supported behavior changes.
- Require all known reproducible bugs in claimed behavior to be fixed before
  moving on.

## Milestone 1: Toolchain And Layout

- Add a freestanding 32-bit C toolchain path using Clang/LLD or an `i686-elf`
  cross compiler.
- Keep NASM for the boot sector, loader, interrupt stubs, and port I/O stubs.
- Split the binary layout into boot sector, stage 2 loader, and kernel image.
- Keep deterministic FAT12 image generation.
- Current implementation status:
  - `tools\check-toolchain.ps1` detects NASM plus the optional 32-bit C
    toolchain.
  - `tools\build32.ps1` builds a preview `kernel32.elf` and `kernel32.bin` when
    LLVM or `i686-elf-gcc` is installed.
  - `tools\build-stage2.ps1` builds a preview `stage2.bin` artifact with A20
    enablement and a minimal `BootInfo` stub.
  - `tools\build32-image.ps1` builds `dist32\leonos32.img`, an experimental
    protected-mode image.
  - `tools\test32-qemu.ps1` verifies that stage 2 enters protected mode,
    reaches the 32-bit C kernel, and handles QEMU-injected keyboard and mouse
    file-open input.
  - `tools\test32-visual.ps1` verifies the 32-bit native 1920x1080 framebuffer
    shell by pixel-checking a QEMU screenshot.
  - `src32\include\bootinfo.h` defines the planned loader-to-kernel handoff.
  - `src32\kernel\entry.asm` and `src32\kernel\kernel.c` provide the first
    freestanding 32-bit kernel entry point.

## Milestone 2: Stage 2 Loader

- Move BIOS-only work out of the kernel and into stage 2.
- Stage 2 will gather VBE framebuffer details and the E820 memory map.
- Stage 2 will load the 32-bit kernel and pass a fixed `BootInfo` structure.
- The protected-mode kernel must not call BIOS.
- Current implementation status:
  - The experimental 32-bit image embeds `kernel32.bin` into stage 2, copies it
    to 1 MiB in protected mode, passes `BootInfo` in `EBX`, and verifies C entry
    over serial.
  - BIOS E820 memory-map collection is passed through `BootInfo`.
  - BIOS VBE `1920x1080x32` framebuffer collection is passed through
    `BootInfo`.
  - Stage 2 preloads FAT, root-directory, and a bounded data-sector cache for
    the current read-only FAT12 shell.

## Milestone 3: 32-bit Kernel Core

- Add GDT, IDT, PIC, PIT, exception handlers, serial logging, and panic output.
- Add a physical memory bitmap from E820.
- Add a minimal heap allocator.
- Add clear failure paths instead of silent hangs.
- Current implementation status:
  - The 32-bit kernel installs an IDT for CPU exceptions.
  - The PIC is remapped. IRQ1 keyboard, IRQ2 cascade, and IRQ12 mouse are
    unmasked for the current shell, and IRQ0 timer ticks feed the event queue.
  - Unhandled exceptions report vector, error code, and EIP over serial before
    halting.
  - E820-backed physical page allocation is implemented for the first 32 MiB and
    smoke-tested with alloc/free/reuse checks.
  - 32-bit paging is enabled with identity maps for low memory and the VBE
    framebuffer.
  - The kernel heap now has a page-backed allocation path and boot smoke test.
  - A general driver model is still pending.

## Milestone 4: Drivers And UI Port

- Port keyboard, mouse, framebuffer rendering, and read-only FAT12 to the C
  kernel services.
- Preserve current user behavior: boot, file list, file preview, keyboard open,
  mouse open, and native 1080p output.
- Current implementation status:
  - The 32-bit kernel scans the preloaded FAT12 root directory and opens files
    from the preloaded data cache.
  - The 32-bit framebuffer shell is a cooperative desktop shell / simple WM
    (`shell_ui.inc.c`) scaled to the active VBE mode (1920x1080, 1366x768,
    1280x720, or 1024x768). It manages multiple kernel-drawn windows (Files with
    Files/Preview tabs, About, Apps, Log), a taskbar, Start menu, draggable chrome,
    and minimize/maximize/close controls.     Sprite sheet `assets/ui/leonos-desktop-spritesheet-transparent.png` is converted
    at build time to `src32/include/ui_sprites.h` (`tools/generate-ui-sprites.ps1`,
    `assets/ui/spritesheet-manifest.json`, `tools/verify-ui-sprites.ps1`). No runtime
    PNG load. Legacy `ui_icons.h` is fallback only.
    Default Files window uses most of the work area on 720p/1080p; 720p uses larger
    chrome and 2x glyph scaling (`gui_font_scale`). Live keys **S/T/M/X/C** mirror
    F-keys when QEMU grabs function keys. The Files tab layout fixes
    720p list/preview overlap (clipped sidebar list on tab 0; full-width preview
    on tab 1). Stage2 picks a build-time preferred mode with serial fallback;
    layout and hit testing use BootInfo width/height. `tools\test32-visual.ps1`,
    `tools\test32-visual-720p.ps1` (includes a 720p overlap guard), and
    `tools\test32-shell-qemu.ps1` (keyboard shortcuts + serial proofs) cover the
    shell.     `tools\run32-qemu.ps1` and `tools\run32-hdd-qemu.ps1` default to `Small720p`
    (720p guest + GTK zoom-to-fit). Also: `Native1080p`, `ScaledMaximize`. No
    `-window-size` (some QEMU builds reject it). Still no compositor or runtime PNG
    loader; UI sprites use simple per-pixel alpha over the framebuffer.
  - IRQ1 keyboard navigation and IRQ12 PS/2 mouse movement/clicks are
    implemented and tested in QEMU.
  - The cursor uses saved-background restore on mouse movement to avoid trails
    and avoid full-screen repaint flashing.
  - The 32-bit kernel has a BIOS-free ATA PIO (primary master) read-only disk
    driver. When a FAT32 disk is attached it parses the MBR, parses the FAT32
    BPB, walks the root-directory cluster chain, lists 8.3 files, and follows
    FAT32 cluster chains to open files. With no disk it reports
    `LeonOS 32-bit ATA no disk` and falls back to the FAT12 floppy path, so the
    existing floppy tests remain the regression baseline.
  - `tools\build32-hdd.ps1` builds the deterministic FAT32 image
    `dist32\leonos32-hdd.img` from the `fs32\` fixture directory.
  - `tools\test32-hdd-qemu.ps1` boots the floppy with the FAT32 disk attached
    and asserts ATA detection, FAT32 mount, root listing, and a FAT32 file
    open. `tools\test32-hdd-visual.ps1` validates the FAT32 file browser
    framebuffer.
  - BootInfo was intentionally not extended: the protected-mode kernel owns the
    ATA driver and reads FAT32 metadata directly from the disk, so stage2 needs
    no new fields and the FAT12 floppy handoff is unchanged.
  - A minimal FAT32 write path exists: pressing `W` makes the kernel
    create-or-overwrite a single small (<= 512-byte) 8.3 root file
    (`WRITE32.TXT`). It allocates one free cluster when needed, updates both FAT
    copies, writes the data sector, updates the directory entry's first-cluster
    and size fields, and issues an ATA `CACHE FLUSH`. It then reads the file
    back in the same boot. `tools\test32-hdd-write-qemu.ps1` drives this and
    independently re-parses the FAT32 image on the host to confirm the bytes.
  - FAT32 write support stops there: no other file names, no multi-sector write
    files, no delete/rename, no FSInfo free-space updates, and no FAT12 writes.
  - Subdirectories, long file names, ATA devices other than the primary master,
    partitions other than the first MBR FAT32 partition, process launching, and
    compositor behavior are not implemented.

## Milestone 5: OS Groundwork

- Add a timer-backed kernel event queue.
- Add cooperative kernel tasks.
- Keep UI responsive while background kernel work runs.
- Do not claim process isolation or user mode until implemented and tested.
- Current implementation status:
  - Timer, keyboard, and mouse events are queued in-kernel.
  - A cooperative in-kernel task runner schedules event, desktop, and monitor
    tasks.
  - This is not process scheduling: tasks are built into the kernel and do not
    have user-mode isolation.

## Milestone 6: First External Program Loading

- Add a tiny flat executable format and load one real program from disk.
- Keep it honest: ring-0 cooperative execution, not user mode.
- Current implementation status:
  - `LEO1` is a 32-byte-header flat executable format (magic, abi version,
    entry offset, image size, bss size, reserved).
  - `src32\app\helloapp.asm` is assembled by NASM to `HELLOAPP.LEO` and stored
    on the FAT32 volume by `tools\build32-hdd.ps1`.
  - Pressing `A` makes the 32-bit kernel read `HELLOAPP.LEO` from FAT32,
    validate the `LEO1` header, copy the image into one page-backed page, zero
    the remainder, and call the entry point in ring 0.
  - The kernel passes a fixed in-kernel API table by register contract
    (`EAX` = `LeoAppApi*` with `serial_print`/`app_print`, `EBX` = load base).
    The app returns cooperatively and the kernel frees the page.
  - `tools\test32-hdd-app-qemu.ps1` boots the FAT32 disk, presses `A`, and
    asserts the loader and app serial lines.
  - This is explicitly NOT a process model: there is no user mode, no memory
    isolation between the app and the kernel, no syscall boundary (the app
    calls in-kernel function pointers directly), no scheduler/process
    abstraction, no relocations, and no dynamic linking. Only one fixed app
    name and single-page (<= 4 KiB) flat images are supported.

## Milestone 7: First Ring-3 User Mode And Syscall Gate

- Cross the privilege boundary for real: run one program in ring 3 that can
  only reach the kernel through a syscall interrupt, not through function
  pointers.
- Keep it honest: this is ring-3 execution plus a tiny syscall path, not a
  process/scheduler model.
- Current implementation status:
  - The kernel installs its own GDT (replacing stage2's minimal one) with the
    kernel code/data selectors unchanged (`0x08`/`0x10`) plus ring-3 user code
    (`0x18`, used as `0x1B`) and user data (`0x20`, used as `0x23`), and a TSS
    (`0x28`). The TSS only carries `ss0`/`esp0` so the CPU has a ring-0 stack to
    switch to on an `int 0x80` / IRQ taken from ring 3. `gdt_flush`/`tss_flush`
    live in `src32\kernel\interrupts.asm`; `gdt_init` runs from `kmain`.
  - `int 0x80` is an IDT gate with DPL 3, routed through the existing
    `isr_common`/`isr_dispatch` path (vector `0x80`) to `syscall_dispatch`.
  - Syscalls: `SYS_EXIT` (eax=0), `SYS_WRITE` (eax=1, ebx=string pointer),
    framebuffer info/fill/present, and event poll. `SYS_WRITE` validates the
    pointer against the loaded user image/stack virtual range and scans
    length-bounded before printing.
  - `enter_user_mode` saves the kernel context and `iret`s to ring 3; the
    `SYS_EXIT` handler calls `resume_to_kernel`, which restores the saved kernel
    stack so the launch behaves like a blocking call that returns after exit.
  - `src32\app\uhello.asm` assembles to `UHELLO.LEO`, a `LEO1` *version 2*
    position-independent image stored on the FAT32 volume. Pressing `U` maps it
    into the fixed user virtual window at `0x40000000` and enters ring 3. The
    app prints via `SYS_WRITE` and exits via `SYS_EXIT`; it never touches a
    kernel pointer.
  - `src32\app\ugfx.asm` assembles to `UGFX.LEO`, a multi-page framebuffer
    syscall probe. `src32\user\cdemo.c` builds through the LEO1 C crt0/linker
    path into `UCDEMO.LEO`, proving freestanding C globals/strings and stack
    syscall buffers work in ring 3.
  - `tools\test32-hdd-userapp-qemu.ps1` boots the FAT32 disk, presses `U`, and
    asserts the boundary lines (`GDT/TSS ring3 ready`,
    `user mode enter UHELLO.LEO`, `UHELLO ran via syscall`, `user app exited`,
    `user app returned to kernel`), failing on any loader error, bad syscall
    pointer/number, `CPU exception`, or `PANIC`.
  - Still NOT implemented (intentional): multitasking / a process table (one app
    runs at a time as a blocking cooperative call), separate per-process address
    spaces (the app shares the kernel page directory with one temporary user
    virtual mapping), per-process file handles, `fork`/`exec` or a user shell,
    preemptive scheduling of user code, signals, SMP, user-space sockets, a
    user-space browser API, and security hardening. The ring-0 `HELLOAPP.LEO` loader/test from Milestone 6 is
    preserved unchanged.

## Milestone 8: Minimal Browser Networking

- Add the smallest QEMU-proven browser surface first; do not claim a full
  browser until page parsing/rendering and a usable navigation model exist and
  are tested.
- Current implementation status:
  - The 32-bit kernel detects and initializes QEMU's RTL8139 PCI NIC.
  - The NIC runs with masked interrupts and polled RX/TX rings.
  - `tools\test32-net-qemu.ps1` boots with `-nic user,model=rtl8139` and now
    requires serial proof for:
    - gateway ARP request and gateway ARP resolution;
    - one ICMP echo request and reply;
    - DNS-server ARP request and resolution;
    - one UDP DNS A-record query for `www.google.com` and a parsed IPv4 answer;
    - one TCP connection to the resolved `www.google.com:443`;
    - one minimal TLS 1.3 ClientHello with SNI `www.google.com`;
    - one received Google TLS ServerHello;
    - derived TLS 1.3 handshake/application keys for
      `TLS_CHACHA20_POLY1305_SHA256`;
    - verified server Finished, encrypted client Finished, and encrypted
      HTTPS `GET /search?q=google&gbv=1&hl=en`;
    - one decrypted Google `HTTP/1.1 200` status response;
    - decrypted Google HTML body text crossing TLS record boundaries;
    - one HTML tokenizer pass that counts actual anchors, links, scripts,
      styles, images, forms, and inputs from the decrypted response;
    - a bounded structured preview-block list built from that same real HTML
      stream for titles, headings, links, list items, body text, and image
      placeholders;
    - a bounded 64-node document tree from real HTML tags with parent/depth
      tracking and visible text-byte counters;
    - a bounded 96-box viewport layout table from the structured preview with
      line, link, and control box counters;
    - parsed anchor `href` targets bound to preview link layout boxes, plus
      mouse hit-testing that reports the clicked URL on serial;
    - clicked same-host anchor targets promoted into the URL bar and queued as
      fresh HTTPS navigation attempts with a navigation-specific SYN proof;
    - bounded inline CSS telemetry from real `<style>` content for rule,
      declaration, color, font, URL, and `display:none` hints;
    - a bounded simple-CSS rule table for real inline `tag`, `.class`, and
      `#id` selectors mapped to preview flags such as hidden, center, bold,
      large, and boxed;
    - bounded render flags from real tags/attributes for simple hidden,
      centered, bold, boxed input, and button-style preview blocks;
    - a bounded inline-JavaScript scanner for script bytes, tokens, functions,
      variables, `document`, `window`, and `location` references, plus a tiny
      literal `document.write("text")` preview hook;
    - one streamed attribute parser pass that counts actual `href`, `src`, and
      form `action` resource references from those tags;
    - a bounded resource queue that normalizes fetchable `href`, `src`, and
      `action` targets from the current Google response;
    - a second same-host HTTPS resource attempt that reports its live phase
      plus a real response status and byte count when QEMU user-net/Google
      returns one, or `TMO` when the handshake/request stalls;
    - opening the Browser window with `F11` after the Google load completes.
  - The Browser window shows the Google URL, the decrypted HTTP status line,
    a structured preview from real text/tags extracted from Google's returned
    HTML, CSS and JavaScript summary blocks when inline styles/scripts are
    present, simple preview-level CSS/tag/attribute styling hints, and an info
    drawer for the current network response plus parsed HTML/CSS/JS/DOM/layout
    structure/resource counters, clicked-link/navigation status, and queued
    resource/navigation probe phase/status.
  - Still NOT implemented: DHCP, sockets, general-purpose TLS, certificate
    validation, CSS cascade/layout, computed styles, general JavaScript
    execution, DOM APIs, event loop/timers, remote image decoding, form
    submission, replacing the current document with clicked navigation results,
    reliable queued-resource HTTPS GET completion, generalized or multi-host
    resource fetching, packet retransmission, multiple simultaneous connections,
    pixel-accurate web page rendering, generalized ARP cache expiry, NIC
    interrupts, or support for NICs other than RTL8139 in QEMU.
    LeonOS shows extracted Google text and only claims the tiny CSS/JS subsets
    listed above.

## Milestone 9: Browser Engine Foundation

- Goal: turn the current one-host HTTPS proof into a small browser core that can
  grow without lying about unsupported web-platform features.
  - Build next:
  - a general browser request object instead of hardcoded `www.google.com`;
  - TCP retransmission/timeouts and connection-close state;
  - reusable DNS and multi-host TLS state;
  - a heap-backed general document model instead of fixed global page text
    buffers and the current 64-node bounded tree;
  - navigation result parsing/replacement, history/back-forward state, and a
    URL/search entry path;
  - HTTP redirect following for same-host pages;
  - multi-connection fetch workers for queued resources;
  - enough HTML tree construction to preserve headings, paragraphs, lists,
    links, and image placeholders;
  - visual tests for the Google preview, info drawer, and scrolling.
- Needed before calling it a real browser:
  - certificate/time validation;
  - multiple simultaneous connections;
  - generalized CSS box/layout engine beyond the current bounded preview layout;
  - PNG/JPEG/GIF/WebP image decoding or placeholders with dimensions;
  - text input, forms, focus, and submission;
  - JavaScript runtime or an explicit non-JS browser scope;
  - user-space process isolation and memory limits for untrusted content.

## Bug Policy

Each milestone uses a known-bug-zero gate. Any reproducible defect in boot,
disk loading, framebuffer rendering, keyboard input, mouse input, file opening,
panic/error output, build scripts, or QEMU automation blocks the milestone.

Missing future features are not bugs unless the milestone claims them as
supported behavior.
