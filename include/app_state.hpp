#pragma once

#include <Arduino.h>
#include "config.hpp"

// Centralized application state
// Step 3: Aggregate scattered global variables into a single structure

struct AppState {
  // Playback state
  int currentSelectedIndex = 0;      // n
  int currentPlayingIndex = 0;
  int volume = 10;                    // 0..21
  int brightnessIndex = 2;             // bri, 0..4
  bool isPlaying = true;
  bool stopped = false;               // stoped (keeping original spelling for compatibility)
  PlaybackMode playMode = PlaybackMode::Sequential;
  
  // UI state
  bool screenOff = false;
  int savedBrightness = 2;
  bool showDeleteDialog = false;
  bool showID3Page = false;
  
  // Battery and time
  int batteryPercent = 0;
  unsigned long lastBatteryUpdate = 0;
  String cachedTimeStr = "";
  unsigned long lastTimeUpdate = 0;
  
  // Spectrum graph
  unsigned long lastGraphUpdate = 0;
  int graphSpeed = 0;
  int graphBars[14] = {0};
  
  // List scrolling
  int lastSelectedIndex = -1;
  unsigned long selectedTime = 0;
  int selectedScrollPos = 8;
  
  // Audio info cache
  String cachedAudioInfo = "";
  unsigned long lastAudioInfoUpdate = 0;
  
  // ID3 metadata
  String id3Title = "";
  String id3Artist = "";
  String id3Album = "";
  String id3Year = "";
  String id3ContentType = "";
  
  // ID3 cover (streaming)
  size_t id3CoverPos = 0;
  size_t id3CoverLen = 0;
  uint8_t* id3CoverBuf = nullptr;
  size_t id3CoverSize = 0;
  
  // ID3 album text scrolling
  int id3AlbumScrollPos = 0;
  unsigned long id3AlbumSelectTime = 0;
  
  // Track switching
  int nextS = 0;  // Request to switch tracks
  bool volUp = false;
  
  // Indexed library + playback queue
  uint32_t libraryOffsets[MAX_LIBRARY_FILES] = {0};   // Song index -> offset in LIBRARY_INDEX_PATH
  uint16_t playbackQueue[MAX_LIBRARY_FILES] = {0};    // Queue index -> song index
  int libraryCount = 0;
  int fileCount = 0;                                  // Queue size (kept for compatibility)
  int pathCacheIndices[FILE_PATH_CACHE_SIZE] = {0};
  String pathCacheValues[FILE_PATH_CACHE_SIZE];
  int pathCacheWritePos = 0;
  String queueDirectory = MUSIC_DIR;                  // Current playback scope

  // Folder browser state
  bool browserMode = false;
  String browserCurrentDir = MUSIC_DIR;
  bool browserEntryIsDir[MAX_BROWSER_ENTRIES] = {0};
  int browserEntrySongIndex[MAX_BROWSER_ENTRIES] = {0};  // -1 for directory entries
  String browserEntryName[MAX_BROWSER_ENTRIES];
  String browserEntryPath[MAX_BROWSER_ENTRIES];          // For directories: target dir
  int browserEntryCount = 0;
  
  // Helper methods
  int getBrightness() const {
    return BRIGHTNESS_VALUES[brightnessIndex];
  }

  void resetPathCache() {
    pathCacheWritePos = 0;
    for (int i = 0; i < FILE_PATH_CACHE_SIZE; ++i) {
      pathCacheIndices[i] = -1;
      pathCacheValues[i] = "";
    }
  }

  void resetLibraryState() {
    libraryCount = 0;
    fileCount = 0;
    currentSelectedIndex = 0;
    currentPlayingIndex = 0;
    queueDirectory = MUSIC_DIR;
    for (int i = 0; i < MAX_LIBRARY_FILES; ++i) {
      libraryOffsets[i] = 0;
      playbackQueue[i] = 0;
    }
    resetPathCache();
    resetBrowserEntries();
  }

  void resetBrowserEntries() {
    browserEntryCount = 0;
    browserMode = false;
    browserCurrentDir = MUSIC_DIR;
    for (int i = 0; i < MAX_BROWSER_ENTRIES; ++i) {
      browserEntryIsDir[i] = false;
      browserEntrySongIndex[i] = -1;
      browserEntryName[i] = "";
      browserEntryPath[i] = "";
    }
  }
  
  void resetID3Metadata() {
    id3Title = "";
    id3Artist = "";
    id3Album = "";
    id3Year = "";
    id3ContentType = "";
    id3CoverPos = 0;
    id3CoverLen = 0;
    if (id3CoverBuf) {
      heap_caps_free(id3CoverBuf);
      id3CoverBuf = nullptr;
      id3CoverSize = 0;
    }
  }
};
