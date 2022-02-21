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
#include <string.h>
#include <zlib.h>

#include "framebuffer.h"

namespace timg {
namespace {
constexpr uint8_t kPNGHeader[] = {0x89, 0x50, 0x4E, 0x47,
                                  '\r', '\n', 0x1A, '\n'};

// Writing PNG-style blocks: [4 len][4 chunk]<data>[4 CRC]
class BlockWriter {
public:
    explicit BlockWriter(uint8_t *pos) : start_block_(pos), pos_(pos) {}

    ~BlockWriter() { assert(finalized_); }

    // Finish last block, start new block.
    void StartNextBlock(const char *chunk_type) {
        if (!finalized_) start_block_ = Finalize();
        assert(strlen(chunk_type) == 4);
        // We fix up the initial 4 bytes later once we know the length.
        memcpy(pos_ + 4, chunk_type, 4);
        pos_ += 8;
        finalized_ = false;
    }

    // Finish block
    uint8_t *Finalize() {
        assert(!finalized_);
        const uint32_t data_len = pos_ - start_block_ - 8;
        // We have access to the full buffer, so we can now run crc() over all
        // quickly.
        const uint32_t crc        = crc32(0, start_block_ + 4, data_len + 4);
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

    // Get next position we write to.
    uint8_t *writePos() { return pos_; }

    // Tell how many bytes have been written.
    void updateWritten(size_t written) { pos_ += written; }

private:
    bool finalized_ = true;
    uint8_t *start_block_;  // Where our block started.
    uint8_t *pos_;          // Current write position
};
}  // namespace

template <int with_alpha>
static size_t EncodePNGInternal(const Framebuffer &fb, int compression_level,
                                char *const buffer, size_t size) {
    static constexpr int kCompressionStrategy = Z_RLE;
    static constexpr uint8_t kFilterType      = 0x01;

    const int width            = fb.width();
    const int height           = fb.height();
    uint8_t *const line_buffer = new uint8_t[1 + fb.width() * sizeof(rgba_t)];
    *line_buffer               = kFilterType;

    uint8_t *pos = (uint8_t *)buffer;
    memcpy(pos, kPNGHeader, sizeof(kPNGHeader));
    pos += sizeof(kPNGHeader);

    BlockWriter block(pos);

    // Image header
    block.StartNextBlock("IHDR");
    block.writeInt(width);                // width
    block.writeInt(height);               // height
    block.writeByte(8);                   // bith depth
    block.writeByte(with_alpha ? 6 : 2);  // PNG color type
    block.writeByte(0);                   // compression type: deflate()
    block.writeByte(0);                   // filter method.
    block.writeByte(0);                   // interlace. None.

    block.StartNextBlock("IDAT");
    z_stream stream;
    memset(&stream, 0x00, sizeof(stream));
    deflateInit2(&stream, compression_level, Z_DEFLATED, 15 /*window bits*/,
                 9 /* memlevel*/, kCompressionStrategy);

    const int bytes_per_pixel  = with_alpha ? 4 : 3;
    const int compress_avail   = size - 13;  // IHDR size
    stream.avail_out           = compress_avail;
    stream.next_out            = block.writePos();
    const rgba_t *current_line = fb.begin();

    for (int y = 0; y < height; ++y, current_line += width) {
        uint8_t *out = line_buffer + 1;  // kFilterType already there.
        memcpy(out, current_line, 4);
        out += bytes_per_pixel;
        for (int i = 1; i < width; ++i) {
            *out++ = current_line[i].r - current_line[i - 1].r;
            *out++ = current_line[i].g - current_line[i - 1].g;
            *out++ = current_line[i].b - current_line[i - 1].b;
            if (with_alpha) *out++ = current_line[i].a - current_line[i - 1].a;
        }

        stream.avail_in = 1 + width * bytes_per_pixel;
        stream.next_in  = (uint8_t *)line_buffer;

        deflate(&stream, (y == height - 1) ? Z_FINISH : Z_NO_FLUSH);
    }

    assert(stream.avail_in == 0);  // We expect that to be used up.
    block.updateWritten(compress_avail - stream.avail_out);
    deflateEnd(&stream);
    delete[] line_buffer;

    block.StartNextBlock("IEND");
    return block.Finalize() - (uint8_t *)buffer;
}

size_t EncodePNG(const Framebuffer &fb, int compression_level,
                 ColorEncoding encoding, char *buffer, size_t size) {
    if (encoding == ColorEncoding::kRGBA_32)
        return EncodePNGInternal<true>(fb, compression_level, buffer, size);
    else
        return EncodePNGInternal<false>(fb, compression_level, buffer, size);
}
}  // namespace timg
