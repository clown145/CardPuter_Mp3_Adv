#include "../include/network_player.hpp"
#include "../include/config.hpp"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace NetworkPlayer {

namespace {

constexpr int kHttpRetryCount = 2;

String trimCopy(const String& in) {
  String s = in;
  s.trim();
  return s;
}

String normalizeBaseUrl(const String& in) {
  String base = trimCopy(in);
  if (base.length() > 0 && !base.startsWith("http://") && !base.startsWith("https://")) {
    base = String("http://") + base;
  }
  while (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return base;
}

void upsertCookiePair(String& jar, const String& pair) {
  int eq = pair.indexOf('=');
  if (eq <= 0) return;

  String key = pair.substring(0, eq);
  key.trim();
  String val = pair.substring(eq + 1);
  val.trim();
  if (key.length() == 0) return;

  String keyPrefix = key + "=";
  String lowerKeyPrefix = keyPrefix;
  lowerKeyPrefix.toLowerCase();

  int start = 0;
  while (start < jar.length()) {
    int end = jar.indexOf(';', start);
    if (end < 0) end = jar.length();
    String seg = jar.substring(start, end);
    seg.trim();
    String lowerSeg = seg;
    lowerSeg.toLowerCase();
    if (lowerSeg.startsWith(lowerKeyPrefix)) {
      String before = jar.substring(0, start);
      String after = end < jar.length() ? jar.substring(end + 1) : "";
      before.trim();
      after.trim();
      jar = before;
      if (jar.length() > 0 && after.length() > 0) jar += "; ";
      if (after.length() > 0) jar += after;
      break;
    }
    start = end + 1;
  }

  jar.trim();
  if (jar.length() > 0) jar += "; ";
  jar += key + "=" + val;
}

void mergeSetCookieHeader(String& jar, const String& headerValue) {
  int start = 0;
  while (start <= headerValue.length()) {
    int end = headerValue.indexOf('\n', start);
    String token = end < 0 ? headerValue.substring(start) : headerValue.substring(start, end);
    token.trim();

    int semi = token.indexOf(';');
    String pair = (semi < 0) ? token : token.substring(0, semi);
    int eq = pair.indexOf('=');
    if (eq > 0) {
      upsertCookiePair(jar, pair);
    }

    if (end < 0) break;
    start = end + 1;
  }
}

String extractHostFromUrl(const String& url) {
  int start = 0;
  int scheme = url.indexOf("://");
  if (scheme >= 0) start = scheme + 3;
  int pathPos = url.indexOf('/', start);
  String hostPort = pathPos >= 0 ? url.substring(start, pathPos) : url.substring(start);
  int atPos = hostPort.lastIndexOf('@');
  if (atPos >= 0) hostPort = hostPort.substring(atPos + 1);
  int colonPos = hostPort.indexOf(':');
  return colonPos > 0 ? hostPort.substring(0, colonPos) : hostPort;
}

bool httpGet(const String& url,
             const String& cookieJar,
             String& outBody,
             String& outSetCookie,
             String& outError) {
  outBody = "";
  outSetCookie = "";
  outError = "";

  String host = extractHostFromUrl(url);
  if (host.length() == 0) {
    outError = "Invalid URL host";
    return false;
  }

  IPAddress ip;
  if (!WiFi.hostByName(host.c_str(), ip)) {
    outError = String("DNS failed: ") + host;
    return false;
  }

  bool isHttps = url.startsWith("https://");
  int lastCode = 0;
  String lastHttpError = "";
  for (int attempt = 0; attempt < kHttpRetryCount; ++attempt) {
    HTTPClient http;
    http.setConnectTimeout(NETWORK_HTTP_TIMEOUT_MS);
    http.setTimeout(NETWORK_HTTP_TIMEOUT_MS);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);

    bool beginOk = false;
    WiFiClient client;
    WiFiClientSecure secureClient;
    if (isHttps) {
      secureClient.setInsecure();
      secureClient.setTimeout((NETWORK_HTTP_TIMEOUT_MS + 999) / 1000);
      beginOk = http.begin(secureClient, url);
    } else {
      beginOk = http.begin(client, url);
    }
    if (!beginOk) {
      lastHttpError = "HTTP begin failed";
      delay(120);
      continue;
    }

    const char* headerKeys[] = {"Set-Cookie", "set-cookie"};
    http.collectHeaders(headerKeys, 2);
    if (cookieJar.length() > 0) {
      http.addHeader("Cookie", cookieJar);
    }

    int code = http.GET();
    lastCode = code;
    if (code > 0) {
      outBody = http.getString();
      int headerCount = http.headers();
      for (int i = 0; i < headerCount; ++i) {
        String name = http.headerName(i);
        name.toLowerCase();
        if (name == "set-cookie") {
          if (outSetCookie.length() > 0) outSetCookie += "\n";
          outSetCookie += http.header(i);
        }
      }
      http.end();

      if (code >= 200 && code < 300) {
        return true;
      }
      lastHttpError = String("HTTP status: ") + String(code);
      delay(120);
      continue;
    }

    String detail = HTTPClient::errorToString(code);
    http.end();
    lastHttpError = String("HTTP GET failed: ") + String(code) + " " + detail;
    delay(120);
  }

  if (lastHttpError.length() > 0) {
    outError = lastHttpError;
  } else if (lastCode != 0) {
    outError = String("HTTP request failed: ") + String(lastCode);
  } else {
    outError = "HTTP request failed";
  }
  return false;
}

String getStringFromObject(const JsonObjectConst& obj,
                           const char* key1,
                           const char* key2 = nullptr,
                           const char* key3 = nullptr,
                           const char* key4 = nullptr) {
  const char* keys[4] = {key1, key2, key3, key4};
  for (int i = 0; i < 4; ++i) {
    if (keys[i] == nullptr) continue;
    if (!obj.containsKey(keys[i])) continue;
    JsonVariantConst v = obj[keys[i]];
    if (v.is<const char*>()) return String(v.as<const char*>());
    if (v.is<int>()) return String(v.as<int>());
    if (v.is<unsigned int>()) return String(v.as<unsigned int>());
    if (v.is<long>()) return String(v.as<long>());
    if (v.is<unsigned long>()) return String(v.as<unsigned long>());
    if (v.is<long long>()) return String((long)v.as<long long>());
  }
  return "";
}

bool hasTrackHash(const AppState& appState, const String& hash) {
  for (int i = 0; i < appState.networkTrackCount; ++i) {
    if (appState.networkTrackHash[i] == hash) return true;
  }
  return false;
}

void appendTrackFromObject(const JsonObjectConst& obj, AppState& appState) {
  if (appState.networkTrackCount >= MAX_NETWORK_TRACKS) return;

  String hash = getStringFromObject(obj, "hash", "Hash", "audio_hash", "file_hash");
  hash.trim();
  if (hash.length() == 0) return;
  if (hasTrackHash(appState, hash)) return;

  String title = getStringFromObject(obj, "filename", "songname", "song_name", "name");
  String artist = getStringFromObject(obj, "author_name", "singername", "artist", "singer");
  String albumAudioId = getStringFromObject(obj, "album_audio_id", "mixsongid", "audio_id", "audioid");

  if (title.length() == 0) title = hash;

  int idx = appState.networkTrackCount++;
  appState.networkTrackHash[idx] = hash;
  appState.networkTrackTitle[idx] = title;
  appState.networkTrackArtist[idx] = artist;
  appState.networkTrackAlbumAudioId[idx] = albumAudioId;
}

void walkTracks(const JsonVariantConst& node, AppState& appState) {
  if (appState.networkTrackCount >= MAX_NETWORK_TRACKS) return;

  if (node.is<JsonObjectConst>()) {
    JsonObjectConst obj = node.as<JsonObjectConst>();
    appendTrackFromObject(obj, appState);
    for (JsonPairConst kv : obj) {
      walkTracks(kv.value(), appState);
      if (appState.networkTrackCount >= MAX_NETWORK_TRACKS) return;
    }
  } else if (node.is<JsonArrayConst>()) {
    JsonArrayConst arr = node.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      walkTracks(v, appState);
      if (appState.networkTrackCount >= MAX_NETWORK_TRACKS) return;
    }
  }
}

bool looksLikeAudioUrl(const String& url) {
  if (!url.startsWith("http://") && !url.startsWith("https://")) return false;
  String lower = url;
  lower.toLowerCase();
  return lower.indexOf(".mp3") >= 0 || lower.indexOf(".flac") >= 0 || lower.indexOf(".m4a") >= 0 ||
         lower.indexOf(".wav") >= 0 || lower.indexOf(".aac") >= 0;
}

void collectUrls(const JsonVariantConst& node, String& bestAudioUrl, String& fallbackUrl) {
  if (node.is<const char*>()) {
    String s = String(node.as<const char*>());
    if (s.startsWith("http://") || s.startsWith("https://")) {
      if (fallbackUrl.length() == 0) fallbackUrl = s;
      if (bestAudioUrl.length() == 0 && looksLikeAudioUrl(s)) bestAudioUrl = s;
    }
    return;
  }

  if (node.is<JsonObjectConst>()) {
    for (JsonPairConst kv : node.as<JsonObjectConst>()) {
      collectUrls(kv.value(), bestAudioUrl, fallbackUrl);
      if (bestAudioUrl.length() > 0) return;
    }
    return;
  }

  if (node.is<JsonArrayConst>()) {
    for (JsonVariantConst v : node.as<JsonArrayConst>()) {
      collectUrls(v, bestAudioUrl, fallbackUrl);
      if (bestAudioUrl.length() > 0) return;
    }
  }
}

