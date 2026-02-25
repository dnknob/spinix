.intel_syntax noprefix

.section .text

.global idt_flush
.type idt_flush, @function
idt_flush:
    lidt [rdi]
    ret

.macro ISR_NOERRCODE num
.global isr\num
.type isr\num, @function
isr\num:
    push 0
    push \num
    jmp interrupt_common
.endm

.macro ISR_ERRCODE num
.global isr\num
.type isr\num, @function
isr\num:
    push \num
    jmp interrupt_common
.endm

.macro IRQ num irq_num
.global irq\num
.type irq\num, @function
irq\num:
    push 0
    push \irq_num
    jmp interrupt_common
.endm

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE   21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

.extern interrupt_handler
.type interrupt_common, @function
interrupt_common:
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

    mov ax, ds
    push rax
    mov ax, es
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax

    mov rax, [rsp + 176]    /* CS is at +176, not +168 */
    and rax, 3
    cmp rax, 3
    jne 1f
    /* swapgs */

1:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rdi, rsp
    mov r15, rsp            /* save RSP in callee-saved r15, NOT rdi */
    and rsp, ~0xF

    cld
    call interrupt_handler

    mov rsp, r15            /* restore from r15 (preserved by C ABI) */

    mov rax, [rsp + 176]    /* CS offset fix here too */
    and rax, 3
    cmp rax, 3
    jne 2f
    /* swapgs */

2:
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax

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

.att_syntax prefix
