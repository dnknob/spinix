.intel_syntax noprefix

.section .text

.global mmu_load_cr3
.type mmu_load_cr3, @function
mmu_load_cr3:
    mov cr3, rdi
    ret

.global mmu_get_cr3
.type mmu_get_cr3, @function
mmu_get_cr3:
    mov rax, cr3
    ret

.global mmu_invlpg
.type mmu_invlpg, @function
mmu_invlpg:
    invlpg [rdi]
    ret

.global mmu_flush_tlb
.type mmu_flush_tlb, @function
mmu_flush_tlb:
    mov rax, cr3
    mov cr3, rax
    ret
