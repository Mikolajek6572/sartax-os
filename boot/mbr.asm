; Sartax OS - MBR (sektor 0 dysku twardego)
; Laduje sektor konfiguracji (LBA 1) i stage2 (LBA 2..33) i skacze do stage2
bits 16
org 0x7C00

CFG     equ 0x8E00          ; sektor konfiguracji
STAGE2  equ 0x9000
PARAMS  equ 0x5800

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [drive], dl

    mov si, msg_boot
    call print

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc fail

    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc fail

    cmp dword [CFG], 'SXCF'
    jne fail

    ; wybor slotu jadra (Shift = poprzednia wersja)
    mov eax, [CFG + 12]
    and eax, 1
    mov byte [PARAMS + 0x18], 0
    test byte [0x417], 3
    jz .slot_ok
    xor eax, 1
    mov ebx, eax
    imul ebx, 12
    cmp dword [CFG + 16 + 8 + bx], 0     ; czy slot ma jadro
    jne .shift_ok
    xor eax, 1
    jmp .slot_ok
.shift_ok:
    mov byte [PARAMS + 0x18], 1
.slot_ok:
    mov [PARAMS + 0x14], eax
    mov ebx, eax
    imul ebx, 12
    mov eax, [CFG + 16 + bx]
    mov [PARAMS + 0x00], eax              ; kernel_lba
    mov eax, [CFG + 16 + 4 + bx]
    mov [PARAMS + 0x04], eax              ; kernel_sectors
    mov eax, [CFG + 16 + 8 + bx]
    mov [PARAMS + 0x0C], eax              ; kernel_size
    test eax, eax
    jz fail
    mov word [PARAMS + 0x08], 512
    mov al, [drive]
    mov [PARAMS + 0x0A], al
    mov byte [PARAMS + 0x0B], 2           ; boot z dysku
    mov eax, [CFG + 8]
    mov [PARAMS + 0x10], eax              ; preferowana rozdzielczosc

    mov dl, [drive]
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

drive:    db 0
msg_boot: db "Sartax OS", 13, 10, 0
msg_fail: db "Blad rozruchu - brak systemu na dysku", 13, 10, 0

align 4
dap:
    db 16, 0
    dw 33               ; sektory: konfiguracja + stage2
    dw CFG, 0
    dq 1

times 0x1B0-($-$$) db 0
    db 'SRTX'           ; znacznik Sartax
times 446-($-$$) db 0
times 64 db 0           ; tablica partycji (wypelnia instalator)
dw 0xAA55
