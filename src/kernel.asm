BITS 16
ORG 0x0000
CPU 386

FAT_SEG equ 0x2000
ROOT_SEG equ 0x2200
FILE_SEG equ 0x2500
FAT_LBA equ 1
ROOT_LBA equ 19
DATA_LBA equ 33
FAT_SECTORS equ 9
ROOT_SECTORS equ 14
ROOT_ENTRIES equ 224

SCREEN_W equ 320
SCREEN_H equ 216
NATIVE_W equ 1920
NATIVE_H equ 1080
XSCALE equ 6
YSCALE equ 5
; Crisp text uses the BIOS 8x16 ROM font, drawn at 2x.
FONT_GW equ 8
FONT_GH equ 16
FONT_SX equ 2
FONT_SY equ 2
FONT_CELL_W equ 18
FONT_CELL_H equ 32
CURSOR_BACK_W equ 9
CURSOR_BACK_H equ 13
CURSOR_BACK_PIXELS equ CURSOR_BACK_W * XSCALE * CURSOR_BACK_H * YSCALE

; -- Windows 10 style desktop layout (native 1920x1080 pixels) --
; The window size is fixed; its top-left (win_x/win_y) is movable at runtime.
WIN_W equ 1280
WIN_H equ 768
TITLE_H equ 64
ADDR_H equ 32
SIDEBAR_W equ 252
STATUS_H equ 32
CONTENT_OFF equ TITLE_H + ADDR_H
PANEL_H equ WIN_H - CONTENT_OFF - STATUS_H
STATUS_OFF equ WIN_H - STATUS_H
WIN_X_MAX equ NATIVE_W - WIN_W - 12
WIN_Y_MAX equ 240
TB_Y equ 1008
TB_H equ 72

; Fill a native-pixel rectangle with a named 32-bit color (absolute pixels).
%macro PFILL 5
    mov word [px_x], %1
    mov word [px_y], %2
    mov word [px_w], %3
    mov word [px_h], %4
    mov eax, [%5]
    mov [rect_color32], eax
    call px_fill
%endmacro

; Vertical gradient fill between two named 32-bit colors (absolute pixels).
%macro PVGRAD 6
    mov word [px_x], %1
    mov word [px_y], %2
    mov word [px_w], %3
    mov word [px_h], %4
    mov eax, [%5]
    mov [grad_top], eax
    mov eax, [%6]
    mov [grad_bot], eax
    call px_vgrad
%endmacro

; Window-relative fill: offsets are added to the live window position.
%macro WFILL 5
    mov ax, [win_x]
    add ax, %1
    mov [px_x], ax
    mov ax, [win_y]
    add ax, %2
    mov [px_y], ax
    mov word [px_w], %3
    mov word [px_h], %4
    mov eax, [%5]
    mov [rect_color32], eax
    call px_fill
%endmacro

; Window-relative vertical gradient.
%macro WVGRAD 6
    mov ax, [win_x]
    add ax, %1
    mov [px_x], ax
    mov ax, [win_y]
    add ax, %2
    mov [px_y], ax
    mov word [px_w], %3
    mov word [px_h], %4
    mov eax, [%5]
    mov [grad_top], eax
    mov eax, [%6]
    mov [grad_bot], eax
    call px_vgrad
%endmacro

; Draw a string at a character cell (row, col, palette color index, label).
%macro PTEXT 4
    mov dh, %1
    mov dl, %2
    mov bl, %3
    mov si, %4
    call put_string_at
%endmacro

start:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE
    cld
    sti

    mov [boot_drive], dl
    call serial_init
    mov si, serial_boot
    call serial_print

    call load_filesystem
    call setup_vbe_1080p
    call enable_unreal_mode

    ; Linear address of the BIOS 8x16 font (avoids 16-bit segment wrap).
    movzx eax, word [font_seg]
    shl eax, 4
    movzx ebx, word [font_off]
    add eax, ebx
    mov [font_lin], eax

    call setup_keyboard
    call setup_mouse

    mov si, serial_ready
    call serial_print

    call boot_animation

main_loop:
    cmp byte [dirty], 0
    je .skip_render
    cmp byte [cursor_drawn], 0
    je .render_now
    call restore_cursor_backing
.render_now:
    call render_desktop
    mov byte [dirty], 0
.skip_render:
    call process_keyboard
    call process_drag
    call process_mouse_click
    cmp byte [mouse_dirty], 0
    je .skip_mouse_render
    call refresh_mouse_cursor
.skip_mouse_render:
    hlt
    jmp main_loop

load_filesystem:
    mov ax, FAT_SEG
    mov es, ax
    xor bx, bx
    mov ax, FAT_LBA
    mov cx, FAT_SECTORS
    call read_sectors

    mov ax, ROOT_SEG
    mov es, ax
    xor bx, bx
    mov ax, ROOT_LBA
    mov cx, ROOT_SECTORS
    call read_sectors

    call count_files
    mov si, serial_fs
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
    jc disk_fail
    add bx, 512
    inc ax
    loop .next
    ret

disk_fail:
    mov si, serial_disk_error
    call serial_print
.halt:
    hlt
    jmp .halt

setup_vbe_1080p:
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    mov [font_seg], es
    mov [font_off], bp

    mov ax, cs
    mov es, ax
    mov di, vbe_info
    mov dword [vbe_info], "VBE2"
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail

    mov ax, [vbe_info + 16]
    mov [mode_list_seg], ax
    mov ax, [vbe_info + 14]
    mov [mode_list_off], ax
    mov word [selected_vbe_mode], 0

.scan_next:
    mov ax, [mode_list_seg]
    mov es, ax
    mov si, [mode_list_off]
    mov cx, [es:si]
    cmp cx, 0xFFFF
    je vbe_fail
    add word [mode_list_off], 2
    mov [current_vbe_mode], cx

    mov ax, cs
    mov es, ax
    mov di, mode_info
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .scan_next
    test word [mode_info], 0x0080
    jz .scan_next
    cmp word [mode_info + 18], NATIVE_W
    jne .scan_next
    cmp word [mode_info + 20], NATIVE_H
    jne .scan_next
    cmp byte [mode_info + 25], 32
    jne .scan_next

    mov ax, [current_vbe_mode]
    mov [selected_vbe_mode], ax

    mov ax, [mode_info + 50]
    test ax, ax
    jnz .pitch_ok
    mov ax, [mode_info + 16]
.pitch_ok:
    mov [vbe_pitch], ax
    mov eax, [mode_info + 40]
    mov [lfb_base], eax

    mov ax, 0x4F02
    mov bx, [selected_vbe_mode]
    or bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail

    mov si, serial_vbe
    call serial_print
    ret

vbe_fail:
    mov si, serial_vbe_fail
    call serial_print
.halt:
    hlt
    jmp .halt

enable_unreal_mode:
    cli
    push ds
    mov eax, cs
    shl eax, 4
    add eax, gdt_start
    mov [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp short .protected
.protected:
    mov ax, 0x10
    mov fs, ax
    mov es, ax
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp short .real
.real:
    mov ax, cs
    mov es, ax
    pop ds
    sti
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

render_desktop:
    push cs
    pop ds

    ; Desktop wallpaper: smooth blue gradient across the whole screen.
    PVGRAD 0, 0, NATIVE_W, NATIVE_H, c_wptop, c_wpbot

    ; Soft drop shadow under the window.
    WFILL 10, 16, WIN_W, WIN_H, c_shadow
    WFILL 6, 10, WIN_W + 4, WIN_H, c_shadow2

    ; Window body and a crisp 2px frame.
    WFILL 0, 0, WIN_W, WIN_H, c_white
    WFILL -2, -2, WIN_W + 4, 2, c_border
    WFILL -2, WIN_H, WIN_W + 4, 2, c_border
    WFILL -2, -2, 2, WIN_H + 4, c_border
    WFILL WIN_W, -2, 2, WIN_H + 4, c_border

    ; Title bar gradient + accent underline.
    WVGRAD 0, 0, WIN_W, TITLE_H, c_titletop, c_titlebot
    WFILL 0, TITLE_H - 2, WIN_W, 2, c_accent

    ; Window control buttons (minimize / close).
    WFILL WIN_W - 64, 0, 64, TITLE_H - 2, c_close
    WFILL WIN_W - 128, 0, 64, TITLE_H - 2, c_titlebot

    ; Address bar.
    WFILL 0, TITLE_H, WIN_W, ADDR_H, c_addr

    ; Sidebar panel and the vertical split line.
    WFILL 0, CONTENT_OFF, SIDEBAR_W, PANEL_H, c_side
    WFILL SIDEBAR_W - 1, CONTENT_OFF, 2, PANEL_H, c_split

    ; In-window status strip.
    WFILL 0, STATUS_OFF - 1, WIN_W, 1, c_split
    WFILL 0, STATUS_OFF, WIN_W, STATUS_H, c_status

    ; Window text is positioned relative to the window's top-left.
    mov ax, [win_x]
    mov [text_org_x], ax
    mov ax, [win_y]
    mov [text_org_y], ax

    PTEXT 0, 1, 0, title_text
    PTEXT 0, 65, 0, min_glyph
    PTEXT 0, 69, 15, close_glyph
    PTEXT 2, 1, 0, path_text
    PTEXT 3, 1, 9, side_title_text
    PTEXT 3, 15, 9, viewer_text

    call render_file_list
    call render_viewer
    call render_status

    xor ax, ax
    mov [text_org_x], ax
    mov [text_org_y], ax
    call draw_taskbar

    mov byte [cursor_drawn], 0
    call render_mouse_cursor
    ret

; ---- Clip px_x/px_y/px_w/px_h to the screen. Returns CF set if nothing to draw,
;      otherwise clip_w/clip_h hold the visible size. ----
px_clip:
    mov ax, [px_x]
    cmp ax, NATIVE_W
    jae .empty
    mov bx, [px_y]
    cmp bx, NATIVE_H
    jae .empty

    mov cx, [px_w]
    mov dx, NATIVE_W
    sub dx, ax
    cmp cx, dx
    jbe .w_ok
    mov cx, dx
.w_ok:
    test cx, cx
    jz .empty
    mov [clip_w], cx

    mov cx, [px_h]
    mov dx, NATIVE_H
    sub dx, bx
    cmp cx, dx
    jbe .h_ok
    mov cx, dx
.h_ok:
    test cx, cx
    jz .empty
    mov [clip_h], cx
    clc
    ret
.empty:
    stc
    ret

; ---- Fast native-pixel rectangle fill (px_*, rect_color32) ----
px_fill:
    push es
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    call px_clip
    jc .skip

    xor ax, ax
    mov es, ax

    mov edi, [lfb_base]
    movzx eax, word [px_y]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    movzx eax, word [px_x]
    shl eax, 2
    add edi, eax

    mov dx, [clip_h]
.row:
    push edi
    movzx ecx, word [clip_w]
    mov eax, [rect_color32]
    a32 rep stosd
    pop edi
    add edi, [vbe_pitch]
    dec dx
    jnz .row

.skip:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop es
    ret

; ---- Fast native-pixel vertical gradient (px_*, grad_top, grad_bot) ----
px_vgrad:
    push es
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov eax, [grad_top]
    mov ebx, eax
    and ebx, 0xFF
    mov [gt_b], bx
    mov ebx, eax
    shr ebx, 8
    and ebx, 0xFF
    mov [gt_g], bx
    mov ebx, eax
    shr ebx, 16
    and ebx, 0xFF
    mov [gt_r], bx

    mov eax, [grad_bot]
    mov ebx, eax
    and ebx, 0xFF
    sub bx, [gt_b]
    mov [gd_b], bx
    mov ebx, eax
    shr ebx, 8
    and ebx, 0xFF
    sub bx, [gt_g]
    mov [gd_g], bx
    mov ebx, eax
    shr ebx, 16
    and ebx, 0xFF
    sub bx, [gt_r]
    mov [gd_r], bx

    mov ax, [px_h]
    dec ax
    jnz .den_ok
    mov ax, 1
.den_ok:
    mov [gden], ax

    call px_clip
    jc .done

    xor ax, ax
    mov es, ax

    mov edi, [lfb_base]
    movzx eax, word [px_y]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    movzx eax, word [px_x]
    shl eax, 2
    add edi, eax

    xor si, si
.row:
    cmp si, [clip_h]
    jae .done

    mov ax, [gd_b]
    imul si
    idiv word [gden]
    add ax, [gt_b]
    mov [cc_b], al

    mov ax, [gd_g]
    imul si
    idiv word [gden]
    add ax, [gt_g]
    mov [cc_g], al

    mov ax, [gd_r]
    imul si
    idiv word [gden]
    add ax, [gt_r]
    mov [cc_r], al

    xor eax, eax
    mov al, [cc_r]
    shl eax, 8
    mov al, [cc_g]
    shl eax, 8
    mov al, [cc_b]
    mov [rect_color32], eax

    push edi
    push esi
    movzx ecx, word [clip_w]
    a32 rep stosd
    pop esi
    pop edi
    add edi, [vbe_pitch]
    inc si
    jmp .row

.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop es
    ret

; ---- Windows 10 style taskbar ----
draw_taskbar:
    PFILL 0, TB_Y, NATIVE_W, TB_H, c_tbar
    PFILL 0, TB_Y, NATIVE_W, 2, c_accent

    ; Start button: four tilted accent squares.
    PFILL 30, TB_Y + 18, 16, 16, c_accent2
    PFILL 50, TB_Y + 18, 16, 16, c_accent2
    PFILL 30, TB_Y + 38, 16, 16, c_accent2
    PFILL 50, TB_Y + 38, 16, 16, c_accent2

    ; Search pill.
    PFILL 110, TB_Y + 16, 360, 40, c_search
    PTEXT 32, 7, 7, search_text

    ; Pinned app icons.
    PFILL 520, TB_Y + 18, 36, 36, c_accent
    PFILL 580, TB_Y + 18, 36, 36, c_orange
    PFILL 640, TB_Y + 18, 36, 36, c_green
    PFILL 700, TB_Y + 18, 36, 36, c_close

    ; Clock and date on the right.
    PTEXT 32, 96, 15, clock_text
    PTEXT 33, 96, 7, date_text
    ret

; ---- One-time boot animation ----
boot_animation:
    PVGRAD 0, 0, NATIVE_W, NATIVE_H, c_bootbg_top, c_bootbg_bot
    PTEXT 19, 50, 15, boot_title
    PTEXT 21, 49, 7, boot_sub
    PFILL 760, 600, 400, 6, c_track

    mov word [anim_w], 16
.step:
    mov ax, [anim_w]
    cmp ax, 400
    ja .done
    mov word [px_x], 760
    mov word [px_y], 600
    mov [px_w], ax
    mov word [px_h], 6
    mov eax, [c_accent2]
    mov [rect_color32], eax
    call px_fill
    call boot_delay
    add word [anim_w], 16
    jmp .step
.done:
    call boot_delay
    ret

boot_delay:
    push ecx
    mov ecx, 0x00A00000
.spin:
    dec ecx
    jnz .spin
    pop ecx
    ret

draw_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push eax
    push ebx
    push ecx
    push edx
    push edi

    mov [rect_color], al
    xor ah, ah
    shl ax, 2
    mov bp, ax
    mov eax, [color_table + bp]
    mov [rect_color32], eax

    mov ax, bx
    mov bp, XSCALE
    mul bp
    mov [rect_x_px], ax

    mov ax, cx
    mov bp, YSCALE
    mul bp
    mov [rect_y_px], ax

    mov ax, dx
    mov bp, XSCALE
    mul bp
    mov [rect_w_px], ax

    mov ax, si
    mov bp, YSCALE
    mul bp
    mov [rect_h_px], ax

    mov edi, [lfb_base]
    xor eax, eax
    mov ax, [rect_y_px]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    xor eax, eax
    mov ax, [rect_x_px]
    shl eax, 2
    add edi, eax

    mov dx, [rect_h_px]
.row:
    push edi
    mov cx, [rect_w_px]
    mov eax, [rect_color32]
.pixel:
    mov [fs:edi], eax
    add edi, 4
    loop .pixel
    pop edi
    add edi, [vbe_pitch]
    dec dx
    jnz .row

    pop edi
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

put_string_at:
    push ax
    push bx
    push cx
    push dx
    push si

.next:
    lodsb
    test al, al
    jz .done
    call draw_char_at
    inc dl
    jmp .next

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

put_char_at:
    push ax
    push bx
    push cx
    push dx
    mov [char_tmp], al
    mov al, [char_tmp]
    call draw_char_at
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---- Draw one character using the BIOS 8x16 ROM font, scaled 2x ----
draw_char_at:
    push es
    push eax
    push ebx
    push cx
    push dx
    push si
    push di

    mov [glyph_color], bl
    mov [char_tmp], al
    mov [save_dx], dx

    movzx ax, byte [save_dx]
    mov bx, FONT_CELL_W
    mul bx
    add ax, [text_org_x]
    mov [glyph_x], ax

    movzx ax, byte [save_dx + 1]
    mov bx, FONT_CELL_H
    mul bx
    add ax, [text_org_y]
    mov [glyph_y], ax

    mov al, [glyph_color]
    xor ah, ah
    shl ax, 2
    mov bx, ax
    mov eax, [color_table + bx]
    mov [rect_color32], eax

    movzx ebx, byte [char_tmp]
    shl ebx, 4
    add ebx, [font_lin]
    mov [glyph_lin], ebx

    xor ax, ax
    mov es, ax

    mov byte [glyph_row], 0
.row:
    mov ebx, [glyph_lin]
    mov al, [fs:ebx]
    mov [glyph_bits], al
    inc dword [glyph_lin]
    mov byte [glyph_col], 0
.col:
    test byte [glyph_bits], 0x80
    jz .next_col
    call draw_glyph_block
.next_col:
    shl byte [glyph_bits], 1
    inc byte [glyph_col]
    cmp byte [glyph_col], 8
    jb .col
    inc byte [glyph_row]
    cmp byte [glyph_row], 16
    jb .row

    pop di
    pop si
    pop dx
    pop cx
    pop ebx
    pop eax
    pop es
    ret

; ---- Draw a single 2x2 font pixel block (assumes ES = flat) ----
draw_glyph_block:
    push eax
    push ebx
    push edx
    push edi

    movzx ax, byte [glyph_col]
    shl ax, 1
    add ax, [glyph_x]
    cmp ax, NATIVE_W
    jae .skip
    mov [gb_x], ax

    movzx ax, byte [glyph_row]
    shl ax, 1
    add ax, [glyph_y]
    cmp ax, NATIVE_H
    jae .skip
    mov [gb_y], ax

    mov edi, [lfb_base]
    movzx eax, word [gb_y]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    movzx eax, word [gb_x]
    shl eax, 2
    add edi, eax

    mov eax, [rect_color32]
    mov [fs:edi], eax
    mov [fs:edi + 4], eax
    add edi, [vbe_pitch]
    mov [fs:edi], eax
    mov [fs:edi + 4], eax

.skip:
    pop edi
    pop edx
    pop ebx
    pop eax
    ret

render_file_list:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    mov ax, ROOT_SEG
    mov es, ax
    xor di, di
    mov word [render_index], 0
    mov cx, ROOT_ENTRIES

.entry:
    cmp byte [es:di], 0
    je .done
    call is_visible_entry
    jc .skip

    mov ax, [render_index]
    cmp ax, 18
    jae .done

    call format_name

    ; Row background rectangle inside the sidebar (highlight when selected).
    mov ax, [render_index]
    add ax, 4
    mov bx, FONT_CELL_H
    mul bx
    add ax, [win_y]
    mov [px_y], ax
    mov ax, [win_x]
    mov [px_x], ax
    mov word [px_w], SIDEBAR_W
    mov word [px_h], FONT_CELL_H
    mov eax, [c_side]
    mov bp, [render_index]
    cmp bp, [selected]
    jne .setcol
    mov eax, [c_sel]
.setcol:
    mov [rect_color32], eax
    call px_fill

    mov ax, [render_index]
    add ax, 4
    mov dh, al
    mov dl, 1
    mov bl, 0
    mov si, name_buffer
    call put_string_at
    inc word [render_index]

.skip:
    add di, 32
    loop .entry

.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

is_visible_entry:
    cmp byte [es:di], 0xE5
    je .hidden
    mov al, [es:di + 11]
    test al, 0x1E
    jnz .hidden
    clc
    ret
.hidden:
    stc
    ret

format_name:
    push ax
    push bx
    push cx
    push di
    push si

    mov si, name_buffer
    mov cx, 8
    xor bx, bx
.base:
    mov al, [es:di + bx]
    cmp al, ' '
    je .base_next
    mov [si], al
    inc si
.base_next:
    inc bx
    loop .base

    cmp byte [es:di + 8], ' '
    je .finish
    mov byte [si], '.'
    inc si
    mov cx, 3
    xor bx, bx
.ext:
    mov al, [es:di + 8 + bx]
    cmp al, ' '
    je .ext_next
    mov [si], al
    inc si
.ext_next:
    inc bx
    loop .ext

.finish:
    mov byte [si], 0
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret

render_viewer:
    cmp byte [file_loaded], 0
    jne .loaded
    mov dh, 5
    mov dl, 15
    mov bl, 0
    mov si, no_file_text
    call put_string_at
    mov dh, 7
    mov dl, 15
    mov bl, 0
    mov si, controls_text
    call put_string_at
    ret

.loaded:
    mov dh, 4
    mov dl, 15
    mov bl, 0
    mov si, open_prefix
    call put_string_at
    mov dh, 4
    mov dl, 21
    mov bl, 9
    mov si, open_name
    call put_string_at
    call render_file_content
    ret

render_file_content:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    mov ax, FILE_SEG
    mov es, ax
    xor di, di
    mov cx, [open_size]
    cmp cx, 1200
    jbe .size_ok
    mov cx, 1200
.size_ok:
    mov byte [content_row], 5
    mov byte [content_col], 15

.next:
    cmp cx, 0
    je .done
    mov al, [es:di]
    inc di
    dec cx
    cmp al, 13
    je .next
    cmp al, 10
    je .newline
    cmp al, 9
    jne .printable
    mov al, ' '
.printable:
    cmp al, 32
    jae .emit
    mov al, '.'
.emit:
    mov dh, [content_row]
    mov dl, [content_col]
    cmp dh, 23
    jae .done
    cmp dl, 70
    ja .newline_emit
    mov bl, 0
    call put_char_at
    inc byte [content_col]
    jmp .next

.newline_emit:
.newline:
    inc byte [content_row]
    mov byte [content_col], 15
    jmp .next

.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

render_status:
    mov dh, 23
    mov dl, 1
    mov bl, 0
    mov si, status_left
    call put_string_at

    cmp byte [mouse_ok], 0
    je .mouse_bad
    mov dh, 23
    mov dl, 52
    mov bl, 0
    mov si, mouse_ok_text
    call put_string_at
    ret
.mouse_bad:
    mov dh, 23
    mov dl, 50
    mov bl, 0
    mov si, mouse_bad_text
    call put_string_at
    ret

render_mouse_cursor:
    cmp byte [mouse_ok], 0
    je .done
    cli
    mov bx, [mouse_x]
    mov cx, [mouse_y]
    call save_cursor_backing_at
    mov bx, [mouse_x]
    mov cx, [mouse_y]
    call draw_cursor_arrow_at
    mov ax, [mouse_x]
    mov [cursor_old_x], ax
    mov ax, [mouse_y]
    mov [cursor_old_y], ax
    mov byte [cursor_drawn], 1
    mov byte [mouse_dirty], 0
    sti
.done:
    ret

refresh_mouse_cursor:
    cmp byte [mouse_ok], 0
    je .done
    cli
    cmp byte [cursor_drawn], 0
    je .draw_new
    call restore_cursor_backing
.draw_new:
    mov bx, [mouse_x]
    mov cx, [mouse_y]
    call save_cursor_backing_at
    mov bx, [mouse_x]
    mov cx, [mouse_y]
    call draw_cursor_arrow_at
    mov ax, [mouse_x]
    mov [cursor_old_x], ax
    mov ax, [mouse_y]
    mov [cursor_old_y], ax
    mov byte [cursor_drawn], 1
    mov byte [mouse_dirty], 0
    sti
.done:
    ret

save_cursor_backing_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push eax
    push ebx
    push edi

    mov ax, bx
    mov [cursor_old_x], ax
    mov bp, XSCALE
    mul bp
    mov [rect_x_px], ax

    mov ax, cx
    mov [cursor_old_y], ax
    mov bp, YSCALE
    mul bp
    mov [rect_y_px], ax

    mov edi, [lfb_base]
    xor eax, eax
    mov ax, [rect_y_px]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    xor eax, eax
    mov ax, [rect_x_px]
    shl eax, 2
    add edi, eax

    mov si, cursor_backing
    mov dx, CURSOR_BACK_H * YSCALE
.row:
    push edi
    mov cx, CURSOR_BACK_W * XSCALE
.pixel:
    mov eax, [fs:edi]
    mov [si], eax
    add si, 4
    add edi, 4
    loop .pixel
    pop edi
    add edi, [vbe_pitch]
    dec dx
    jnz .row

    pop edi
    pop ebx
    pop eax
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

restore_cursor_backing:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push eax
    push ebx
    push edi

    mov ax, [cursor_old_x]
    mov bp, XSCALE
    mul bp
    mov [rect_x_px], ax

    mov ax, [cursor_old_y]
    mov bp, YSCALE
    mul bp
    mov [rect_y_px], ax

    mov edi, [lfb_base]
    xor eax, eax
    mov ax, [rect_y_px]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    xor eax, eax
    mov ax, [rect_x_px]
    shl eax, 2
    add edi, eax

    mov si, cursor_backing
    mov dx, CURSOR_BACK_H * YSCALE
.row:
    push edi
    mov cx, CURSOR_BACK_W * XSCALE
.pixel:
    mov eax, [si]
    mov [fs:edi], eax
    add si, 4
    add edi, 4
    loop .pixel
    pop edi
    add edi, [vbe_pitch]
    dec dx
    jnz .row

    mov byte [cursor_drawn], 0
    pop edi
    pop ebx
    pop eax
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

draw_cursor_arrow_at:
    push ax
    push bx
    push cx

    inc bx
    inc cx
    mov al, 0
    call draw_cursor_shape_at

    pop cx
    pop bx
    push bx
    push cx
    mov al, 15
    call draw_cursor_shape_at

    pop cx
    pop bx
    pop ax
    ret

draw_cursor_shape_at:
    push ax
    push bx
    push cx
    push dx
    push si

    mov [cursor_color], al
    mov [cursor_base_x], bx
    mov [cursor_base_y], cx
    mov si, cursor_shape
    mov byte [cursor_row], 0
.row:
    mov al, [si]
    inc si
    mov [cursor_bits], al
    mov byte [cursor_col], 0
.col:
    test byte [cursor_bits], 0x80
    jz .next_col
    push si
    call draw_cursor_cell
    pop si
.next_col:
    shl byte [cursor_bits], 1
    inc byte [cursor_col]
    cmp byte [cursor_col], 8
    jb .col
    inc byte [cursor_row]
    cmp byte [cursor_row], 12
    jb .row

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

draw_cursor_cell:
    push ax
    push bx
    push cx
    push dx
    push bp
    push eax
    push ebx
    push edi

    mov al, [cursor_color]
    xor ah, ah
    shl ax, 2
    mov bp, ax
    mov eax, [color_table + bp]
    mov [rect_color32], eax

    mov ax, [cursor_base_x]
    xor bx, bx
    mov bl, [cursor_col]
    add ax, bx
    cmp ax, SCREEN_W
    jae .done
    mov bp, XSCALE
    mul bp
    mov [rect_x_px], ax

    mov ax, [cursor_base_y]
    xor bx, bx
    mov bl, [cursor_row]
    add ax, bx
    cmp ax, SCREEN_H
    jae .done
    mov bp, YSCALE
    mul bp
    mov [rect_y_px], ax

    mov edi, [lfb_base]
    xor eax, eax
    mov ax, [rect_y_px]
    mov ebx, [vbe_pitch]
    mul ebx
    add edi, eax
    xor eax, eax
    mov ax, [rect_x_px]
    shl eax, 2
    add edi, eax

    mov dx, YSCALE
.row:
    push edi
    mov cx, XSCALE
    mov eax, [rect_color32]
.pixel:
    mov [fs:edi], eax
    add edi, 4
    loop .pixel
    pop edi
    add edi, [vbe_pitch]
    dec dx
    jnz .row

.done:
    pop edi
    pop ebx
    pop eax
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

count_files:
    push ax
    push bx
    push cx
    push di
    push es

    mov ax, ROOT_SEG
    mov es, ax
    xor di, di
    xor bx, bx
    mov cx, ROOT_ENTRIES
.entry:
    cmp byte [es:di], 0
    je .done
    call is_visible_entry
    jc .skip
    inc bx
.skip:
    add di, 32
    loop .entry
.done:
    mov [visible_count], bx
    cmp bx, 0
    je .zero
    mov ax, [selected]
    cmp ax, bx
    jb .out
    dec bx
    mov [selected], bx
    jmp .out
.zero:
    mov word [selected], 0
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

process_keyboard:
    mov al, [kbd_action]
    mov byte [kbd_action], 0
    cmp al, 1
    je .up
    cmp al, 2
    je .down
    cmp al, 3
    je .enter
    cmp al, 4
    je .escape
    jmp .done

.up:
    cmp word [selected], 0
    je .done
    dec word [selected]
    mov byte [dirty], 1
    jmp .done

.down:
    mov ax, [selected]
    inc ax
    cmp ax, [visible_count]
    jae .done
    mov [selected], ax
    mov byte [dirty], 1
    jmp .done

.enter:
    call open_selected_file
    jmp .done

.escape:
    mov byte [file_loaded], 0
    mov byte [dirty], 1

.done:
    ret

setup_keyboard:
    cli
    xor ax, ax
    mov es, ax
    mov word [es:0x09 * 4], keyboard_irq
    mov ax, cs
    mov [es:0x09 * 4 + 2], ax
    in al, 0x21
    and al, 0xFD
    out 0x21, al
    sti
    ret

keyboard_irq:
    push ax
    push ds
    push cs
    pop ds
    in al, 0x60
    cmp al, 0xE0
    jne .not_prefix
    mov byte [kbd_extended], 1
    jmp .eoi

.not_prefix:
    test al, 0x80
    jnz .release
    cmp al, 0x48
    je .up
    cmp al, 0x50
    je .down
    cmp al, 0x1C
    je .enter
    cmp al, 0x01
    je .escape
    jmp .clear

.up:
    mov byte [kbd_action], 1
    jmp .clear
.down:
    mov byte [kbd_action], 2
    jmp .clear
.enter:
    mov byte [kbd_action], 3
    jmp .clear
.escape:
    mov byte [kbd_action], 4
    jmp .clear
.release:
.clear:
    mov byte [kbd_extended], 0
.eoi:
    mov al, 0x20
    out 0x20, al
    pop ds
    pop ax
    iret

process_mouse_click:
    cmp byte [mouse_ok], 0
    je .done
    mov al, [mouse_buttons]
    mov bl, [prev_mouse_buttons]
    mov [prev_mouse_buttons], al
    test al, 1
    jz .done
    test bl, 1
    jnz .done
    cmp byte [dragging], 0
    jne .done

    ; Native click position.
    mov ax, [mouse_x]
    mov cx, 6
    mul cx
    mov [drag_nx], ax
    mov ax, [mouse_y]
    mov cx, 5
    mul cx
    mov [drag_ny], ax

    ; Close button: top-right 64px of the title bar.
    mov ax, [drag_nx]
    mov bx, [win_x]
    add bx, WIN_W - 64
    cmp ax, bx
    jb .not_close
    mov bx, [win_x]
    add bx, WIN_W
    cmp ax, bx
    jae .not_close
    mov ax, [drag_ny]
    mov bx, [win_y]
    cmp ax, bx
    jb .not_close
    mov bx, [win_y]
    add bx, TITLE_H
    cmp ax, bx
    jae .not_close
    mov byte [file_loaded], 0
    mov byte [dirty], 1
    ret

.not_close:
    ; Sidebar file row.
    mov ax, [drag_nx]
    mov bx, [win_x]
    cmp ax, bx
    jb .done
    mov bx, [win_x]
    add bx, SIDEBAR_W
    cmp ax, bx
    ja .done

    mov ax, [drag_ny]
    mov bx, [win_y]
    add bx, 128
    cmp ax, bx
    jb .done
    sub ax, bx
    xor dx, dx
    mov bx, FONT_CELL_H
    div bx
    cmp ax, [visible_count]
    jae .done
    mov [selected], ax
    call open_selected_file
    mov byte [dirty], 1
.done:
    ret

; ---- Title-bar window dragging ----
process_drag:
    cmp byte [mouse_ok], 0
    je .ret
    mov al, [mouse_buttons]
    test al, 1
    jz .release

    cmp byte [dragging], 0
    jne .move

    ; Begin drag only if the press is on the title bar (left of the buttons).
    mov ax, [mouse_x]
    mov cx, 6
    mul cx
    mov [drag_nx], ax
    mov ax, [mouse_y]
    mov cx, 5
    mul cx
    mov [drag_ny], ax

    mov ax, [drag_nx]
    mov bx, [win_x]
    cmp ax, bx
    jb .ret
    mov bx, [win_x]
    add bx, WIN_W - 128
    cmp ax, bx
    jae .ret
    mov ax, [drag_ny]
    mov bx, [win_y]
    cmp ax, bx
    jb .ret
    mov bx, [win_y]
    add bx, TITLE_H
    cmp ax, bx
    jae .ret

    mov ax, [drag_nx]
    sub ax, [win_x]
    mov [drag_dx], ax
    mov ax, [drag_ny]
    sub ax, [win_y]
    mov [drag_dy], ax
    mov byte [dragging], 1
    jmp .ret

.move:
    mov ax, [mouse_x]
    mov cx, 6
    mul cx
    sub ax, [drag_dx]
    js .x0
    cmp ax, WIN_X_MAX
    jbe .x_ok
    mov ax, WIN_X_MAX
    jmp .x_ok
.x0:
    xor ax, ax
.x_ok:
    mov [win_x], ax

    mov ax, [mouse_y]
    mov cx, 5
    mul cx
    sub ax, [drag_dy]
    js .y0
    cmp ax, WIN_Y_MAX
    jbe .y_ok
    mov ax, WIN_Y_MAX
    jmp .y_ok
.y0:
    xor ax, ax
.y_ok:
    mov [win_y], ax
    mov byte [dirty], 1
    jmp .ret

.release:
    mov byte [dragging], 0
.ret:
    ret

open_selected_file:
    call find_selected_entry
    jc .done
    call format_name
    push di
    mov si, name_buffer
    mov di, open_name
    call copy_string
    pop di
    mov ax, [es:di + 26]
    mov [current_cluster], ax
    mov ax, [es:di + 28]
    mov [open_size], ax
    call load_current_file
    mov byte [file_loaded], 1
    mov byte [dirty], 1
    mov si, serial_open
    call serial_print
.done:
    ret

find_selected_entry:
    push ax
    push bx
    push cx

    mov ax, ROOT_SEG
    mov es, ax
    xor di, di
    xor bx, bx
    mov cx, ROOT_ENTRIES
.entry:
    cmp byte [es:di], 0
    je .missing
    call is_visible_entry
    jc .skip
    cmp bx, [selected]
    je .found
    inc bx
.skip:
    add di, 32
    loop .entry
.missing:
    stc
    jmp .out
.found:
    clc
.out:
    pop cx
    pop bx
    pop ax
    ret

copy_string:
    push ax
.next:
    lodsb
    mov [di], al
    inc di
    test al, al
    jnz .next
    pop ax
    ret

load_current_file:
    push ax
    push bx
    push cx
    push dx
    push es

    mov ax, FILE_SEG
    mov es, ax
    xor bx, bx
    mov ax, [current_cluster]

.cluster:
    cmp ax, 2
    jb .done
    cmp ax, 0x0FF8
    jae .done
    cmp bx, 8192
    jae .done
    push ax
    push bx
    sub ax, 2
    add ax, DATA_LBA
    call read_lba
    pop bx
    pop ax
    jc disk_fail
    add bx, 512
    call fat_next
    jmp .cluster

.done:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fat_next:
    push bx
    push cx
    push dx
    push ds
    mov dx, ax
    mov bx, ax
    shr bx, 1
    add bx, dx
    mov cx, FAT_SEG
    mov ds, cx
    mov ax, [bx]
    test dx, 1
    jz .even
    shr ax, 4
    jmp .mask
.even:
    and ax, 0x0FFF
.mask:
    and ax, 0x0FFF
    pop ds
    pop dx
    pop cx
    pop bx
    ret

setup_mouse:
    cli
    xor ax, ax
    mov es, ax
    mov word [es:0x74 * 4], mouse_irq
    mov ax, cs
    mov [es:0x74 * 4 + 2], ax

    call ps2_wait_input_clear
    jc .failed
    mov al, 0xA8
    out 0x64, al

    call ps2_wait_input_clear
    jc .failed
    mov al, 0x20
    out 0x64, al
    call ps2_wait_output_full
    jc .failed
    in al, 0x60
    or al, 0x02
    and al, 0xDF
    mov [ps2_config], al

    call ps2_wait_input_clear
    jc .failed
    mov al, 0x60
    out 0x64, al
    call ps2_wait_input_clear
    jc .failed
    mov al, [ps2_config]
    out 0x60, al

    mov al, 0xF6
    call mouse_write
    jc .failed
    mov al, 0xF4
    call mouse_write
    jc .failed

    in al, 0xA1
    and al, 0xEF
    out 0xA1, al
    in al, 0x21
    and al, 0xFB
    out 0x21, al

    mov byte [mouse_ok], 1
    sti
    ret

.failed:
    mov byte [mouse_ok], 0
    sti
    ret

ps2_wait_input_clear:
    push cx
    mov cx, 0xFFFF
.wait:
    in al, 0x64
    test al, 0x02
    jz .ok
    loop .wait
    stc
    pop cx
    ret
.ok:
    clc
    pop cx
    ret

ps2_wait_output_full:
    push cx
    mov cx, 0xFFFF
.wait:
    in al, 0x64
    test al, 0x01
    jnz .ok
    loop .wait
    stc
    pop cx
    ret
.ok:
    clc
    pop cx
    ret

mouse_write:
    mov [mouse_cmd], al
    call ps2_wait_input_clear
    jc .failed
    mov al, 0xD4
    out 0x64, al
    call ps2_wait_input_clear
    jc .failed
    mov al, [mouse_cmd]
    out 0x60, al
    call ps2_wait_output_full
    jc .failed
    in al, 0x60
    cmp al, 0xFA
    jne .failed
    clc
    ret
.failed:
    stc
    ret

mouse_irq:
    push ax
    push bx
    push dx
    push ds
    push cs
    pop ds

    in al, 0x64
    test al, 0x01
    jz .eoi
    in al, 0x60

    mov bl, [mouse_packet_index]
    cmp bl, 0
    jne .not_first
    test al, 0x08
    jz .eoi
    mov [mouse_packet0], al
    mov byte [mouse_packet_index], 1
    jmp .eoi

.not_first:
    cmp bl, 1
    jne .third
    mov [mouse_packet1], al
    mov byte [mouse_packet_index], 2
    jmp .eoi

.third:
    mov [mouse_packet2], al
    mov byte [mouse_packet_index], 0
    call update_mouse_from_packet

.eoi:
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop ds
    pop dx
    pop bx
    pop ax
    iret

update_mouse_from_packet:
    push ax
    push bx

    mov al, [mouse_packet0]
    and al, 0x07
    mov [mouse_buttons], al

    mov al, [mouse_packet1]
    cbw
    add ax, [mouse_x]
    cmp ax, 0
    jge .x_min_ok
    xor ax, ax
.x_min_ok:
    cmp ax, SCREEN_W - CURSOR_BACK_W
    jle .x_ok
    mov ax, SCREEN_W - CURSOR_BACK_W
.x_ok:
    mov [mouse_x], ax

    mov al, [mouse_packet2]
    cbw
    neg ax
    add ax, [mouse_y]
    cmp ax, 0
    jge .y_min_ok
    xor ax, ax
.y_min_ok:
    cmp ax, SCREEN_H - CURSOR_BACK_H
    jle .y_ok
    mov ax, SCREEN_H - CURSOR_BACK_H
.y_ok:
    mov [mouse_y], ax
    inc word [mouse_packets]
    mov byte [mouse_dirty], 1

    pop bx
    pop ax
    ret

serial_init:
    mov dx, 0x03F9
    xor al, al
    out dx, al
    mov dx, 0x03FB
    mov al, 0x80
    out dx, al
    mov dx, 0x03F8
    mov al, 0x03
    out dx, al
    mov dx, 0x03F9
    xor al, al
    out dx, al
    mov dx, 0x03FB
    mov al, 0x03
    out dx, al
    mov dx, 0x03FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x03FC
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
    push dx
    mov ah, al
.wait:
    mov dx, 0x03FD
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x03F8
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

boot_drive db 0
sectors_per_cylinder db 36
sectors_per_track db 18
rect_color db 0
rect_color32 dd 0
rect_x_px dw 0
rect_y_px dw 0
rect_w_px dw 0
rect_h_px dw 0
px_x dw 0
px_y dw 0
px_w dw 0
px_h dw 0
grad_top dd 0
grad_bot dd 0
gt_b dw 0
gt_g dw 0
gt_r dw 0
gd_b dw 0
gd_g dw 0
gd_r dw 0
gden dw 1
cc_b db 0
cc_g db 0
cc_r db 0
anim_w dw 0
clip_w dw 0
clip_h dw 0
text_org_x dw 0
text_org_y dw 0
save_dx dw 0
font_lin dd 0
glyph_lin dd 0
gb_x dw 0
gb_y dw 0
win_x dw 320
win_y dw 96
dragging db 0
drag_nx dw 0
drag_ny dw 0
drag_dx dw 0
drag_dy dw 0
char_tmp db 0
glyph_color db 15
glyph_bits db 0
glyph_col db 0
glyph_row db 0
glyph_x dw 0
glyph_y dw 0
font_seg dw 0
font_off dw 0
vbe_pitch dd NATIVE_W * 4
lfb_base dd 0
mode_list_seg dw 0
mode_list_off dw 0
selected_vbe_mode dw 0
current_vbe_mode dw 0
visible_count dw 0
selected dw 0
render_index dw 0
dirty db 1
file_loaded db 0
current_cluster dw 0
open_size dw 0
content_row db 0
content_col db 0

mouse_ok db 0
mouse_x dw 160
mouse_y dw 100
mouse_dirty db 1
cursor_drawn db 0
cursor_old_x dw 160
cursor_old_y dw 100
cursor_base_x dw 0
cursor_base_y dw 0
cursor_bits db 0
cursor_col db 0
cursor_row db 0
cursor_color db 15
mouse_buttons db 0
prev_mouse_buttons db 0
mouse_packet_index db 0
mouse_packet0 db 0
mouse_packet1 db 0
mouse_packet2 db 0
mouse_packets dw 0
mouse_cmd db 0
ps2_config db 0
kbd_action db 0
kbd_extended db 0

name_buffer times 13 db 0
open_name times 13 db 0

title_text db "LeonOS Files", 0
path_text db "C:/LeonOS", 0
files_text db "Files", 0
viewer_text db "Preview", 0
no_file_text db "Select a file to preview", 0
controls_text db "Use the arrow keys or click a file", 0
open_prefix db "Open:", 0
status_left db "FAT12 ready  -  drag the title bar to move", 0
start_text db "LeonOS", 0
desktop_icon_text db "This PC", 0
side_title_text db "Quick access", 0
mouse_ok_text db "Mouse on", 0
mouse_bad_text db "Mouse: not detected", 0
min_glyph db "-", 0
close_glyph db "X", 0
search_text db "Type here to search", 0
clock_text db "10:45 PM", 0
date_text db "2026-05-29", 0
boot_title db "LeonOS", 0
boot_sub db "Starting...", 0

align 4
c_white     dd 0x00FFFFFF
c_side      dd 0x00F3F4F6
c_sel       dd 0x00CCE8FF
c_sel_hi    dd 0x000078D7
c_addr      dd 0x00F7F9FC
c_status    dd 0x00F0F1F3
c_titletop  dd 0x00FFFFFF
c_titlebot  dd 0x00EEF1F5
c_accent    dd 0x000078D7
c_accent2   dd 0x0000A4EF
c_border    dd 0x00C8CDD4
c_split     dd 0x00E2E6EA
c_tbar      dd 0x001F2226
c_search    dd 0x002C3036
c_shadow    dd 0x000C2138
c_shadow2   dd 0x00132C49
c_wptop     dd 0x00103A6E
c_wpbot     dd 0x00328BD6
c_close     dd 0x00E81123
c_orange    dd 0x00E8862B
c_green     dd 0x0016A085
c_track     dd 0x00203A52
c_bootbg_top dd 0x00061122
c_bootbg_bot dd 0x00103A6E

cursor_shape:
    db 10000000b
    db 11000000b
    db 11100000b
    db 11110000b
    db 11111000b
    db 11111100b
    db 11100000b
    db 11000000b
    db 10100000b
    db 00100000b
    db 00110000b
    db 00000000b

cursor_backing times CURSOR_BACK_PIXELS dd 0

serial_boot db "LeonOS kernel loaded", 13, 10, 0
serial_fs db "FAT12 root loaded", 13, 10, 0
serial_vbe db "VBE 1920x1080x32 framebuffer ready", 13, 10, 0
serial_vbe_fail db "VBE 1920x1080x32 not available", 13, 10, 0
serial_ready db "LeonOS GUI ready", 13, 10, 0
serial_open db "FAT12 file opened", 13, 10, 0
serial_disk_error db "LeonOS disk read error", 13, 10, 0

align 4
color_table:
    dd 0x00000000 ; 0 black
    dd 0x001B1F24 ; 1 dark shell
    dd 0x000B5CAB ; 2 desktop blue
    dd 0x00D7E9FF ; 3 selected row
    dd 0x00E81123 ; 4 close red
    dd 0x00545B63 ; 5 shadow
    dd 0x00AA5500 ; 6 brown
    dd 0x00E9EEF4 ; 7 light gray
    dd 0x00CAD0D8 ; 8 border gray
    dd 0x000078D7 ; 9 Windows blue
    dd 0x0025A0DA ; 10 bright blue
    dd 0x00F3FAFF ; 11 pale panel
    dd 0x00FF5555 ; 12 light red
    dd 0x00EAF4FF ; 13 toolbar blue
    dd 0x00FFFF55 ; 14 yellow
    dd 0x00FFFFFF ; 15 white

font5x7:
    db ' ', 0x00,0x00,0x00,0x00,0x00,0x00,0x00
    db '!', 0x04,0x04,0x04,0x04,0x04,0x00,0x04
    db '-', 0x00,0x00,0x00,0x1F,0x00,0x00,0x00
    db '.', 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C
    db ',', 0x00,0x00,0x00,0x00,0x00,0x0C,0x08
    db ':', 0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00
    db '/', 0x01,0x02,0x04,0x08,0x10,0x00,0x00
    db '?', 0x0E,0x11,0x01,0x02,0x04,0x00,0x04
font_unknown:
    db '?', 0x0E,0x11,0x01,0x02,0x04,0x00,0x04
    db '0', 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E
    db '1', 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E
    db '2', 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F
    db '3', 0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E
    db '4', 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02
    db '5', 0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E
    db '6', 0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E
    db '7', 0x1F,0x01,0x02,0x04,0x08,0x08,0x08
    db '8', 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E
    db '9', 0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E
    db 'A', 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11
    db 'B', 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E
    db 'C', 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E
    db 'D', 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E
    db 'E', 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F
    db 'F', 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10
    db 'G', 0x0E,0x11,0x10,0x17,0x11,0x11,0x0E
    db 'H', 0x11,0x11,0x11,0x1F,0x11,0x11,0x11
    db 'I', 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E
    db 'J', 0x01,0x01,0x01,0x01,0x11,0x11,0x0E
    db 'K', 0x11,0x12,0x14,0x18,0x14,0x12,0x11
    db 'L', 0x10,0x10,0x10,0x10,0x10,0x10,0x1F
    db 'M', 0x11,0x1B,0x15,0x15,0x11,0x11,0x11
    db 'N', 0x11,0x19,0x15,0x13,0x11,0x11,0x11
    db 'O', 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E
    db 'P', 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10
    db 'Q', 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D
    db 'R', 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11
    db 'S', 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E
    db 'T', 0x1F,0x04,0x04,0x04,0x04,0x04,0x04
    db 'U', 0x11,0x11,0x11,0x11,0x11,0x11,0x0E
    db 'V', 0x11,0x11,0x11,0x11,0x11,0x0A,0x04
    db 'W', 0x11,0x11,0x11,0x15,0x15,0x15,0x0A
    db 'X', 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11
    db 'Y', 0x11,0x11,0x0A,0x04,0x04,0x04,0x04
    db 'Z', 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F
    db 0

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

align 16
vbe_info times 512 db 0
mode_info times 256 db 0
