#include "AudioSource.h"
#include "AudioManager.h"
#include "../Core/Log.h"

namespace VibeEngine {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AudioSource::Start()
{
    if (!AudioManager::Get().IsInitialized()) {
        LOG_WARN("AudioSource::Start — AudioManager not initialized");
        return;
    }
    if (m_Clip && m_Clip->IsLoaded())
        CreateVoice();

    if (m_PlayOnStart && m_Voice)
        Play();
}

void AudioSource::OnDestroy()
{
    DestroyVoice();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void AudioSource::SetClip(std::shared_ptr<AudioClip> clip)
{
    // If a voice already exists with a different format, recreate it.
    bool needRecreate = (m_Voice != nullptr);
    if (needRecreate) DestroyVoice();

    m_Clip   = std::move(clip);
    m_Paused = false;

    if (needRecreate && m_Clip && m_Clip->IsLoaded())
        CreateVoice();
}

void AudioSource::SetVolume(float v)
{
    m_Volume = v;
    if (m_Voice) m_Voice->SetVolume(v);
}

void AudioSource::SetPitch(float p)
{
    m_Pitch = p;
    if (m_Voice) m_Voice->SetFrequencyRatio(p);
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void AudioSource::Play()
{
    if (!m_Voice) {
        // Voice might not exist yet (SetClip called before Start)
        if (AudioManager::Get().IsInitialized() && m_Clip && m_Clip->IsLoaded())
            CreateVoice();
        if (!m_Voice) return;
    }

    // Flush any pending buffers, then resubmit from start
    m_Voice->Stop();
    m_Voice->FlushSourceBuffers();
    m_Paused = false;

    SubmitBuffer();
    m_Voice->Start(0);
}

void AudioSource::Stop()
{
    if (!m_Voice) return;
    m_Voice->Stop();
    m_Voice->FlushSourceBuffers();
    m_Paused = false;
}

void AudioSource::Pause()
{
    if (!m_Voice || m_Paused) return;
    m_Voice->Stop();
    m_Paused = true;
}

void AudioSource::Resume()
{
    if (!m_Voice || !m_Paused) return;
    m_Voice->Start(0);
    m_Paused = false;
}

bool AudioSource::IsPlaying() const
{
    if (!m_Voice || m_Paused) return false;
    XAUDIO2_VOICE_STATE state;
    m_Voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued > 0;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void AudioSource::CreateVoice()
{
    if (!m_Clip || !m_Clip->IsLoaded()) return;

    IXAudio2* xa = AudioManager::Get().GetDevice();
    if (!xa) return;

    HRESULT hr = xa->CreateSourceVoice(&m_Voice, &m_Clip->GetFormat());
    if (FAILED(hr) || !m_Voice) {
        LOG_WARN("AudioSource: CreateSourceVoice failed (hr=0x%08X)", hr);
        m_Voice = nullptr;
        return;
    }

    m_Voice->SetVolume(m_Volume);
    m_Voice->SetFrequencyRatio(m_Pitch);
}

void AudioSource::DestroyVoice()
{
    if (!m_Voice) return;
    m_Voice->Stop();
    m_Voice->FlushSourceBuffers();
    m_Voice->DestroyVoice();
    m_Voice  = nullptr;
    m_Paused = false;
}

void AudioSource::SubmitBuffer()
{
    if (!m_Voice || !m_Clip || !m_Clip->IsLoaded()) return;

    XAUDIO2_BUFFER buf = {};
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    buf.AudioBytes = m_Clip->GetDataSize();
    buf.pAudioData = m_Clip->GetData().data();
    buf.LoopCount  = m_Loop ? XAUDIO2_LOOP_INFINITE : 0;

    HRESULT hr = m_Voice->SubmitSourceBuffer(&buf);
    if (FAILED(hr))
        LOG_WARN("AudioSource: SubmitSourceBuffer failed (hr=0x%08X)", hr);
}

void AudioSource::ApplyLoop()
{
    // Loop flag takes effect on the NEXT SubmitBuffer call; if currently
    // playing, restart to apply the new loop setting immediately.
    if (IsPlaying()) Play();
}

} // namespace VibeEngine
