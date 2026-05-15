#include "AudioClip.h"
#include "../Core/Log.h"
#include <fstream>
#include <cstring>

namespace VibeEngine {

// ---------------------------------------------------------------------------
// Minimal RIFF/WAVE parser — handles PCM and IEEE_FLOAT WAV files.
// Skips any unknown chunks (LIST, fact, JUNK, …).
// ---------------------------------------------------------------------------
bool AudioClip::LoadFromFile(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        LOG_WARN("AudioClip: cannot open '%ls'", path.c_str());
        return false;
    }

    // ---- RIFF header -------------------------------------------------------
    char tag[4];
    f.read(tag, 4);
    if (std::memcmp(tag, "RIFF", 4) != 0) {
        LOG_WARN("AudioClip: not a RIFF file '%ls'", path.c_str());
        return false;
    }

    uint32_t riffSize;
    f.read(reinterpret_cast<char*>(&riffSize), 4);

    char wave[4];
    f.read(wave, 4);
    if (std::memcmp(wave, "WAVE", 4) != 0) {
        LOG_WARN("AudioClip: RIFF type is not WAVE in '%ls'", path.c_str());
        return false;
    }

    // ---- Chunk scan --------------------------------------------------------
    bool gotFmt  = false;
    bool gotData = false;

    while (f && !gotData) {
        char   chunkId[4];
        uint32_t chunkSize = 0;

        f.read(chunkId, 4);
        f.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (!f) break;

        // Align reads: RIFF chunks are word-aligned (padded to even size)
        uint32_t paddedSize = (chunkSize + 1) & ~1u;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            // ---- fmt chunk ---------------------------------------------------
            uint16_t audioFormat, numChannels;
            uint32_t sampleRate, byteRate;
            uint16_t blockAlign, bitsPerSample;

            f.read(reinterpret_cast<char*>(&audioFormat),  2);
            f.read(reinterpret_cast<char*>(&numChannels),  2);
            f.read(reinterpret_cast<char*>(&sampleRate),   4);
            f.read(reinterpret_cast<char*>(&byteRate),     4);
            f.read(reinterpret_cast<char*>(&blockAlign),   2);
            f.read(reinterpret_cast<char*>(&bitsPerSample),2);

            m_Format.wFormatTag      = audioFormat;
            m_Format.nChannels       = numChannels;
            m_Format.nSamplesPerSec  = sampleRate;
            m_Format.nAvgBytesPerSec = byteRate;
            m_Format.nBlockAlign     = blockAlign;
            m_Format.wBitsPerSample  = bitsPerSample;
            m_Format.cbSize          = 0;

            // Skip extra bytes (e.g. WAVEFORMATEXTENSIBLE extension)
            if (paddedSize > 16)
                f.seekg(paddedSize - 16, std::ios::cur);

            gotFmt = true;
        }
        else if (std::memcmp(chunkId, "data", 4) == 0) {
            // ---- data chunk --------------------------------------------------
            if (!gotFmt) {
                LOG_WARN("AudioClip: 'data' before 'fmt ' in '%ls'", path.c_str());
                return false;
            }
            m_Data.resize(chunkSize);
            f.read(reinterpret_cast<char*>(m_Data.data()), chunkSize);
            if (chunkSize & 1) f.seekg(1, std::ios::cur); // pad byte
            gotData = true;
        }
        else {
            // Skip unknown chunk
            f.seekg(paddedSize, std::ios::cur);
        }
    }

    if (!gotData || m_Data.empty()) {
        LOG_WARN("AudioClip: no audio data found in '%ls'", path.c_str());
        return false;
    }

    LOG_INFO("AudioClip: loaded '%ls' — %u Hz, %u ch, %u-bit, %u bytes",
             path.c_str(),
             m_Format.nSamplesPerSec,
             m_Format.nChannels,
             m_Format.wBitsPerSample,
             static_cast<unsigned>(m_Data.size()));

    return true;
}

} // namespace VibeEngine
