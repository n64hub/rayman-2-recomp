// On Windows, prints where the program crashed to stderr: the exception and each frame of the crashing thread's stack,
// named from the .pdb beside the exe. Recompiled functions carry the game's names (syms/), so a trace points at the
// game code that crashed; abort(), std::terminate and the CRT's checks print theirs too. --stacks <seconds> (development) prints every thread's stack that long after start, so a hang
// shows where each thread waits or spins.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>

static void print_stack(HANDLE process, HANDLE thread, CONTEXT context) {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (int i = 0; i < 48; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0) {
            break;
        }
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            fprintf(stderr, "  %2d %s+0x%llX", i, symbol->Name, (unsigned long long)displacement);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
                fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
            }
            fprintf(stderr, "\n");
        } else {
            fprintf(stderr, "  %2d %p\n", i, (void*)frame.AddrPC.Offset);
        }
    }
}

static LONG WINAPI print_crash(EXCEPTION_POINTERS* info) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    fprintf(stderr, "Crash: exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode,
        info->ExceptionRecord->ExceptionAddress);
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        fprintf(stderr, " (%s 0x%llX)", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");

    print_stack(process, GetCurrentThread(), *info->ContextRecord);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}


// abort(), std::terminate and the CRT's invalid parameter check end the program by __fastfail, which no exception
// filter sees, so each prints the stack it was called on first.
static void print_here(const char* what) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    fprintf(stderr, "Crash: %s\n", what);
    CONTEXT context{};
    RtlCaptureContext(&context);
    print_stack(process, GetCurrentThread(), context);
    fflush(stderr);
}

void install_crash_trace() {
    SetUnhandledExceptionFilter(print_crash);
    signal(SIGABRT, [](int) { print_here("abort()"); });
    std::set_terminate([] { print_here("std::terminate"); std::abort(); });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
        print_here("invalid parameter to a CRT function");
        std::abort();
    });
}

void print_stacks_after(double seconds) {
    std::thread([seconds] {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(process, nullptr, TRUE);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) {
                continue;
            }
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                entry.th32ThreadID);
            if (thread == nullptr) {
                continue;
            }
            SuspendThread(thread);
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(thread, &context)) {
                fprintf(stderr, "Thread %lu:\n", entry.th32ThreadID);
                print_stack(process, thread, context);
            }
            ResumeThread(thread);
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
        fflush(stderr);
    }).detach();
}
#else
void install_crash_trace() {}
void print_stacks_after(double) {}
#endif
