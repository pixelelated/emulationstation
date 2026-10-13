// SPDX-License-Identifier: MIT AND BSD-3-Clause
// Copyright (c) 2026 pixelelated contributors
// Padding recurrence/serialization adapted from bodgit/rvz (BSD-3-Clause).
// See RVZ-NOTICES.md and LICENSE.rvz-padding. Container code follows Dolphin's
// published WiaAndRvz format; no RetroArch or Dolphin implementation is copied.
#include "rvz_hash.h"
#include "rcheevos/include/rc_hash.h"
#include "libretro-common/include/streams/interface_stream.h"
#include "libretro-common/include/libretro.h"
#include <boost/uuid/detail/sha1.hpp>
#include <zstd.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>

namespace
{
constexpr uint64_t Sector = 0x8000;
constexpr uint32_t MaxChunk = 2 * 1024 * 1024;
constexpr uint32_t MaxWork = MaxChunk + 1024 * 1024;
constexpr uint64_t MaxDisc = uint64_t(2) * 1024 * 1024 * 1024;
constexpr uint64_t MaxFile = uint64_t(4) * 1024 * 1024 * 1024;
constexpr uint32_t MaxGroups = 65536;
constexpr uint32_t MaxRaw = 4096;
using Bytes = std::vector<uint8_t>;

void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Invalid or unsupported GameCube RVZ");
}
uint32_t be32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
        (uint32_t(p[2]) << 8) | p[3];
}
uint64_t be64(const uint8_t* p)
{
    return (uint64_t(be32(p)) << 32) | be32(p + 4);
}
bool sha1Matches(const uint8_t* data, size_t size, const uint8_t* expected)
{
    boost::uuids::detail::sha1 state;
    boost::uuids::detail::sha1::digest_type digest;
    state.process_bytes(data, size);
    state.get_digest(digest);
    // Boost changed digest_type from host words to bytes. Accept both APIs.
    for (size_t i = 0; i < 20; ++i)
    {
        const auto byte = sizeof(digest[0]) == 1 ? digest[i] :
            ((digest[i / 4] >> (24 - 8 * (i % 4))) & 0xff);
        if (byte != expected[i])
            return false;
    }
    return true;
}

// BSD-3-Clause adaptation of bodgit/rvz/internal/padding/reader.go.
// State is local, and output uses explicit bytes on both endian families.
void padding(const uint8_t* seed, uint64_t offset, uint8_t* out, size_t size)
{
    std::array<uint32_t, 521> words{};
    for (size_t i = 0; i < 17; ++i)
        words[i] = be32(seed + 4 * i);
    for (size_t i = 17; i < words.size(); ++i)
        words[i] = (words[i - 17] << 23) ^ (words[i - 16] >> 9) ^ words[i - 1];
    const auto advance = [&]() {
        for (size_t i = 0; i < 32; ++i)
            words[i] ^= words[i + words.size() - 32];
        for (size_t i = 32; i < words.size(); ++i)
            words[i] ^= words[i - 32];
    };
    for (unsigned i = 0; i < 4; ++i)
        advance();
    size_t byteIndex = static_cast<size_t>(offset % Sector);
    while (byteIndex >= words.size() * 4)
    {
        advance();
        byteIndex -= words.size() * 4;
    }
    const unsigned shifts[] = {24, 18, 8, 0};
    for (size_t i = 0; i < size; ++i)
    {
        if (byteIndex == words.size() * 4)
        {
            advance();
            byteIndex = 0;
        }
        out[i] = static_cast<uint8_t>(words[byteIndex / 4] >> shifts[byteIndex % 4]);
        ++byteIndex;
    }
}

Bytes unpack(const Bytes& packed, size_t expected, uint64_t discOffset)
{
    Bytes result(expected);
    size_t input = 0, output = 0, records = 0;
    while (input < packed.size())
    {
        require(++records <= 4096 && packed.size() - input >= 4);
        const uint32_t tag = be32(packed.data() + input);
        input += 4;
        const uint32_t length = tag & 0x7fffffff;
        require(length <= result.size() - output);
        if (tag & 0x80000000)
        {
            require(packed.size() - input >= 68);
            if (length)
                padding(packed.data() + input, discOffset + output, result.data() + output, length);
            input += 68;
        }
        else
        {
            require(length <= packed.size() - input);
            if (length)
                std::memcpy(result.data() + output, packed.data() + input, length);
            input += length;
        }
        output += length;
    }
    require(output == expected);
    return result;
}

