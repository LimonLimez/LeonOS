BITS 16
ORG 0x0000
CPU 386

STAGE2_PHYS_ADDR equ 0x00010000
KERNEL32_LOAD_ADDR equ 0x00100000
BOOTINFO_ADDR equ 0x00009000
FAT_BUFFER_SEG equ 0x4000
ROOT_BUFFER_SEG equ 0x4200
DATA_CACHE_SEG equ 0x5000
FAT_BUFFER_ADDR equ 0x00040000
ROOT_BUFFER_ADDR equ 0x00042000
DATA_CACHE_ADDR equ 0x00050000
FAT_LBA equ 1
ROOT_LBA equ 19
DATA_LBA equ 33
FAT_SECTORS equ 9
ROOT_SECTORS equ 14
ROOT_ENTRIES equ 224
DATA_CACHE_SECTORS equ 128
CODE_SEL equ 0x08
DATA_SEL equ 0x10

stage2_start:
    cli
    mov ax, cs
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    sti

    mov [boot_drive], dl
    call serial_init
    mov si, msg_stage2
    call serial_print

    call enable_a20
    call build_minimal_bootinfo
    call collect_e820
    call setup_vbe_mode
    call preload_fat12_buffers

%ifdef HAS_KERNEL32
    mov si, msg_pm
    call serial_print
    jmp enter_protected_mode
%else

    mov si, msg_ready
    call serial_print

.halt:
    hlt
    jmp .halt
%endif

enable_a20:
    in al, 0x92
    or al, 00000010b
    out 0x92, al
    ret

build_minimal_bootinfo:
    push es
    xor ax, ax
    mov es, ax
    mov di, BOOTINFO_ADDR
    mov dword [es:di + 0], 0x4C454F4E
    mov dword [es:di + 4], 1
    mov dword [es:di + 8], 0
    mov dword [es:di + 12], 0
    mov dword [es:di + 16], 0
    mov dword [es:di + 20], 0
    mov dword [es:di + 24], 0
    mov dword [es:di + 28], 0
    xor eax, eax
    mov al, [boot_drive]
    mov [es:di + 800], eax
    mov dword [es:di + 804], 1
    mov dword [es:di + 808], 19
    mov dword [es:di + 812], 33
    mov dword [es:di + 816], FAT_BUFFER_ADDR
    mov dword [es:di + 820], FAT_SECTORS
    mov dword [es:di + 824], ROOT_BUFFER_ADDR
    mov dword [es:di + 828], ROOT_ENTRIES
    mov dword [es:di + 832], DATA_CACHE_ADDR
    mov dword [es:di + 836], DATA_CACHE_SECTORS
    pop es
    ret

collect_e820:
    push es
    push bp
    xor ax, ax
    mov es, ax
    xor ebx, ebx
    xor bp, bp
    mov di, BOOTINFO_ADDR + 32

.next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov dword [es:di + 20], 0
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .done

    mov eax, [es:di + 8]
    or eax, [es:di + 12]
    jz .skip_entry

    inc bp
    add di, 24
    cmp bp, 32
    jae .done

.skip_entry:
    test ebx, ebx
    jne .next

.done:
    mov di, BOOTINFO_ADDR + 28
    movzx eax, bp
    mov [es:di], eax
    pop bp
    pop es
    ret

%ifndef VBE_PREF_W
%define VBE_PREF_W 1920
%define VBE_PREF_H 1080
%endif

; Pick a 32-bit linear VBE mode. Tries the build-time preferred size first, then a
; small fallback chain, and records the actual width/height in BootInfo.
setup_vbe_mode:
    mov ax, cs
    mov es, ax
    mov di, vbe_info
    mov dword [vbe_info], "VBE2"
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fail_all

    mov ax, [vbe_info + 16]
    mov [mode_list_seg], ax
    mov ax, [vbe_info + 14]
    mov [mode_list_off], ax
    mov [mode_list_off_initial], ax

    mov ax, VBE_PREF_W
    mov bx, VBE_PREF_H
    call vbe_try_dimensions
    jc .report_ok

%ifdef VBE_PREF_720
    mov ax, 1366
    mov bx, 768
    call vbe_try_dimensions
    jc .report_ok
    mov ax, 1024
    mov bx, 768
    call vbe_try_dimensions
    jc .report_ok
    mov ax, 1920
    mov bx, 1080
    call vbe_try_dimensions
    jc .report_ok
%else
    mov ax, 1366
    mov bx, 768
    call vbe_try_dimensions
    jc .report_ok
    mov ax, 1280
    mov bx, 720
    call vbe_try_dimensions
    jc .report_ok
    mov ax, 1024
    mov bx, 768
    call vbe_try_dimensions
    jc .report_ok
%endif
    jmp .fail_all

.report_ok:
    mov si, msg_vbe_prefix
    call serial_print
    mov ax, [vbe_selected_w]
    call serial_print_u16
    mov si, msg_vbe_mid
    call serial_print
    mov ax, [vbe_selected_h]
    call serial_print_u16
    mov si, msg_vbe_suffix
    call serial_print
    ret

.fail_all:
    mov si, msg_vbe_fail
    call serial_print
    ret

; AX = width, BX = height. Carry set on success.
vbe_try_dimensions:
    mov [desired_w], ax
    mov [desired_h], bx
    mov ax, [mode_list_off_initial]
    mov [mode_list_off], ax

