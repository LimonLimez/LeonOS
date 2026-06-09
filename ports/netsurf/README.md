# LeonOS NetSurf Port

This directory is the start of a real existing-browser port, not a fake Google
renderer. The target is NetSurf because it is a C browser with a framebuffer
frontend model and a smaller dependency surface than Chromium, Firefox, or
WebKit.

## Current Status

Implemented in LeonOS:

- ring-3 `LEO1` v2 user apps with `int 0x80`;
- multi-page user app loading from FAT32 up to the current 4 MiB image cap;
- a fixed 16 MiB `0x40000000` user virtual window so freestanding C globals,
  strings, heap allocations, and stack syscall buffers work;
- a tiny LEO1 crt0/linker path plus `UCDEMO.LEO`, a C user app verified in QEMU;
- `SYS_BROWSER_OPEN` plus `UBROWSER.LEO`, a ring-3 launcher that queues a real
  kernel HTTPS browser session (Programs -> **LeonOS Browser**, or press **B**);
- `SYS_YIELD`, `SYS_MILLIS`, `SYS_MALLOC`, and `SYS_FREE` for long-running port
  harnesses;
- `UNETRUN.LEO` plus Programs -> **NetSurf Runtime** (or press **N**): proves the
  yield/heap/malloc path and then opens the real browser;
- `NETSURF.LEO` plus Programs -> **NetSurf Port** (or press **M**): launches
  NetSurf's real `monkey` frontend/core in ring 3 as an interactive browser
  app. It creates a NetSurf window, opens
  `https://www.google.com/?igu=1&hl=en&gbv=1`, streams real Google HTTPS body
  bytes and subresources through LeonOS `SYS_NET_STREAM_*`, feeds those real
  bodies to NetSurf callbacks, runs the HTML/CSS/parser/content pipeline,
  decodes bitmap images, forwards real HTTP status/content-type metadata
  through `SYS_NET_STREAM_META`, redraws through the monkey plotter, accepts
  scroll/key/mouse events, and supports typed HTTPS URLs through the top bar;
- `SYS_NET_FETCH` plus `UWEB.LEO` (**User Web**, press **X**): fetches a real
  HTTPS page into ring-3 memory, prints a text preview, then opens the kernel
  browser UI;
- `SYS_NET_STREAM_OPEN`, `SYS_NET_STREAM_POLL`, `SYS_NET_STREAM_READ`,
  `SYS_NET_STREAM_META`, and `SYS_NET_STREAM_CLOSE` plus `USTREAM.LEO` (**Net
  Stream**, press **S**): proves one active ring-3 HTTPS stream handle by
  reading the real Google HTTPS body in 1 KiB chunks, copying metadata, and
  closing without leaking leftover TLS bytes into the kernel browser renderer;
- framebuffer info/fill/present syscalls;
- a minimal event-poll syscall;
- `UGFX.LEO`, a user-mode graphics probe that exercises those syscalls in QEMU;
- `ports\netsurf\build-leonos-probe.ps1` compiles 602 i386 freestanding objects
  from real upstream NetSurf project libraries plus NetSurf's real `monkey`
  browser frontend/core target and the LeonOS QuickJS browser backend. The probe
  also links a freestanding
  `dist32\netsurf-probe\netsurf-monkey.elf` from 597 browser objects. SVG
  support-library objects are compiled, but excluded from that link because SVG
  rendering is not enabled and no XML parser is ported yet;
- the in-kernel LeonOS browser (F11) with TLS, layout, forms, images, and strict
  NOJS mode. This is the browser users run today while the NetSurf frontend is
  still being ported.

Not implemented yet:

- full libc for user apps (`ports\netsurf\leonos_libc` is enough to compile and
  link the probed NetSurf target, but it is not a complete standards libc);
- real file descriptors and POSIX path APIs (many currently compile as clean
  failure stubs);
