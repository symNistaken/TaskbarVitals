#include "../src/drivers/fastpair_stream.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

int main() {
    // Captured from a Marshall EMBERTON II right after the stream opened (two reads on Windows).
    const Bytes model = { 0x03, 0x01, 0x00, 0x03, 0x15, 0xae, 0x46 };
    const Bytes address = { 0x03, 0x02, 0x00, 0x06, 0x74, 0xc4, 0x92, 0xcd, 0xb2, 0x7c };

    {
        FastPairStream::Info info;
        assert(FastPairStream::Parse(model.data(), model.size(), info) == model.size());
        assert(info.hasModelId && info.modelId == 0x15AE46 && !info.hasLeAddress);
        assert(FastPairStream::Parse(address.data(), address.size(), info) == address.size());
        assert(info.hasLeAddress && info.leAddress == 0x74C492CDB27CULL);  // 74:C4:92:CD:B2:7C
    }

    // Both messages in one read, after an acknowledgement for an unrelated message (group FF).
    {
        Bytes all = { 0xff, 0x01, 0x00, 0x02, 0x03, 0x08 };
        all.insert(all.end(), model.begin(), model.end());
        all.insert(all.end(), address.begin(), address.end());
        FastPairStream::Info info;
        assert(FastPairStream::Parse(all.data(), all.size(), info) == all.size());
        assert(info.hasModelId && info.modelId == 0x15AE46);
        assert(info.hasLeAddress && info.leAddress == 0x74C492CDB27CULL);
    }

    // A message split across reads: nothing past the last complete message is consumed.
    {
        Bytes all = model;
        all.insert(all.end(), address.begin(), address.begin() + 5);
        FastPairStream::Info info;
        std::size_t used = FastPairStream::Parse(all.data(), all.size(), info);
        assert(used == model.size() && info.hasModelId && !info.hasLeAddress);
        Bytes rest(all.begin() + used, all.end());
        rest.insert(rest.end(), address.begin() + 5, address.end());
        assert(FastPairStream::Parse(rest.data(), rest.size(), info) == rest.size());
        assert(info.hasLeAddress && info.leAddress == 0x74C492CDB27CULL);
    }

    // Wrong lengths and an all-zero address are ignored; a header alone is incomplete.
    {
        const Bytes bad = { 0x03, 0x01, 0x00, 0x02, 0x15, 0xae,                      // model id must be 3 bytes
                            0x03, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // zero address
                            0x03, 0x02, 0x00 };                                          // partial header
        FastPairStream::Info info;
        assert(FastPairStream::Parse(bad.data(), bad.size(), info) == 16);
        assert(!info.hasModelId && !info.hasLeAddress);
    }

    // A later address update replaces the earlier one (the LE address rotates).
    {
        Bytes two = address;
        const Bytes newer = { 0x03, 0x02, 0x00, 0x06, 0x69, 0xb4, 0xf5, 0xf4, 0x8a, 0x3c };
        two.insert(two.end(), newer.begin(), newer.end());
        FastPairStream::Info info;
        FastPairStream::Parse(two.data(), two.size(), info);
        assert(info.leAddress == 0x69B4F5F48A3CULL);
    }

    std::puts("Fast Pair message stream tests passed.");
}
