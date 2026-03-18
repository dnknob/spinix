.section .text
    .global _start
    .type   _start, @function

_start:
    # Null outermost frame for stack unwinders
    xorq    %rbp, %rbp

    # Save argc and argv in callee-saved registers
    popq    %r12                    # r12 = argc
    movq    %rsp, %r13              # r13 = argv

    # Align stack to 16 bytes (SysV AMD64 ABI)
    andq    $-16, %rsp

    # Call main(argc, argv)
    movq    %r12, %rdi
    movq    %r13, %rsi
    call    main

    # Exit with main's return value
    movl    %eax, %edi              # status
    movq    $60,  %rax              # sys_exit
    syscall

    ud2

    .size _start, . - _start