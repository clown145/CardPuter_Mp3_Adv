#pragma once

#include <Arduino.h>
#include <FS.h>
#include "app_state.hpp"
#include "config.hpp"
#include "M5Cardputer.h"  // For M5Canvas type alias
#include <ESP32Time.h>    // For ESP32Time class

namespace FileManager {

// Callbacks for file operations that need external actions
struct Callbacks {
  void (*resetClock)() = nullptr;
  void (*onFileDeleted)(int deletedIndex, int newPlayingIndex) = nullptr;
};

// Backward-compatible entry: rebuild index from dirname and load playback queue
void listFiles(fs::FS& fs, const char* dirname, uint8_t levels, AppState& appState);

// Build / reload library index from a directory tree and rebuild playback queue
bool rebuildLibraryIndex(fs::FS& fs, const char* dirname, uint8_t levels, AppState& appState);

// Load existing library index into memory offsets and rebuild playback queue
bool loadLibraryIndex(fs::FS& fs, AppState& appState);

// Read full file path from current playback queue index
bool getPathByQueueIndex(fs::FS& fs, AppState& appState, int queueIndex, String& outPath);

// Build playback queue from a target directory (recursive)
bool buildQueueForDirectory(fs::FS& fs, AppState& appState, const char* dirname, int preferredSongIndex = -1);

// Build folder browser entries (immediate children only)
bool buildBrowserEntries(fs::FS& fs, AppState& appState, const char* dirname);

// Delete currently selected file from SD card and update appState
// Handles index adjustments, playback state, and triggers callbacks
void deleteCurrentFile(fs::FS& fs, AppState& appState, const Callbacks& callbacks);

// Capture current screen content and save as BMP to SD card
// Creates /screen directory if it doesn't exist
void captureScreenshot(fs::FS& fs, M5Canvas& sprite, ESP32Time& rtc);

}  // namespace FileManager
