// Texture cooking: mip chain + S3TC/RGTC block compression (stb_dxt), with a
// disk cache so a texture is compressed once and later loads skip both the
// PNG/JPEG decode and the compression.
#include <fstream>
#include <cstring> // stb_dxt uses memcpy without including it
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

#include "engine/TextureCook.h"
#include "engine/JobSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace engine {

namespace {

constexpr std::uint32_t kMagic = 0x58455443; // "CTEX"
constexpr std::uint32_t kVersion = 2;        // bump when cooking changes (invalidates caches)

struct FileHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t format;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mipCount;
};
struct FileMip {
    std::uint64_t offset;
    std::uint64_t size;
    std::uint32_t width;
    std::uint32_t height;
};

const std::array<float, 256>& srgbToLinearTable()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            t[static_cast<std::size_t>(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

std::uint8_t linearToSrgb8(float c)
{
    static const std::array<std::uint8_t, 4096> table = [] {
        std::array<std::uint8_t, 4096> t{};
        for (int i = 0; i < 4096; ++i) {
            const float l = static_cast<float>(i) / 4095.0f;
            const float s = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            t[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(s, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        return t;
    }();
    return table[static_cast<std::size_t>(std::clamp(c, 0.0f, 1.0f) * 4095.0f + 0.5f)];
}

// Next mip level with a 2x2 box filter (odd sizes clamp at the edge).
std::vector<std::uint8_t> halve(const std::vector<std::uint8_t>& src, int w, int h, int& outW, int& outH,
                                const CookOptions& o)
{
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4);
    const auto& lin = srgbToLinearTable();
    const bool srgb = o.usage == TextureUsage::Color && o.srgb;
    auto rows = [&](std::size_t y0, std::size_t y1) {
        for (int y = static_cast<int>(y0); y < static_cast<int>(y1); ++y) {
            const int sy0 = std::min(y * 2, h - 1), sy1 = std::min(y * 2 + 1, h - 1);
            for (int x = 0; x < outW; ++x) {
                const int sx0 = std::min(x * 2, w - 1), sx1 = std::min(x * 2 + 1, w - 1);
                const std::uint8_t* p[4] = {&src[(static_cast<std::size_t>(sy0) * w + sx0) * 4], &src[(static_cast<std::size_t>(sy0) * w + sx1) * 4],
                                            &src[(static_cast<std::size_t>(sy1) * w + sx0) * 4], &src[(static_cast<std::size_t>(sy1) * w + sx1) * 4]};
                std::uint8_t* d = &dst[(static_cast<std::size_t>(y) * outW + x) * 4];
                if (o.usage == TextureUsage::Normal) {
                    float n[3] = {0, 0, 0};
                    for (const std::uint8_t* q : p)
                        for (int k = 0; k < 3; ++k)
                            n[k] += q[k] / 127.5f - 1.0f;
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    for (int k = 0; k < 3; ++k)
                        d[k] = static_cast<std::uint8_t>(std::clamp((len > 1e-6f ? n[k] / len : (k == 2 ? 1.0f : 0.0f)) * 127.5f + 127.5f, 0.0f, 255.0f));
                } else {
                    // Colour weighted by alpha: transparent texels (often black) must not darken
                    // the visible ones in smaller levels (dark fringes / speckles on particles, decals, leaves).
                    const int asum = p[0][3] + p[1][3] + p[2][3] + p[3][3];
                    float wgt[4];
                    for (int i = 0; i < 4; ++i)
                        wgt[i] = asum > 0 ? p[i][3] / static_cast<float>(asum) : 0.25f;
                    for (int k = 0; k < 3; ++k) {
                        if (srgb)
                            d[k] = linearToSrgb8(lin[p[0][k]] * wgt[0] + lin[p[1][k]] * wgt[1] + lin[p[2][k]] * wgt[2] + lin[p[3][k]] * wgt[3]);
                        else
                            d[k] = static_cast<std::uint8_t>(std::clamp(p[0][k] * wgt[0] + p[1][k] * wgt[1] + p[2][k] * wgt[2] + p[3][k] * wgt[3] + 0.5f, 0.0f, 255.0f));
                    }
                }
                d[3] = static_cast<std::uint8_t>((p[0][3] + p[1][3] + p[2][3] + p[3][3] + 2) / 4);
            }
        }
    };
    if (outH >= 128)
        JobSystem::global().parallelFor(static_cast<std::size_t>(outH), 32, rows);
    else
        rows(0, static_cast<std::size_t>(outH));
    return dst;
}

// Gives fully transparent texels the colour of their visible neighbours (a few texels
// deep), so bilinear filtering at the edge of a cut-out never blends in their black.
void bleedColorIntoTransparent(std::vector<std::uint8_t>& rgba, int w, int h)
{
    std::vector<std::uint8_t> known(static_cast<std::size_t>(w) * h);
    std::size_t unknown = 0;
    for (std::size_t i = 0; i < known.size(); ++i) {
        known[i] = rgba[i * 4 + 3] > 0;
        unknown += !known[i];
    }
    if (unknown == 0 || unknown == known.size())
        return;
    std::vector<std::uint8_t> next = known;
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                if (known[i])
                    continue;
                int sum[3] = {0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if ((dx | dy) == 0 || nx < 0 || ny < 0 || nx >= w || ny >= h)
                            continue;
                        const std::size_t j = static_cast<std::size_t>(ny) * w + nx;
                        if (!known[j])
                            continue;
                        for (int k = 0; k < 3; ++k)
                            sum[k] += rgba[j * 4 + k];
                        ++n;
                    }
                if (n > 0) {
                    for (int k = 0; k < 3; ++k)
                        rgba[i * 4 + k] = static_cast<std::uint8_t>(sum[k] / n);
                    next[i] = 1;
                    changed = true;
                }
            }
        known = next;
        if (!changed)
            break;
    }
}

void compressLevel(const std::uint8_t* rgba, int w, int h, TextureFormat format, std::uint8_t* out)
{
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const std::size_t blockBytes = textureFormatBlockBytes(format);
    auto rows = [&](std::size_t by0, std::size_t by1) {
        std::uint8_t block[64];
        std::uint8_t rg[32];
        for (int by = static_cast<int>(by0); by < static_cast<int>(by1); ++by)
            for (int bx = 0; bx < bw; ++bx) {
                for (int py = 0; py < 4; ++py)
                    for (int px = 0; px < 4; ++px) {
                        const int x = std::min(bx * 4 + px, w - 1), y = std::min(by * 4 + py, h - 1); // pad by repeating the edge
                        std::memcpy(&block[(py * 4 + px) * 4], &rgba[(static_cast<std::size_t>(y) * w + x) * 4], 4);
                    }
                std::uint8_t* dst = out + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
                if (format == TextureFormat::BC5) {
                    for (int i = 0; i < 16; ++i) {
                        rg[i * 2] = block[i * 4];
                        rg[i * 2 + 1] = block[i * 4 + 1];
                    }
                    stb_compress_bc5_block(dst, rg);
                } else {
                    stb_compress_dxt_block(dst, block, format == TextureFormat::BC3 ? 1 : 0, STB_DXT_HIGHQUAL);
                }
            }
    };
    if (bh >= 32)
        JobSystem::global().parallelFor(static_cast<std::size_t>(bh), 16, rows);
    else
        rows(0, static_cast<std::size_t>(bh));
}

std::filesystem::path defaultCacheDir()
{
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(local) / "fps_engine" / "cache" / "textures";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "fps_engine" / "textures";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".cache" / "fps_engine" / "textures";
#endif
    std::error_code ec;
    return std::filesystem::temp_directory_path(ec) / "fps_engine" / "textures";
}

std::uint64_t fnv1a(const std::string& s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

FILE* openFile(const std::filesystem::path& file, const char* mode)
{
#ifdef _WIN32
    const std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(file.wstring().c_str(), wmode.c_str());
#else
    return std::fopen(file.c_str(), mode);
#endif
}

} // namespace

TextureCookConfig& textureCookConfig()
{
    static TextureCookConfig config = [] {
        TextureCookConfig c;
        c.cacheDir = defaultCacheDir();
        // Cleared at start when asked to (requestCookCacheClear): nothing reads the files yet.
        std::error_code ec;
        const std::filesystem::path root = c.cacheDir.parent_path();
        if (std::filesystem::exists(root / "clear_requested", ec)) {
            std::filesystem::remove_all(c.cacheDir, ec);
            std::filesystem::remove_all(root / "meshes", ec);
            std::filesystem::remove(root / "clear_requested", ec);
        }
        return c;
    }();
    return config;
}

void requestCookCacheClear()
{
    const std::filesystem::path root = textureCookConfig().cacheDir.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::ofstream(root / "clear_requested") << "cooked textures and meshes are deleted at the next start\n";
}

std::size_t textureFormatBlockBytes(TextureFormat format)
{
    switch (format) {
    case TextureFormat::BC1: return 8;
    case TextureFormat::BC3:
    case TextureFormat::BC5: return 16;
    default: return 0;
    }
}

std::size_t textureLevelBytes(TextureFormat format, int width, int height)
{
    if (format == TextureFormat::RGBA8)
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    return static_cast<std::size_t>((width + 3) / 4) * static_cast<std::size_t>((height + 3) / 4) * textureFormatBlockBytes(format);
}

const char* textureFormatName(TextureFormat format)
{
    switch (format) {
    case TextureFormat::BC1: return "BC1";
    case TextureFormat::BC3: return "BC3";
    case TextureFormat::BC5: return "BC5";
    default: return "RGBA8";
    }
}

namespace {

// Share of pixels whose alpha, multiplied by `scale`, is above `cutoff`.
float alphaCoverageOf(const std::vector<std::uint8_t>& rgba, float cutoff, float scale)
{
    const std::size_t count = rgba.size() / 4;
    if (count == 0)
        return 0.0f;
    std::size_t above = 0;
    for (std::size_t i = 0; i < count; ++i)
        above += static_cast<float>(rgba[i * 4 + 3]) / 255.0f * scale > cutoff ? 1 : 0;
    return static_cast<float>(above) / static_cast<float>(count);
}

// Copy of a mip level with alpha scaled so its coverage matches `target` (Castaño 2010).
std::vector<std::uint8_t> preserveCoverage(const std::vector<std::uint8_t>& level, float cutoff, float target)
{
    float lo = 0.0f, hi = 8.0f;
    for (int i = 0; i < 12; ++i) {
        const float mid = (lo + hi) * 0.5f;
        (alphaCoverageOf(level, cutoff, mid) > target ? hi : lo) = mid;
    }
    const float scale = (lo + hi) * 0.5f;
    std::vector<std::uint8_t> out = level;
    for (std::size_t i = 3; i < out.size(); i += 4)
        out[i] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(out[i]) * scale + 0.5f, 0.0f, 255.0f));
    return out;
}

} // namespace

