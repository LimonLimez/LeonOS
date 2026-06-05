; LeonOS LEO1 v2 flat executable: UGFX.LEO
;
; Ring-3 framebuffer syscall probe. This is the first real user-mode graphics
; ABI needed before an existing browser frontend can be ported out of the
; kernel. It does not contain browser code.

BITS 32
ORG 0

%define SYS_EXIT       0
%define SYS_WRITE      1
%define SYS_FB_INFO    2
%define SYS_FB_FILL    3
%define SYS_FB_PRESENT 4
%define SYS_EVENT_POLL 5

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
    pop ebp
    sub ebp, .pic

    mov eax, SYS_WRITE
    lea ebx, [ebp + msg]
    int 0x80

    sub esp, 32

    mov eax, SYS_FB_INFO
    mov ebx, esp
    int 0x80

    ; Draw a simple centered browser-surface placeholder through ring 3.
    mov eax, SYS_FB_FILL
    mov ebx, 96
    mov ecx, 96
    mov edx, 760
    mov esi, 360
    mov edi, 0x00F8FAFC
    int 0x80

    mov eax, SYS_FB_FILL
    mov ebx, 96
    mov ecx, 96
    mov edx, 760
    mov esi, 52
    mov edi, 0x001A73E8
    int 0x80

    mov eax, SYS_FB_FILL
    mov ebx, 124
    mov ecx, 176
    mov edx, 704
    mov esi, 64
    mov edi, 0x00E8F0FE
    int 0x80

    mov eax, SYS_FB_FILL
    mov ebx, 124
    mov ecx, 264
    mov edx, 360
    mov esi, 128
    mov edi, 0x00DFF7EA
    int 0x80

    mov eax, SYS_FB_PRESENT
    int 0x80

    mov eax, SYS_EVENT_POLL
    lea ebx, [esp + 16]
    int 0x80

    mov eax, SYS_EXIT
    int 0x80

.hang:
    jmp .hang

msg: db "UGFX framebuffer syscall app", 13, 10, 0

; Keep this app larger than one 4 KiB page. It proves the ring-3 loader can
; read, map, and clean up multi-page user images instead of only tiny probes.
times 5000 db 0

leo_image_end:
