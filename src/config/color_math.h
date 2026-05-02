#pragma once

#include <cstdint>

struct ColorBytes {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 255.0;
};

struct OklabColor {
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};

struct OklchColor {
    double l = 0.0;
    double c = 0.0;
    double h = 0.0;
};

struct HsvColor {
    double h = 0.0;
    double s = 0.0;
    double v = 0.0;
};

ColorBytes ColorBytesFromRgba(std::uint32_t rgba);
std::uint32_t RgbaFromColorBytes(ColorBytes color);
OklabColor OklabFromColorBytes(ColorBytes color);
ColorBytes ColorBytesFromOklab(OklabColor color, double alpha);
OklchColor OklchFromOklab(OklabColor color);
OklabColor OklabFromOklch(OklchColor color);
OklchColor OklchFromColorBytes(ColorBytes color);
ColorBytes ColorBytesFromOklch(OklchColor color, double alpha);
HsvColor HsvFromColorBytes(ColorBytes color);
ColorBytes ColorBytesFromHsv(HsvColor color, double alpha);
OklabColor MixOklab(OklabColor from, OklabColor to, double amount);
OklabColor RotateOklabHue(OklabColor color, double degrees);