CookedTexture cookTexture(const std::uint8_t* rgba, int width, int height, const CookOptions& options)
{
    CookedTexture result;
    if (!rgba || width <= 0 || height <= 0)
        return result;
    TextureFormat format = TextureFormat::RGBA8;
    if (options.compress) {
        if (options.usage == TextureUsage::Normal) {
            format = TextureFormat::BC5;
        } else {
            const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            bool alpha = false;
            for (std::size_t i = 0; i < count && !alpha; ++i)
                alpha = rgba[i * 4 + 3] != 255;
            format = alpha ? TextureFormat::BC3 : TextureFormat::BC1;
        }
    }
    result.format = format;

    // Mip chain in RGBA8; levels above maxSize are only steps toward the smaller ones.
    std::vector<std::uint8_t> level(rgba, rgba + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    int w = width, h = height;
    if (options.usage == TextureUsage::Color)
        bleedColorIntoTransparent(level, w, h);
    bool first = true;
    const bool coverage = options.alphaCoverage > 0.0f && options.alphaCoverage < 1.0f && options.usage == TextureUsage::Color;
    const float targetCoverage = coverage ? alphaCoverageOf(level, options.alphaCoverage, 1.0f) : 0.0f;
    for (;;) {
        const bool fits = options.maxSize <= 0 || std::max(w, h) <= options.maxSize;
        if (fits) {
            // Smaller levels keep the coverage of the source (the chain itself stays unscaled).
            std::vector<std::uint8_t> scaled;
            if (coverage && (w != width || h != height))
                scaled = preserveCoverage(level, options.alphaCoverage, targetCoverage);
            const std::vector<std::uint8_t>& out = scaled.empty() ? level : scaled;
            if (first) {
                result.width = w;
                result.height = h;
                first = false;
            }
            TextureMip mip;
            mip.offset = result.data.size();
            mip.size = textureLevelBytes(format, w, h);
            mip.width = w;
            mip.height = h;
            result.data.resize(mip.offset + mip.size);
            if (format == TextureFormat::RGBA8)
                std::memcpy(result.data.data() + mip.offset, out.data(), mip.size);
            else
                compressLevel(out.data(), w, h, format, result.data.data() + mip.offset);
            result.mips.push_back(mip);
            if (!options.mipmaps)
                break;
        }
        if (w == 1 && h == 1)
            break;
        int nw = 0, nh = 0;
        level = halve(level, w, h, nw, nh, options);
        w = nw;
        h = nh;
    }
    return result;
}

std::string textureCacheKey(const std::filesystem::path& source, const std::string& extra, const CookOptions& o)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(source, ec);
    const auto time = std::filesystem::last_write_time(source, ec).time_since_epoch().count();
    const std::u8string path = std::filesystem::absolute(source, ec).lexically_normal().generic_u8string();
    std::string key(path.begin(), path.end());
    key += '|' + std::to_string(static_cast<unsigned long long>(size)) + '|' + std::to_string(static_cast<long long>(time)) + '|' + extra + '|' + std::to_string(static_cast<int>(o.usage)) +
           (o.srgb ? "s" : "l") + (o.compress ? "c" : "u") + (o.mipmaps ? "m" : "n") + std::to_string(o.maxSize) +
           (o.alphaCoverage > 0.0f ? "a" + std::to_string(static_cast<int>(o.alphaCoverage * 1000.0f + 0.5f)) : std::string()) + "|v" +
           std::to_string(kVersion);
    return key;
}