.scan:
    mov ax, [mode_list_seg]
    mov es, ax
    mov si, [mode_list_off]
    mov cx, [es:si]
    cmp cx, 0xFFFF
    je .no_match
    add word [mode_list_off], 2
    mov [current_vbe_mode], cx

    mov ax, cs
    mov es, ax
    mov di, mode_info
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .scan
    test word [mode_info], 0x0080
    jz .scan
    mov dx, [desired_w]
    cmp word [mode_info + 18], dx
    jne .scan
    mov dx, [desired_h]
    cmp word [mode_info + 20], dx
    jne .scan
    cmp byte [mode_info + 25], 32
    jne .scan

    mov ax, [current_vbe_mode]
    or ax, 0x4000
    mov bx, ax
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .no_match

    mov ax, [mode_info + 18]
    mov [vbe_selected_w], ax
    mov ax, [mode_info + 20]
    mov [vbe_selected_h], ax

    push es
    xor ax, ax
    mov es, ax
    mov di, BOOTINFO_ADDR
    mov eax, [mode_info + 40]
    mov [es:di + 8], eax
    xor eax, eax
    mov ax, [mode_info + 50]
    test ax, ax
    jnz .pitch_ok
    mov ax, [mode_info + 16]
.pitch_ok:
    mov [es:di + 12], eax
    movzx eax, word [vbe_selected_w]
    mov [es:di + 16], eax
    movzx eax, word [vbe_selected_h]
    mov [es:di + 20], eax
    mov dword [es:di + 24], 32
    pop es
    stc
    ret

.no_match:
    clc
    ret

; Print AX as unsigned decimal (no leading zeros except zero -> "0").
serial_print_u16:
    push bx
    push cx
    push dx
    mov bx, 10
    mov cx, 0
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .div
.emit:
    pop dx
    add dl, '0'
    mov al, dl
    call serial_write
    loop .emit
    pop dx
    pop cx
    pop bx
    ret

preload_fat12_buffers:
    mov ax, FAT_BUFFER_SEG
    mov es, ax
    xor bx, bx
    mov ax, FAT_LBA
    mov cx, FAT_SECTORS
    call read_sectors

    mov ax, ROOT_BUFFER_SEG
    mov es, ax
    xor bx, bx
    mov ax, ROOT_LBA
    mov cx, ROOT_SECTORS
    call read_sectors

    mov ax, DATA_CACHE_SEG
    mov es, ax
    xor bx, bx
    mov ax, DATA_LBA
    mov cx, DATA_CACHE_SECTORS
    call read_sectors

    mov si, msg_fat
    call serial_print
    ret

read_sectors:
.next:
    push ax
    push bx
    push cx
    call read_lba
    pop cx
    pop bx
    pop ax
    jc .failed
    add bx, 512
    jnc .no_wrap
    ; Crossed a 64 KiB boundary: bump ES so long reads do not overwrite
    ; the start of the buffer.
    push ax
    mov ax, es
    add ax, 0x1000
    mov es, ax
    pop ax
    xor bx, bx
.no_wrap:
    inc ax
    loop .next
    clc
    ret
.failed:
    stc
    ret

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

%ifdef HAS_KERNEL32
enter_protected_mode:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SEL:(STAGE2_PHYS_ADDR + protected_start)

BITS 32
protected_start:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000
    cld

    mov esi, STAGE2_PHYS_ADDR + msg_pm_ok
    call pm_serial_print

    mov esi, STAGE2_PHYS_ADDR + kernel32_payload
    mov edi, KERNEL32_LOAD_ADDR
    mov ecx, kernel32_payload_end - kernel32_payload
    rep movsb

    mov ebx, BOOTINFO_ADDR
    mov eax, KERNEL32_LOAD_ADDR
    jmp eax

pm_serial_print:
    lodsb
    test al, al
    jz .done
    call pm_serial_write
    jmp pm_serial_print
.done:
    ret

pm_serial_write:
    push eax
.wait:
    mov dx, 0x3F8 + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop eax
    mov dx, 0x3F8
    out dx, al
    ret

BITS 16
%endif

serial_init:
    mov dx, 0x3F8 + 1
    xor al, al
    out dx, al
    mov dx, 0x3F8 + 3
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x03
    out dx, al
    mov dx, 0x3F8 + 1
    xor al, al
    out dx, al
    mov dx, 0x3F8 + 3
    mov al, 0x03
    out dx, al
    mov dx, 0x3F8 + 2
    mov al, 0xC7
    out dx, al
    mov dx, 0x3F8 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_print:
    lodsb
    test al, al
    jz .done
    call serial_write
    jmp serial_print
.done:
    ret

serial_write:
    push ax
.wait:
    mov dx, 0x3F8 + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, 0x3F8
    out dx, al
    ret

boot_drive db 0
sectors_per_cylinder db 36
sectors_per_track db 18
mode_list_seg dw 0
mode_list_off dw 0
mode_list_off_initial dw 0
current_vbe_mode dw 0
desired_w dw 0
desired_h dw 0
vbe_selected_w dw 0
vbe_selected_h dw 0
msg_stage2 db "LeonOS stage2 entered", 13, 10, 0
msg_ready db "LeonOS stage2 BootInfo stub ready", 13, 10, 0
msg_pm db "LeonOS stage2 entering protected mode", 13, 10, 0
msg_pm_ok db "LeonOS stage2 protected mode OK", 13, 10, 0
msg_vbe_prefix db "LeonOS stage2 VBE ", 0
msg_vbe_mid db "x", 0
msg_vbe_suffix db "x32 ready", 13, 10, 0
msg_vbe_fail db "LeonOS stage2 VBE mode unavailable", 13, 10, 0
msg_fat db "LeonOS stage2 FAT12 buffers ready", 13, 10, 0

align 16
vbe_info times 512 db 0
mode_info times 256 db 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd STAGE2_PHYS_ADDR + gdt_start

%ifdef HAS_KERNEL32
align 16
kernel32_payload:
    incbin KERNEL32_BIN
kernel32_payload_end:
%endif
