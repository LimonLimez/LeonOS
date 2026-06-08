# LeonOS Browser Checklist

This is the browser roadmap for turning the current HTTPS previewer into a usable hobby-OS browser.

## 1. Large HTTPS Page Loading

- [x] Advertise a realistic TCP receive window instead of the old 4 KB window.
- [x] Keep reading the primary HTTPS response until a real completion signal or a deliberate parse/idle limit.
- [x] Correctly finalize chunked responses at the zero-size chunk.
- [x] Avoid closing the TLS connection just because the first renderable preview exists.
- [x] Prove a larger real HTTPS HTML response in QEMU with serial logs.
- [x] Fix the Google desktop/search header-only stall seen in QEMU before claiming Google is fully loaded.

Verified on 2026-06-04 with `tools/test32-net-qemu.ps1 -Resolution 1080p`: real HTTPS to
`https://www.google.com/?igu=1&hl=en`, status 200, identity body, real Google HTML/DOM/CSS/JS/resource stats,
and `LeonOS net browser response complete fin decoded 225321` or better in current runs.

Also verified on 2026-06-04 with
`tools/test32-browser-url-qemu.ps1 -Url 'www.google.com/search?q=leonos&igu=1&hl=en' -Resolution 1080p`:
the typed URL stayed intact, Google returned status 200, and the search page completed with
`LeonOS net browser response complete fin decoded 91425`. This proves the previous header-only stall is fixed;
it does not mean JavaScript search interactivity is finished.

## 2. DOM And Layout

- [x] Build a small DOM tree for supported elements.
- [x] Add block and inline layout, margins, padding, borders, wrapping, and tables.
- [x] Keep unsupported elements visible as honest placeholders.

Verified on 2026-06-04 with `tools/test32-browser-layout-qemu.ps1 -Resolution 1080p`: the in-OS
DOM/layout fixture uses the same parser and shell renderer as fetched pages and proved `DOM nodes 33`,
`supported 27`, `block 10`, `inline 3`, `table 11`, `controls 2`, `unsupported 6`, `placeholders 6`,
plus `form model forms 1 controls 2 focusable 2 editable 1`, `layout boxes 33`, `links 1`,
`controls 3`, `block 24`, `inline 9`, `table 10`, `boxes 22`, `wraps 1`, `margin 24`,
`padding 22`, and `border 22`.

Also rerun on 2026-06-04 with `tools/test32-net-qemu.ps1 -Resolution 1080p`: real HTTPS Google still
loads and emits expanded DOM/layout counters. This does not claim a full W3C DOM API, full CSS layout, or
JavaScript execution; those remain later browser milestones.

## 3. Forms

- [x] Add focusable text fields with caret editing.
- [x] Submit GET forms from Enter/buttons.
- [x] Make Google search work through real form submission.

Verified on 2026-06-04 with `tools/test32-browser-forms-qemu.ps1 -Resolution 1080p`: LeonOS loaded
real Google HTML from `https://www.google.com/?igu=1&hl=en`, parsed the real form model, focused the
real `name q` search field, edited it to `leonos`, submitted a real GET form URL
`https://www.google.com/search?q=leonos&igu=1&hl=en&gbv=1`, and decoded the real Google search response
with `LeonOS net browser response complete fin decoded 91672`.

Current form scope is honest and limited: text/search/url/email/password inputs and textarea-style text
controls get focus/caret editing; Enter submits GET forms; clickable submit/buttons submit; hidden inputs
are included for generic forms. Google search uses a small compatibility filter that keeps `igu`, `hl`,
and `gbv=1` while dropping volatile Google tracking hidden fields because those repeatedly returned
headers-only responses in this tiny HTTP/1.0 client. POST, file upload, select/radio/checkbox behavior,
validation, autocomplete, and JavaScript-driven form behavior are not supported yet.

## 4. Images

- [x] Decode and render PNG.
- [x] Decode and render JPEG.
- [x] Keep unsupported formats labeled instead of faked.

Verified on 2026-06-04 with `tools/test32-browser-images-qemu.ps1 -Resolution 1080p`: the in-OS
`leonos://image-selftest` page decoded a real PNG fixture, decoded a real baseline JPEG fixture, rendered
both as native pixel thumbnails in the browser window, labeled an SVG fixture as unsupported instead of
faking it, and emitted `LeonOS net browser image summary decoded 2 unsupported 1 png 1 jpeg 1`.
The test also produced `dist32/test32-browser-images-1080p.ppm`, converted for review to
`dist32/test32-browser-images-1080p.png`.

