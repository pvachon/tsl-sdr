%define CRS_OFFS_RBX                0
%define CRS_OFFS_RSP                8
%define CRS_OFFS_RBP                16
%define CRS_OFFS_R12                24
%define CRS_OFFS_R13                32
%define CRS_OFFS_R14                40
%define CRS_OFFS_R15                48
%define CRS_OFFS_X87_CW             56
%define CRS_OFFS_MXCSR              60

%define CRS_OFFS_RSI                64
%define CRS_OFFS_RDI                72
%define CRS_OFFS_RDX                80
%define CRS_OFFS_RCX                88
%define CRS_OFFS_R8                 96
%define CRS_OFFS_R9                 104

    BITS    64
    CPU     Haswell

    ; void coro_plat_ctx_swap(struct coro_reg_state *tgt, struct coro_reg_state *cur)
    ;   This function is virtually invisible to the caller - it does not touch the
    ;   stack frame. It's as if it never existed...
    GLOBAL coro_plat_ctx_swap
    SECTION .text
coro_plat_ctx_swap:

    ; rdi - target context to be activated
    ; rsi - current context, to be saved

    ; Save caller context so we can unwrap during the next switch
    mov [rsi + CRS_OFFS_RBX], rbx
    mov [rsi + CRS_OFFS_RSP], rsp
    mov [rsi + CRS_OFFS_RBP], rbp
    mov [rsi + CRS_OFFS_R12], r12
    mov [rsi + CRS_OFFS_R13], r13
    mov [rsi + CRS_OFFS_R14], r14
    mov [rsi + CRS_OFFS_R15], r15

    ; Store the x87 control word
    fstcw [rsi + CRS_OFFS_X87_CW]

    ; Store the MMX status register
    stmxcsr [rsi + CRS_OFFS_MXCSR]

    ; Unwind activation context
    mov rbx, [rdi + CRS_OFFS_RBX]
    mov rsp, [rdi + CRS_OFFS_RSP]
    mov rbp, [rdi + CRS_OFFS_RBP]
    mov r12, [rdi + CRS_OFFS_R12]
    mov r13, [rdi + CRS_OFFS_R13]
    mov r14, [rdi + CRS_OFFS_R14]
    mov r15, [rdi + CRS_OFFS_R15]

    ; Load the x87 control word
    fldcw [rdi + CRS_OFFS_X87_CW]

    ; Empty Multimedia State (lol Intel)
    emms

    ; Load the MMX control word
    ldmxcsr [rdi + CRS_OFFS_MXCSR]

    ; We're now in the stack context of the caller, so we can simply
    ; return
    retn

    ; void coro_plat_ctx_swap_start(struct coro_reg_state *tgt, struct coro_reg_state *cur)
    ;   Start the given coroutine, passing the arguments specified in the context to the called
    ;   function.
    GLOBAL coro_plat_ctx_swap_start
    SECTION .text
coro_plat_ctx_swap_start:

    ; rdi - target context to be activated
    ; rsi - current context, to be saved

    ; Save caller context so we can unwrap during the next switch
    mov [rsi + CRS_OFFS_RBX], rbx
    mov [rsi + CRS_OFFS_RSP], rsp
    mov [rsi + CRS_OFFS_RBP], rbp
    mov [rsi + CRS_OFFS_R12], r12
    mov [rsi + CRS_OFFS_R13], r13
    mov [rsi + CRS_OFFS_R14], r14
    mov [rsi + CRS_OFFS_R15], r15

    ; Store the x87 control word
    fstcw [rsi + CRS_OFFS_X87_CW]

    ; Store the MMX status register
    stmxcsr [rsi + CRS_OFFS_MXCSR]

    ; Unwind activation context
    mov rbx, [rdi + CRS_OFFS_RBX]
    mov rsp, [rdi + CRS_OFFS_RSP]
    mov rbp, [rdi + CRS_OFFS_RBP]
    mov r12, [rdi + CRS_OFFS_R12]
    mov r13, [rdi + CRS_OFFS_R13]
    mov r14, [rdi + CRS_OFFS_R14]
    mov r15, [rdi + CRS_OFFS_R15]

    ; Set up the arguments
    mov rsi, [rdi + CRS_OFFS_RSI]
    mov rdx, [rdi + CRS_OFFS_RDX]
    mov rcx, [rdi + CRS_OFFS_RCX]
    mov r8, [rdi + CRS_OFFS_R8]
    mov r9, [rdi + CRS_OFFS_R9]

    ; Clobber rdi with the first arg for the callee
    mov rdi, [rdi + CRS_OFFS_RDI]

    ; Empty Multimedia State (lol Intel)
    emms

    ; We're now in the stack context of the new coroutine
    retn