std::filesystem::path textureCachePath(const std::string& key)
{
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.ctex", static_cast<unsigned long long>(fnv1a(key)));
    return textureCookConfig().cacheDir / name;
}

bool writeCookedTexture(const std::filesystem::path& file, const CookedTexture& t)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Write to a temporary name first: another thread or process may read the cache meanwhile.
    std::filesystem::path tmp = file;
    tmp += ".tmp" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&t)));
    FILE* f = openFile(tmp, "wb");
    if (!f)
        return false;
    const FileHeader header{kMagic, kVersion, static_cast<std::uint32_t>(t.format), static_cast<std::uint32_t>(t.width),
                            static_cast<std::uint32_t>(t.height), static_cast<std::uint32_t>(t.mips.size())};
    bool ok = std::fwrite(&header, sizeof header, 1, f) == 1;
    for (const TextureMip& m : t.mips) {
        const FileMip fm{m.offset, m.size, static_cast<std::uint32_t>(m.width), static_cast<std::uint32_t>(m.height)};
        ok = ok && std::fwrite(&fm, sizeof fm, 1, f) == 1;
    }
    ok = ok && (t.data.empty() || std::fwrite(t.data.data(), t.data.size(), 1, f) == 1);
    ok = std::fclose(f) == 0 && ok;
    if (ok)
        std::filesystem::rename(tmp, file, ec);
    if (!ok || ec)
        std::filesystem::remove(tmp, ec);
    return ok && !ec;
}

