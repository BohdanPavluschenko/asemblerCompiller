.data
    hello db 'Hello, World!', 10  ; 10 = newline
    hello_len equ $ - hello        ; довжина рядка

.code
main PROC

    ; write(1, hello, hello_len)
    mov rcx, 1        ; file descriptor 1 = stdout
    mov rsi, offset hello    ; адреса рядка
    mov rdx, offset hello_len ; довжина рядка

    ; exit(0)
    xor rdi, rdi      ; код виходу 0
main ENDP
END
