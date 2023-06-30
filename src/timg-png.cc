// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2021 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "timg-png.h"

#include <arpa/inet.h>
#include <assert.h>
#include <libdeflate.h>
#include <string.h>

#include "framebuffer.h"

namespace timg {
namespace {
// https://w3.org/TR/png/#5PNG-file-signature
constexpr uint8_t kPNGHeader[] = {0x89, 0x50, 0x4E, 0x47,
                                  '\r', '\n', 0x1A, '\n'};

// Writing PNG-style chunks into provided buffer.
// [4 len][4 chunk]<data>[4 CRC]
// https://w3.org/TR/png/#5Chunk-layout
class ChunkWriter {
public:
    explicit ChunkWriter(uint8_t *pos) : start_block_(pos), pos_(pos) {}

    ~ChunkWriter() { assert(finalized_); }

    // Finish last chunk, start new chunk.
    uint8_t *StartNextChunk(const char *chunk_type) {
        if (!finalized_) start_block_ = Finalize();
        assert(strlen(chunk_type) == 4);
        // We fix up the initial 4 bytes later once we know the length.
        memcpy(pos_ + 4, chunk_type, 4);
        pos_ += 8;
        finalized_ = false;
        return pos_;
    }

    // Finish chunk and return position of next write.
    uint8_t *Finalize() {
        assert(!finalized_);
        const uint32_t data_len = pos_ - start_block_ - 8;
        // We have access to the full buffer, so we can now run crc() over all
        // quickly.
        const uint32_t crc =
            libdeflate_crc32(0, start_block_ + 4, data_len + 4);
        const uint32_t crc_bigint = htonl(crc);
        memcpy(pos_, &crc_bigint, 4);
        pos_ += 4;

        // Length. At the very beginning of block.
        const uint32_t bigint_len = htonl(data_len);
        memcpy(start_block_, &bigint_len, 4);

        finalized_ = true;
        return pos_;
    }

    void writeByte(uint8_t value) { *pos_++ = value; }

    void writeInt(uint32_t value) {
        value = htonl(value);
        memcpy(pos_, &value, 4);
        pos_ += 4;
    }

    // Tell how many bytes have been written.
    void updateWritten(size_t written) { pos_ += written; }

private:
    bool finalized_ = true;
    uint8_t *start_block_;  // Where our block started.
    uint8_t *pos_;          // Current write position
};

template <bool with_alpha>
static size_t EncodePNGInternal(const Framebuffer &fb, int compression_level,
                                char *const buffer, size_t size) {
    static constexpr uint8_t kFilterType = 0x01;  // simplest substract filter

    const int width  = fb.width();
    const int height = fb.height();

    uint8_t *pos = (uint8_t *)buffer;
    memcpy(pos, kPNGHeader, sizeof(kPNGHeader));
    pos += sizeof(kPNGHeader);

    ChunkWriter block(pos);

    // Image header
    block.StartNextChunk("IHDR");
    block.writeInt(width);                // width
    block.writeInt(height);               // height
    block.writeByte(8);                   // bith depth
    block.writeByte(with_alpha ? 6 : 2);  // PNG color type
    block.writeByte(0);                   // compression type: deflate()
    block.writeByte(0);                   // filter method.
    block.writeByte(0);                   // interlace. None.

    // Prepare data to be compressed.
    // TODO: to reduce allocation overhead in repeated calls, maybe ask the
    // caller to provide a sufficiently large scratch buffer ?
    const size_t cbuffer_size =
        width * height * sizeof(rgba_t) + height * sizeof(kFilterType);
    uint8_t *const compress_buffer = new uint8_t[cbuffer_size];

    constexpr int bytes_per_pixel = with_alpha ? 4 : 3;
    const rgba_t *current_line    = fb.begin();
    uint8_t *out                  = compress_buffer;
    for (int y = 0; y < height; ++y, current_line += width) {
        *out++ = kFilterType;
        memcpy(out, current_line, sizeof(rgba_t));  // First pixel
        out += bytes_per_pixel;
        for (int i = 1; i < width; ++i) {
            *out++ = current_line[i].r - current_line[i - 1].r;
            *out++ = current_line[i].g - current_line[i - 1].g;
            *out++ = current_line[i].b - current_line[i - 1].b;
            if (with_alpha) *out++ = current_line[i].a - current_line[i - 1].a;
        }
    }

    // Write image IDAT data.
    uint8_t *const start_data = block.StartNextChunk("IDAT");
    const int compress_avail  = size - (start_data - (uint8_t *)buffer);
    libdeflate_compressor *const compressor =
        libdeflate_alloc_compressor(compression_level);

    const size_t written_size = libdeflate_zlib_compress(
        compressor, compress_buffer, out - compress_buffer,  //
        start_data, compress_avail);
    block.updateWritten(written_size);

    libdeflate_free_compressor(compressor);
    delete[] compress_buffer;

    block.StartNextChunk("IEND");
    return block.Finalize() - (uint8_t *)buffer;
}
}  // namespace

namespace png {
size_t Encode(const Framebuffer &fb, int compression_level,
              ColorEncoding encoding, char *buffer, size_t size) {
    if (encoding == ColorEncoding::kRGB_24) {
        return EncodePNGInternal<false>(fb, compression_level, buffer, size);
    }
    return EncodePNGInternal<true>(fb, compression_level, buffer, size);
}

size_t UpperBound(int width, int height) {
    static constexpr size_t kPNGHeaderOverhead = 128;  // reality about ~57
    const size_t image_data_size =
        width * height * sizeof(rgba_t) + height * 1 /*filter-per-row*/;
    return libdeflate_zlib_compress_bound(nullptr, image_data_size) +
           kPNGHeaderOverhead;
}
}  // namespace png
}  // namespace timg
