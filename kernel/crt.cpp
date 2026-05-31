// kernel/crt.cpp — Runtime C++ mínimo para ambiente bare-metal
//
// Fornece:
//    1. __cxa_pure_virtual — chamada quando um método virtual
//       puro é invocado antes da vtable estar pronta (bug).
//    2. Iteração sobre .init_array — executa os construtores
//       de objetos globais/estáticos antes de kernel_main.
//    3. __cxa_atexit stub — evita erros de linkagem (não temos
//       destruição ordenada em bare-metal).
//
//  Não depende de nenhuma biblioteca padrão.
// ============================================================

// ---- Símbolos do linker que delimitam a secção .init_array ----
// O linker.ld deve expô-los (adicionado no ficheiro atualizado).
extern "C" {
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
}

// ---- Itera sobre .init_array e chama cada construtor ----
// Esta função é chamada a partir do boot.asm antes de kernel_main.
extern "C" void cpp_init_global_ctors() {
    for (void (**ctor)() = __init_array_start;
         ctor < __init_array_end;
         ctor++) {
        if (*ctor) (*ctor)();
    }
}

// ---- Stub para métodos virtuais puros ----
// Se o código chamar um método virtual puro (bug de programação),
// entramos em halt em vez de crash não controlado.
extern "C" void __cxa_pure_virtual() {
    // Em produção aqui chamaríamos kpanic; mas kpanic depende do fb
    // que pode não estar inicializado, então simplesmente travamos.
    __asm__ volatile("cli; hlt");
    while (true) { __asm__ volatile("hlt"); }
}

// ---- Stub para __cxa_atexit ----
// Registar destrutores de saída não faz sentido num kernel bare-metal;
// o kernel nunca "sai". Simplesmente ignoramos.
extern "C" int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

// ---- Stub para __dso_handle ----
// Necessário para __cxa_atexit; o linker usa este símbolo internamente.
void* __dso_handle = nullptr;   // <-- removido 'extern'