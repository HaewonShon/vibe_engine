#pragma once
// xaudio2.h pulls in the proper mmreg.h definitions for WAVEFORMATEX
#include <xaudio2.h>
#include <vector>
#include <cstdint>
#include <string>

namespace VibeEngine {

// ============================================================================
// AudioClip
//
// Holds raw PCM audio data loaded from a WAV file.
// The data pointer is stable for the lifetime of the clip — safe to hand
// directly to XAudio2 as a XAUDIO2_BUFFER.
//
// Supported WAV formats:
//   PCM  (wFormatTag = 1)   — 8 or 16 bit, mono or stereo, any sample rate
//   IEEE_FLOAT (wFormatTag = 3) — 32-bit float mono/stereo
//
// Usage:
//   auto clip = std::make_shared<AudioClip>();
//   if (!clip->LoadFromFile(L"Sounds/impact.wav")) { ... }
//   AudioManager::Get().PlayOneShot(clip);
// ============================================================================
class AudioClip {
public:
    AudioClip()  = default;
    ~AudioClip() = default;

    // Non-copyable, movable
    AudioClip(const AudioClip&)            = delete;
    AudioClip& operator=(const AudioClip&) = delete;
    AudioClip(AudioClip&&)                 = default;
    AudioClip& operator=(AudioClip&&)      = default;

    // Load a WAV file.  Returns true on success.
    bool LoadFromFile(const std::wstring& path);

    // --- Accessors -----------------------------------------------------------
    bool                        IsLoaded()    const { return !m_Data.empty(); }
    const WAVEFORMATEX&         GetFormat()   const { return m_Format; }
    const std::vector<uint8_t>& GetData()     const { return m_Data; }
    uint32_t                    GetDataSize() const { return static_cast<uint32_t>(m_Data.size()); }

private:
    WAVEFORMATEX         m_Format = {};
    std::vector<uint8_t> m_Data;
};

} // namespace VibeEngine
