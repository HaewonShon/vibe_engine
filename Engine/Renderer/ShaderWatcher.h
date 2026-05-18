#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <atomic>
#include <string>
#include <thread>

namespace VibeEngine {

// ---------------------------------------------------------------------------
// ShaderWatcher
//
// Monitors a flat directory for .hlsl file modifications using
// ReadDirectoryChangesW on a background thread.  Thread-safe: only an
// atomic_bool is shared between the watcher thread and the main thread.
//
// Typical usage (one instance per pipeline):
//   watcher.Watch(L"C:/myProject/bin/Shaders");
//   // each frame (before recording GPU commands):
//   if (watcher.PollDirty()) { WaitForGPU(); pipeline.HotReload(); }
// ---------------------------------------------------------------------------
class ShaderWatcher {
public:
    ~ShaderWatcher();

    // Begin watching 'directory' (non-recursive, last-write events only).
    // Safe to call again after Stop() — restarts the thread.
    void Watch(const std::wstring& directory);

    // Stop the background thread and release Win32 handles.
    // Called automatically by the destructor.
    void Stop();

    // Returns true once per group of change events, then resets to false.
    // Thread-safe (atomic exchange).
    bool PollDirty();

    bool IsRunning() const { return m_Running.load(); }

private:
    void ThreadFunc();

    std::wstring     m_Directory;
    std::thread      m_Thread;
    std::atomic_bool m_Dirty   { false };
    std::atomic_bool m_Running { false };
    HANDLE           m_StopEvent = INVALID_HANDLE_VALUE;
};

} // namespace VibeEngine
