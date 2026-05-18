#include "ShaderWatcher.h"
#include "../Core/Log.h"
#include <string>

namespace VibeEngine {

// ---------------------------------------------------------------------------
ShaderWatcher::~ShaderWatcher()
{
    Stop();
}

// ---------------------------------------------------------------------------
void ShaderWatcher::Watch(const std::wstring& directory)
{
    if (m_Running.load()) Stop();

    m_Directory = directory;
    m_Dirty     = false;

    // Manual-reset event — signaled by Stop() to unblock the watcher thread.
    m_StopEvent = CreateEventW(nullptr, /*manual=*/TRUE, /*init=*/FALSE, nullptr);

    m_Running = true;
    m_Thread  = std::thread(&ShaderWatcher::ThreadFunc, this);
}

// ---------------------------------------------------------------------------
void ShaderWatcher::Stop()
{
    if (!m_Running.exchange(false)) return;   // already stopped

    if (m_StopEvent != INVALID_HANDLE_VALUE)
        SetEvent(m_StopEvent);               // wake thread

    if (m_Thread.joinable())
        m_Thread.join();

    if (m_StopEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(m_StopEvent);
        m_StopEvent = INVALID_HANDLE_VALUE;
    }
}

// ---------------------------------------------------------------------------
bool ShaderWatcher::PollDirty()
{
    return m_Dirty.exchange(false);
}

// ---------------------------------------------------------------------------
// Background thread — uses OVERLAPPED ReadDirectoryChangesW so we can
// simultaneously wait on the stop event without blocking indefinitely.
// ---------------------------------------------------------------------------
void ShaderWatcher::ThreadFunc()
{
    HANDLE dir = CreateFileW(
        m_Directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (dir == INVALID_HANDLE_VALUE) {
        const std::string narrow(m_Directory.begin(), m_Directory.end());
        LOG_WARN("ShaderWatcher: cannot open directory '%s' (err=%lu) — hot reload disabled.",
                 narrow.c_str(), GetLastError());
        m_Running = false;
        return;
    }

    // Buffer must be DWORD-aligned; 4 KiB is ample for a flat shader directory.
    alignas(DWORD) char buf[4096];

    OVERLAPPED ov  = {};
    ov.hEvent = CreateEventW(nullptr, /*manual=*/TRUE, /*init=*/FALSE, nullptr);

    while (m_Running.load()) {
        DWORD bytes = 0;
        ResetEvent(ov.hEvent);

        // Asynchronous: returns immediately with ERROR_IO_PENDING when the
        // directory handle has the OVERLAPPED flag.
        BOOL ok = ReadDirectoryChangesW(
            dir,
            buf, sizeof(buf),
            /*watchSubtree=*/FALSE,       // shaders are in a flat directory
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes,
            &ov,
            /*completionRoutine=*/nullptr);

        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            LOG_WARN("ShaderWatcher: ReadDirectoryChangesW failed (err=%lu).", GetLastError());
            break;
        }

        // Wait for either a filesystem event or the stop signal.
        HANDLE handles[] = { ov.hEvent, m_StopEvent };
        const DWORD wait = WaitForMultipleObjects(2, handles, /*waitAll=*/FALSE, INFINITE);

        if (wait == WAIT_OBJECT_0) {
            // Filesystem event — retrieve the result.
            GetOverlappedResult(dir, &ov, &bytes, /*wait=*/FALSE);

            if (bytes > 0) {
                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf);
                for (;;) {
                    // FileNameLength is in bytes; FileName is not null-terminated.
                    const std::wstring name(info->FileName,
                                            info->FileNameLength / sizeof(wchar_t));

                    // Only trigger on .hlsl files.
                    if (name.size() >= 5 &&
                        _wcsicmp(name.c_str() + name.size() - 5, L".hlsl") == 0)
                    {
                        m_Dirty = true;
                        LOG_INFO("ShaderWatcher: '%s' modified — hot reload pending.",
                                 std::string(name.begin(), name.end()).c_str());
                        break;   // one dirty flag covers the whole batch
                    }
                    if (info->NextEntryOffset == 0) break;
                    info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<char*>(info) + info->NextEntryOffset);
                }
            }
        } else {
            // Stop event (or error) — cancel the pending IO and exit.
            CancelIo(dir);
            // Drain the pending operation so the handle can be closed cleanly.
            GetOverlappedResult(dir, &ov, &bytes, /*wait=*/TRUE);
            break;
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(dir);
}

} // namespace VibeEngine
