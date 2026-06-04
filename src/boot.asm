%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1
%endif

BITS 16
ORG 0x7C00

KERNEL_LOAD_SEG equ 0x1000
KERNEL_START_LBA equ 33
SECTORS_PER_TRACK equ 18
HEADS equ 2

    jmp short boot_start
    nop

    db "LEONOS  "
    dw 512
    db 1
    dw 1
    db 2
    dw 224
    dw 2880
    db 0xF0
    dw 9
    dw 18
    dw 2
    dd 0
    dd 0
    db 0
    db 0
    db 0x29
    dd 0x26052902
    db "LEONOS     "
    db "FAT12   "

boot_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl
    mov si, msg_loading
    call print_string

    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx
    mov si, KERNEL_START_LBA
    mov cx, KERNEL_SECTORS

.load_kernel:
    push cx
    push si
    push bx
    mov ax, si
    call read_lba
    jc disk_error
    pop bx
    pop si
    pop cx
    add bx, 512
    jnc .sector_done
    mov ax, es
    add ax, 0x1000
    mov es, ax
    xor bx, bx
.sector_done:
    inc si
    loop .load_kernel

    mov dl, [boot_drive]
    jmp KERNEL_LOAD_SEG:0x0000

disk_error:
    mov si, msg_disk_error
    call print_string
.halt:
    hlt
    jmp .halt

read_lba:
    push ax
    push bx
    push cx
    push dx

    div byte [sectors_per_cylinder]
    mov ch, al

    mov al, ah
    xor ah, ah
    div byte [sectors_per_track]
    mov dh, al
    mov cl, ah
    inc cl

    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    jc .failed
    clc
    jmp .done

.failed:
    stc

.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp print_string
.done:
    ret

boot_drive db 0
sectors_per_cylinder db SECTORS_PER_TRACK * HEADS
sectors_per_track db SECTORS_PER_TRACK
msg_loading db "LeonOS: loading KERNEL.SYS", 13, 10, 0
msg_disk_error db "LeonOS: disk read error", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