- generalized socket/TLS syscalls exposed to ring 3; the current stream API is
  still one global HTTPS transaction, not arbitrary sockets or concurrent
  connections;
- generalized multi-window/tab browser chrome on top of the current resident
  NetSurf window;
- a proper `setjmp`/`longjmp`, wall-clock time, full libm, zlib inflate, and
  full iconv (only UTF-8/ASCII pass-through is present);
- a polished LeonOS-native NetSurf frontend beyond the current monkey-backed
  framebuffer bridge.

## Fetch Upstream Source

Use the fetch script when we are ready to inspect or build upstream NetSurf and
its project libraries:

```powershell
powershell -ExecutionPolicy Bypass -File .\ports\netsurf\fetch-netsurf.ps1
```

The source workspace is fetched into `ports\netsurf\vendor\`, which is ignored
by git. It includes `netsurf`, `buildsystem`, the core libraries (`libdom`,
`libcss`, `libhubbub`, `libparserutils`, `libwapcaplet`, `libnsbmp`,
`libnsgif`, `libnsutils`) plus optional/frontend libraries (`libsvgtiny`,
`libnsfb`). Keep LeonOS-specific frontend and ABI files outside `vendor`.

On Windows, `libnsbmp` and `libnsgif` are checked out without their `test`
fixtures because those repositories contain AFL filenames with `:` characters,
which Windows cannot materialize.

Official NetSurf source information:

- https://www.netsurf-browser.org/downloads/source/
- https://www.netsurf-browser.org/documentation/develop

## Build Probe

After fetching upstream source, compile and link the current freestanding
NetSurf probe:

```powershell
powershell -ExecutionPolicy Bypass -File .\ports\netsurf\build-leonos-probe.ps1
```

To build the same smoke app against another real HTTPS startup URL:

```powershell
powershell -ExecutionPolicy Bypass -File .\ports\netsurf\build-leonos-probe.ps1 -StartUrl https://www.example.com/
```

The probe generates the needed parser/color/entity tables, installs libdom
binding headers into the expected include layout, builds NetSurf's small host
property-parser generator with the local LLVM/MinGW toolchain, compiles the
library and `monkey` browser frontend/core objects into `dist32\netsurf-probe\`,
then links `dist32\netsurf-probe\netsurf-monkey.elf` and
`dist32\netsurf-probe\NETSURF.LEO`.

Expected current result:

- `libwapcaplet`: 1 object
- `libparserutils`: 15 objects, built with `WITHOUT_ICONV_FILTER`
- `libhubbub`: 30 objects
- `libcss`: 303 objects
- `libdom`: 95 objects, including the real Hubbub parser binding
- `libnsbmp`: 1 object
- `libnsgif`: 2 objects
- `libsvgtiny`: 5 objects
- `libnsutils`: 3 objects
- `netsurf-monkey`: 364 real NetSurf browser/frontend/JavaScript objects

Total: 821 objects plus `ports\netsurf\leonos_libc`; the linked
`netsurf-monkey.elf` and `NETSURF.LEO` use 816 browser objects because SVG is
not enabled in this target.

Run the QEMU proof for the `LEO1` app:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-netsurf-qemu.ps1
powershell -ExecutionPolicy Bypass -File .\tools\test32-hdd-ustream-qemu.ps1
```

The NetSurf test uses a serial log file instead of `-serial stdio` and waits
for `LeonOS shell ready` before launching the app. It requires serial proof of
`WINDOW NEW`, `WINDOW SET_URL WIN 0 URL
https://www.google.com/?igu=1&hl=en&gbv=1`,
`NETSURF LEO HTTPS fetch begin
https://www.google.com/?igu=1&hl=en&gbv=1`,
`LeonOS user app copy begin bytes=`, `LeonOS user app image copied`,
`LeonOS user net fetch done bytes`,
`LeonOS user net fetch meta status 200 type text/html`, `NETSURF LEO HTTPS
metadata status 200 type text/html`,
`NETSURF LEO HTTPS fetch data callbacks begin`, real streamed callback bytes,
Hubbub parser states such as `BEFORE_HTML` and `IN_BODY`, `WINDOW REDRAW`, and
`PLOT` commands. The regression rejects the old Google compatibility excerpt
path, then requires `GENERIC FINISHED` and return to the kernel.

Honest current browser limits:

- QuickJS JavaScript is now the default NetSurf browser backend and starts with
  `--enable_javascript=1`. Google inline scripts execute, DOM lookups work, and
  native DOM insertion is wired back into NetSurf, but the browser API surface is
  still incomplete compared with Chromium/Firefox. Use `-NoJavaScript` for a
  strict static fallback or `-UseDuktape` to debug the older generated-binding
  backend;
- no curl fetcher and no generalized socket/TLS API exposed to NetSurf yet; the
  current fetch path is a LeonOS-specific one-handle `SYS_NET_STREAM_*` bridge
  that now polls statefully, returns body bytes plus real status/content-type
  metadata, and queues NetSurf fetch contexts until the one kernel stream is
  free. It is still not full response headers, cookies as browser API,
  redirects, arbitrary sockets, or true concurrent connections;
- PNG/JPEG/GIF/BMP-style image paths are partially covered by NetSurf's image
  handlers plus the LeonOS `stb_image` bridge; WebP remains unsupported;
- no SVG rendering in the linked target;
- no real POSIX directory/file persistence;
- no PDF output;
- no compressed zlib resource loading.

## Port Milestones

1. Build a tiny freestanding C user app against the LeonOS syscall ABI. Done
   with `UCDEMO.LEO`.
2. Add a user heap. Done: up to 2048 pages mapped after the user stack;
   `SYS_MALLOC`.
3. Add file/sysclock/event-loop syscalls. Partial: `SYS_MILLIS` and
   `SYS_YIELD` run kernel net/desktop tasks while the user app keeps running.
4. Expose TCP/TLS through a socket-like user API. Partial: a blocking
   `SYS_NET_FETCH` HTTPS bridge exists for smoke tests, and a one-handle
   `SYS_NET_STREAM_*` API can open, poll, read, read metadata, and close one
   HTTPS response from ring 3. This is still not arbitrary TCP sockets or
   concurrent TLS connections.
5. Compile NetSurf's portable libraries for i386 freestanding LeonOS. Done for
   the current support-library set listed above.
6. Compile and link NetSurf browser core sources against the LeonOS libc
   surface. Done as `netsurf-monkey.elf` and `NETSURF.LEO`.
7. Implement a LeonOS framebuffer/window frontend. Partial: the monkey frontend
   now draws to the LeonOS framebuffer, presents through syscalls, and receives
   basic key/mouse events through a stdin bridge; a polished native chrome is
   still future work.
8. Run a static HTML page in the NetSurf frontend. Superseded by the current
   real Google homepage render with image subresources.
9. Connect the frontend to the LeonOS network API. Partial: NetSurf `https://`
   fetches now use LeonOS `SYS_NET_STREAM_*` for one streamed response body and
   real status/content-type metadata.
10. Move the NetSurf fetcher from `SYS_NET_FETCH` to `SYS_NET_STREAM_*`. Done
    for the current smoke fetcher; it still buffers the response before handing
    chunks to NetSurf callbacks.

NetSurf's own `docs/implementing-new-frontend.md` says a new frontend must wire
operation tables into the core and that the mandatory table groups are misc,
window, fetch, bitmap, and layout. Its framebuffer documentation also shows that
the framebuffer frontend expects a linearly mapped output surface plus an input
surface. The current LeonOS syscall layer covers only the first sliver of that:
framebuffer size, rectangle fill, present, and basic event polling.

For the most complete current browser path, use Programs -> NetSurf Port or
press **M**. The older in-kernel LeonOS browser remains available with F11,
Programs -> LeonOS Browser, or `leonos://browser` for port status.
