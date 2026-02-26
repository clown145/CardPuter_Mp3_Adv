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

String extractDisplayName(const String& fullPath) {
  String fileName = fullPath;
  int lastSlash = fileName.lastIndexOf('/');
  if (lastSlash >= 0) {
    fileName = fileName.substring(lastSlash + 1);
  }
  int lastDot = fileName.lastIndexOf('.');
  if (lastDot >= 0) {
    fileName = fileName.substring(0, lastDot);
  }
  return fileName;
}

String extractBaseName(const String& fullPath) {
  int lastSlash = fullPath.lastIndexOf('/');
  if (lastSlash < 0) return fullPath;
  return fullPath.substring(lastSlash + 1);
}

String getParentDir(const String& dir) {
  if (dir == "/") return "/";
  int lastSlash = dir.lastIndexOf('/');
  if (lastSlash <= 0) return "/";
  return dir.substring(0, lastSlash);
}

bool pathInDirectoryRecursive(const String& path, const String& dir) {
  if (dir == "/") return path.startsWith("/");
  String prefix = dir + "/";
  return path.startsWith(prefix);
}

bool addBrowserDirectoryEntry(AppState& appState, const String& dirName, const String& dirPath) {
  for (int i = 0; i < appState.browserEntryCount; ++i) {
    if (appState.browserEntryIsDir[i] && appState.browserEntryPath[i] == dirPath) return true;
  }
  if (appState.browserEntryCount >= MAX_BROWSER_ENTRIES) return false;

  int idx = appState.browserEntryCount++;
  appState.browserEntryIsDir[idx] = true;
  appState.browserEntrySongIndex[idx] = -1;
  appState.browserEntryName[idx] = dirName;
  appState.browserEntryPath[idx] = dirPath;
  return true;
}

