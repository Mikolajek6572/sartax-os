; Sartax OS - stage2
; - wlacza A20, czyta mape pamieci (E820)
; - laduje jadro pod 1 MB (unreal mode)
; - ustawia tryb graficzny VBE (32 bpp)
; - przechodzi do trybu 64-bit i skacze do jadra
bits 16
org 0x9000

PARAMS   equ 0x5800
BOOTINFO equ 0x5000
E820BUF  equ 0x5100
VBEINFO  equ 0x6000
MODEINFO equ 0x6200
BUFSEG   equ 0x1000          ; bufor 0x10000
KERNEL   equ 0x100000
PML4     equ 0x70000

P_LBA    equ PARAMS + 0x00
P_SECT   equ PARAMS + 0x04
P_SSIZE  equ PARAMS + 0x08
P_DRIVE  equ PARAMS + 0x0A
P_TYPE   equ PARAMS + 0x0B
P_KSIZE  equ PARAMS + 0x0C
P_PREFW  equ PARAMS + 0x10
P_PREFH  equ PARAMS + 0x12
P_SLOT   equ PARAMS + 0x14
P_PREV   equ PARAMS + 0x18

stage2:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x4FF0
    sti
    cld

    cmp byte [P_PREV], 0
    je .noprev
    mov si, msg_prev
    call print
.noprev:
    mov si, msg_load
    call print

    call enable_a20
    call check_lm
    call read_e820
    call load_kernel
    call setup_vbe
    jmp enter_long

; ---------------------------------------------------------------
enable_a20:
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    test al, 2
    jnz .ok
    or al, 2
    and al, 0xFE
    out 0x92, al
.ok:
    ret

check_lm:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no
    ret
.no:
    mov si, msg_nolm
    jmp fatal

read_e820:
    xor ebx, ebx
    mov di, E820BUF
    xor bp, bp
.next:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    mov dword [di + 20], 1
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    jcxz .skip
    inc bp
    add di, 24
    cmp bp, 32
    jae .done
.skip:
    test ebx, ebx
    jnz .next
.done:
    mov [BOOTINFO + 0x28], bp
    mov word [BOOTINFO + 0x2A], 0
    ret

; ---------------------------------------------------------------
; unreal mode: DS/ES z limitem 4 GB
unreal:
    cli
    push ds
    push es
    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short $+2
    mov bx, 0x10
    mov ds, bx
    mov es, bx
    and al, 0xFE
    mov cr0, eax
    pop es
    pop ds
    sti
    ret

load_kernel:
    mov eax, [P_LBA]
    mov [cur_lba], eax
    mov eax, [P_SECT]
    mov [left], eax
    mov dword [dest], KERNEL
    ; ile sektorow na 32 KB
    xor edx, edx
    mov eax, 32768
    movzx ecx, word [P_SSIZE]
    div ecx
    mov [chunk], ax
.loop:
    mov eax, [left]
    test eax, eax
    jz .done
    movzx ecx, word [chunk]
    cmp eax, ecx
    jbe .n
    mov eax, ecx
.n:
    mov [dap_cnt], ax
    mov [cnt], ax
    mov eax, [cur_lba]
    mov [dap_lba], eax
    mov word [dap_off], 0
    mov word [dap_seg], BUFSEG
    mov byte [tries], 4
.retry:
    mov si, dap
    mov ah, 0x42
    mov dl, [P_DRIVE]
    int 0x13
    jnc .read_ok
    dec byte [tries]
    jz .err
    xor ah, ah
    mov dl, [P_DRIVE]
    int 0x13
    jmp .retry
.read_ok:
    call unreal
    movzx eax, word [cnt]
    movzx ecx, word [P_SSIZE]
    mul ecx
    mov ecx, eax
    push ecx
    shr ecx, 2
    mov esi, 0x10000
    mov edi, [dest]
    a32 rep movsd
    pop ecx
    add [dest], ecx
    movzx eax, word [cnt]
    add [cur_lba], eax
    sub [left], eax
    mov al, '.'
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp .loop
.done:
    ret
.err:
    mov si, msg_disk
    jmp fatal

; ---------------------------------------------------------------
setup_vbe:
    mov dword [VBEINFO], 'VBE2'
    mov ax, 0x4F00
    mov di, VBEINFO
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov word [best_mode], 0xFFFF
    mov word [fb1_mode], 0xFFFF
    mov word [fb2_mode], 0xFFFF
    mov word [any_mode], 0xFFFF
    mov si, [VBEINFO + 14]
    mov ax, [VBEINFO + 16]
    mov fs, ax
