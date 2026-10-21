; Sartax OS - sektor rozruchowy plyty (El Torito, bez emulacji)
; Plik obrazu rozruchowego: [cdboot 2 KB][stage2 16 KB][jadro]
bits 16
org 0x7C00

STAGE2  equ 0x9000
PARAMS  equ 0x5800
TMP     equ 0x7000

    jmp short start
    nop
times 8-($-$$) db 0
bi_pvd:     dd 0            ; boot-info-table (wypelnia xorriso)
bi_file:    dd 0
bi_len:     dd 0
bi_csum:    dd 0
times 64-($-$$) db 0
kernel_size: dd 0           ; wypelnia skrypt budowania

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [cd_drive], dl

    ; czy na dysku 0x80 jest juz zainstalowany Sartax OS?
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, 0x80
    int 0x13
    jc boot_cd
    mov dword [dap_lba], 1
    mov word [dap_cnt], 1
    mov word [dap_off], TMP
    mov dl, 0x80
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc boot_cd
    cmp dword [TMP], 'SXCF'
    jne boot_cd

    mov si, msg_installed
    call print
    ; czekaj ~5 s na klawisz
    xor ah, ah
    int 0x1A
    mov [t0], dx
.wait:
    mov ah, 1
    int 0x16
    jnz .key
    xor ah, ah
    int 0x1A
    sub dx, [t0]
    cmp dx, 91
    jb .wait
    ; brak klawisza - start z dysku
    mov dword [dap_lba], 0
    mov dl, 0x80
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc boot_cd
    jmp chain
.key:
    xor ah, ah
    int 0x16
    mov si, msg_nl
    call print

boot_cd:
    mov si, msg_cd
    call print
    ; stage2: 8 sektorow po 2048 zaraz za tym sektorem
    mov eax, [bi_file]
    inc eax
    mov [dap_lba], eax
    mov word [dap_cnt], 8
    mov word [dap_off], STAGE2
    mov dl, [cd_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc fail

    mov eax, [bi_file]
    add eax, 9
    mov [PARAMS + 0x00], eax
    mov eax, [kernel_size]
    mov [PARAMS + 0x0C], eax
    add eax, 2047
    shr eax, 11
    mov [PARAMS + 0x04], eax
    mov word [PARAMS + 0x08], 2048
    mov al, [cd_drive]
    mov [PARAMS + 0x0A], al
    mov byte [PARAMS + 0x0B], 1
    mov word [PARAMS + 0x10], 1024
    mov word [PARAMS + 0x12], 768
    mov dword [PARAMS + 0x14], 0
    mov byte [PARAMS + 0x18], 0
    mov dl, [cd_drive]
    jmp 0:STAGE2

fail:
    mov si, msg_fail
    call print
.hang:
    hlt
    jmp .hang

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp print
.done:
    ret

cd_drive: db 0
t0:       dw 0
msg_installed: db "Sartax OS jest juz zainstalowany na dysku.", 13, 10
               db "Nacisnij dowolny klawisz, aby uruchomic instalator z plyty...", 13, 10, 0
msg_nl:   db 13, 10, 0
msg_cd:   db "Sartax OS - instalator", 13, 10, 0
msg_fail: db "Blad odczytu plyty", 13, 10, 0

align 4
dap:
    db 16, 0
dap_cnt: dw 0
dap_off: dw 0
dap_seg: dw 0
dap_lba: dq 0

; kod przeniesiony poza pierwsze 512 B, bo nadpisujemy 0x7C00
times 0x400-($-$$) db 0
chain:
    cli
    mov si, TMP
    mov di, 0x7C00
    mov cx, 256
    rep movsw
    mov dl, 0x80
    sti
    jmp 0:0x7C00

times 2048-($-$$) db 0
