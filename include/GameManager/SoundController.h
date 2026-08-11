#pragma once
#include <raylib.h>

class SoundController {
public:
    SoundController() = default;
    ~SoundController() = default;

    // SFX
    void playSFX(Sound sound);
    void setSFXVolume(float volume);

    // Music
    void playMusic(Music music);
    void stopMusic();
    void pauseMusic();
    void resumeMusic();
    void updateMusic();
    void setMusicVolume(float volume);

private:
    float sfx_volume = 1.0f;
    float music_volume = 1.0f;
    
    Music current_music = { 0 };
    bool is_music_playing = false;
};