Current image scope is honest and limited: PNG supports 8-bit RGB/RGBA non-interlaced images with
stored-deflate zlib blocks and PNG filters 0-4; JPEG supports small 8-bit baseline SOF0 images with
parsed quantization/Huffman tables, 1 or 3 components, and sampling factors up to 2x2. Progressive JPEG,
interlaced/palette PNG, compressed-deflate PNG streams beyond the stored-block subset, ICO, SVG, WebP,
GIF, animated images, large image scaling, HTTP content-type routing beyond the current resource queue,
and CSS/background images are still future work. Unsupported resources are shown as labels, not fake pixels.

Regression reruns on 2026-06-04 after Step 4:
`tools/test32-browser-layout-qemu.ps1 -Resolution 1080p`,
`tools/test32-net-qemu.ps1 -Resolution 1080p`, and
`tools/test32-browser-forms-qemu.ps1 -Resolution 1080p` all passed.

## 5. CSS Subset

- [x] Implement tag, class, and id selectors.
- [x] Support color, font weight/size, display, width/height, margin, padding, border, and background.
- [x] Apply simple cascade order.

Verified on 2026-06-04 with `tools/test32-browser-css-qemu.ps1 -Resolution 1080p`: the in-OS
`leonos://css-subset-selftest` page used stylesheet tag/class/id selectors plus inline styles, stored
7 CSS rules, matched and cascaded 11 rule applications, and proved nonzero applied counters for
color, background, width, height, margin, padding, and border. The same run emitted layout boxes with
CSS-driven margins, padding, and borders.

Regression reruns on 2026-06-04 after Step 5:
`tools/test32-browser-layout-qemu.ps1 -Resolution 1080p`,
`tools/test32-browser-images-qemu.ps1 -Resolution 1080p`, and
`tools/test32-browser-forms-qemu.ps1 -Resolution 1080p -TimeoutSeconds 520` all passed. The forms test
also loaded real Google HTML, applied the CSS subset counters against that page, edited the real `q`
search field, submitted the real GET URL, and loaded the real Google search response.

Current CSS scope is honest and limited: simple single selectors only (`tag`, `.class`, `#id`), source-order
cascade for stored rules, inline style override, common named colors, `#rgb`, `#rrggbb`, `rgb(r,g,b)`, and
integer pixel lengths. It does not implement selector combinators, pseudo-classes, media queries, full
specificity sorting, external stylesheet fetching beyond existing resource discovery, CSS backgrounds with
images/gradients, floats/flex/grid, real font metrics, or full browser layout.

## 6. Navigation

- [x] Back, forward, reload, stop.
- [x] Loading, complete, and error states.
- [x] Link clicks, address bar, and scrolling remain stable on large pages.

Verified on 2026-06-04 with `tools/test32-browser-navigation-qemu.ps1 -Resolution 1080p`: the in-OS
browser opened the DOM/layout and CSS fixtures, scrolled the first page, pushed history entries, restored
the first page with `history back 0/2 scroll 6`, moved forward, reloaded the local CSS page, clicked the
real parsed first link with QEMU mouse input, entered `LOADING`, stopped that pending load with Esc, and
typed an unsupported `http://` URL through the address bar to prove the `ERROR` state.

Regression reruns on 2026-06-04 after Step 6:
`tools/test32-browser-css-qemu.ps1 -Resolution 1080p`,
`tools/test32-browser-layout-qemu.ps1 -Resolution 1080p`,
`tools/test32-browser-forms-qemu.ps1 -Resolution 1080p -TimeoutSeconds 520`, and
`tools/test32-browser-images-qemu.ps1 -Resolution 1080p` all passed. The forms test still loads real
Google HTML, edits the real `q` field, submits a real GET search URL, and completes the real Google search
response.

Current navigation scope is honest and limited: back/forward are an 8-entry in-memory history stack,
reload supports real HTTPS URLs plus the current `leonos://` browser fixtures, stop cancels the current
tiny-client fetch/resource state, link clicks open normalized HTTPS URLs, and address-bar submission
supports HTTPS only. There is no persisted session history, multi-tab history, redirect-chain UI,
download handling, POST resubmission prompt, favicon/title history database, service worker cache, or
JavaScript-driven navigation yet.

## 7. Unsupported Web Features

- [x] Clearly show when JavaScript is required.
- [x] Later evaluate a tiny JavaScript engine or a strict no-JS browser mode.

Verified on 2026-06-04 with `tools/test32-browser-unsupported-qemu.ps1 -Resolution 1080p`: the in-OS
`leonos://unsupported-selftest` page includes script code, `noscript`, an event-handler attribute,
a `javascript:` link, SVG, canvas/iframe media-style embeds, a custom element, a template, and `srcset`.
LeonOS now inserts a visible `NOJS MODE` warning block into the rendered page, reports
`LeonOS net browser unsupported summary mode NOJS jsrequired yes`, counts event handlers and
`javascript:` URLs, keeps unsupported DOM placeholders visible, and labels JavaScript output as a static
preview instead of pretending scripts executed.

