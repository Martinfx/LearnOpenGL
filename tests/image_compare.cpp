// Golden image comparison for the renderer tests (tests/golden_image.sh):
//   image_compare <reference.ppm> <image.ppm> [--mean <max mean abs diff, 0..255>]
//                 [--bad <max % of pixels off by more than --threshold>] [--threshold <0..255>]
//                 [--diff <out.ppm>]
// Exit 0 when the image matches within the tolerance, 1 when it differs, 2 on bad input.
// The tolerance absorbs driver and rounding noise (llvmpipe vs. GPUs, dithering, TAA); real
// regressions (a missing pass, black frames, wrong colours, shifted geometry) exceed it.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> rgb;
};

bool readPpm(const char* path, Image& image)
{
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int maxValue = 0;
    if (!(in >> magic >> image.width >> image.height >> maxValue) || magic != "P6" || maxValue != 255 || image.width <= 0 ||
        image.height <= 0)
        return false;
    in.get(); // the single whitespace after the header
    image.rgb.resize(static_cast<std::size_t>(image.width) * image.height * 3);
    in.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    return static_cast<std::size_t>(in.gcount()) == image.rgb.size();
}

bool writePpm(const char* path, const Image& image)
{
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: image_compare <reference.ppm> <image.ppm> [--mean N] [--bad PERCENT] [--threshold N] [--diff out.ppm]\n");
        return 2;
    }
    double maxMean = 3.0, maxBad = 1.0;
    int threshold = 40;
    const char* diffPath = nullptr;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--mean"))
            maxMean = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--bad"))
            maxBad = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--threshold"))
            threshold = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--diff"))
            diffPath = argv[i + 1];
    }
    Image reference, image;
    if (!readPpm(argv[1], reference)) {
        std::fprintf(stderr, "image_compare: cannot read %s\n", argv[1]);
        return 2;
    }
    if (!readPpm(argv[2], image)) {
        std::fprintf(stderr, "image_compare: cannot read %s\n", argv[2]);
        return 2;
    }
    if (reference.width != image.width || reference.height != image.height) {
        std::printf("image_compare: size %dx%d, reference %dx%d\n", image.width, image.height, reference.width, reference.height);
        return 1;
    }
    // Per pixel: the largest channel difference. Diff image: grey = the frame, red = over threshold.
    Image diff = image;
    double sum = 0.0;
    std::size_t bad = 0;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t p = 0; p < pixels; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(image.rgb[p * 3 + c]) - static_cast<int>(reference.rgb[p * 3 + c]));
            sum += d;
            worst = d > worst ? d : worst;
        }
        const unsigned char grey = static_cast<unsigned char>((image.rgb[p * 3] + image.rgb[p * 3 + 1] + image.rgb[p * 3 + 2]) / 9);
        diff.rgb[p * 3] = worst > threshold ? 255 : grey;
        diff.rgb[p * 3 + 1] = worst > threshold ? 0 : grey;
        diff.rgb[p * 3 + 2] = worst > threshold ? 0 : grey;
        if (worst > threshold)
            ++bad;
    }
    const double mean = sum / static_cast<double>(pixels * 3);
    const double badPercent = 100.0 * static_cast<double>(bad) / static_cast<double>(pixels);
    const bool pass = mean <= maxMean && badPercent <= maxBad;
    std::printf("image_compare: mean diff %.2f (max %.2f), %.2f%% pixels off by > %d (max %.2f%%): %s\n", mean, maxMean, badPercent,
                threshold, maxBad, pass ? "match" : "DIFFERENT");
    if (diffPath && !writePpm(diffPath, diff))
        std::fprintf(stderr, "image_compare: cannot write %s\n", diffPath);
    return pass ? 0 : 1;
}
