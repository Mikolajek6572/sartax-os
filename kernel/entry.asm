; Sartax OS - wejscie jadra (64-bit), naglowek obrazu, przerwania
bits 64

extern kmain
extern isr_dispatch
extern _image_end
extern _bss_start
extern _bss_end
extern _kernel_end

global _start
global isr_table
global pristine_image
global gdt_load

section .text.boot
_start:
    jmp real_start
align 8
; naglowek obrazu jadra (offset 8)
    db 'SXKN'
    dd 1                          ; wersja naglowka
    dd _image_end - 0x100000      ; rozmiar obrazu
    dd 0
image_version:
    db 'VERSION_STRING__'         ; podmieniane przez build (16 B)

real_start:
    cli
    mov rsp, stack_top
    mov r12, rdi

    ; nietknieta kopia obrazu (instalator zapisuje ja na dysk)
    mov rsi, 0x100000
    mov rdi, _kernel_end
    add rdi, 0xFFF
    and rdi, ~0xFFF
    mov [pristine_image], rdi
    mov rcx, _image_end
    sub rcx, 0x100000
    cld
    rep movsb

    ; zerowanie BSS (pristine_image jest w .data)
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; wlasne GDT
    lgdt [gdt_desc]
    push 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; SSE (BearSSL i gcc tego nie uzywaja, ale niech bedzie wlaczone)
    mov rax, cr0
    and ax, 0xFFFB
    or ax, 0x2
    mov cr0, rax
    mov rax, cr4
    or ax, (3 << 9)
    mov cr4, rax

    mov rdi, r12
    call kmain
.halt:
    cli
    hlt
    jmp .halt

gdt_load:
    ret

; ---------------------------------------------------------------
section .text
%assign i 0
%rep 48
isr %+ i:
%if i == 8 || i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 17 || i == 21 || i == 29 || i == 30
%else
    push 0
%endif
    push i
    jmp isr_common
%assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    cld
    call isr_dispatch
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

section .data
align 8
pristine_image: dq 0
isr_table:
%assign i 0
%rep 48
    dq isr %+ i
%assign i i+1
%endrep

align 16
gdt:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dq gdt

section .bss
align 16
stack_bottom:
    resb 256 * 1024
stack_top:
