.intel_syntax noprefix

.section .text

.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    lgdt [rdi]              # Load the GDT
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    pop rdi                 # Pop return address into RDI
    mov rax, 0x08           # Kernel code selector
    push rax                # Push new CS
    push rdi                # Push return address
    retfq                   # Far return to reload CS

.global tss_flush
.type tss_flush, @function
tss_flush:
    mov ax, 0x28            # TSS selector (index 5, RPL 0)
    ltr ax                  # Load Task Register
    ret

.att_syntax prefix
