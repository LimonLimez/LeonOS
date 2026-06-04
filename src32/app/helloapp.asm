; LeonOS LEO1 flat executable: HELLOAPP.LEO
;
; This is NOT a user-mode program. It is a tiny ring-0 cooperative flat binary
; loaded by the 32-bit kernel from the FAT32 hard disk. There is no memory
; isolation and no syscall boundary: the kernel passes a fixed in-kernel API
; table and the app calls those function pointers directly.
;
; ABI v1 contract (set by the kernel loader):
;   EAX = pointer to struct LeoAppApi { u32 abi_version;
;                                       void (*serial_print)(const char*);
;                                       void (*app_print)(const char*); }
;   EBX = load base (linear address the image was copied to)
;   Entry is reached with `call`, so the app returns with `ret`.
;   The app is a well-behaved cdecl callee: it preserves EBX, ESI, EDI, EBP.
;
; LEO1 header (32 bytes, little-endian):
;   +0  magic "LEO1"
;   +4  abi version
;   +8  entry offset (from image start)
;   +12 image size (bytes the loader copies)
;   +16 bss size (extra zeroed bytes after the image)
;   +20..+28 reserved (zero)

BITS 32
ORG 0

leo_header:
    db 'L', 'E', 'O', '1'
    dd 1
    dd entry
    dd leo_image_end
    dd 0
    dd 0
    dd 0
    dd 0

entry:
    push ebx
    push esi
    push edi
    mov edi, eax                    ; edi = API table

    lea esi, [ebx + msg_serial]     ; absolute address = base + offset
    push esi
    call [edi + 4]                  ; api->serial_print
    add esp, 4

    lea esi, [ebx + msg_app]
    push esi
    call [edi + 8]                  ; api->app_print
    add esp, 4

    pop edi
    pop esi
    pop ebx
    ret

msg_serial: db "HELLOAPP ran", 13, 10, 0
msg_app:    db "HELLOAPP.LEO RAN IN RING 0", 0

leo_image_end:
