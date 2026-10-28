// src/tools/IconGenerator.cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Color {
    uint8_t r, g, b, a;
};

float sdSegment(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax, pay = py - ay;
    float bax = bx - ax, bay = by - ay;
    float denom = bax * bax + bay * bay;
    float h = denom > 0.0f ? std::clamp((pax * bax + pay * bay) / denom, 0.0f, 1.0f) : 0.0f;
    float dx = pax - bax * h;
    float dy = pay - bay * h;
    return std::sqrt(dx * dx + dy * dy);
}

float sdRoundRect(float px, float py, float cx, float cy, float hx, float hy, float r) {
    float qx = std::abs(px - cx) - hx + r;
    float qy = std::abs(py - cy) - hy + r;
    float outDist = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                              std::max(qy, 0.0f) * std::max(qy, 0.0f));
    float inDist = std::min(std::max(qx, qy), 0.0f);
    return outDist + inDist - r;
}

void blendPixel(Color& dst, uint8_t sr, uint8_t sg, uint8_t sb, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f) return;
    float inv = 1.0f - alpha;
    float outA = alpha + (dst.a / 255.0f) * inv;
    if (outA <= 0.0f) return;
    float r = (sr * alpha + dst.r * (dst.a / 255.0f) * inv) / outA;
    float g = (sg * alpha + dst.g * (dst.a / 255.0f) * inv) / outA;
    float b = (sb * alpha + dst.b * (dst.a / 255.0f) * inv) / outA;
    dst.r = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
    dst.g = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
    dst.b = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
    dst.a = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
}

std::vector<uint8_t> renderBgraFrame(int size) {
    std::vector<uint8_t> bgra(size * size * 4, 0);
    const float invS = 1.0f / static_cast<float>(size);
    const float aa = 1.25f * invS;

    for (int y = 0; y < size; ++y) {
        float ny = (static_cast<float>(size - 1 - y) + 0.5f) * invS;
        for (int x = 0; x < size; ++x) {
            float nx = (static_cast<float>(x) + 0.5f) * invS;
            Color px{0, 0, 0, 0};

            float dBox = sdRoundRect(nx, ny, 0.5f, 0.5f, 0.44f, 0.44f, 0.18f);
            if (dBox < aa) {
                float cov = std::clamp(0.5f - dBox / (2.0f * aa), 0.0f, 1.0f);
                float t = std::clamp((nx + ny) * 0.5f, 0.0f, 1.0f);
                uint8_t br = static_cast<uint8_t>(40 * (1.0f - t) + 18 * t);
                uint8_t bg = static_cast<uint8_t>(42 * (1.0f - t) + 19 * t);
                uint8_t bb = static_cast<uint8_t>(54 * (1.0f - t) + 28 * t);
                blendPixel(px, br, bg, bb, cov);

                float dBorder = std::abs(dBox + 0.018f) - 0.018f;
                if (dBorder < aa) {
                    float bCov = std::clamp(0.5f - dBorder / (2.0f * aa), 0.0f, 1.0f) * cov;
                    blendPixel(px, 189, 147, 249, bCov);
                }
            }

            if (px.a > 0) {
                float dLeft = std::min(
                    sdSegment(nx, ny, 0.27f, 0.34f, 0.16f, 0.50f),
                    sdSegment(nx, ny, 0.16f, 0.50f, 0.27f, 0.66f));
                float dRight = std::min(
                    sdSegment(nx, ny, 0.73f, 0.34f, 0.84f, 0.50f),
                    sdSegment(nx, ny, 0.84f, 0.50f, 0.73f, 0.66f));
                float dBr = std::min(dLeft, dRight) - 0.032f;
                if (dBr < aa) {
                    float c = std::clamp(0.5f - dBr / (2.0f * aa), 0.0f, 1.0f);
                    blendPixel(px, 139, 233, 253, c);
                }

                float dA = std::min(
                               sdSegment(nx, ny, 0.34f, 0.74f, 0.50f, 0.24f),
                               sdSegment(nx, ny, 0.50f, 0.24f, 0.66f, 0.74f)) - 0.041f;
                if (dA < aa) {
                    float c = std::clamp(0.5f - dA / (2.0f * aa), 0.0f, 1.0f);
                    float ty = std::clamp((ny - 0.24f) / 0.50f, 0.0f, 1.0f);
                    uint8_t ar = static_cast<uint8_t>(248 * (1.0f - ty) + 189 * ty);
                    uint8_t ag = static_cast<uint8_t>(248 * (1.0f - ty) + 147 * ty);
                    uint8_t ab = static_cast<uint8_t>(242 * (1.0f - ty) + 249 * ty);
                    blendPixel(px, ar, ag, ab, c);
                }

                float dBar = sdSegment(nx, ny, 0.39f, 0.55f, 0.61f, 0.55f) - 0.022f;
                float dDiamond = (std::abs(nx - 0.50f) + std::abs(ny - 0.55f)) - 0.082f;
                float dCore = std::min(dBar, dDiamond);
                if (dCore < aa) {
                    float c = std::clamp(0.5f - dCore / (2.0f * aa), 0.0f, 1.0f);
                    blendPixel(px, 80, 250, 123, c);
                }
            }

            size_t idx = static_cast<size_t>((y * size + x) * 4);
            bgra[idx + 0] = px.b;
            bgra[idx + 1] = px.g;
            bgra[idx + 2] = px.r;
            bgra[idx + 3] = px.a;
        }
    }
    return bgra;
}

