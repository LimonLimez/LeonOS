# LeonOS NetSurf Port

This directory is the start of a real existing-browser port, not a fake Google
renderer. The target is NetSurf because it is a C browser with a framebuffer
frontend model and a smaller dependency surface than Chromium/Firefox/WebKit.

## Current Status

Implemented in LeonOS:

- ring-3 `LEO1` v2 user apps with `int 0x80`;
- multi-page user app loading from FAT32 up to the current 1 MiB image cap;
- framebuffer info/fill/present syscalls;
- a minimal event-poll syscall;
- `UGFX.LEO`, a user-mode graphics probe that exercises those syscalls in QEMU.

Not implemented yet:

- C runtime/libc for user apps;
- heap allocation for user apps;
- file descriptors and path APIs;
- socket/TLS syscalls;
- timers suitable for a browser event loop;
- keyboard/mouse routing to a long-running user process;
- a NetSurf build target or frontend.

## Fetch Upstream Source

Use the fetch script when we are ready to inspect or build upstream NetSurf and
its project libraries:

```powershell
powershell -ExecutionPolicy Bypass -File .\ports\netsurf\fetch-netsurf.ps1
```

The source workspace is fetched into `ports\netsurf\vendor\`, which is ignored
by git. It includes `netsurf`, `buildsystem`, the core libraries
(`libdom`, `libcss`, `libhubbub`, `libparserutils`, `libwapcaplet`,
`libnsbmp`, `libnsgif`) plus optional/frontend libraries (`libsvgtiny`,
`libnsfb`). Keep LeonOS-specific frontend and ABI files outside `vendor`.

On Windows, `libnsbmp` and `libnsgif` are checked out without their `test`
fixtures because those repositories contain AFL filenames with `:` characters,
which Windows cannot materialize.

Official NetSurf source information:

- https://www.netsurf-browser.org/downloads/source/
- https://www.netsurf-browser.org/documentation/develop

## Port Milestones

1. Build a tiny freestanding C user app against `leonos_frontend`.
2. Add a user heap. Multi-page `LEO1` image loading is already present up to
   the current 1 MiB cap.
3. Add file/sysclock/event-loop syscalls.
4. Expose TCP/TLS through a socket-like user API.
5. Compile NetSurf's portable libraries for i386 freestanding LeonOS.
6. Implement a LeonOS framebuffer frontend.
7. Run a static HTML page in the NetSurf frontend.
8. Connect the frontend to the LeonOS network API.

NetSurf's own `docs/implementing-new-frontend.md` says a new frontend must wire
operation tables into the core and that the mandatory table groups are misc,
window, fetch, bitmap, and layout. Its framebuffer documentation also shows that
the framebuffer frontend expects a linearly mapped output surface plus an input
surface. The current LeonOS syscall layer covers only the first sliver of that:
framebuffer size, rectangle fill, present, and basic event polling.

Until those steps exist, LeonOS still has its in-kernel HTTPS previewer plus the
new user graphics ABI. It does not run upstream NetSurf yet.
