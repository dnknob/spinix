.intel_syntax noprefix

.section .data

.global syscall_kernel_rsp
.global syscall_user_rip
.global syscall_user_rsp
.global syscall_user_rflags

syscall_kernel_rsp:
    .quad 0

syscall_user_rip:
    .quad 0

syscall_user_rsp:
    .quad 0

syscall_user_rflags:
    .quad 0

.section .text
.global syscall_entry
.type   syscall_entry, @function

syscall_entry:
    mov [rip + syscall_user_rip],    rcx    /* save for sys_fork */
    mov [rip + syscall_user_rflags], r11    /* save for sys_fork */
    mov [rip + syscall_user_rsp],    rsp    /* save for sys_fork */

    mov r11, rsp
    mov rsp, [rip + syscall_kernel_rsp]     /* switch to THIS task's kernel stack */

    push r11
    mov  r11, [rip + syscall_user_rflags]   /* restore r11 = user RFLAGS */
    push r11        /* user RFLAGS  */
    push rcx        /* user RIP     */
    push rax        /* syscall num  */
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9         /* a6 candidate */
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    sti

    mov rax, [rsp + 48]     /* a6 = r9 */
    push rax                /* rsp shifts -8; all offsets below are +8 */

    mov rdi, [rsp + 104]    /* num = rax   */
    mov rsi, [rsp +  96]    /* a1  = rdi   */
    mov rdx, [rsp +  88]    /* a2  = rsi   */
    mov rcx, [rsp +  80]    /* a3  = rdx   */
    mov r8,  [rsp +  72]    /* a4  = r10   */
    mov r9,  [rsp +  64]    /* a5  = r8    */
    call syscall_dispatch
    add rsp, 8              /* remove a6   */

    mov [rsp + 96], rax 

    cli

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rax         /* return value               */
    pop rcx         /* user RIP  → rcx for sysretq */
    pop r11         /* user RFLAGS → r11 for sysretq */
    pop rsp

    sysretq

.att_syntax prefix