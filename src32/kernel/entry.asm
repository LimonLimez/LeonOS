BITS 32

GLOBAL _start
EXTERN kmain

SECTION .text
_start:
    cli
    mov esp, stack_top

    ; Stage 2 will pass the BootInfo pointer in EBX.
    push ebx
    call kmain

.halt:
    hlt
    jmp .halt

SECTION .bss
align 16
stack_bottom:
    resb 16384
stack_top:
