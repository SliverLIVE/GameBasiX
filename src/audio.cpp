// GameBasiX - Software-Mixer fuer WAV- und MP3-Sounds und Musik
#include "audio.h"

#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// MP3-Dekoder (minimp3, gemeinfrei). Er liefert die Samples als Float, genau
// wie der Mixer sie braucht; der Rest der Datei bleibt unveraendert.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_STDIO
#include "minimp3_ex.h"

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kVoices = 24;

SDL_AudioSpec deviceSpec() {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = kChannels;
    spec.freq = kSampleRate;
    return spec;
}

/** Ganzes File in den Speicher lesen - minimp3 arbeitet auf einem Puffer. */
bool readFileBytes(const std::string& file, std::vector<uint8_t>& out) {
    SDL_IOStream* io = SDL_IOFromFile(file.c_str(), "rb");
    if (!io) return false;
    uint8_t block[64 * 1024];
    size_t got = 0;
    while ((got = SDL_ReadIO(io, block, sizeof(block))) > 0)
        out.insert(out.end(), block, block + got);
    const bool ok = SDL_GetIOStatus(io) != SDL_IO_STATUS_ERROR && !out.empty();
    SDL_CloseIO(io);
    return ok;
}

/**
 * Beliebiges Format auf Geraeteformat bringen (Float, stereo, 48 kHz).
 * SDL rechnet dabei Abtastrate und Kanalzahl um; leere Ausgabe = Fehler.
 */
bool convertToDevice(const void* data, size_t bytes, const SDL_AudioSpec& src,
                     std::vector<float>& out, std::string& err) {
    SDL_AudioSpec dst = deviceSpec();
    SDL_AudioStream* conv = SDL_CreateAudioStream(&src, &dst);
    if (!conv) {
        err = "Audiokonvertierung fehlgeschlagen";
        return false;
    }
    SDL_PutAudioStreamData(conv, data, static_cast<int>(bytes));
    SDL_FlushAudioStream(conv);
    const int avail = SDL_GetAudioStreamAvailable(conv);
    if (avail > 0) {
        out.resize(static_cast<size_t>(avail) / sizeof(float));
        const int got = SDL_GetAudioStreamData(conv, out.data(), avail);
        if (got <= 0) out.clear();
        else out.resize(static_cast<size_t>(got) / sizeof(float));
    }
    SDL_DestroyAudioStream(conv);
    if (out.empty()) {
        err = "Datei ohne Audiodaten";
        return false;
    }
    return true;
}

/** Sieht der Anfang nach MP3 aus (ID3-Tag oder Frame-Sync)? */
bool looksLikeMp3(const std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 3 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') return true;
    const size_t end = std::min<size_t>(bytes.size(), 4096);
    for (size_t i = 0; i + 1 < end; ++i)
        if (bytes[i] == 0xFF && (bytes[i + 1] & 0xE0) == 0xE0) return true;
    return false;
}

/** MP3 (mit oder ohne ID3-Tags) zu Float-Samples dekodieren. */
bool decodeMp3(const std::vector<uint8_t>& bytes, std::vector<float>& samples, std::string& err) {
    size_t size = bytes.size();
    // ID3v1-Tag am Ende abschneiden, sonst sucht der Dekoder darin nach Frames.
    if (size > 128 && std::memcmp(bytes.data() + size - 128, "TAG", 3) == 0) size -= 128;

    mp3dec_t decoder;
    mp3dec_file_info_t info{};
    const int result = mp3dec_load_buf(&decoder, bytes.data(), size, &info, nullptr, nullptr);
    if (result != 0 || !info.buffer || !info.samples || info.channels <= 0 || info.hz <= 0) {
        std::free(info.buffer);
        err = "MP3 konnte nicht dekodiert werden";
        return false;
    }

    SDL_AudioSpec src;
    src.format = SDL_AUDIO_F32;
    src.channels = info.channels;
    src.freq = info.hz;
    const size_t values = info.samples;  // inkl. Kanaele
    const bool ok = convertToDevice(info.buffer, values * sizeof(mp3d_sample_t), src, samples, err);
    std::free(info.buffer);
    return ok;
}