void writeU16(std::ofstream& out, uint16_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
}

void writeU32(std::ofstream& out, uint32_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
    out.put(static_cast<char>((v >> 16) & 0xFF));
    out.put(static_cast<char>((v >> 24) & 0xFF));
}

bool generateIcoFile(const std::string& path) {
    const std::vector<int> sizes = {16, 32, 48, 256};
    struct FrameData {
        int size;
        std::vector<uint8_t> bgra;
        std::vector<uint8_t> andMask;
        uint32_t byteSize;
    };
    std::vector<FrameData> frames;

    for (int sz : sizes) {
        FrameData f;
        f.size = sz;
        f.bgra = renderBgraFrame(sz);
        int maskRowBytes = ((sz + 31) / 32) * 4;
        f.andMask.assign(static_cast<size_t>(maskRowBytes * sz), 0);
        f.byteSize = 40u + static_cast<uint32_t>(f.bgra.size() + f.andMask.size());
        frames.push_back(std::move(f));
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    writeU16(out, 0);
    writeU16(out, 1);
    writeU16(out, static_cast<uint16_t>(frames.size()));

    uint32_t offset = 6u + static_cast<uint32_t>(frames.size()) * 16u;
    for (const auto& f : frames) {
        out.put(static_cast<char>(f.size == 256 ? 0 : f.size));
        out.put(static_cast<char>(f.size == 256 ? 0 : f.size));
        out.put(0);
        out.put(0);
        writeU16(out, 1);
        writeU16(out, 32);
        writeU32(out, f.byteSize);
        writeU32(out, offset);
        offset += f.byteSize;
    }

    for (const auto& f : frames) {
        writeU32(out, 40);
        writeU32(out, static_cast<uint32_t>(f.size));
        writeU32(out, static_cast<uint32_t>(f.size * 2));
        writeU16(out, 1);
        writeU16(out, 32);
        writeU32(out, 0);
        writeU32(out, static_cast<uint32_t>(f.bgra.size() + f.andMask.size()));
        writeU32(out, 0);
        writeU32(out, 0);
        writeU32(out, 0);
        writeU32(out, 0);

        out.write(reinterpret_cast<const char*>(f.bgra.data()), static_cast<std::streamsize>(f.bgra.size()));
        out.write(reinterpret_cast<const char*>(f.andMask.data()), static_cast<std::streamsize>(f.andMask.size()));
    }
    return out.good();
}

bool writeTextFile(const std::filesystem::path& p, const char* content) {
    std::ofstream out(p);
    if (!out) return false;
    out << content;
    return out.good();
}

bool generateActionSvgFiles(const std::filesystem::path& baseDir) {
    writeTextFile(baseDir / "aide_project_prompt.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <path d="M9 18 H24 L29 23 H55 V50 H9 Z" fill="#232534" stroke="#bd93f9" stroke-width="4" stroke-linejoin="round"/>
  <polyline points="18,31 27,36 18,42" fill="none" stroke="#50fa7b" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
  <line x1="31" y1="42" x2="41" y2="42" stroke="#50fa7b" stroke-width="4" stroke-linecap="round"/>
  <polygon points="48,9 51,14 56,16 51,18 48,23 45,18 40,16 45,14" fill="#8be9fd"/>
</svg>)SVG");

    writeTextFile(baseDir / "aide_project_rebuild.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <path d="M9 18 H24 L29 23 H55 V50 H9 Z" fill="#232534" stroke="#bd93f9" stroke-width="4" stroke-linejoin="round"/>
  <path d="M24 36 A9 9 0 0 1 40 33" fill="none" stroke="#50fa7b" stroke-width="4" stroke-linecap="round"/>
  <path d="M40 37 A9 9 0 0 1 24 40" fill="none" stroke="#50fa7b" stroke-width="4" stroke-linecap="round"/>
</svg>)SVG");

    writeTextFile(baseDir / "aide_project_recursive.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <line x1="18" y1="16" x2="18" y2="49" stroke="#8be9fd" stroke-width="4" stroke-linecap="round"/>
  <line x1="18" y1="32" x2="33" y2="32" stroke="#8be9fd" stroke-width="4" stroke-linecap="round"/>
  <line x1="18" y1="49" x2="33" y2="49" stroke="#8be9fd" stroke-width="4" stroke-linecap="round"/>
  <rect x="9" y="9" width="19" height="11" rx="2" fill="#bd93f9"/>
  <rect x="33" y="26" width="21" height="11" rx="2" fill="#232534" stroke="#50fa7b" stroke-width="3"/>
  <rect x="33" y="43" width="21" height="11" rx="2" fill="#232534" stroke="#50fa7b" stroke-width="3"/>
  <polygon points="48,7 51,12 56,14 51,16 48,21 45,16 40,14 45,12" fill="#ffb86c"/>
</svg>)SVG");

    writeTextFile(baseDir / "aide_file_prompt.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <path d="M14 9 H40 L50 19 V55 H14 Z" fill="#232534" stroke="#8be9fd" stroke-width="4" stroke-linejoin="round"/>
  <polyline points="40,9 40,19 50,19" fill="none" stroke="#8be9fd" stroke-width="4" stroke-linejoin="round"/>
  <polyline points="21,30 29,36 21,42" fill="none" stroke="#50fa7b" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
  <line x1="33" y1="42" x2="42" y2="42" stroke="#50fa7b" stroke-width="4" stroke-linecap="round"/>
</svg>)SVG");

    writeTextFile(baseDir / "aide_file_rebuild.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <path d="M14 9 H40 L50 19 V55 H14 Z" fill="#232534" stroke="#8be9fd" stroke-width="4" stroke-linejoin="round"/>
  <polyline points="40,9 40,19 50,19" fill="none" stroke="#8be9fd" stroke-width="4" stroke-linejoin="round"/>
  <path d="M23 35 A9 9 0 0 1 41 32" fill="none" stroke="#ffb86c" stroke-width="4" stroke-linecap="round"/>
  <path d="M41 37 A9 9 0 0 1 23 40" fill="none" stroke="#ffb86c" stroke-width="4" stroke-linecap="round"/>
</svg>)SVG");

    writeTextFile(baseDir / "aide_code_explain.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <polyline points="18,18 8,32 18,46" fill="none" stroke="#8be9fd" stroke-width="4.5" stroke-linecap="round" stroke-linejoin="round"/>
  <polyline points="46,18 56,32 46,46" fill="none" stroke="#8be9fd" stroke-width="4.5" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M27 23 C27 15 38 15 38 24 C38 30 32 30 32 36" fill="none" stroke="#f1fa8c" stroke-width="5" stroke-linecap="round"/>
  <circle cx="32" cy="45" r="3" fill="#f1fa8c"/>
</svg>)SVG");

    writeTextFile(baseDir / "aide_add_docs.svg", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <path d="M14 9 H40 L50 19 V55 H14 Z" fill="#232534" stroke="#bd93f9" stroke-width="4" stroke-linejoin="round"/>
  <line x1="21" y1="27" x2="41" y2="27" stroke="#8be9fd" stroke-width="4" stroke-linecap="round"/>
  <line x1="21" y1="35" x2="37" y2="35" stroke="#8be9fd" stroke-width="4" stroke-linecap="round"/>
  <circle cx="47" cy="47" r="11" fill="#50fa7b" stroke="#191a24" stroke-width="2"/>
  <line x1="41" y1="47" x2="53" y2="47" stroke="#191a24" stroke-width="4" stroke-linecap="round"/>
  <line x1="47" y1="41" x2="47" y2="53" stroke="#191a24" stroke-width="4" stroke-linecap="round"/>
</svg>)SVG");

    return true;
}

bool generateSvgFile(const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;

    out << R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#282a36"/>
      <stop offset="50%" stop-color="#191a24"/>
      <stop offset="100%" stop-color="#11121a"/>
    </linearGradient>
    <linearGradient id="ag" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#f8f8f2"/>
      <stop offset="100%" stop-color="#bd93f9"/>
    </linearGradient>
  </defs>
  <rect x="16" y="16" width="224" height="224" rx="52" fill="url(#bg)" stroke="#bd93f9" stroke-width="8"/>
  <polyline points="69,87 41,128 69,169" fill="none" stroke="#8be9fd" stroke-width="16" stroke-linecap="round" stroke-linejoin="round"/>
  <polyline points="187,87 215,128 187,169" fill="none" stroke="#8be9fd" stroke-width="16" stroke-linecap="round" stroke-linejoin="round"/>
  <polyline points="87,190 128,62 169,190" fill="none" stroke="url(#ag)" stroke-width="20" stroke-linecap="round" stroke-linejoin="round"/>
  <line x1="100" y1="141" x2="156" y2="141" stroke="#50fa7b" stroke-width="11" stroke-linecap="round"/>
  <polygon points="128,119 150,141 128,163 106,141" fill="#50fa7b" stroke="#191a24" stroke-width="5"/>
</svg>
)SVG";
    out.close();

    std::filesystem::path svgPath(path);
    generateActionSvgFiles(svgPath.parent_path());
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::string icoPath = argv[1];
    if (!generateIcoFile(icoPath)) return 2;

    if (argc >= 3) {
        if (!generateSvgFile(argv[2])) return 3;
    }
    return 0;
}
