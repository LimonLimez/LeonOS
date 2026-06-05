BITS 32

GLOBAL leo_header
EXTERN leonos_user_main
EXTERN leo_image_end
EXTERN leo_bss_size

SECTION .text.leo_header

leo_header:
    db 'L', 'E', 'O', '1'
    dd 2
    dd entry - leo_header
    dd leo_image_end - leo_header
    dd leo_bss_size
    dd 0
    dd 0
    dd 0

entry:
    cld
    call leonos_user_main
    mov eax, 0
    int 0x80

.hang:
    jmp .hang

SECTION .note.GNU-stack noalloc noexec nowrite progbits
