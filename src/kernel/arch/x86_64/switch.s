.intel_syntax noprefix

.section .text

.extern current_task

.global switch_to_task_asm
.type switch_to_task_asm, @function
switch_to_task_asm:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov rax, [rip + current_task]
    test rax, rax
    jz .Lno_save
    mov [rax], rsp

.Lno_save:
    mov [rip + current_task], rdi
    mov rsp, [rdi]

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

.global enter_userspace
.type   enter_userspace, @function
enter_userspace:
    cli

    push 0x1B       /* SS  = GDT_USER_DATA | 3 */
    push rsi        /* RSP = user stack         */
    push rdx        /* RFLAGS                   */
    push 0x23       /* CS  = GDT_USER_CODE | 3  */
    push rdi        /* RIP = user entry point   */

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8,  r8
    xor r9,  r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    iretq

.att_syntax prefix