.next:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xFFFF
    je .chosen
    push si
    push cx
    mov ax, 0x4F01
    mov di, MODEINFO
    int 0x10
    pop cx
    pop si
    cmp ax, 0x004F
    jne .next
    mov ax, [MODEINFO]
    and ax, 0x99
    cmp ax, 0x99            ; obslugiwany, kolor, grafika, LFB
    jne .next
    cmp byte [MODEINFO + 25], 32
    jne .next
    cmp byte [MODEINFO + 27], 6
    jne .next
    mov [any_mode], cx
    mov ax, [MODEINFO + 18]
    mov bx, [MODEINFO + 20]
    cmp ax, [P_PREFW]
    jne .f1
    cmp bx, [P_PREFH]
    jne .f1
    mov [best_mode], cx
    jmp .next
.f1:
    cmp ax, 1024
    jne .f2
    cmp bx, 768
    jne .f2
    mov [fb1_mode], cx
    jmp .next
.f2:
    cmp ax, 800
    jne .next
    cmp bx, 600
    jne .next
    mov [fb2_mode], cx
    jmp .next
.chosen:
    mov cx, [best_mode]
    cmp cx, 0xFFFF
    jne .set
    mov cx, [fb1_mode]
    cmp cx, 0xFFFF
    jne .set
    mov cx, [fb2_mode]
    cmp cx, 0xFFFF
    jne .set
    mov cx, [any_mode]
    cmp cx, 0xFFFF
    je .fail
.set:
    push cx
    mov ax, 0x4F01
    mov di, MODEINFO
    int 0x10
    pop bx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov eax, [MODEINFO + 40]
    mov [BOOTINFO + 0x0C], eax
    movzx eax, word [MODEINFO + 16]
    cmp word [VBEINFO + 4], 0x0300
    jb .oldpitch
    movzx eax, word [MODEINFO + 50]
.oldpitch:
    mov [BOOTINFO + 0x10], eax
    movzx eax, word [MODEINFO + 18]
    mov [BOOTINFO + 0x14], eax
    movzx eax, word [MODEINFO + 20]
    mov [BOOTINFO + 0x18], eax
    movzx eax, byte [MODEINFO + 25]
    mov [BOOTINFO + 0x1C], eax
    ret
.fail:
    mov si, msg_vbe
    jmp fatal

; ---------------------------------------------------------------
enter_long:
    mov dword [BOOTINFO + 0x00], 'SRTX'
    movzx eax, byte [P_TYPE]
    mov [BOOTINFO + 0x04], eax
    movzx eax, byte [P_DRIVE]
    mov [BOOTINFO + 0x08], eax
    mov eax, [P_KSIZE]
    mov [BOOTINFO + 0x20], eax
    mov eax, [P_SLOT]
    mov [BOOTINFO + 0x24], eax
    mov eax, [P_LBA]
    mov [BOOTINFO + 0x2C], eax
    movzx eax, byte [P_PREV]
    mov [BOOTINFO + 0x30], eax

    cli
    ; maska PIC
    mov al, 0xFF
    out 0xA1, al
    out 0x21, al

    ; tablice stron: identycznosc 0..4 GB stronami 2 MB
    mov ax, PML4 >> 4
    mov es, ax
    xor di, di
    xor eax, eax
    mov ecx, 0x6000 / 4
    rep stosd
    mov dword [es:0x0000], 0x71003
    mov dword [es:0x1000], 0x72003
    mov dword [es:0x1008], 0x73003
    mov dword [es:0x1010], 0x74003
    mov dword [es:0x1018], 0x75003
    mov di, 0x2000
    mov eax, 0x83
    mov cx, 2048
.pd:
    mov [es:di], eax
    add eax, 0x200000
    add di, 8
    loop .pd
    xor ax, ax
    mov es, ax

    lgdt [gdt_desc]
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax
    mov eax, PML4
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    jmp 0x08:long_entry

bits 64
long_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x90000
    mov rdi, BOOTINFO
    mov rax, KERNEL
    jmp rax

bits 16
; ---------------------------------------------------------------
fatal:
    call print
.h:
    hlt
    jmp .h

print:
    lodsb
    test al, al
    jz .d
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp print
.d:
    ret

align 8
gdt:
    dq 0
    dq 0x00AF9A000000FFFF   ; 0x08 kod 64-bit
    dq 0x00CF92000000FFFF   ; 0x10 dane
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

align 4
dap:
    db 16, 0
dap_cnt: dw 0
dap_off: dw 0
dap_seg: dw 0
dap_lba: dq 0

cur_lba:  dd 0
left:     dd 0
dest:     dd 0
chunk:    dw 0
cnt:      dw 0
tries:    db 0
best_mode: dw 0
fb1_mode:  dw 0
fb2_mode:  dw 0
any_mode:  dw 0

msg_load: db "Ladowanie Sartax OS", 0
msg_prev: db "Uruchamiam poprzednia wersje systemu", 13, 10, 0
msg_nolm: db 13, 10, "Sartax OS wymaga procesora 64-bit (w VirtualBox: typ Other/Unknown (64-bit))", 0
msg_disk: db 13, 10, "Blad odczytu jadra z dysku", 0
msg_vbe:  db 13, 10, "Brak trybu graficznego VBE 32-bit", 0

times 16384-($-$$) db 0
