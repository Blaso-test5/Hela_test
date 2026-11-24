#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hela {

struct ResourceUid {
    uint64_t serial = 0;
    uint32_t generation = 0;

    bool Valid() const { return serial != 0; }
    bool operator==(const ResourceUid& other) const {
        return serial == other.serial && generation == other.generation;
    }
    bool operator!=(const ResourceUid& other) const { return !(*this == other); }
    std::string Text() const {
        return std::to_string(serial) + "." + std::to_string(generation);
    }
};

class UidIssuer {
public:
    static UidIssuer& Instance() {
        static UidIssuer issuer;
        return issuer;
    }

    ResourceUid Next() {
        ResourceUid uid;
        uid.serial = next_.fetch_add(1, std::memory_order_relaxed) + 1;
        uid.generation = 0;
        return uid;
    }

private:
    std::atomic<uint64_t> next_{0};
};

enum class CausalOp { Draw, Copy, Readback };

struct CausalEvent {
    CausalOp op = CausalOp::Draw;
    ResourceUid src;
    ResourceUid dst;
    uint64_t presentCount = 0;
    uint64_t clOrdinal = 0;
    uint64_t copySequence = 0;
    uint64_t fenceValue = 0;
};

struct ChainProof {
    bool ok = false;
    std::vector<std::string> reasons;
};

inline ChainProof ValidateChain(const CausalEvent& draw,
                                const CausalEvent& copy,
                                const CausalEvent& readback) {
    ChainProof proof;
    const auto fail = [&proof](const char* reason) {
        proof.reasons.push_back(reason);
    };

    if (draw.op != CausalOp::Draw) fail("first event is not a draw");
    if (copy.op != CausalOp::Copy) fail("second event is not a copy");
    if (readback.op != CausalOp::Readback) fail("third event is not a readback");

    if (!draw.dst.Valid() || !copy.src.Valid() || !copy.dst.Valid() || !readback.src.Valid()) {
        fail("missing resource UID");
    } else {
        if (draw.dst != copy.src) fail("draw.dst != copy.src");
        if (copy.dst != readback.src) fail("copy.dst != readback.src");
    }

    if (!(draw.clOrdinal < copy.clOrdinal && copy.clOrdinal < readback.clOrdinal)) {
        fail("clOrdinal is not strictly increasing");
    }
    if (!(draw.fenceValue <= copy.fenceValue && copy.fenceValue <= readback.fenceValue)) {
        fail("fenceValue is not monotonic");
    }
    if (copy.copySequence == 0) fail("copySequence missing");
    if (copy.copySequence != readback.copySequence) fail("copySequence mismatch");
    if (copy.presentCount != readback.presentCount) fail("Present window mismatch");

    proof.ok = proof.reasons.empty();
    return proof;
}

} // namespace hela