struct StreamCloser
{
    void operator()(intfstream_t* stream) const
    {
        if (stream)
        {
            intfstream_close(stream);
            std::free(stream);
        }
    }
};
struct Raw
{
    uint64_t offset, end, aligned;
    uint32_t first, count;
};
struct Group
{
    uint64_t offset;
    uint32_t size, packed;
    bool compressed;
};
class Reader
{
public:
    explicit Reader(const char* path) : stream(intfstream_open_file(path,
        RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE))
    {
        require(stream != nullptr);
        const int64_t size = intfstream_get_size(stream.get());
        require(size >= 0x48 + 0xd5 && uint64_t(size) <= MaxFile);
        fileSize = static_cast<uint64_t>(size);
        const Bytes header = physical(0, 0x48);
        require(be32(header.data()) == 0x52565a01);
        const uint32_t version = be32(header.data() + 4);
        const uint32_t compatible = be32(header.data() + 8);
        require(version >= 0x00030000 && version <= 0x01000000 &&
            compatible >= 0x00030000 && compatible <= version);
        require(sha1Matches(header.data(), 0x34, header.data() + 0x34));
        discSize = be64(header.data() + 0x24);
        require(discSize >= 0x2460 && discSize <= MaxDisc &&
            be64(header.data() + 0x2c) == fileSize);
        const uint32_t secondSize = be32(header.data() + 0xc);
        require(secondSize >= 0xd5 && secondSize <= 4096);
        const Bytes second = physical(0x48, secondSize);
        require(sha1Matches(second.data(), second.size(), header.data() + 0x10));
        require(be32(second.data()) == 1 && be32(second.data() + 0x90) == 0);
        compression = be32(second.data() + 4);
        require((compression == 0 || compression == 5) && second[0xd4] == 0);
        chunk = be32(second.data() + 0xc);
        require(chunk >= Sector && chunk <= MaxChunk && (chunk & (chunk - 1)) == 0);
        std::copy_n(second.data() + 0x10, head.size(), head.begin());
        require(be32(head.data() + 0x1c) == 0xc2339f3d);
        const uint32_t rawCount = be32(second.data() + 0xb4);
        const uint32_t groupCount = be32(second.data() + 0xc4);
        require(rawCount && rawCount <= MaxRaw && groupCount && groupCount <= MaxGroups);
        Bytes table = decode(be64(second.data() + 0xc8), be32(second.data() + 0xd0),
            groupCount * 12, compression == 5);
        groups.reserve(groupCount);
        for (uint32_t i = 0; i < groupCount; ++i)
        {
            const uint8_t* p = table.data() + size_t(i) * 12;
            const uint32_t flags = be32(p + 4);
            Group group{uint64_t(be32(p)) * 4, flags & 0x7fffffff, be32(p + 8),
                (flags & 0x80000000) != 0};
            require(group.size <= MaxWork && group.packed <= MaxWork);
            require(group.offset <= fileSize && group.size <= fileSize - group.offset);
            groups.push_back(group);
        }
        table = decode(be64(second.data() + 0xb8), be32(second.data() + 0xc0),
            rawCount * 24, compression == 5);
        raw.reserve(rawCount);
        for (uint32_t i = 0; i < rawCount; ++i)
        {
            const uint8_t* p = table.data() + size_t(i) * 24;
            const uint64_t offset = be64(p), length = be64(p + 8);
            require(offset < discSize && length && length <= discSize - offset);
            Raw entry{offset, offset + length, offset - offset % Sector,
                be32(p + 16), be32(p + 20)};
            require(entry.first < groups.size() && entry.count <= groups.size() - entry.first);
            require(entry.count == (entry.end - entry.aligned + chunk - 1) / chunk);
            raw.push_back(entry);
        }
        std::sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) { return a.offset < b.offset; });
        uint64_t end = raw.front().offset;
        require(end == 0 || end == head.size());
        for (const Raw& entry : raw)
        {
            require(entry.offset == end);
            end = entry.end;
        }
        require(end == discSize);
    }

    void seek(int64_t offset, int origin)
    {
        uint64_t base;
        if (origin == SEEK_SET) base = 0;
        else if (origin == SEEK_CUR) base = position;
        else if (origin == SEEK_END) base = discSize;
        else { failed = true; return; }
        if (offset < 0)
        {
            if (offset < -static_cast<int64_t>(base)) { failed = true; return; }
            position = base - static_cast<uint64_t>(-offset);
        }
        else
        {
            if (static_cast<uint64_t>(offset) > discSize - base) { failed = true; return; }
            position = base + static_cast<uint64_t>(offset);
        }
    }
    int64_t tell() const { return static_cast<int64_t>(position); }
    size_t read(void* destination, size_t requested)
    {
        if (failed || requested > discSize - position || requested > 128 * 1024 * 1024 - totalRead)
        {
            failed = true;
            return 0;
        }
        totalRead += requested;
        auto* out = static_cast<uint8_t*>(destination);
        size_t done = 0;
        while (done < requested)
        {
            if (position < head.size())
            {
                const size_t take = std::min(requested - done, head.size() - size_t(position));
                std::memcpy(out + done, head.data() + position, take);
                position += take;
                done += take;
                continue;
            }
            const auto found = std::upper_bound(raw.begin(), raw.end(), position,
                [](uint64_t value, const Raw& entry) { return value < entry.offset; });
            require(found != raw.begin());
            const Raw& entry = *(found - 1);
            require(position < entry.end);
            const uint64_t groupBase = entry.aligned + (position - entry.aligned) / chunk * chunk;
            const uint32_t index = entry.first + uint32_t((position - entry.aligned) / chunk);
            const size_t length = static_cast<size_t>(std::min(uint64_t(chunk), entry.end - groupBase));
            if (cachedIndex != index || cachedBase != groupBase || cache.size() != length)
            {
                const Group& group = groups[index];
                Bytes decoded;
                if (!group.size)
                {
                    accountDecoded(length);
                    decoded.assign(length, 0);
                }
                else
                {
                    decoded = decode(group.offset, group.size,
                        group.packed ? group.packed : length, group.compressed && compression == 5);
                    if (group.packed)
                    {
                        accountDecoded(length);
                        decoded = unpack(decoded, length, groupBase);
                    }
                }
                cache = std::move(decoded);
                cachedIndex = index;
                cachedBase = groupBase;
            }
            const size_t into = static_cast<size_t>(position - groupBase);
            const size_t take = std::min(requested - done, cache.size() - into);
            require(take != 0);
            std::memcpy(out + done, cache.data() + into, take);
            done += take;
            position += take;
        }
        return done;
    }
    bool failed = false;

