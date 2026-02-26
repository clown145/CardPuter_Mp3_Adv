#include "../include/file_manager.hpp"
#include "../include/config.hpp"
#include <SD.h>
#include "M5Cardputer.h"
#include <ESP32Time.h>
#include <cstdio>

namespace FileManager {

namespace {

bool isSupportedAudioFile(const String& fullPath) {
  String lower = fullPath;
  lower.toLowerCase();
  int dot = lower.lastIndexOf('.');
  if (dot < 0) return false;
  String ext = lower.substring(dot + 1);
  return ext == "mp3" || ext == "wav";
}

String normalizeDir(const char* dirname) {
  String dir = String(dirname ? dirname : "/");
  if (!dir.startsWith("/")) dir = String("/") + dir;
  if (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);
  return dir;
}

String buildEntryPath(const String& baseDir, const char* entryName) {
  String name = String(entryName ? entryName : "");
  if (name.startsWith("/")) return name;
  if (baseDir == "/") return String("/") + name;
  return baseDir + "/" + name;
}

void scanDirectoryToIndex(fs::FS& fs,
                          const String& dir,
                          uint8_t levels,
                          File& indexFile,
                          int& songCount) {
  if (songCount >= MAX_LIBRARY_FILES) return;

  File root = fs.open(dir.c_str());
  if (!root || !root.isDirectory()) {
    LOG_PRINTF("scan skip (not directory): %s\n", dir.c_str());
    return;
  }

  File entry = root.openNextFile();
  while (entry && songCount < MAX_LIBRARY_FILES) {
    String fullPath = buildEntryPath(dir, entry.name());

    if (entry.isDirectory()) {
      if (levels > 0) {
        scanDirectoryToIndex(fs, fullPath, levels - 1, indexFile, songCount);
      }
    } else {
      if (isSupportedAudioFile(fullPath)) {
        indexFile.println(fullPath);
        songCount++;
      }
    }

    entry = root.openNextFile();
  }
}

void rebuildQueueFromLibrary(AppState& appState) {
  appState.fileCount = appState.libraryCount;
  for (int i = 0; i < appState.libraryCount; ++i) {
    appState.playbackQueue[i] = static_cast<uint16_t>(i);
  }

  if (appState.fileCount <= 0) {
    appState.currentSelectedIndex = 0;
    appState.currentPlayingIndex = 0;
    return;
  }

  if (appState.currentSelectedIndex < 0 || appState.currentSelectedIndex >= appState.fileCount) {
    appState.currentSelectedIndex = 0;
  }
  if (appState.currentPlayingIndex < 0 || appState.currentPlayingIndex >= appState.fileCount) {
    appState.currentPlayingIndex = appState.currentSelectedIndex;
  }
}

bool readPathBySongIndex(fs::FS& fs, AppState& appState, int songIndex, String& outPath) {
  if (songIndex < 0 || songIndex >= appState.libraryCount) return false;

  for (int i = 0; i < FILE_PATH_CACHE_SIZE; ++i) {
    if (appState.pathCacheIndices[i] == songIndex) {
      outPath = appState.pathCacheValues[i];
      return outPath.length() > 0;
    }
  }

  File indexFile = fs.open(LIBRARY_INDEX_PATH, FILE_READ);
  if (!indexFile) {
    LOG_PRINTF("Failed to open index file: %s\n", LIBRARY_INDEX_PATH);
    return false;
  }

  if (!indexFile.seek(appState.libraryOffsets[songIndex])) {
    LOG_PRINTF("Failed to seek index offset for song %d\n", songIndex);
    indexFile.close();
    return false;
  }

  String line = indexFile.readStringUntil('\n');
  indexFile.close();
  line.trim();
  if (line.length() == 0) return false;

  int slot = appState.pathCacheWritePos % FILE_PATH_CACHE_SIZE;
  appState.pathCacheIndices[slot] = songIndex;
  appState.pathCacheValues[slot] = line;
  appState.pathCacheWritePos = (slot + 1) % FILE_PATH_CACHE_SIZE;

  outPath = line;
  return true;
}

int findQueueIndexByPath(fs::FS& fs, AppState& appState, const String& targetPath) {
  if (targetPath.length() == 0) return -1;

  String current;
  for (int q = 0; q < appState.fileCount; ++q) {
    if (getPathByQueueIndex(fs, appState, q, current) && current == targetPath) {
      return q;
    }
  }
  return -1;
}

}  // namespace

void listFiles(fs::FS& fs, const char* dirname, uint8_t levels, AppState& appState) {
  (void)rebuildLibraryIndex(fs, dirname, levels, appState);
}

bool rebuildLibraryIndex(fs::FS& fs, const char* dirname, uint8_t levels, AppState& appState) {
  String dir = normalizeDir(dirname);
  LOG_PRINTF("Rebuilding library index from: %s\n", dir.c_str());

  if (!fs.exists(MUSIC_DIR)) {
    fs.mkdir(MUSIC_DIR);
  }

  if (fs.exists(LIBRARY_INDEX_PATH)) {
    fs.remove(LIBRARY_INDEX_PATH);
  }

  File indexFile = fs.open(LIBRARY_INDEX_PATH, FILE_WRITE);
  if (!indexFile) {
    LOG_PRINTF("Failed to create index file: %s\n", LIBRARY_INDEX_PATH);
    return false;
  }

  int songCount = 0;
  scanDirectoryToIndex(fs, dir, levels, indexFile, songCount);
  indexFile.close();

  LOG_PRINTF("Index build finished: %d songs\n", songCount);
  return loadLibraryIndex(fs, appState);
}

bool loadLibraryIndex(fs::FS& fs, AppState& appState) {
  appState.resetLibraryState();

  File indexFile = fs.open(LIBRARY_INDEX_PATH, FILE_READ);
  if (!indexFile) {
    LOG_PRINTF("Index not found: %s\n", LIBRARY_INDEX_PATH);
    return false;
  }

  while (indexFile.available() && appState.libraryCount < MAX_LIBRARY_FILES) {
    uint32_t offset = static_cast<uint32_t>(indexFile.position());
    String line = indexFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    appState.libraryOffsets[appState.libraryCount] = offset;
    appState.libraryCount++;
  }
  indexFile.close();

  rebuildQueueFromLibrary(appState);
  appState.resetPathCache();

  LOG_PRINTF("Loaded index: libraryCount=%d queueSize=%d\n", appState.libraryCount, appState.fileCount);
  if (appState.libraryCount >= MAX_LIBRARY_FILES) {
    LOG_PRINTF("WARNING: reached MAX_LIBRARY_FILES=%d\n", MAX_LIBRARY_FILES);
  }

  return appState.fileCount > 0;
}

bool getPathByQueueIndex(fs::FS& fs, AppState& appState, int queueIndex, String& outPath) {
  if (queueIndex < 0 || queueIndex >= appState.fileCount) return false;

  int songIndex = static_cast<int>(appState.playbackQueue[queueIndex]);
  return readPathBySongIndex(fs, appState, songIndex, outPath);
}

void deleteCurrentFile(fs::FS& fs, AppState& appState, const Callbacks& callbacks) {
  if (appState.fileCount == 0 || appState.currentSelectedIndex < 0 || appState.currentSelectedIndex >= appState.fileCount) {
    LOG_PRINTLN("No file to delete");
    return;
  }

  const int deletedQueueIndex = appState.currentSelectedIndex;
  String fileToDelete;
  if (!getPathByQueueIndex(fs, appState, deletedQueueIndex, fileToDelete)) {
    LOG_PRINTLN("Failed to resolve selected file path");
    return;
  }

  bool wasPlaying = (appState.isPlaying && !appState.stopped);
  bool deletingPlayingSong = (deletedQueueIndex == appState.currentPlayingIndex);

  String playingPath;
  if (!deletingPlayingSong) {
    (void)getPathByQueueIndex(fs, appState, appState.currentPlayingIndex, playingPath);
  }

  LOG_PRINTF("Attempting to delete: %s (queue index %d)\n", fileToDelete.c_str(), deletedQueueIndex);
  if (!fs.remove(fileToDelete)) {
    LOG_PRINTF("Failed to delete file: %s\n", fileToDelete.c_str());
    return;
  }

  LOG_PRINTF("File deleted successfully: %s\n", fileToDelete.c_str());

  if (!rebuildLibraryIndex(fs, MUSIC_DIR, LIBRARY_SCAN_MAX_DEPTH, appState)) {
    LOG_PRINTLN("Rebuild index after delete failed");
  }
  if (appState.fileCount == 0) {
    (void)rebuildLibraryIndex(fs, "/", LIBRARY_SCAN_MAX_DEPTH, appState);
  }

  if (appState.fileCount <= 0) {
    appState.isPlaying = false;
    appState.stopped = true;
    appState.currentSelectedIndex = 0;
    appState.currentPlayingIndex = 0;
    LOG_PRINTLN("No more files available");
    return;
  }

  int newPlayingIndex = 0;
  if (!deletingPlayingSong && playingPath.length() > 0) {
    int found = findQueueIndexByPath(fs, appState, playingPath);
    if (found >= 0) {
      newPlayingIndex = found;
    }
  } else {
    newPlayingIndex = appState.currentSelectedIndex;
    if (newPlayingIndex >= appState.fileCount) newPlayingIndex = appState.fileCount - 1;
    if (newPlayingIndex < 0) newPlayingIndex = 0;
  }

  appState.currentPlayingIndex = newPlayingIndex;
  if (deletingPlayingSong) {
    appState.currentSelectedIndex = newPlayingIndex;
  } else {
    int selectedAfterDelete = deletedQueueIndex;
    if (selectedAfterDelete >= appState.fileCount) selectedAfterDelete = appState.fileCount - 1;
    if (selectedAfterDelete < 0) selectedAfterDelete = 0;
    appState.currentSelectedIndex = selectedAfterDelete;
  }

  if (deletingPlayingSong) {
    if (callbacks.resetClock) callbacks.resetClock();
    appState.nextS = 1;
    if (wasPlaying) {
      appState.isPlaying = true;
      appState.stopped = false;
    }
  }

  if (callbacks.onFileDeleted) {
    callbacks.onFileDeleted(deletedQueueIndex, appState.currentPlayingIndex);
  }
}

void captureScreenshot(fs::FS& fs, M5Canvas& sprite, ESP32Time& rtc) {
  // Create /screen directory if it doesn't exist
  if (!fs.exists(SCREEN_DIR)) {
    fs.mkdir(SCREEN_DIR);
    LOG_PRINTLN("Created /screen directory");
  }
  
  // Generate filename with timestamp (optimized: use char buffer instead of String concatenation)
  char filename[64];
  String timestamp = rtc.getTime("%Y%m%d_%H%M%S");
  snprintf(filename, sizeof(filename), "%s/screenshot_%s.bmp", SCREEN_DIR, timestamp.c_str());
  
  // Open file for writing
  File file = fs.open(filename, FILE_WRITE);
  if (!file) {
    LOG_PRINTF("Failed to create screenshot file: %s\n", filename);
    return;
  }
  
  // BMP header (24-bit, no compression)
  uint8_t bmpHeader[54] = {
    0x42, 0x4D,  // 'BM'
    0x00, 0x00, 0x00, 0x00,  // File size (will be filled later)
    0x00, 0x00, 0x00, 0x00,  // Reserved
    0x36, 0x00, 0x00, 0x00,  // Offset to pixel data (54 bytes)
    0x28, 0x00, 0x00, 0x00,  // DIB header size (40 bytes)
    0x00, 0x00, 0x00, 0x00,  // Width (will be filled)
    0x00, 0x00, 0x00, 0x00,  // Height (will be filled, positive = bottom-up)
    0x01, 0x00,  // Planes (1)
    0x18, 0x00,  // Bits per pixel (24)
    0x00, 0x00, 0x00, 0x00,  // Compression (none)
    0x00, 0x00, 0x00, 0x00,  // Image size (0 for uncompressed)
    0x00, 0x00, 0x00, 0x00,  // X pixels per meter
    0x00, 0x00, 0x00, 0x00,  // Y pixels per meter
    0x00, 0x00, 0x00, 0x00,  // Colors used
    0x00, 0x00, 0x00, 0x00   // Important colors
  };
  
  // Fill width and height (little-endian)
  bmpHeader[18] = SCREEN_WIDTH & 0xFF;
  bmpHeader[19] = (SCREEN_WIDTH >> 8) & 0xFF;
  bmpHeader[20] = (SCREEN_WIDTH >> 16) & 0xFF;
  bmpHeader[21] = (SCREEN_WIDTH >> 24) & 0xFF;
  bmpHeader[22] = SCREEN_HEIGHT & 0xFF;
  bmpHeader[23] = (SCREEN_HEIGHT >> 8) & 0xFF;
  bmpHeader[24] = (SCREEN_HEIGHT >> 16) & 0xFF;
  bmpHeader[25] = (SCREEN_HEIGHT >> 24) & 0xFF;
  
  // Calculate row size (must be multiple of 4)
  int rowSize = ((SCREEN_WIDTH * 3 + 3) / 4) * 4;
  int imageSize = rowSize * SCREEN_HEIGHT;
  int fileSize = 54 + imageSize;
  
  // Fill file size (little-endian)
  bmpHeader[2] = fileSize & 0xFF;
  bmpHeader[3] = (fileSize >> 8) & 0xFF;
  bmpHeader[4] = (fileSize >> 16) & 0xFF;
  bmpHeader[5] = (fileSize >> 24) & 0xFF;
  
  // Write header
  file.write(bmpHeader, 54);
  
  // Read pixels from sprite and write to file (BMP is bottom-up, so start from bottom row)
  uint8_t rowBuffer[rowSize];
  for (int y = SCREEN_HEIGHT - 1; y >= 0; y--) {
    int bufIdx = 0;
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      // Read pixel from sprite (RGB565)
      // readPixel already returns standard RGB565 format (byte-swapped internally)
      uint16_t pixel = sprite.readPixel(x, y);
      // Extract R, G, B from RGB565 (5-6-5 bits)
      // RGB565 format: RRRRR GGGGGG BBBBB (bits 15-11, 10-5, 4-0)
      uint8_t r5 = (pixel >> 11) & 0x1F;
      uint8_t g6 = (pixel >> 5) & 0x3F;
      uint8_t b5 = pixel & 0x1F;
      // Expand to 8 bits (scale 5-bit to 8-bit, 6-bit to 8-bit)
      uint8_t r = (r5 << 3) | (r5 >> 2);
      uint8_t g = (g6 << 2) | (g6 >> 4);
      uint8_t b = (b5 << 3) | (b5 >> 2);
      // Write as BGR (BMP format uses BGR byte order)
      rowBuffer[bufIdx++] = b;
      rowBuffer[bufIdx++] = g;
      rowBuffer[bufIdx++] = r;
    }
    // Pad row to multiple of 4 bytes
    while (bufIdx < rowSize) {
      rowBuffer[bufIdx++] = 0;
    }
    file.write(rowBuffer, rowSize);
  }
  
  file.close();
  LOG_PRINTF("Screenshot saved: %s\n", filename);
}

}  // namespace FileManager
