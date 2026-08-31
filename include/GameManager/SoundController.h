#pragma once
#include <raylib.h>
#include <Rendering/AssetID.h>

class AssetManager;

class SoundController {
public:
    explicit SoundController(const AssetManager& asset_manager);
    ~SoundController() = default;

    // SFX
    void playSFX(AssetID id);
    void stopSFX(AssetID id);
    bool isSFXPlaying(AssetID id) const;
    void setSFXVolume(float volume);

    // Music
    void playMusic(AssetID id);
    void stopMusic();
    void pauseMusic();
    void resumeMusic();
    void updateMusic();
    void setMusicVolume(float volume);

private:
    const AssetManager& asset_manager;

    float sfx_volume = 1.0f;
    float music_volume = 1.0f;
    
    Music current_music = { 0 };
    Music previous_music = { 0 };
    bool is_music_playing = false;
    bool has_current_music = false;
    bool is_fading = false;
    float current_volume_scale = 1.0f;
    float previous_volume_scale = 0.0f;
    AssetID current_music_id = AssetID::BGM_EXPLORE;
    static constexpr float FADE_TIME = 3.0f;
};