private:
    Bytes physical(uint64_t offset, size_t bytes)
    {
        require(bytes <= MaxWork && offset <= fileSize && bytes <= fileSize - offset);
        require(intfstream_seek(stream.get(), static_cast<int64_t>(offset), SEEK_SET) >= 0);
        Bytes result(bytes);
        require(intfstream_read(stream.get(), result.data(), bytes) == static_cast<int64_t>(bytes));
        return result;
    }
    void accountDecoded(size_t bytes)
    {
        require(bytes <= MaxWork && bytes <= 512 * 1024 * 1024 - totalDecoded);
        totalDecoded += bytes;
    }
    Bytes decode(uint64_t offset, uint32_t bytes, size_t expected, bool compressed)
    {
        accountDecoded(expected);
        Bytes input = physical(offset, bytes);
        if (!compressed)
        {
            require(input.size() == expected);
            return input;
        }
        require(!input.empty());
        const size_t frameSize = ZSTD_findFrameCompressedSize(input.data(), input.size());
        require(!ZSTD_isError(frameSize) && frameSize == input.size());
        Bytes output(expected);
        const size_t actual = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
        require(!ZSTD_isError(actual) && actual == expected);
        return output;
    }
    std::unique_ptr<intfstream_t, StreamCloser> stream;
    uint64_t fileSize = 0, discSize = 0, position = 0;
    uint32_t chunk = 0, compression = 0;
    std::array<uint8_t, 128> head{};
    std::vector<Raw> raw;
    std::vector<Group> groups;
    Bytes cache;
    uint32_t cachedIndex = UINT32_MAX;
    uint64_t cachedBase = 0;
    size_t totalRead = 0, totalDecoded = 0;
};

void* openRvz(const char* path)
{
    try { return new Reader(path); }
    catch (...) { return nullptr; }
}
void closeRvz(void* opaque) { delete static_cast<Reader*>(opaque); }
void seekRvz(void* opaque, int64_t offset, int origin)
{
    static_cast<Reader*>(opaque)->seek(offset, origin);
}
int64_t tellRvz(void* opaque) { return static_cast<Reader*>(opaque)->tell(); }
size_t readRvz(void* opaque, void* data, size_t bytes)
{
    Reader& reader = *static_cast<Reader*>(opaque);
    try { return reader.read(data, bytes); }
    catch (...) { reader.failed = true; return 0; }
}
struct Iterator
{
    explicit Iterator(const char* path) { rc_hash_initialize_iterator(&value, path, nullptr, 0); }
    ~Iterator() { rc_hash_destroy_iterator(&value); }
    rc_hash_iterator_t value;
};
} // namespace

bool generateGameCubeRvzHash(char hash[33], const char* path)
{
    hash[0] = '\0';
    try
    {
        Iterator iterator(path);
        if (!iterator.value.path)
            return false;
        iterator.value.callbacks.filereader = {openRvz, seekRvz, tellRvz, readRvz, closeRvz};
        if (rc_hash_generate(hash, RC_CONSOLE_GAMECUBE, &iterator.value))
            return true;
    }
    catch (...) {}
    hash[0] = '\0';
    return false;
}
