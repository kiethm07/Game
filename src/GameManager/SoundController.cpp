#include <GameManager/SoundController.h>
#include <Rendering/AssetManager.h>

SoundController::SoundController(const AssetManager& asset_manager) 
    : asset_manager(asset_manager) {}

void SoundController::playSFX(AssetID id) {
    Sound sound = asset_manager.getSound(id);
    if (sound.stream.buffer != nullptr) {
        float pitch = GetRandomValue(95, 105) / 100.0f;
        float volume_variance = GetRandomValue(90, 100) / 100.0f;
        
        SetSoundPitch(sound, pitch);
        SetSoundVolume(sound, sfx_volume * volume_variance);
        PlaySound(sound);
    }
}

void SoundController::stopSFX(AssetID id) {
    Sound sound = asset_manager.getSound(id);
    if (sound.stream.buffer != nullptr) {
        StopSound(sound);
    }
}

bool SoundController::isSFXPlaying(AssetID id) const {
    Sound sound = asset_manager.getSound(id);
    if (sound.stream.buffer != nullptr) {
        return IsSoundPlaying(sound);
    }
    return false;
}

void SoundController::setSFXVolume(float volume) {
    sfx_volume = volume;
}

void SoundController::playMusic(AssetID id) {
    if (is_music_playing && has_current_music && current_music_id == id) {
        return;
    }
    Music music = asset_manager.getMusic(id);
    if (music.stream.buffer != nullptr) {
        if (is_music_playing && current_music.stream.buffer != nullptr) {
            if (previous_music.stream.buffer != nullptr) {
                StopMusicStream(previous_music);
            }
            previous_music = current_music;
            previous_volume_scale = current_volume_scale;
        } else {
            previous_volume_scale = 0.0f;
        }

        current_volume_scale = 0.0f;
        is_fading = true;

        current_music = music;
        current_music_id = id;
        has_current_music = true;
        SetMusicVolume(current_music, 0.0f);
        PlayMusicStream(current_music);
        is_music_playing = true;
    }
}

void SoundController::stopMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        StopMusicStream(current_music);
    }
    if (previous_music.stream.buffer != nullptr) {
        StopMusicStream(previous_music);
        previous_music = Music{ 0 };
    }
    is_music_playing = false;
    has_current_music = false;
    is_fading = false;
    current_volume_scale = 1.0f;
    previous_volume_scale = 0.0f;
}

void SoundController::pauseMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        PauseMusicStream(current_music);
    }
    if (previous_music.stream.buffer != nullptr) {
        PauseMusicStream(previous_music);
    }
}

void SoundController::resumeMusic() {
    if (is_music_playing && current_music.stream.buffer != nullptr) {
        ResumeMusicStream(current_music);
    }
    if (previous_music.stream.buffer != nullptr) {
        ResumeMusicStream(previous_music);
    }
}

void SoundController::updateMusic() {
    float dt = GetFrameTime();
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 0.0166f;
    }

    if (is_fading) {
        float fade_step = dt / FADE_TIME;
        current_volume_scale += fade_step;
        if (current_volume_scale >= 1.0f) {
            current_volume_scale = 1.0f;
        }

        previous_volume_scale -= fade_step;
        if (previous_volume_scale <= 0.0f) {
            previous_volume_scale = 0.0f;
        }

        if (current_volume_scale >= 1.0f && previous_volume_scale <= 0.0f) {
            is_fading = false;
            if (previous_music.stream.buffer != nullptr) {
                StopMusicStream(previous_music);
                previous_music = Music{ 0 };
            }
        }
    }

    if (is_music_playing && current_music.stream.buffer != nullptr) {
        UpdateMusicStream(current_music);
        SetMusicVolume(current_music, music_volume * current_volume_scale);
    }

    if (previous_music.stream.buffer != nullptr) {
        UpdateMusicStream(previous_music);
        SetMusicVolume(previous_music, music_volume * previous_volume_scale);
    }
}

void SoundController::setMusicVolume(float volume) {
    music_volume = volume;
    if (current_music.stream.buffer != nullptr) {
        SetMusicVolume(current_music, music_volume * current_volume_scale);
    }
    if (previous_music.stream.buffer != nullptr) {
        SetMusicVolume(previous_music, music_volume * previous_volume_scale);
    }
}
