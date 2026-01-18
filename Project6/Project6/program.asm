EXTERN ExitProcess:PROC
EXTERN GetStdHandle:PROC
EXTERN WriteFile:PROC

.data
    hello db "Hello, World!", 10
    hello_len equ $ - hello

.code
main PROC
    sub rsp, 56
    mov ecx, -11
    call GetStdHandle
    mov rcx, rax
    mov rdx, offset hello
    mov r8d, offset hello_len
    lea r9, [rsp+40]
    mov dword ptr [rsp+40], 0
    mov qword ptr [rsp+32], 0
    call WriteFile
    add rsp, 56
    xor ecx, ecx
    call ExitProcess
main ENDP
END
