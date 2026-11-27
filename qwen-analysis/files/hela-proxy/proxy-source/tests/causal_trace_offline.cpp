#include "../src/causal_trace.h"

#include <cstdio>
#include <set>

namespace {
int failures = 0;

void Check(bool condition, const char* name) {
    if (condition) {
        std::printf("PASS %s\n", name);
    } else {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

hela::CausalEvent Draw(const hela::ResourceUid& dst, uint64_t ordinal,
                       uint64_t fence, uint64_t present = 100) {
    hela::CausalEvent event;
    event.op = hela::CausalOp::Draw;
    event.dst = dst;
    event.clOrdinal = ordinal;
    event.fenceValue = fence;
    event.presentCount = present;
    return event;
}

hela::CausalEvent Copy(const hela::ResourceUid& src,
                       const hela::ResourceUid& dst,
                       uint64_t ordinal, uint64_t fence,
                       uint64_t sequence, uint64_t present = 100) {
    hela::CausalEvent event;
    event.op = hela::CausalOp::Copy;
    event.src = src;
    event.dst = dst;
    event.clOrdinal = ordinal;
    event.fenceValue = fence;
    event.copySequence = sequence;
    event.presentCount = present;
    return event;
}

hela::CausalEvent Readback(const hela::ResourceUid& src,
                           uint64_t ordinal, uint64_t fence,
                           uint64_t sequence, uint64_t present = 100) {
    hela::CausalEvent event;
    event.op = hela::CausalOp::Readback;
    event.src = src;
    event.clOrdinal = ordinal;
    event.fenceValue = fence;
    event.copySequence = sequence;
    event.presentCount = present;
    return event;
}
}

int main() {
    using namespace hela;

    std::set<uint64_t> serials;
    for (int i = 0; i < 1000; ++i) {
        serials.insert(UidIssuer::Instance().Next().serial);
    }
    Check(serials.size() == 1000, "unique_resource_UIDs");
    Check(!ResourceUid{}.Valid(), "zero_resource_UID_invalid");

    const ResourceUid target = UidIssuer::Instance().Next();
    const ResourceUid backbuffer = UidIssuer::Instance().Next();
    const ResourceUid other = UidIssuer::Instance().Next();

    Check(ValidateChain(Draw(target, 10, 5),
                       Copy(target, backbuffer, 11, 5, 7),
                       Readback(backbuffer, 12, 6, 7)).ok,
          "valid_chain");
    Check(!ValidateChain(Draw(other, 10, 5),
                         Copy(target, backbuffer, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 7)).ok,
          "different_resource_rejected");
    Check(!ValidateChain(Draw(target, 10, 5),
                         Copy(target, other, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 7)).ok,
          "readback_source_mismatch_rejected");
    Check(!ValidateChain(Draw(target, 10, 5),
                         Copy(target, backbuffer, 12, 5, 7),
                         Readback(backbuffer, 11, 6, 7)).ok,
          "out_of_order_rejected");
    Check(!ValidateChain(Draw(target, 10, 9),
                         Copy(target, backbuffer, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 7)).ok,
          "non_monotonic_fence_rejected");
    Check(!ValidateChain(Draw(target, 10, 5),
                         Copy(target, backbuffer, 11, 5, 0),
                         Readback(backbuffer, 12, 6, 0)).ok,
          "zero_copy_sequence_rejected");
    Check(!ValidateChain(Draw(target, 10, 5),
                         Copy(target, backbuffer, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 8)).ok,
          "copy_sequence_mismatch_rejected");
    Check(!ValidateChain(Draw(ResourceUid{}, 10, 5),
                         Copy(ResourceUid{}, backbuffer, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 7)).ok,
          "zero_resource_UID_rejected");
    Check(!ValidateChain(Draw(target, 10, 5),
                         Copy(target, backbuffer, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 7, 200)).ok,
          "Present_window_mismatch_rejected");

    ResourceUid targetGeneration1 = target;
    targetGeneration1.generation = 1;
    Check(!ValidateChain(Draw(targetGeneration1, 10, 5),
                         Copy(target, backbuffer, 11, 5, 7),
                         Readback(backbuffer, 12, 6, 7)).ok,
          "generation_mismatch_rejected");

    if (failures != 0) {
        std::printf("%d test falliti\n", failures);
        return 1;
    }
    std::printf("tutti i test causal_trace offline passano\n");
    return 0;
}