String getMessageField(const JsonObjectConst& obj) {
  static const char* kMessageKeys[] = {"msg", "message", "error", "err_msg", "errmsg"};
  for (const char* key : kMessageKeys) {
    if (!obj.containsKey(key)) continue;
    JsonVariantConst v = obj[key];
    if (v.is<const char*>()) return String(v.as<const char*>());
    if (v.is<int>()) return String(v.as<int>());
    if (v.is<long>()) return String(v.as<long>());
  }
  return "";
}

bool variantToLong(const JsonVariantConst& v, long& out) {
  if (v.is<int>()) {
    out = v.as<int>();
    return true;
  }
  if (v.is<long>()) {
    out = v.as<long>();
    return true;
  }
  if (v.is<unsigned int>()) {
    out = (long)v.as<unsigned int>();
    return true;
  }
  if (v.is<unsigned long>()) {
    out = (long)v.as<unsigned long>();
    return true;
  }
  if (v.is<const char*>()) {
    out = atol(v.as<const char*>());
    return true;
  }
  return false;
}

bool isApiResponseOk(const JsonVariantConst& root, String& outMessage) {
  outMessage = "";
  if (!root.is<JsonObjectConst>()) {
    return true;
  }

  JsonObjectConst obj = root.as<JsonObjectConst>();
  outMessage = getMessageField(obj);

  if (obj.containsKey("status")) {
    long status = 0;
    if (variantToLong(obj["status"], status)) {
      return status == 1 || status == 200;
    }
  }

  if (obj.containsKey("success") && obj["success"].is<bool>()) {
    return obj["success"].as<bool>();
  }

  if (obj.containsKey("error_code")) {
    long code = 0;
    if (variantToLong(obj["error_code"], code)) {
      return code == 0;
    }
  }

  if (obj.containsKey("code")) {
    long code = 0;
    if (variantToLong(obj["code"], code)) {
      return code == 0 || code == 1 || code == 200;
    }
  }

  return true;
}

}  // namespace

