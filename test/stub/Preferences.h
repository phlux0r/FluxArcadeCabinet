#pragma once
// Host stub: no persistence, so every run starts from a zero high score.
class Preferences {
public:
    bool   begin(const char*, bool) { return true; }
    void   end() {}
    int    getInt(const char*, int def) { return def; }
    size_t putInt(const char*, int) { return 4; }
};
