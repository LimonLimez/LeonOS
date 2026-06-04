; LeonOS LEO1 v2 flat executable: UHELLO.LEO
;
; This IS a ring-3 user-mode program. The 32-bit kernel marks this image's
; code/stack pages user-accessible and enters it at ring 3 via iret. The app
; gets NO kernel function pointers and cannot touch kernel memory; its only way
; to talk to the kernel is the int 0x80 syscall gate.
;
; Syscall ABI (int 0x80):
;   EAX = syscall number
;     0 = SYS_EXIT  (no args; never returns to the app)
;     1 = SYS_WRITE (EBX = pointer to a NUL-terminated string in this image)
;
; The app is position-independent: it finds its own load base with a call/pop
; so it can form absolute pointers without the kernel passing a base register.
;
; LEO1 header (32 bytes, little-endian):
;   +0  magic "LEO1"
;   +4  abi version (2 = ring-3 user app)
;   +8  entry offset (from image start)
;   +12 image size (bytes the loader copies)
;   +16 bss size (extra zeroed bytes after the image)
;   +20..+28 reserved (zero)

BITS 32
ORG 0

leo_header:
    db 'L', 'E', 'O', '1'
    dd 2
    dd entry
    dd leo_image_end
    dd 0
    dd 0
    dd 0
    dd 0

entry:
    call .pic
.pic:
    pop esi
    sub esi, .pic                   ; esi = load base (ORG 0, so .pic == its offset)

    mov eax, 1                      ; SYS_WRITE
    lea ebx, [esi + msg]            ; absolute address of the string
    int 0x80

    mov eax, 0                      ; SYS_EXIT
    int 0x80

.hang:
    jmp .hang                       ; the kernel never returns us here

msg: db "UHELLO ran via syscall", 13, 10, 0

leo_image_end:
