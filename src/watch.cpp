// [Debug] Watch=<hex address>: a hardware data breakpoint (DR0, 4-byte write)
// on the main thread. Every write is logged with the instruction that made it
// and its caller, up to 20 times. For "who sets this flag?" questions that
// static analysis can't answer (writes through a this pointer).
#include "watch.h"

#include <windows.h>

namespace watch {
namespace {

void (*g_log)(const char*, ...);
uintptr_t g_addr;
HANDLE g_main;
volatile LONG g_hits;

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    if (InterlockedIncrement(&g_hits) <= 20) {
        DWORD ret = 0;
        // The first return address on the stack (the caller of this function).
        for (int i = 0; i < 64; ++i) {
            DWORD v = ((DWORD*)c->Esp)[i];
            if (v >= 0x401000 && v < 0x6f0000) {
                ret = v;
                break;
            }
        }
        g_log("watch: [%#x] = %#lx, written just before %#lx (caller near %#lx)", (unsigned)g_addr,
              *(unsigned long*)g_addr, (unsigned long)c->Eip, (unsigned long)ret);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI Arm(LPVOID) {
    Sleep(200);  // let DllMain return first
    if (SuspendThread(g_main) == (DWORD)-1) return 0;
    CONTEXT c = {};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(g_main, &c);
    c.Dr0 = g_addr;
    c.Dr7 = (c.Dr7 & ~0xF0003ul) | 1 | (1ul << 16) | (3ul << 18);  // L0, write, 4 bytes
    BOOL ok = SetThreadContext(g_main, &c);
    ResumeThread(g_main);
    g_log("watch: data breakpoint on %#x %s", (unsigned)g_addr, ok ? "armed" : "FAILED");
    return 0;
}

}  // namespace

void Install(uintptr_t addr, void (*log)(const char*, ...)) {
    g_log = log;
    g_addr = addr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, 0);
    AddVectoredExceptionHandler(1, Handler);
    CloseHandle(CreateThread(nullptr, 0, Arm, nullptr, 0, nullptr));
}

}  // namespace watch
