#include <GameManager/SoundController.h>
#include <Rendering/AssetManager.h>

SoundController::SoundController(const AssetManager& asset_manager) 
    : asset_manager(asset_manager) {}

void SoundController::playSFX(AssetID id) {
    Sound sound = asset_manager.getSound(id);
    if (sound.stream.buffer != nullptr) {
        SetSoundVolume(sound, sfx_volume);
        PlaySound(sound);
    }
}

void SoundController::setSFXVolume(float volume) {
    sfx_volume = volume;
}

void SoundController::playMusic(AssetID id) {
    Music music = asset_manager.getMusic(id);
    if (music.stream.buffer != nullptr) {
        if (is_music_playing && current_music.stream.buffer != nullptr) {
            StopMusicStream(current_music);
        }
        current_music = music;
        SetMusicVolume(current_music, music_volume);
        PlayMusicStream(current_music);
        is_music_playing = true;
    }
}

void SoundController::stopMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        StopMusicStream(current_music);
        is_music_playing = false;
    }
}

void SoundController::pauseMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        PauseMusicStream(current_music);
    }
}

void SoundController::resumeMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        ResumeMusicStream(current_music);
    }
}

void SoundController::updateMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        UpdateMusicStream(current_music);
    }
}

void SoundController::setMusicVolume(float volume) {
    music_volume = volume;
    if (current_music.stream.buffer != nullptr) {
        SetMusicVolume(current_music, music_volume);
    }
}
