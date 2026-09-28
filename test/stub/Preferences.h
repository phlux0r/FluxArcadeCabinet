#pragma once
// Host stub: no persistence, so every run starts from a zero high score.
class Preferences {
public:
    bool   begin(const char*, bool) { return true; }
    void   end() {}
    int    getInt(const char*, int def) { return def; }
    size_t putInt(const char*, int) { return 4; }
    float  getFloat(const char*, float def) { return def; }
    size_t putFloat(const char*, float) { return 4; }
    bool   getBool(const char*, bool def) { return def; }
    size_t putBool(const char*, bool) { return 1; }
};
