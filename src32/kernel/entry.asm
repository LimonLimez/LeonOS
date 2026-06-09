BITS 32

GLOBAL _start
EXTERN kmain
EXTERN __bss_start
EXTERN __bss_end

%define KERNEL_BOOT_STACK_SIZE 131072

SECTION .text
_start:
    cli
    mov esp, stack_top
    cld

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

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
    resb KERNEL_BOOT_STACK_SIZE
stack_top:
