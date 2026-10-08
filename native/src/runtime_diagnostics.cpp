#include "runtime_diagnostics.h"
#include <QFile>
#include <QVariantMap>
#include <QtGlobal>

#if defined(Q_OS_DARWIN)
#include <mach/mach.h>
#endif
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace podlord {
QVariantList runtimeDiagnostics() {
    const auto mb = [](quint64 bytes) { return QString::number(static_cast<double>(bytes) / (1024 * 1024), 'f', 1) + " MB"; };
    QString rss = "Unavailable", memory = "Unavailable", cpu = "Unavailable", threads = "Unavailable";
    QString memoryLabel = "Process memory", memoryDescription = "No supported process memory counter is available.";
#if defined(Q_OS_DARWIN)
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic), &count) == KERN_SUCCESS)
        rss = mb(basic.resident_size);
    task_vm_info_data_t vm{};
    count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS && count >= TASK_VM_INFO_REV1_COUNT)
        memory = mb(vm.phys_footprint);
    memoryLabel = "Physical footprint";
    memoryDescription = "Kernel-accounted physical footprint; not private virtual memory.";
    thread_act_array_t ports = nullptr;
    mach_msg_type_number_t portCount = 0;
    if (task_threads(mach_task_self(), &ports, &portCount) == KERN_SUCCESS) {
        threads = QString::number(portCount);
        for (mach_msg_type_number_t i = 0; i < portCount; ++i) mach_port_deallocate(mach_task_self(), ports[i]);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(ports), static_cast<vm_size_t>(portCount) * sizeof(thread_t));
    }
#elif defined(Q_OS_LINUX)
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly)) {
        for (const auto& line : status.readAll().split('\n')) {
            const auto fields = QString::fromUtf8(line).simplified().split(' ');
            if (fields.size() < 2) continue;
            bool valid = false;
            const auto value = fields[1].toULongLong(&valid);
            if (!valid) continue;
            if (fields[0] == "VmRSS:") rss = mb(value * 1024);
            else if (fields[0] == "RssAnon:") memory = mb(value * 1024);
            else if (fields[0] == "Threads:") threads = QString::number(value);
        }
    }
    memoryLabel = "Anonymous resident memory";
    memoryDescription = "Resident anonymous pages; not private virtual memory.";
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        rss = mb(counters.WorkingSetSize); memory = mb(counters.PrivateUsage);
    }
    memoryLabel = "Private memory";
    memoryDescription = "Committed private bytes reported by the operating system.";
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const auto seconds = [](FILETIME time) { return static_cast<double>((static_cast<quint64>(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 10000000; };
        cpu = QString::number(seconds(kernel) + seconds(user), 'f', 3) + " s";
    }
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) {
            unsigned total = 0;
            do { if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++total; } while (Thread32Next(snapshot, &entry));
            if (GetLastError() == ERROR_NO_MORE_FILES) threads = QString::number(total);
        }
        CloseHandle(snapshot);
    }
#endif
#if defined(Q_OS_UNIX)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        const auto seconds = [](timeval time) { return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) / 1000000; };
        cpu = QString::number(seconds(usage.ru_utime) + seconds(usage.ru_stime), 'f', 3) + " s";
    }
#endif
    return {
        QVariantMap{{"id", "rss"}, {"label", "Process RSS"}, {"value", rss}, {"description", "Resident pages, including shared mappings."}},
        QVariantMap{{"id", "memory"}, {"label", memoryLabel}, {"value", memory}, {"description", memoryDescription}},
        QVariantMap{{"id", "cpuTime"}, {"label", "Process CPU time"}, {"value", cpu}, {"description", "Cumulative user and kernel CPU time; not a utilization percentage."}},
        QVariantMap{{"id", "threads"}, {"label", "Threads"}, {"value", threads}, {"description", "Current operating-system thread count."}}
    };
}
}