/** WAV oder MP3 laden und fertig fuer den Mixer zurueckgeben. */
bool loadAudio(const std::string& file, std::vector<float>& samples, std::string& err) {
    SDL_AudioSpec src;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (SDL_LoadWAV(file.c_str(), &src, &buf, &len)) {
        const bool ok = convertToDevice(buf, len, src, samples, err);
        SDL_free(buf);
        return ok;
    }

    std::vector<uint8_t> bytes;
    if (!readFileBytes(file, bytes)) {
        err = "Datei nicht lesbar";
        return false;
    }
    if (!looksLikeMp3(bytes)) {
        err = "Format nicht erkannt, WAV oder MP3 erwartet";
        return false;
    }
    return decodeMp3(bytes, samples, err);
}
}  // namespace

bool Audio::init() {
    if (headless) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, i18n::tr("Audio konnte nicht initialisiert werden: %s\n").c_str(),
                     SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec = deviceSpec();
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, this);
    if (!stream_) {
        std::fprintf(stderr, i18n::tr("Audiogeraet konnte nicht geoeffnet werden: %s\n").c_str(),
                     SDL_GetError());
        return false;
    }
    mutex_ = SDL_CreateMutex();
    voices_.assign(kVoices, Voice{});
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Audio::shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    if (mutex_) {
        SDL_DestroyMutex(mutex_);
        mutex_ = nullptr;
    }
}

int Audio::nextId() const {
    for (int id = 0; id < 4096; ++id)
        if (sounds_.find(id) == sounds_.end()) return id;
    return -1;
}

bool Audio::loadSound(const std::string& file, int id, int buffers, std::string& err) {
    std::vector<float> samples;
    std::string detail;
    if (!loadAudio(file, samples, detail)) {
        err = "Sound konnte nicht geladen werden: " + file + " - " + detail;
        return false;
    }

    Sound s;
    s.samples = std::move(samples);
    s.frames = s.samples.size() / kChannels;
    s.buffers = std::max(1, std::min(4, buffers));

    if (mutex_) SDL_LockMutex(mutex_);
    sounds_[id] = std::move(s);
    if (id > highestId_) highestId_ = id;
    if (mutex_) SDL_UnlockMutex(mutex_);
    return true;
}

static void panGains(double pan, double volume, float& gl, float& gr) {
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    if (volume < 0) volume = 0;
    if (volume > 8) volume = 8;
    double ang = (pan + 1.0) * 3.14159265358979323846 / 4.0;
    gl = static_cast<float>(std::cos(ang) * volume);
    gr = static_cast<float>(std::sin(ang) * volume);
}

int Audio::playSound(int soundId, double pan, double volume) {
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) return 0;
    if (headless) return 0;

    float gl, gr;
    panGains(pan, volume, gl, gr);

    int channel = 0;
    if (mutex_) SDL_LockMutex(mutex_);
    int slot = -1;
    for (int i = 0; i < static_cast<int>(voices_.size()); ++i) {
        if (!voices_[i].active || voices_[i].music) {
            if (!voices_[i].active) slot = i;
            break;
        }
    }
    if (slot < 0) {
        // aelteste Stimme verdraengen
        size_t best = 0;
        Uint64 oldest = ~0ull;
        for (int i = 0; i < static_cast<int>(voices_.size()); ++i) {
            if (voices_[i].music) continue;
            if (voices_[i].started < oldest) {
                oldest = voices_[i].started;
                best = static_cast<size_t>(i);
            }
        }
        slot = static_cast<int>(best);
    }
    Voice& v = voices_[static_cast<size_t>(slot)];
    v.active = true;
    v.music = false;
    v.soundId = soundId;
    v.pos = 0;
    v.loop = false;
    v.paused = false;
    v.gainL = gl;
    v.gainR = gr;
    v.started = SDL_GetTicks();
    channel = slot + 1;
    if (mutex_) SDL_UnlockMutex(mutex_);
    return channel;
}

void Audio::stopChannel(int channel) {
    if (headless) return;
    if (mutex_) SDL_LockMutex(mutex_);
    if (channel > 0 && channel <= static_cast<int>(voices_.size()))
        voices_[static_cast<size_t>(channel - 1)] = Voice{};
    if (mutex_) SDL_UnlockMutex(mutex_);
}

bool Audio::channelPlaying(int channel) const {
    if (channel <= 0 || channel > static_cast<int>(voices_.size())) return false;
    return voices_[static_cast<size_t>(channel - 1)].active;
}

void Audio::hush() {
    if (headless) return;
    if (mutex_) SDL_LockMutex(mutex_);
    for (auto& v : voices_)
        if (!v.music) v = Voice{};
    if (mutex_) SDL_UnlockMutex(mutex_);
}

