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

    mov rax, [rip + current_task]   /* rax = current_task pointer */
    test rax, rax
    jz .Lno_save                    /* skip save if current_task is NULL (first switch) */
    mov [rax], rsp                  /* current_task->rsp = rsp */

.Lno_save:
    mov [rip + current_task], rdi

    mov rsp, [rdi]                  /* rsp = next_task->rsp */

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

.size switch_to_task_asm, . - switch_to_task_asm