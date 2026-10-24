// GameBasiX - Software-Mixer fuer WAV-Sounds und Musik (SDL3-Audio)
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

class Audio {
public:
    bool headless = false;

    bool init();
    void shutdown();

    bool loadSound(const std::string& file, int id, int buffers, std::string& err);
    int playSound(int soundId, double pan, double volume);
    void stopChannel(int channel);
    bool channelPlaying(int channel) const;
    void hush();

    bool playMusic(const std::string& file, bool loop, std::string& err);
    void stopMusic();
    void pauseMusic(bool pause);
    bool musicPlaying() const;
    void setMusicVolume(double v);
    int nextId() const;

private:
    struct Sound {
        std::vector<float> samples;  // interleaved stereo, 48 kHz
        size_t frames = 0;
        int buffers = 1;
    };

    struct Voice {
        bool active = false;
        bool music = false;
        int soundId = -1;
        size_t pos = 0;
        bool loop = false;
        bool paused = false;
        float gainL = 1.0f;
        float gainR = 1.0f;
        Uint64 started = 0;
    };

    static void SDLCALL audioCallback(void* userdata, SDL_AudioStream* stream, int additional,
                                      int total);

    SDL_AudioStream* stream_ = nullptr;
    SDL_Mutex* mutex_ = nullptr;
    std::unordered_map<int, Sound> sounds_;
    Sound music_;
    std::vector<Voice> voices_;  // Kanal n = voices_[n-1]
    int musicChannel_ = -1;
    double musicVolume_ = 1.0;
    int highestId_ = 0;
};
