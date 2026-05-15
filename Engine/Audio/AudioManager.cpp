#pragma comment(lib, "xaudio2.lib")

#include "AudioManager.h"
#include "AudioClip.h"
#include "../Core/Log.h"
#include <algorithm>

namespace VibeEngine {

// ---------------------------------------------------------------------------
bool AudioManager::Initialize()
{
    if (m_Initialized) return true;

    // XAudio2 2.9 ships in the Windows SDK and doesn't need a separate install.
    HRESULT hr = XAudio2Create(m_XAudio2.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        LOG_ERROR("AudioManager: XAudio2Create failed (hr=0x%08X)", hr);
        return false;
    }

#if defined(_DEBUG)
    XAUDIO2_DEBUG_CONFIGURATION dbgCfg = {};
    dbgCfg.TraceMask       = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
    dbgCfg.BreakMask       = XAUDIO2_LOG_ERRORS;
    dbgCfg.LogFunctionName = TRUE;
    m_XAudio2->SetDebugConfiguration(&dbgCfg);
#endif

    hr = m_XAudio2->CreateMasteringVoice(&m_MasterVoice);
    if (FAILED(hr)) {
        LOG_ERROR("AudioManager: CreateMasteringVoice failed (hr=0x%08X)", hr);
        m_XAudio2.Reset();
        return false;
    }

    m_Initialized = true;
    LOG_INFO("AudioManager: XAudio2 initialized.");
    return true;
}

// ---------------------------------------------------------------------------
void AudioManager::Shutdown()
{
    if (!m_Initialized) return;

    // Destroy all pending one-shot voices
    {
        std::lock_guard<std::mutex> lock(m_OneShotMutex);
        for (auto& e : m_OneShots) {
            if (e.voice) {
                e.voice->Stop();
                e.voice->DestroyVoice();
            }
        }
        m_OneShots.clear();
    }

    m_Cache.clear();

    if (m_MasterVoice) {
        m_MasterVoice->DestroyVoice();
        m_MasterVoice = nullptr;
    }
    m_XAudio2.Reset();
    m_Initialized = false;

    LOG_INFO("AudioManager: shutdown.");
}

// ---------------------------------------------------------------------------
// Called each frame — destroys source voices whose buffers have finished.
// We check XAUDIO2_VOICE_STATE::BuffersQueued == 0 which means the voice
// has played through its buffer and gone idle.
// ---------------------------------------------------------------------------
void AudioManager::Update()
{
    if (!m_Initialized) return;

    std::lock_guard<std::mutex> lock(m_OneShotMutex);

    auto it = m_OneShots.begin();
    while (it != m_OneShots.end()) {
        XAUDIO2_VOICE_STATE state;
        it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

        if (state.BuffersQueued == 0) {
            // Playback finished — reclaim the voice
            it->voice->Stop();
            it->voice->DestroyVoice();
            it = m_OneShots.erase(it);
        }
        else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
std::shared_ptr<AudioClip> AudioManager::LoadClip(const std::wstring& path)
{
    // Check cache first
    auto it = m_Cache.find(path);
    if (it != m_Cache.end()) return it->second;

    auto clip = std::make_shared<AudioClip>();
    if (!clip->LoadFromFile(path)) {
        LOG_WARN("AudioManager: failed to load clip '%ls'", path.c_str());
        return nullptr;
    }

    m_Cache[path] = clip;
    return clip;
}

// ---------------------------------------------------------------------------
void AudioManager::PlayOneShot(const std::shared_ptr<AudioClip>& clip,
                                float volume,
                                float pitch)
{
    if (!m_Initialized || !clip || !clip->IsLoaded()) return;

    // Create a new source voice for this one-shot play
    IXAudio2SourceVoice* voice = nullptr;
    const WAVEFORMATEX& fmt = clip->GetFormat();

    HRESULT hr = m_XAudio2->CreateSourceVoice(&voice, &fmt);
    if (FAILED(hr) || !voice) {
        LOG_WARN("AudioManager::PlayOneShot — CreateSourceVoice failed (hr=0x%08X)", hr);
        return;
    }

    // Copy data so the clip can be released independently
    OneShotEntry entry;
    entry.data = clip->GetData();   // copy
    entry.voice = voice;

    // Build buffer
    XAUDIO2_BUFFER buf = {};
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    buf.AudioBytes = static_cast<UINT32>(entry.data.size());
    buf.pAudioData = entry.data.data();

    hr = voice->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) {
        LOG_WARN("AudioManager::PlayOneShot — SubmitSourceBuffer failed (hr=0x%08X)", hr);
        voice->DestroyVoice();
        return;
    }

    voice->SetVolume(volume);
    // Pitch is expressed as a frequency ratio (1.0 = unchanged)
    voice->SetFrequencyRatio(pitch);
    voice->Start(0);

    std::lock_guard<std::mutex> lock(m_OneShotMutex);
    m_OneShots.push_back(std::move(entry));
}

// ---------------------------------------------------------------------------
void AudioManager::SetMasterVolume(float v)
{
    m_MasterVolume = v;
    if (m_MasterVoice)
        m_MasterVoice->SetVolume(v);
}

} // namespace VibeEngine
