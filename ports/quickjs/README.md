# LeonOS QuickJS Port

This is the next JavaScript-engine milestone for the browser work. It builds
the official QuickJS core as a freestanding LeonOS user app (`UQJS.LEO`) and
runs real ECMAScript features inside QEMU.

It is not wired into NetSurf yet. The current browser still uses NetSurf with
JavaScript disabled by default; this probe is the replacement-engine path that
will let us move beyond the old Duktape compatibility ceiling.

Build:

```powershell
powershell -ExecutionPolicy Bypass -File .\ports\quickjs\build-quickjs-probe.ps1
```