Decision: LeonOS browser is now explicitly a strict no-JS browser for this milestone. A tiny JavaScript
engine was not added because real browser JavaScript would also require DOM mutation APIs, events,
timers, fetch/XHR, storage, security boundaries, and layout invalidation. Instead, the current browser
parses scripts only to detect dependency signals and keeps rendering the static HTML/CSS subset it
actually supports.

Regression reruns on 2026-06-04 after Step 7:
`tools/test32-browser-layout-qemu.ps1 -Resolution 1080p`,
`tools/test32-browser-css-qemu.ps1 -Resolution 1080p`,
`tools/test32-browser-navigation-qemu.ps1 -Resolution 1080p`,
`tools/test32-browser-images-qemu.ps1 -Resolution 1080p`, and
`tools/test32-browser-forms-qemu.ps1 -Resolution 1080p -TimeoutSeconds 520` all passed. The real Google
forms test still loaded real Google HTML, edited the real `q` field, submitted a real GET search URL,
and completed the real Google search response while honestly reporting `NOJS` limitations.

Current unsupported-feature scope is honest and limited: scripts are not executed; event-handler
attributes and `javascript:` URLs are counted and disabled; `noscript`, SVG, canvas/iframe/media,
custom elements, templates, responsive image sets, and other unsupported page features are surfaced
through counters, placeholders, and the info panel. This is not a real JavaScript runtime and does not
support dynamic DOM, timers, XHR/fetch, storage, service workers, Web APIs, or JS-driven rendering.

## 8. Ring-3 HTTPS Stream API

- [x] Add a user-visible stream open/poll/read/meta/close ABI.
- [x] Prove a real HTTPS response can be read in chunks by a ring-3 app.
- [x] Keep stream close from leaking leftover TLS bytes into the kernel browser renderer.
- [x] Move the NetSurf fetcher from the blocking `SYS_NET_FETCH` bridge onto the stream ABI.

Verified on 2026-06-06 with `tools/test32-hdd-ustream-qemu.ps1`: `USTREAM.LEO`
opened `https://www.google.com/`, received real DNS/TCP/TLS/HTTPS status 200,
read the Google body through `SYS_NET_STREAM_READ` in 1 KiB chunks, copied real
metadata with `SYS_NET_STREAM_META` (`text/html; charset=UTF-8`), closed the
stream, and returned to the kernel. The regression rejects kernel-browser render
telemetry after close so stream cleanup stays owned by the ring-3 app.

Current stream scope is honest and limited: it is one active global HTTPS
transaction on top of the existing tiny TLS path. It is not arbitrary TCP, not a
BSD sockets API, not multi-host/concurrent networking, and not wired into
NetSurf as a fully asynchronous multi-connection fetcher yet. The current
NetSurf smoke fetcher uses the stream ABI statefully, streams body chunks into
NetSurf callbacks, and serializes NetSurf fetch contexts onto the one active
kernel HTTPS stream instead of dropping immediately when the stream is busy.

Verified on 2026-06-07 with
`tools/test32-hdd-netsurf-qemu.ps1 -TimeoutSeconds 520 -StartUrl "https://www.google.com/?igu=1&hl=en&gbv=1" -RequireSubresource`:
`NETSURF.LEO` loaded through the FAT32 ring-3 loader, printed
`LeonOS user app copy begin bytes=2264488`, entered user mode, started the real
NetSurf monkey frontend/core, opened
`https://www.google.com/?igu=1&hl=en&gbv=1`, resolved Google DNS,
completed TCP/TLS/HTTPS with status 200, streamed real Google body bytes through
`SYS_NET_STREAM_*`, forwarded `text/html; charset=UTF-8` metadata,
fed an 80 KB-class real response body through NetSurf data callbacks without the
old Google compatibility excerpt, fetched real Google image/CSS subresources,
decoded and blitted the real Google logo, reached Hubbub parser states
`BEFORE_HTML` and `IN_BODY`, emitted monkey redraw `PLOT` commands, quit cleanly,
and returned to the kernel in noninteractive regression mode.

Also verified on 2026-06-07 with
`tools/test32-hdd-netsurf-interactive-qemu.ps1 -TimeoutSeconds 420 -StartUrl "https://www.google.com/?igu=1&hl=en&gbv=1" -TypedUrl "example.com"`:
the resident NetSurf app rendered Google, accepted keyboard scroll input, focused
the top URL bar, typed a custom URL, and navigated to `https://example.com/`.

This build compiles Duktape plus generated NetSurf DOM JavaScript bindings, but
JavaScript is disabled by default. Duktape still times out on or cannot parse
Google's script bundles, so the current proven browser mode is real static
HTML/CSS/images over HTTPS, not Chrome-class JavaScript site compatibility.
