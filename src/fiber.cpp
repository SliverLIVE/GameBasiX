// GameBasiX - Fiber-Implementierung
#include "fiber.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten/fiber.h>

#include <vector>
#else
#include <ucontext.h>

#include <cstdlib>
#include <vector>
#endif

namespace gbxfiber {

#if defined(_WIN32)

namespace {
Handle g_main = nullptr;
}

bool available() { return true; }

Handle mainHandle() {
    if (g_main) return g_main;
    if (IsThreadAFiber()) {
        g_main = GetCurrentFiber();
    } else {
        g_main = ConvertThreadToFiber(nullptr);
    }
    return g_main;
}

Handle create(void (*entry)(void*), void* arg, std::size_t stackBytes) {
    if (!entry) return nullptr;
    return CreateFiber(stackBytes, entry, arg);
}

void destroy(Handle fiber) {
    if (fiber && fiber != mainHandle()) DeleteFiber(fiber);
}

void switchTo(Handle fiber) {
    if (!fiber) return;
    // Der Thread muss vor dem ersten Wechsel eine Fiber sein.
    mainHandle();
    SwitchToFiber(fiber);
}

#elif !defined(__EMSCRIPTEN__)

namespace {

struct Fiber {
    ucontext_t ctx{};
    std::vector<char> stack;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    bool started = false;
};

ucontext_t g_scheduler{};
Fiber* g_starting = nullptr;  // nur fuer den allerersten Sprung in eine Fiber
bool g_schedulerReady = false;

void trampoline() {
    Fiber* fiber = g_starting;
    g_starting = nullptr;
    if (!fiber || !fiber->entry) return;
    fiber->entry(fiber->arg);
    // Der Task-Code kehrt nie zurueck; falls doch, darf dieser Thread nicht
    // einfach weiterlaufen.
    std::abort();
}

}  // namespace

bool available() { return true; }

Handle mainHandle() {
    if (!g_schedulerReady) {
        getcontext(&g_scheduler);
        g_schedulerReady = true;
    }
    return &g_scheduler;
}

Handle create(void (*entry)(void*), void* arg, std::size_t stackBytes) {
    if (!entry) return nullptr;
    Fiber* fiber = new Fiber();
    fiber->stack.resize(stackBytes ? stackBytes : 1024 * 1024);
    fiber->entry = entry;
    fiber->arg = arg;
    getcontext(&fiber->ctx);
    fiber->ctx.uc_stack.ss_sp = fiber->stack.data();
    fiber->ctx.uc_stack.ss_size = fiber->stack.size();
    fiber->ctx.uc_link = nullptr;
    makecontext(&fiber->ctx, trampoline, 0);
    return fiber;
}

void destroy(Handle fiber) {
    delete static_cast<Fiber*>(fiber);
}

void switchTo(Handle handle) {
    Fiber* fiber = static_cast<Fiber*>(handle);
    if (!fiber) return;
    if (!fiber->started) {
        fiber->started = true;
        g_starting = fiber;
    }
    swapcontext(&g_scheduler, &fiber->ctx);
}

#else  // __EMSCRIPTEN__

namespace {

struct Fiber {
    emscripten_fiber_t ctx{};
    std::vector<char> stack;
    std::vector<char> asyncifyStack;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

Fiber g_main;
bool g_mainReady = false;
Fiber* g_current = nullptr;

// Asyncify legt beim Verlassen einer Fiber den C-Stack dort ab. Der
// Interpreter ist tief verschachtelt, deshalb braucht jede Fiber genug Platz.
constexpr std::size_t kAsyncifyStackBytes = 1024 * 1024;

Fiber* mainFiber() {
    if (!g_mainReady) {
        g_main.asyncifyStack.resize(kAsyncifyStackBytes);
        emscripten_fiber_init_from_current_context(
            &g_main.ctx, g_main.asyncifyStack.data(), g_main.asyncifyStack.size());
        g_mainReady = true;
        g_current = &g_main;
    }
    return &g_main;
}

}  // namespace

bool available() { return true; }

Handle mainHandle() { return mainFiber(); }

Handle create(void (*entry)(void*), void* arg, std::size_t stackBytes) {
    if (!entry) return nullptr;
    Fiber* fiber = new Fiber();
    fiber->stack.resize(stackBytes ? stackBytes : 1024 * 1024);
    fiber->asyncifyStack.resize(kAsyncifyStackBytes);
    fiber->entry = entry;
    fiber->arg = arg;
    emscripten_fiber_init(&fiber->ctx, entry, arg, fiber->stack.data(), fiber->stack.size(),
                          fiber->asyncifyStack.data(), fiber->asyncifyStack.size());
    return fiber;
}

void destroy(Handle handle) {
    Fiber* fiber = static_cast<Fiber*>(handle);
    if (fiber && fiber != &g_main) delete fiber;
}

void switchTo(Handle handle) {
    Fiber* target = static_cast<Fiber*>(handle);
    if (!target) return;
    Fiber* current = g_current ? g_current : mainFiber();
    if (current == target) return;
    g_current = target;
    emscripten_fiber_swap(&current->ctx, &target->ctx);
    g_current = current;
}

#endif

}  // namespace gbxfiber