bool ensureWifiConnected(AppState& appState, String& outMessage) {
  outMessage = "";
  if (WiFi.status() == WL_CONNECTED) {
    outMessage = String("WiFi OK: ") + WiFi.localIP().toString();
    return true;
  }
  String ssid = trimCopy(appState.networkWifiSsid);
  if (ssid.length() == 0) {
    outMessage = "SSID empty";
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), appState.networkWifiPassword.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
  }

  if (WiFi.status() != WL_CONNECTED) {
    outMessage = "WiFi connect timeout";
    return false;
  }

  outMessage = String("WiFi OK: ") + WiFi.localIP().toString();
  return true;
}

bool sendCaptcha(AppState& appState, String& outMessage) {
  String wifiMsg;
  if (!ensureWifiConnected(appState, wifiMsg)) {
    outMessage = wifiMsg;
    return false;
  }

  String base = normalizeBaseUrl(appState.networkApiBaseUrl);
  if (base.length() == 0) {
    outMessage = "API base URL empty";
    return false;
  }

  String mobile = trimCopy(appState.networkPhone);
  if (mobile.length() == 0) {
    outMessage = "Phone empty";
    return false;
  }

  String url = base + "/captcha/sent?mobile=" + mobile + "&ts=" + String(millis());
  String body;
  String setCookie;
  String err;
  if (!httpGet(url, appState.networkCookie, body, setCookie, err)) {
    outMessage = String("Captcha failed: ") + err;
    return false;
  }

  if (setCookie.length() > 0) {
    mergeSetCookieHeader(appState.networkCookie, setCookie);
  }

  DynamicJsonDocument doc(8192);
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    outMessage = String("Captcha response invalid JSON: ") + jerr.c_str();
    return false;
  }

  String apiMsg;
  if (!isApiResponseOk(doc.as<JsonVariantConst>(), apiMsg)) {
    outMessage = apiMsg.length() > 0 ? apiMsg : String("Captcha API rejected");
    return false;
  }

  outMessage = apiMsg.length() > 0 ? apiMsg : String("Captcha sent");
  return true;
}

bool loginByCaptcha(AppState& appState, String& outMessage) {
  String wifiMsg;
  if (!ensureWifiConnected(appState, wifiMsg)) {
    outMessage = wifiMsg;
    return false;
  }

  String base = normalizeBaseUrl(appState.networkApiBaseUrl);
  if (base.length() == 0) {
    outMessage = "API base URL empty";
    return false;
  }

  String mobile = trimCopy(appState.networkPhone);
  String code = trimCopy(appState.networkCode);
  if (mobile.length() == 0 || code.length() == 0) {
    outMessage = "Phone or code empty";
    return false;
  }

  String url = base + "/login/cellphone?mobile=" + mobile + "&code=" + code + "&ts=" + String(millis());
  String body;
  String setCookie;
  String err;
  if (!httpGet(url, appState.networkCookie, body, setCookie, err)) {
    outMessage = String("Login failed: ") + err;
    return false;
  }

  if (setCookie.length() > 0) {
    mergeSetCookieHeader(appState.networkCookie, setCookie);
  }

  DynamicJsonDocument doc(20000);
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    outMessage = String("Login response invalid JSON: ") + jerr.c_str();
    return false;
  }

  String apiMsg;
  if (!isApiResponseOk(doc.as<JsonVariantConst>(), apiMsg)) {
    outMessage = apiMsg.length() > 0 ? apiMsg : String("Login API rejected");
    return false;
  }

  if (appState.networkCookie.length() == 0) {
    outMessage = "Login done, but cookie missing";
    return false;
  }

  outMessage = apiMsg.length() > 0 ? apiMsg : String("Login OK");
  return true;
}

