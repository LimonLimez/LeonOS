BITS 32

GLOBAL idt_load
GLOBAL isr0
GLOBAL isr1
GLOBAL isr2
GLOBAL isr3
GLOBAL isr4
GLOBAL isr5
GLOBAL isr6
GLOBAL isr7
GLOBAL isr8
GLOBAL isr9
GLOBAL isr10
GLOBAL isr11
GLOBAL isr12
GLOBAL isr13
GLOBAL isr14
GLOBAL isr15
GLOBAL isr16
GLOBAL isr17
GLOBAL isr18
GLOBAL isr19
GLOBAL isr20
GLOBAL isr21
GLOBAL isr22
GLOBAL isr23
GLOBAL isr24
GLOBAL isr25
GLOBAL isr26
GLOBAL isr27
GLOBAL isr28
GLOBAL isr29
GLOBAL isr30
GLOBAL isr31
GLOBAL irq0
GLOBAL irq1
GLOBAL irq2
GLOBAL irq3
GLOBAL irq4
GLOBAL irq5
GLOBAL irq6
GLOBAL irq7
GLOBAL irq8
GLOBAL irq9
GLOBAL irq10
GLOBAL irq11
GLOBAL irq12
GLOBAL irq13
GLOBAL irq14
GLOBAL irq15
GLOBAL isr128
GLOBAL gdt_flush
GLOBAL tss_flush
GLOBAL enter_user_mode
GLOBAL resume_to_kernel

EXTERN isr_dispatch

idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

%macro ISR_NOERR 2
%1:
    push dword 0
    push dword %2
    jmp isr_common
%endmacro

%macro ISR_ERR 2
%1:
    push dword %2
    jmp isr_common
%endmacro

ISR_NOERR isr0, 0
ISR_NOERR isr1, 1
ISR_NOERR isr2, 2
ISR_NOERR isr3, 3
ISR_NOERR isr4, 4
ISR_NOERR isr5, 5
ISR_NOERR isr6, 6
ISR_NOERR isr7, 7
ISR_ERR   isr8, 8
ISR_NOERR isr9, 9
ISR_ERR   isr10, 10
ISR_ERR   isr11, 11
ISR_ERR   isr12, 12
ISR_ERR   isr13, 13
ISR_ERR   isr14, 14
ISR_NOERR isr15, 15
ISR_NOERR isr16, 16
ISR_ERR   isr17, 17
ISR_NOERR isr18, 18
ISR_NOERR isr19, 19
ISR_NOERR isr20, 20
ISR_NOERR isr21, 21
ISR_NOERR isr22, 22
ISR_NOERR isr23, 23
ISR_NOERR isr24, 24
ISR_NOERR isr25, 25
ISR_NOERR isr26, 26
ISR_NOERR isr27, 27
ISR_NOERR isr28, 28
ISR_ERR   isr29, 29
ISR_ERR   isr30, 30
ISR_NOERR isr31, 31
ISR_NOERR irq0, 32
ISR_NOERR irq1, 33
ISR_NOERR irq2, 34
ISR_NOERR irq3, 35
ISR_NOERR irq4, 36
ISR_NOERR irq5, 37
ISR_NOERR irq6, 38
ISR_NOERR irq7, 39
ISR_NOERR irq8, 40
ISR_NOERR irq9, 41
ISR_NOERR irq10, 42
ISR_NOERR irq11, 43
ISR_NOERR irq12, 44
ISR_NOERR irq13, 45
ISR_NOERR irq14, 46
ISR_NOERR irq15, 47
ISR_NOERR isr128, 0x80

isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call isr_dispatch
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iretd

; void gdt_flush(const struct GdtPtr *ptr)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    push 0x08
    push .reload
    retf
.reload:
    ret

; void tss_flush(void)
tss_flush:
    mov ax, 0x28
    ltr ax
    ret

; void enter_user_mode(u32 entry, u32 user_esp, u32 user_ss, u32 user_cs)
; Saves the current ring-0 kernel context, then iret's into ring 3. Control
; comes back here (via resume_to_kernel) when the user app makes the exit
; syscall, so this behaves like a normal call that returns after the app exits.
enter_user_mode:
    pushad
    mov [saved_kernel_esp], esp
    mov eax, [esp + 36]          ; entry point (linear)
    mov ecx, [esp + 40]          ; user stack top
    mov edx, [esp + 44]          ; user SS (RPL 3)
    mov ebx, [esp + 48]          ; user CS (RPL 3)

    pushfd
    pop esi
    or esi, 0x200                ; force IF=1 in user mode

    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push edx                     ; SS
    push ecx                     ; ESP
    push esi                     ; EFLAGS
    push ebx                     ; CS
    push eax                     ; EIP
    iretd

; void resume_to_kernel(void) - never returns to its caller; instead unwinds
; back to enter_user_mode's caller using the saved kernel ESP.
resume_to_kernel:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_kernel_esp]
    popad
    ret

SECTION .bss
saved_kernel_esp: resd 1
