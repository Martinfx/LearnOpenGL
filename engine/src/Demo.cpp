#include "engine/Demo.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>

namespace engine {

namespace {
std::atomic<std::uint32_t>& seedStore()
{
    static std::atomic<std::uint32_t> seed{std::random_device{}()};
    return seed;
}

constexpr char kMagic[8] = {'F', 'P', 'S', 'D', 'E', 'M', 'O', '1'};

template <class T>
void put(std::ostream& out, const T& v)
{
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T>
bool get(std::istream& in, T& v)
{
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(T)));
}
void putString(std::ostream& out, const std::string& s)
{
    put(out, static_cast<std::uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
bool getString(std::istream& in, std::string& s)
{
    std::uint32_t n = 0;
    if (!get(in, n) || n > (1u << 20))
        return false;
    s.resize(n);
    return n == 0 || static_cast<bool>(in.read(s.data(), n));
}
} // namespace

std::uint32_t gameSeed() { return seedStore().load(); }
void setGameSeed(std::uint32_t seed) { seedStore().store(seed); }

std::uint32_t seedFor(const char* stream)
{
    // FNV-1a over the stream name, mixed with the game seed.
    std::uint32_t h = 2166136261u ^ gameSeed();
    for (const char* c = stream; *c; ++c)
        h = (h ^ static_cast<unsigned char>(*c)) * 16777619u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

float Demo::duration() const
{
    float t = 0.0f;
    for (const DemoFrame& f : frames)
        t += f.dt;
    return t;
}

bool saveDemo(const Demo& demo, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        if (error)
            *error = "Cannot write " + file.string();
        return false;
    }
    out.write(kMagic, sizeof(kMagic));
    putString(out, demo.level);
    put(out, demo.seed);
    put(out, static_cast<std::uint32_t>(demo.cvars.size()));
    for (const auto& [name, value] : demo.cvars) {
        putString(out, name);
        putString(out, value);
    }
    put(out, static_cast<std::uint32_t>(demo.frames.size()));
    for (const DemoFrame& f : demo.frames) {
        put(out, f.dt);
        put(out, f.mouseDelta.x);
        put(out, f.mouseDelta.y);
        put(out, f.cursor.x);
        put(out, f.cursor.y);
        put(out, f.scroll);
        put(out, static_cast<std::uint8_t>(std::min<std::size_t>(f.keys.size(), 255)));
        for (std::size_t i = 0; i < f.keys.size() && i < 255; ++i)
            put(out, f.keys[i]);
        put(out, f.player.x);
        put(out, f.player.y);
        put(out, f.player.z);
    }
    if (!out) {
        if (error)
            *error = "Cannot write " + file.string();
        return false;
    }
    return true;
}

bool loadDemo(Demo& demo, const std::filesystem::path& file, std::string* error)
{
    auto fail = [&](const std::string& why) {
        if (error)
            *error = file.string() + ": " + why;
        return false;
    };
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return fail("cannot open");
    char magic[8] = {};
    if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0)
        return fail("not a demo file");
    Demo d;
    std::uint32_t count = 0;
    if (!getString(in, d.level) || !get(in, d.seed) || !get(in, count) || count > 100000)
        return fail("damaged header");
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string name, value;
        if (!getString(in, name) || !getString(in, value))
            return fail("damaged header");
        d.cvars[name] = value;
    }
    if (!get(in, count) || count > 100000000u)
        return fail("damaged frames");
    d.frames.resize(count);
    for (DemoFrame& f : d.frames) {
        std::uint8_t keys = 0;
        if (!get(in, f.dt) || !get(in, f.mouseDelta.x) || !get(in, f.mouseDelta.y) || !get(in, f.cursor.x) ||
            !get(in, f.cursor.y) || !get(in, f.scroll) || !get(in, keys))
            return fail("cut short");
        f.keys.resize(keys);
        for (auto& k : f.keys)
            if (!get(in, k))
                return fail("cut short");
        if (!get(in, f.player.x) || !get(in, f.player.y) || !get(in, f.player.z))
            return fail("cut short");
    }
    demo = std::move(d);
    return true;
}

FrameStats frameStats(const std::vector<float>& frameSeconds)
{
    FrameStats s;
    std::vector<float> t;
    for (float v : frameSeconds)
        if (v > 0.0f)
            t.push_back(v);
    if (t.empty())
        return s;
    s.frames = static_cast<int>(t.size());
    s.seconds = std::accumulate(t.begin(), t.end(), 0.0);
    s.averageFps = s.frames / s.seconds;
    std::sort(t.begin(), t.end(), std::greater<float>()); // slowest first
    const std::size_t worst = std::max<std::size_t>(1, t.size() / 100);
    const double worstSum = std::accumulate(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(worst), 0.0);
    s.low1Fps = static_cast<double>(worst) / worstSum;
    s.worstMs = t.front() * 1000.0;
    s.minFps = 1.0 / t.front();
    s.medianMs = t[t.size() / 2] * 1000.0;
    return s;
}

std::string formatFrameStats(const FrameStats& s)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%d frames in %.1f s: average %.1f FPS, 1%% low %.1f FPS, median %.2f ms, worst %.2f ms",
                  s.frames, s.seconds, s.averageFps, s.low1Fps, s.medianMs, s.worstMs);
    return buf;
}

} // namespace engine