bool loadPlaylistTracks(AppState& appState, String& outMessage) {
  String wifiMsg;
  if (!ensureWifiConnected(appState, wifiMsg)) {
    outMessage = wifiMsg;
    return false;
  }

  String base = normalizeBaseUrl(appState.networkApiBaseUrl);
  if (base.length() == 0) {
    outMessage = "API base URL empty";
    return false;
  }

  String playlistId = trimCopy(appState.networkPlaylistId);
  if (playlistId.length() == 0) {
    outMessage = "Playlist ID empty";
    return false;
  }

  String url = base + "/playlist/track/all/new?listid=" + playlistId + "&page=1&pagesize=" +
               String(MAX_NETWORK_TRACKS) + "&ts=" + String(millis());
  String body;
  String setCookie;
  String err;
  if (!httpGet(url, appState.networkCookie, body, setCookie, err)) {
    outMessage = String("Load playlist failed: ") + err;
    return false;
  }

  if (setCookie.length() > 0) {
    mergeSetCookieHeader(appState.networkCookie, setCookie);
  }

  DynamicJsonDocument doc(140000);
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    outMessage = String("JSON parse failed: ") + jerr.c_str();
    return false;
  }

  String apiMsg;
  if (!isApiResponseOk(doc.as<JsonVariantConst>(), apiMsg)) {
    outMessage = apiMsg.length() > 0 ? apiMsg : String("Playlist API rejected");
    return false;
  }

  appState.networkTrackCount = 0;
  for (int i = 0; i < MAX_NETWORK_TRACKS; ++i) {
    appState.networkTrackTitle[i] = "";
    appState.networkTrackArtist[i] = "";
    appState.networkTrackHash[i] = "";
    appState.networkTrackAlbumAudioId[i] = "";
  }

  walkTracks(doc.as<JsonVariantConst>(), appState);
  if (appState.networkTrackCount <= 0) {
    outMessage = "Playlist has no playable track hash";
    return false;
  }

  outMessage = String("Playlist loaded: ") + String(appState.networkTrackCount);
  return true;
}

bool resolveTrackUrl(AppState& appState, int trackIndex, String& outUrl, String& outMessage) {
  outUrl = "";

  String wifiMsg;
  if (!ensureWifiConnected(appState, wifiMsg)) {
    outMessage = wifiMsg;
    return false;
  }

  if (trackIndex < 0 || trackIndex >= appState.networkTrackCount) {
    outMessage = "Track index out of range";
    return false;
  }

  String base = normalizeBaseUrl(appState.networkApiBaseUrl);
  if (base.length() == 0) {
    outMessage = "API base URL empty";
    return false;
  }

  String hash = trimCopy(appState.networkTrackHash[trackIndex]);
  if (hash.length() == 0) {
    outMessage = "Track hash empty";
    return false;
  }

  String url = base + "/song/url?hash=" + hash + "&quality=128&free_part=1&ts=" + String(millis());
  String albumAudioId = trimCopy(appState.networkTrackAlbumAudioId[trackIndex]);
  if (albumAudioId.length() > 0) {
    url += "&album_audio_id=" + albumAudioId;
  }

  String body;
  String setCookie;
  String err;
  if (!httpGet(url, appState.networkCookie, body, setCookie, err)) {
    outMessage = String("Resolve URL failed: ") + err;
    return false;
  }

  if (setCookie.length() > 0) {
    mergeSetCookieHeader(appState.networkCookie, setCookie);
  }

  DynamicJsonDocument doc(70000);
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    outMessage = String("JSON parse failed: ") + jerr.c_str();
    return false;
  }

  String apiMsg;
  if (!isApiResponseOk(doc.as<JsonVariantConst>(), apiMsg)) {
    outMessage = apiMsg.length() > 0 ? apiMsg : String("Resolve API rejected");
    return false;
  }

  String bestAudioUrl;
  String fallbackUrl;
  collectUrls(doc.as<JsonVariantConst>(), bestAudioUrl, fallbackUrl);
  outUrl = bestAudioUrl.length() > 0 ? bestAudioUrl : fallbackUrl;

  if (outUrl.length() == 0) {
    outMessage = apiMsg.length() > 0 ? apiMsg : String("No stream URL in response");
    return false;
  }

  outMessage = "Stream URL resolved";
  return true;
}

}  // namespace NetworkPlayer
