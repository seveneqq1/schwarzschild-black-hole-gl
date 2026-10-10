#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

// Minimal RGB8 PNG encoder; rows are given bottom-up as returned by glReadPixels.
inline void writePng(const std::string& path, int width, int height,
                     const std::vector<std::uint8_t>& bottomUpRgb) {
    const std::size_t stride = static_cast<std::size_t>(width) * 3u;
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1u) * static_cast<std::size_t>(height));
    for (int row = height - 1; row >= 0; --row) {
        raw.push_back(0); // filter: none
        const auto begin = bottomUpRgb.begin() + static_cast<std::ptrdiff_t>(stride * static_cast<std::size_t>(row));
        raw.insert(raw.end(), begin, begin + static_cast<std::ptrdiff_t>(stride));
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        throw std::runtime_error("PNG compression failed");
    }
    compressed.resize(compressedSize);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to write " + path);
    }
    const auto bigEndian = [](std::uint32_t value) {
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(value >> 24),
                                           static_cast<std::uint8_t>(value >> 16),
                                           static_cast<std::uint8_t>(value >> 8),
                                           static_cast<std::uint8_t>(value)};
    };
    const auto chunk = [&](const char* type, const std::vector<std::uint8_t>& data) {
        const auto length = bigEndian(static_cast<std::uint32_t>(data.size()));
        file.write(reinterpret_cast<const char*>(length.data()), 4);
        file.write(type, 4);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(type), 4);
        if (!data.empty()) {
            crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
        }
        const auto crcBytes = bigEndian(static_cast<std::uint32_t>(crc));
        file.write(reinterpret_cast<const char*>(crcBytes.data()), 4);
    };

    static constexpr std::uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    file.write(reinterpret_cast<const char*>(signature), sizeof(signature));
    std::vector<std::uint8_t> header;
    for (const std::uint32_t value : {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}) {
        const auto bytes = bigEndian(value);
        header.insert(header.end(), bytes.begin(), bytes.end());
    }
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB, no interlace
    chunk("IHDR", header);
    chunk("IDAT", compressed);
    chunk("IEND", {});
}
