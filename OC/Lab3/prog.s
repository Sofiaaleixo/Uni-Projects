    .data
A:  .word 1,2,3,4,5,6,7,8,9
    .word 10,11,12,13,14,15,16
B:  .word 11,22,33,44,55,66,77
    .word 88,99,111,122,133
    .word 144,155,166
C:  .word 0,0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0

    .text
main:
    addi s1, zero, 0   # i = 0
    addi s2, zero, 16  # value of N
    la s3, A           # base of A
    la s4, B           # base of B
    la s5, C           # base of C
    
loop: 
    lw t1, 0(s3)
    lw t2, 0(s4)
    addi s1, s1, 1
    mul t3, t2, t1
    add t3, t3, t1
    sw t3, 0(s5)
    addi s3, s3, 4
    addi s4, s4, 4
    addi s5, s5, 4
    bne s1, s2, loop
    
end:
    addi a7, zero, 10
    ecall             # Exit (syscall)