bool addBrowserSongEntry(AppState& appState, int songIndex, const String& fullPath) {
  if (appState.browserEntryCount >= MAX_BROWSER_ENTRIES) return false;
  int idx = appState.browserEntryCount++;
  appState.browserEntryIsDir[idx] = false;
  appState.browserEntrySongIndex[idx] = songIndex;
  appState.browserEntryName[idx] = extractDisplayName(fullPath);
  appState.browserEntryPath[idx] = fullPath;
  return true;
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
  appState.queueDirectory = MUSIC_DIR;

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

int findSongIndexByPath(fs::FS& fs, AppState& appState, const String& targetPath) {
  if (targetPath.length() == 0) return -1;

  String current;
  for (int i = 0; i < appState.libraryCount; ++i) {
    if (readPathBySongIndex(fs, appState, i, current) && current == targetPath) {
      return i;
    }
  }
  return -1;
}

int findQueueIndexByPathInternal(fs::FS& fs, AppState& appState, const String& targetPath) {
  if (targetPath.length() == 0) return -1;

  String current;
  for (int q = 0; q < appState.fileCount; ++q) {
    if (getPathByQueueIndex(fs, appState, q, current) && current == targetPath) {
      return q;
    }
  }
  return -1;
}

bool removePathRecursive(fs::FS& fs, const String& targetPath) {
  if (targetPath.length() == 0 || targetPath == "/") {
    LOG_PRINTLN("Refusing to delete empty path or root");
    return false;
  }

  File node = fs.open(targetPath.c_str());
  if (!node) {
    LOG_PRINTF("deletePathRecursive: open failed: %s\n", targetPath.c_str());
    return false;
  }

  if (node.isDirectory()) {
    File child = node.openNextFile();
    while (child) {
      String childPath = buildEntryPath(targetPath, child.name());
      child.close();
      if (!removePathRecursive(fs, childPath)) {
        node.close();
        return false;
      }
      child = node.openNextFile();
    }
    node.close();
    if (!fs.rmdir(targetPath.c_str())) {
      LOG_PRINTF("deletePathRecursive: failed to remove dir: %s\n", targetPath.c_str());
      return false;
    }
    return true;
  }

  node.close();
  if (!fs.remove(targetPath.c_str())) {
    LOG_PRINTF("deletePathRecursive: failed to remove file: %s\n", targetPath.c_str());
    return false;
  }
  return true;
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

int findQueueIndexByPath(fs::FS& fs, AppState& appState, const String& targetPath) {
  return findQueueIndexByPathInternal(fs, appState, targetPath);
}

bool buildQueueForDirectory(fs::FS& fs, AppState& appState, const char* dirname, int preferredSongIndex) {
  String dir = normalizeDir(dirname);
  File indexFile = fs.open(LIBRARY_INDEX_PATH, FILE_READ);
  if (!indexFile) {
    LOG_PRINTF("buildQueueForDirectory: index not found: %s\n", LIBRARY_INDEX_PATH);
    return false;
  }

  int currentPlayingSongIndex = -1;
  if (appState.currentPlayingIndex >= 0 && appState.currentPlayingIndex < appState.fileCount) {
    currentPlayingSongIndex = static_cast<int>(appState.playbackQueue[appState.currentPlayingIndex]);
  }

  int queueCount = 0;
  int songIndex = 0;
  int preferredQueueIndex = -1;
  int currentPlayingQueueIndex = -1;

  while (indexFile.available() && songIndex < appState.libraryCount && queueCount < MAX_LIBRARY_FILES) {
    String line = indexFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (pathInDirectoryRecursive(line, dir)) {
      appState.playbackQueue[queueCount] = static_cast<uint16_t>(songIndex);
      if (songIndex == preferredSongIndex) preferredQueueIndex = queueCount;
      if (songIndex == currentPlayingSongIndex) currentPlayingQueueIndex = queueCount;
      queueCount++;
    }
    songIndex++;
  }
  indexFile.close();

  appState.fileCount = queueCount;
  appState.queueDirectory = dir;

  if (queueCount <= 0) {
    appState.currentSelectedIndex = 0;
    appState.currentPlayingIndex = 0;
    return false;
  }

  if (preferredQueueIndex >= 0) {
    appState.currentSelectedIndex = preferredQueueIndex;
    appState.currentPlayingIndex = preferredQueueIndex;
  } else if (currentPlayingQueueIndex >= 0) {
    appState.currentSelectedIndex = currentPlayingQueueIndex;
    appState.currentPlayingIndex = currentPlayingQueueIndex;
  } else {
    appState.currentSelectedIndex = 0;
    appState.currentPlayingIndex = 0;
  }
  appState.lastSelectedIndex = -1;
  appState.selectedScrollPos = SCROLL_INITIAL_POS;

  LOG_PRINTF("Queue rebuilt for dir '%s': %d songs\n", dir.c_str(), queueCount);
  return true;
}

bool buildBrowserEntries(fs::FS& fs, AppState& appState, const char* dirname) {
  String dir = normalizeDir(dirname);

  appState.browserEntryCount = 0;
  appState.browserCurrentDir = dir;
  appState.currentSelectedIndex = 0;

  for (int i = 0; i < MAX_BROWSER_ENTRIES; ++i) {
    appState.browserEntryIsDir[i] = false;
    appState.browserEntrySongIndex[i] = -1;
    appState.browserEntryName[i] = "";
    appState.browserEntryPath[i] = "";
  }

  if (dir != "/") {
    String parentDir = getParentDir(dir);
    (void)addBrowserDirectoryEntry(appState, "..", parentDir);
  }

  File root = fs.open(dir.c_str());
  if (!root || !root.isDirectory()) {
    LOG_PRINTF("buildBrowserEntries: not a directory: %s\n", dir.c_str());
    return false;
  }

  File entry = root.openNextFile();
  while (entry && appState.browserEntryCount < MAX_BROWSER_ENTRIES) {
    String entryPath = buildEntryPath(dir, entry.name());
    if (entry.isDirectory()) {
      String dirName = extractBaseName(entryPath);
      if (!addBrowserDirectoryEntry(appState, dirName, entryPath)) {
        break;
      }
    } else if (isSupportedAudioFile(entryPath)) {
      int songIndex = findSongIndexByPath(fs, appState, entryPath);
      if (!addBrowserSongEntry(appState, songIndex, entryPath)) {
        break;
      }
    }
    entry = root.openNextFile();
  }
  root.close();

  LOG_PRINTF("Browser dir '%s': %d entries\n", dir.c_str(), appState.browserEntryCount);
  return true;
}

bool deletePathRecursive(fs::FS& fs, const char* path) {
  String targetPath = normalizeDir(path);
  return removePathRecursive(fs, targetPath);
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

  String queueDirBeforeDelete = appState.queueDirectory;

  if (!rebuildLibraryIndex(fs, MUSIC_DIR, LIBRARY_SCAN_MAX_DEPTH, appState)) {
    LOG_PRINTLN("Rebuild index after delete failed");
  }
  if (appState.fileCount == 0) {
    (void)rebuildLibraryIndex(fs, "/", LIBRARY_SCAN_MAX_DEPTH, appState);
  }

  if (!buildQueueForDirectory(fs, appState, queueDirBeforeDelete.c_str(), -1)) {
    if (!buildQueueForDirectory(fs, appState, MUSIC_DIR, -1)) {
      (void)buildQueueForDirectory(fs, appState, "/", -1);
    }
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
    int found = findQueueIndexByPathInternal(fs, appState, playingPath);
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
