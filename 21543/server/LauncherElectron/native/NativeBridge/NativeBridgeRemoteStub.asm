; Position-independent x64 initializer copied into the authorized GameApp
; process. All addresses are supplied through RemoteHookInitContext.

OPTION CASEMAP:NONE

PUBLIC RemoteHookInitializerStart
PUBLIC RemoteHookInitializerEnd

.code

RemoteHookInitializerStart PROC
    push rbx
    push rsi
    push rdi
    sub  rsp, 20h
    mov  rbx, rcx

    ; Temporarily extend this initializer thread's loader TLS vector. Copy all
    ; loader-owned entries, append the mapped image template, then switch the
    ; TEB pointer for TLS callbacks and DllMain only.
    mov  rdx, qword ptr [rbx + 8]
    test rdx, rdx
    je   tls_done
    mov  rdi, qword ptr gs:[58h]
    mov  ecx, dword ptr [rbx + 36]
    xor  eax, eax

tls_copy_loop:
    cmp  eax, ecx
    jae  tls_append
    mov  r8, qword ptr [rdi + rax * 8]
    mov  qword ptr [rdx + rax * 8], r8
    inc  eax
    jmp  tls_copy_loop

tls_append:
    mov  r8, qword ptr [rbx + 40]
    test r8, r8
    jz   init_failed
    mov  qword ptr [rdx + rcx * 8], r8
    mov  qword ptr gs:[58h], rdx
    mov  dword ptr [rbx + 64], 1

tls_done:
    ; Register x64 unwind metadata before TLS callbacks or DllMain execute.
    cmp  dword ptr [rbx + 32], 0
    je   unwind_done
    mov  rcx, qword ptr [rbx + 24]
    mov  edx, dword ptr [rbx + 32]
    mov  r8,  qword ptr [rbx]
    mov  rax, qword ptr [rbx + 48]
    test rax, rax
    jz   init_failed
    call rax
    test eax, eax
    jz   init_failed
    mov  dword ptr [rbx + 64], 2

unwind_done:
    ; Invoke the null-terminated TLS callback array on the same thread.
    mov  rsi, qword ptr [rbx + 16]
    test rsi, rsi
    jz   call_entry

callback_loop:
    mov  rax, qword ptr [rsi]
    add  rsi, 8
    test rax, rax
    jz   call_entry
    mov  rcx, qword ptr [rbx]
    mov  edx, 1
    xor  r8d, r8d
    call rax
    jmp  callback_loop

call_entry:
    mov  dword ptr [rbx + 64], 3
    mov  rcx, qword ptr [rbx]
    mov  edx, 1
    xor  r8d, r8d
    mov  rax, qword ptr [rbx + 56]
    test rax, rax
    jz   init_failed
    call rax
    mov  dword ptr [rbx + 68], eax
    test eax, eax
    jz   init_entry_done
    mov  dword ptr [rbx + 64], 4

init_entry_done:
    jz   init_failed
    mov  eax, 4D4D0001h
    jmp  init_finish

init_failed:
    xor  eax, eax

init_finish:
    ; The mapped image is not registered with the OS loader. Restore the
    ; original vector before this temporary initializer thread exits.
    mov  rcx, qword ptr [rbx + 8]
    test rcx, rcx
    jz   tls_restore_done
    mov  qword ptr gs:[58h], rdi

tls_restore_done:
    add  rsp, 20h
    pop  rdi
    pop  rsi
    pop  rbx
    ret
RemoteHookInitializerStart ENDP

RemoteHookInitializerEnd LABEL BYTE

END
