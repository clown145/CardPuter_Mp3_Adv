#pragma once

#include <Arduino.h>
#include "app_state.hpp"

namespace NetworkPlayer {

// Ensure Wi-Fi is connected using appState network credentials.
bool ensureWifiConnected(AppState& appState, String& outMessage);

// Send SMS captcha to appState.networkPhone via KuGouMusicApi service.
bool sendCaptcha(AppState& appState, String& outMessage);

// Login with phone + captcha and store returned cookies in appState.networkCookie.
bool loginByCaptcha(AppState& appState, String& outMessage);

// Load tracks for appState.networkPlaylistId into appState.networkTrack* arrays.
bool loadPlaylistTracks(AppState& appState, String& outMessage);

// Resolve stream URL for the specified network track index.
bool resolveTrackUrl(AppState& appState, int trackIndex, String& outUrl, String& outMessage);

}  // namespace NetworkPlayer

