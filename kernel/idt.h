#pragma once
#include "types.h"

void idt_init(void);

// Instala um handler numa entrada arbitrária da IDT (0-255).
// Usado por isr.c para os vetores de exceção (0-31) e, futuramente,
// por outros subsistemas que precisem de vetores dedicados (ex.:
// vetor de syscall, IPI entre CPUs, etc).
void idt_set_gate(int n, void (*handler)(void));
