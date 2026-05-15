#pragma once
#include <xaudio2.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace VibeEngine {

class AudioClip;

// ============================================================================
// AudioManager  (singleton)
//
// Owns the XAudio2 device and mastering voice.
// Provides clip caching and fire-and-forget playback.
//
// Usage — initialization (call once, typically in Application::OnInit):
//   AudioManager::Get().Initialize();
//
// Usage — load + cache a clip:
//   auto clip = AudioManager::Get().LoadClip(L"Sounds/impact.wav");
//
// Usage — fire-and-forget:
//   AudioManager::Get().PlayOneShot(clip);
//   AudioManager::Get().PlayOneShot(clip, 0.5f); // half volume
//
// Update() must be called every frame (done automatically by Application)
// to free source voices that have finished playing.
//
// Shutdown is called automatically by Application::OnShutdown.
// ============================================================================
class AudioManager {
public:
    static AudioManager& Get() {
        static AudioManager instance;
        return instance;
    }

    // ---- Lifecycle ----------------------------------------------------------
    bool Initialize();
    void Update();          // call each frame — recycles finished one-shot voices
    void Shutdown();

    bool IsInitialized() const { return m_Initialized; }

    // ---- Clip loading -------------------------------------------------------
    // Loads from disk on first call; returns cached ptr on subsequent calls.
    // Returns nullptr on failure.
    std::shared_ptr<AudioClip> LoadClip(const std::wstring& path);

    // ---- Playback -----------------------------------------------------------
    // Creates a temporary source voice, plays clip once, and auto-destroys it.
    void PlayOneShot(const std::shared_ptr<AudioClip>& clip,
                     float volume = 1.0f,
                     float pitch  = 1.0f);

    // ---- Master volume (0.0 – 1.0) ------------------------------------------
    void SetMasterVolume(float v);
    float GetMasterVolume() const { return m_MasterVolume; }

    // ---- Raw access (for AudioSource) ---------------------------------------
    IXAudio2* GetDevice() const { return m_XAudio2.Get(); }

private:
    AudioManager()  = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // One-shot voice entry — kept alive until the voice finishes
    struct OneShotEntry {
        IXAudio2SourceVoice*    voice = nullptr;
        std::vector<uint8_t>    data;   // owns a copy so the clip can be freed
    };

    Microsoft::WRL::ComPtr<IXAudio2>    m_XAudio2;
    IXAudio2MasteringVoice*             m_MasterVoice  = nullptr;
    float                               m_MasterVolume = 1.0f;
    bool                                m_Initialized  = false;

    // Clip cache (path → clip)
    std::unordered_map<std::wstring, std::shared_ptr<AudioClip>> m_Cache;

    // Active one-shot voices (checked each frame in Update())
    std::mutex                   m_OneShotMutex;
    std::vector<OneShotEntry>    m_OneShots;
};

} // namespace VibeEngine