bool Audio::playMusic(const std::string& file, bool loop, std::string& err) {
    Sound s;
    std::vector<float> samples;
    std::string detail;
    if (!loadAudio(file, samples, detail)) {
        err = "Musik konnte nicht geladen werden: " + file + " - " + detail;
        return false;
    }
    s.samples = std::move(samples);
    s.frames = s.samples.size() / kChannels;

    if (headless) {
        music_ = std::move(s);
        musicChannel_ = 1;
        return true;
    }

    if (mutex_) SDL_LockMutex(mutex_);
    music_ = std::move(s);
    int slot = -1;
    for (int i = 0; i < static_cast<int>(voices_.size()); ++i) {
        if (!voices_[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;
    Voice& v = voices_[static_cast<size_t>(slot)];
    v = Voice{};
    v.active = true;
    v.music = true;
    v.loop = loop;
    v.gainL = static_cast<float>(musicVolume_);
    v.gainR = static_cast<float>(musicVolume_);
    v.started = SDL_GetTicks();
    musicChannel_ = slot + 1;
    if (mutex_) SDL_UnlockMutex(mutex_);
    return true;
}

void Audio::stopMusic() {
    if (headless) {
        musicChannel_ = -1;
        music_.samples.clear();
        music_.frames = 0;
        return;
    }
    if (musicChannel_ <= 0) return;
    if (mutex_) SDL_LockMutex(mutex_);
    if (musicChannel_ <= static_cast<int>(voices_.size()))
        voices_[static_cast<size_t>(musicChannel_ - 1)] = Voice{};
    musicChannel_ = -1;
    if (mutex_) SDL_UnlockMutex(mutex_);
}

void Audio::pauseMusic(bool pause) {
    if (musicChannel_ <= 0) return;
    if (mutex_) SDL_LockMutex(mutex_);
    if (musicChannel_ <= static_cast<int>(voices_.size()))
        voices_[static_cast<size_t>(musicChannel_ - 1)].paused = pause;
    if (mutex_) SDL_UnlockMutex(mutex_);
}

bool Audio::musicPlaying() const {
    if (musicChannel_ <= 0) return false;
    if (headless) return true;
    if (musicChannel_ > static_cast<int>(voices_.size())) return false;
    return voices_[static_cast<size_t>(musicChannel_ - 1)].active;
}

void Audio::setMusicVolume(double v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    musicVolume_ = v;
    if (musicChannel_ <= 0) return;
    if (mutex_) SDL_LockMutex(mutex_);
    if (musicChannel_ <= static_cast<int>(voices_.size())) {
        Voice& voice = voices_[static_cast<size_t>(musicChannel_ - 1)];
        voice.gainL = static_cast<float>(v);
        voice.gainR = static_cast<float>(v);
    }
    if (mutex_) SDL_UnlockMutex(mutex_);
}

void SDLCALL Audio::audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int total) {
    Audio* self = static_cast<Audio*>(userdata);
    if (!self || additional <= 0) return;
    int frames = additional / (sizeof(float) * kChannels);
    if (frames <= 0) return;

    static thread_local std::vector<float> mix;
    mix.assign(static_cast<size_t>(frames) * kChannels, 0.0f);

    if (self->mutex_) SDL_LockMutex(self->mutex_);
    for (auto& v : self->voices_) {
        if (!v.active || v.paused) continue;
        const Sound* snd = nullptr;
        if (v.music) {
            snd = &self->music_;
        } else {
            auto it = self->sounds_.find(v.soundId);
            if (it != self->sounds_.end()) snd = &it->second;
        }
        if (!snd || snd->frames == 0) {
            v.active = false;
            continue;
        }
        size_t remaining = snd->frames - v.pos;
        size_t n = std::min(static_cast<size_t>(frames), remaining);
        for (size_t i = 0; i < n; ++i) {
            size_t src = (v.pos + i) * kChannels;
            mix[i * kChannels + 0] += snd->samples[src + 0] * v.gainL;
            mix[i * kChannels + 1] += snd->samples[src + 1] * v.gainR;
        }
        v.pos += n;
        if (v.pos >= snd->frames) {
            if (v.loop) {
                v.pos = 0;
            } else {
                v.active = false;
            }
        }
    }
    if (self->mutex_) SDL_UnlockMutex(self->mutex_);

    for (float& f : mix) {
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
    }
    SDL_PutAudioStreamData(stream, mix.data(), static_cast<int>(mix.size() * sizeof(float)));
    (void)total;
}