namespace {
bool readHeader(FILE* f, CookedTexture& out, std::vector<FileMip>& mips)
{
    FileHeader h{};
    if (std::fread(&h, sizeof h, 1, f) != 1 || h.magic != kMagic || h.version != kVersion || h.format > 3 || h.mipCount == 0 ||
        h.mipCount > 32)
        return false;
    out.format = static_cast<TextureFormat>(h.format);
    out.width = static_cast<int>(h.width);
    out.height = static_cast<int>(h.height);
    mips.resize(h.mipCount);
    return std::fread(mips.data(), sizeof(FileMip), mips.size(), f) == mips.size();
}
} // namespace

bool readCookedTextureInfo(const std::filesystem::path& file, CookedTexture& out)
{
    FILE* f = openFile(file, "rb");
    if (!f)
        return false;
    std::vector<FileMip> mips;
    const bool ok = readHeader(f, out, mips);
    std::fclose(f);
    if (!ok)
        return false;
    out.mips.clear();
    for (const FileMip& m : mips)
        out.mips.push_back({static_cast<std::size_t>(m.offset), static_cast<std::size_t>(m.size), static_cast<int>(m.width),
                            static_cast<int>(m.height)});
    out.data.clear();
    return true;
}

bool readCookedTexture(const std::filesystem::path& file, CookedTexture& out, int firstMip)
{
    FILE* f = openFile(file, "rb");
    if (!f)
        return false;
    std::vector<FileMip> mips;
    bool ok = readHeader(f, out, mips);
    if (ok) {
        firstMip = std::clamp(firstMip, 0, static_cast<int>(mips.size()) - 1);
        const long dataStart = static_cast<long>(sizeof(FileHeader) + sizeof(FileMip) * mips.size());
        const std::uint64_t begin = mips[static_cast<std::size_t>(firstMip)].offset;
        const std::uint64_t end = mips.back().offset + mips.back().size;
        out.width = static_cast<int>(mips[static_cast<std::size_t>(firstMip)].width);
        out.height = static_cast<int>(mips[static_cast<std::size_t>(firstMip)].height);
        out.mips.clear();
        for (std::size_t i = static_cast<std::size_t>(firstMip); i < mips.size(); ++i)
            out.mips.push_back({static_cast<std::size_t>(mips[i].offset - begin), static_cast<std::size_t>(mips[i].size),
                                static_cast<int>(mips[i].width), static_cast<int>(mips[i].height)});
        out.data.resize(static_cast<std::size_t>(end - begin));
        ok = end >= begin && end < (1ull << 32) && std::fseek(f, dataStart + static_cast<long>(begin), SEEK_SET) == 0 &&
             (out.data.empty() || std::fread(out.data.data(), out.data.size(), 1, f) == 1);
    }
    std::fclose(f);
    return ok;
}

} // namespace engine
