#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace DcldProtocol {

    constexpr std::size_t HEADER_SIZE = 64;

    inline std::uint16_t checksum16(const std::uint8_t* data,
                                    std::size_t size)
    {
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i < size; ++i)
            sum += data[i];

        return static_cast<std::uint16_t>(sum & 0xFFFF);
    }

    inline void writeLE16(
        std::array<std::uint8_t, HEADER_SIZE>& h,
        std::size_t offset,
        std::uint16_t value)
    {
        h[offset] = static_cast<std::uint8_t>(value & 0xFF);
        h[offset + 1] =
        static_cast<std::uint8_t>((value >> 8) & 0xFF);
    }

    inline void writeLE24(
        std::array<std::uint8_t, HEADER_SIZE>& h,
        std::size_t offset,
        std::uint32_t value)
    {
        h[offset] = static_cast<std::uint8_t>(value & 0xFF);
        h[offset + 1] =
        static_cast<std::uint8_t>((value >> 8) & 0xFF);
        h[offset + 2] =
        static_cast<std::uint8_t>((value >> 16) & 0xFF);
    }

    // GIF-delay in centiseconden.
    inline std::uint32_t calculateGifRate(std::uint32_t delayCs)
    {
        if (delayCs == 0)
            throw std::invalid_argument("GIF-delay mag niet nul zijn");

        return 100u / delayCs;
    }

    inline std::uint32_t calculateGifAllTimeMs(
        std::uint32_t frameCount,
        std::uint32_t gifRate)
    {
        if (frameCount == 0 || gifRate == 0) {
            throw std::invalid_argument(
                "frameCount and gifRate must be non-zero");
        }

        const std::uint64_t numerator =
        static_cast<std::uint64_t>(frameCount) * 1000ULL;

        return static_cast<std::uint32_t>(
            (numerator + gifRate - 1) / gifRate);
    }

    inline std::array<std::uint8_t, HEADER_SIZE> buildHeader(
        const std::vector<std::uint8_t>& jpeg,
        std::uint16_t frameCount,
        std::uint8_t frameIndex,
        std::uint16_t gifAllTimeMs,
        const std::array<std::uint8_t, 32>& uploadId)
    {
        if (jpeg.empty())
            throw std::invalid_argument("JPEG-frame is leeg");

        if (jpeg.size() > 0xFFFFFFu)
            throw std::length_error("JPEG past niet in 24-bit lengte");

        if (frameCount == 0 || frameIndex >= frameCount)
            throw std::invalid_argument("Ongeldige frameCount/frameIndex");

        std::array<std::uint8_t, HEADER_SIZE> h{};

        // 0x00-0x03: magic
        h[0] = 'D';
        h[1] = 'C';
        h[2] = 'L';
        h[3] = 'd';

        // 0x04: vaste waarde
        h[4] = 0x02;

        // 0x05-0x07: JPEG-lengte, 24-bit little-endian
        writeLE24(h, 0x05, static_cast<std::uint32_t>(jpeg.size()));

        // 0x08: gereserveerd
        // 0x09-0x0A: JPEG-checksum, little-endian
        writeLE16(h, 0x09, checksum16(jpeg.data(), jpeg.size()));

        // 0x0B: gereserveerd
        // 0x0C-0x0D: frameCount, big-endian
        h[0x0C] = static_cast<std::uint8_t>((frameCount >> 8) & 0xFF);
        h[0x0D] = static_cast<std::uint8_t>(frameCount & 0xFF);

        // 0x0E: gereserveerd
        // 0x0F-0x10: gifalltime, little-endian
        writeLE16(h, 0x0F, gifAllTimeMs);

        // 0x11: frame-index, zero-based
        h[0x11] = frameIndex;

        // 0x12-0x13: gereserveerd

        // 0x14-0x33: upload-ID (32 bytes)
        std::copy(uploadId.begin(), uploadId.end(), h.begin() + 0x14);

        // 0x34-0x3D: gereserveerd; blijft nul.

        // 0x3E-0x3F: header-checksum over bytes 0x00-0x3D
        writeLE16(h, 0x3E, checksum16(h.data(), 0x3E));

        return h;
    }

}

