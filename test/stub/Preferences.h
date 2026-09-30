#pragma once
// Host stub: an in-memory NVS, shared by every Preferences in the process
// (as the real one is shared by everything on the board), so high-score
// tables and settings persist from one game instance to the next within a
// run, and every run starts empty. g_prefsStore() is there for tests to
// inspect or clear.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

inline std::map<std::string, std::vector<uint8_t>>& g_prefsStore() {
    static std::map<std::string, std::vector<uint8_t>> store;
    return store;
}

class Preferences {
public:
    bool begin(const char* ns, bool readOnly) { _ns = ns; _ro = readOnly; return true; }
    void end() {}

    size_t putBytes(const char* k, const void* v, size_t n) {
        if (_ro) return 0;
        auto* b = (const uint8_t*)v;
        g_prefsStore()[path(k)] = std::vector<uint8_t>(b, b + n);
        return n;
    }
    size_t getBytesLength(const char* k) {
        auto it = g_prefsStore().find(path(k));
        return it == g_prefsStore().end() ? 0 : it->second.size();
    }
    size_t getBytes(const char* k, void* out, size_t max) {
        auto it = g_prefsStore().find(path(k));
        if (it == g_prefsStore().end()) return 0;
        size_t n = it->second.size() < max ? it->second.size() : max;
        memcpy(out, it->second.data(), n);
        return n;
    }

    int    getInt(const char* k, int def)       { return get(k, def); }
    size_t putInt(const char* k, int v)         { return putBytes(k, &v, sizeof(v)); }
    float  getFloat(const char* k, float def)   { return get(k, def); }
    size_t putFloat(const char* k, float v)     { return putBytes(k, &v, sizeof(v)); }
    bool   getBool(const char* k, bool def)     { return get(k, def); }
    size_t putBool(const char* k, bool v)       { return putBytes(k, &v, sizeof(v)); }

private:
    std::string _ns;
    bool _ro = true;
    std::string path(const char* k) const { return _ns + "/" + k; }
    template <typename T> T get(const char* k, T def) {
        T v;
        return getBytes(k, &v, sizeof(v)) == sizeof(v) ? v : def;
    }
};
