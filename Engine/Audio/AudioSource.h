#pragma once
#include "../Core/Component.h"
#include "AudioClip.h"
#include <xaudio2.h>
#include <memory>

namespace VibeEngine {

// ============================================================================
// AudioSource  (Component)
//
// Attaches a persistent XAudio2 source voice to a GameObject.
// Supports Play / Stop / Pause, looping, volume, and pitch control.
// The voice is created in Start() and destroyed in OnDestroy().
//
// Quick setup:
//   auto* as = go->AddComponent<AudioSource>();
//   as->SetClip(AudioManager::Get().LoadClip(L"Sounds/impact.wav"));
//   as->SetLoop(false);
//   as->SetPlayOnStart(true);
//
// Runtime:
//   as->Play();              // (re)starts from beginning
//   as->Stop();
//   as->SetVolume(0.5f);
//   as->SetPitch(1.5f);      // 1.5× = one and a half times normal speed
//   as->IsPlaying();         // true while buffer is queued
// ============================================================================
class AudioSource : public Component {
public:
    AudioSource()           = default;
    ~AudioSource() override = default;

    // ---- Lifecycle ----------------------------------------------------------
    void Start()          override;
    void OnDestroy()      override;

    // ---- Configuration (call before Start or at runtime) --------------------
    void SetClip       (std::shared_ptr<AudioClip> clip);
    void SetLoop       (bool loop)   { m_Loop        = loop;   ApplyLoop(); }
    void SetPlayOnStart(bool play)   { m_PlayOnStart = play; }
    void SetVolume     (float v);
    void SetPitch      (float p);

    // ---- Playback -----------------------------------------------------------
    // Play() resubmits the audio buffer from the beginning.
    // Calling Play() while already playing stops first, then restarts.
    void Play();
    void Stop();
    void Pause();
    void Resume();

    // ---- State query --------------------------------------------------------
    bool IsPlaying() const;
    bool IsPaused()  const { return m_Paused; }

    float GetVolume() const { return m_Volume; }
    float GetPitch()  const { return m_Pitch;  }

private:
    void CreateVoice();
    void DestroyVoice();
    void SubmitBuffer();
    void ApplyLoop();

    std::shared_ptr<AudioClip>  m_Clip;
    IXAudio2SourceVoice*        m_Voice       = nullptr;

    bool  m_Loop        = false;
    bool  m_PlayOnStart = false;
    bool  m_Paused      = false;
    float m_Volume      = 1.0f;
    float m_Pitch       = 1.0f;
};

} // namespace VibeEngine
