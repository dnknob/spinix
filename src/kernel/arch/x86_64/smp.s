.intel_syntax noprefix

.section .text

.global smp_ap_entry_asm
.type   smp_ap_entry_asm, @function
smp_ap_entry_asm:
    /* Paranoia: ensure IF is clear (Limine should have done this already). */
    cli

    mov  rsp, [rdi + 24]        /* rsp = cpu->stack_top (from extra_argument) */
    and  rsp, -16               /* guarantee 16-byte alignment (ABI requirement) */

    xor  rbp, rbp

    call smp_ap_init_c

.ap_entry_hang:
    cli
    hlt
    jmp  .ap_entry_hang

.size smp_ap_entry_asm, . - smp_ap_entry_asm

.global smp_ap_idle
.type   smp_ap_idle, @function
smp_ap_idle:
    sti                         /* enable interrupts so timers can preempt  */
.idle_hlt:
    hlt                         /* sleep until next interrupt               */
    jmp  .idle_hlt              /* spurious wakeup? go back to sleep        */

.size smp_ap_idle, . - smp_ap_idle

.global smp_read_lapic_id
.type   smp_read_lapic_id, @function
smp_read_lapic_id:
    push rbx                    /* rbx is callee-saved                      */
    mov  eax, 1
    cpuid                       /* leaf 1: ebx[31:24] = initial APIC ID     */
    shr  ebx, 24
    mov  eax, ebx               /* return value in eax (zero-extended→rax)  */
    pop  rbx
    ret

.size smp_read_lapic_id, . - smp_read_lapic_id

.global smp_pause
.type   smp_pause, @function
smp_pause:
    pause
    ret

.size smp_pause, . - smp_pause


.att_syntax prefix