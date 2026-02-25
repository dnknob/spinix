.intel_syntax noprefix

.global fpu_enable
fpu_enable:
    mov rax, cr0
    and rax, ~(1 << 2)      # Clear EM (bit 2) - allows FPU instructions
    or rax, (1 << 1)        # Set MP (bit 1) - monitor coprocessor
    or rax, (1 << 5)        # Set NE (bit 5) - native exception handling
    mov cr0, rax

    mov rax, cr4
    or rax, (1 << 9)        # Set OSFXSR (bit 9) - enable SSE
    or rax, (1 << 10)       # Set OSXMMEXCPT (bit 10) - enable #XF exception
    mov cr4, rax

    fninit
    
    ret
