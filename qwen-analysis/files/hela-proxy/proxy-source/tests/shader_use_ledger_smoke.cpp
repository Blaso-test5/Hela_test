#include "shader_use_ledger.h"

#include <cassert>

int main() {
    using d3d12on11::ShaderUseKey;
    using d3d12on11::ShaderUseLedger;
    using d3d12on11::ShaderUseEventRing;

    ShaderUseLedger ledger;
    ShaderUseKey executed{};
    executed.vsHash = 0x10;
    executed.psHash = 0x20;
    executed.gsHash = 0x30;
    executed.hsHash = 0x40;
    executed.dsHash = 0x50;
    executed.hasGraphicsPipeline = true;
    ShaderUseKey skipped = executed;
    skipped.missingStageMask = 0x02;
    skipped.skipped = true;
    skipped.backbuffer = true;
    ShaderUseKey compute{};
    compute.compute = true;
    compute.csHash = 0x60;
    compute.missingStageMask = 0x20;
    compute.skipped = true;

    ledger.Record(executed, 3, 2);
    ledger.Record(executed, 5, 3);
    ledger.Record(skipped, 5, 4);
    ledger.Record(compute, 6, 2);

    auto snapshot = ledger.TakeSnapshot(8);
    assert(snapshot.totalDraws == 9);
    assert(snapshot.totalDispatches == 2);
    assert(snapshot.overflowDraws == 0);
    assert(snapshot.overflowDispatches == 0);
    assert(snapshot.entries.size() == 3);
    assert(snapshot.entries[0].count.executions == 5);
    assert(!snapshot.entries[0].key.compute);
    assert(snapshot.entries[0].key.vsHash == 0x10);
    assert(snapshot.entries[0].key.gsHash == 0x30);
    assert(snapshot.entries[0].key.hsHash == 0x40);
    assert(snapshot.entries[0].key.dsHash == 0x50);
    assert(snapshot.entries[0].key.hasGraphicsPipeline);
    assert(snapshot.entries[0].count.firstPresentCall == 3);
    assert(snapshot.entries[0].count.lastPresentCall == 5);
    assert(snapshot.entries[1].key.skipped);
    assert(snapshot.entries[1].key.backbuffer);
    assert(snapshot.entries[1].key.missingStageMask == 0x02);
    assert(snapshot.entries[1].count.executions == 4);
    assert(snapshot.entries[2].key.compute);
    assert(snapshot.entries[2].key.csHash == 0x60);
    assert(snapshot.entries[2].key.missingStageMask == 0x20);
    assert(snapshot.entries[2].count.executions == 2);
    uint64_t trackedDraws = 0;
    uint64_t trackedDispatches = 0;
    for (const auto& entry : snapshot.entries) {
        if (entry.key.compute)
            trackedDispatches += entry.count.executions;
        else
            trackedDraws += entry.count.executions;
    }
    assert(trackedDraws + snapshot.omittedDraws + snapshot.overflowDraws ==
        snapshot.totalDraws);
    assert(trackedDispatches + snapshot.omittedDispatches +
        snapshot.overflowDispatches == snapshot.totalDispatches);

    const auto recentEvents = ledger.RecentEvents(ShaderUseEventRing::Capacity);
    assert(recentEvents.size() == 4);
    assert(recentEvents[0].ordinal == 1);
    assert(recentEvents[0].presentCall == 3);
    assert(recentEvents[0].executionCount == 2);
    assert(!recentEvents[0].key.skipped);
    assert(recentEvents[2].key.skipped);
    assert(recentEvents[3].key.compute);

    snapshot = ledger.TakeSnapshot(0);
    assert(snapshot.totalDraws == 0);
    assert(snapshot.totalDispatches == 0);
    assert(snapshot.entries.empty());

    for (std::size_t i = 0; i < ShaderUseLedger::MaxTrackedGroups + 1; ++i) {
        ShaderUseKey key{};
        key.compute = i % 2 == 0;
        key.vsHash = key.compute ? 0 : static_cast<uint64_t>(i + 1);
        key.csHash = key.compute ? static_cast<uint64_t>(i + 1) : 0;
        ledger.Record(key, 7);
    }
    snapshot = ledger.TakeSnapshot(1);
    assert(snapshot.totalDraws == ShaderUseLedger::MaxTrackedGroups / 2);
    assert(snapshot.totalDispatches == ShaderUseLedger::MaxTrackedGroups / 2 + 1);
    assert(snapshot.overflowDraws == 0);
    assert(snapshot.overflowDispatches == 1);
    assert(snapshot.entries.size() == 1);
    assert(snapshot.omittedGroups == ShaderUseLedger::MaxTrackedGroups - 1);
    assert(snapshot.omittedDraws + snapshot.omittedDispatches ==
        ShaderUseLedger::MaxTrackedGroups - 1);
    trackedDraws = 0;
    trackedDispatches = 0;
    for (const auto& entry : snapshot.entries) {
        if (entry.key.compute)
            trackedDispatches += entry.count.executions;
        else
            trackedDraws += entry.count.executions;
    }
    assert(trackedDraws + snapshot.omittedDraws + snapshot.overflowDraws ==
        snapshot.totalDraws);
    assert(trackedDispatches + snapshot.omittedDispatches +
        snapshot.overflowDispatches == snapshot.totalDispatches);
    const auto overflowEvent = ledger.RecentEvents(1);
    assert(overflowEvent.size() == 1);
    assert(overflowEvent[0].ordinal ==
        4 + ShaderUseLedger::MaxTrackedGroups + 1);
    assert(overflowEvent[0].key.compute);
    assert(overflowEvent[0].key.csHash == ShaderUseLedger::MaxTrackedGroups + 1);

    ShaderUseLedger priorityLedger;
    ShaderUseKey common{};
    common.vsHash = 0x80;
    for (std::size_t i = 0; i < 10; ++i) priorityLedger.Record(common, 8);
    ShaderUseKey rareSkipped{};
    rareSkipped.vsHash = 0x90;
    rareSkipped.skipped = true;
    priorityLedger.Record(rareSkipped, 8);
    snapshot = priorityLedger.TakeSnapshot(1);
    assert(snapshot.entries.size() == 1);
    assert(snapshot.entries[0].key.skipped);

    ShaderUseLedger pipelineStateLedger;
    ShaderUseKey graphicsPso{};
    graphicsPso.hasGraphicsPipeline = true;
    ShaderUseKey noGraphicsPso{};
    pipelineStateLedger.Record(graphicsPso, 9);
    pipelineStateLedger.Record(noGraphicsPso, 9);
    snapshot = pipelineStateLedger.TakeSnapshot(4);
    assert(snapshot.entries.size() == 2);
    assert(snapshot.entries[0].key.hasGraphicsPipeline !=
        snapshot.entries[1].key.hasGraphicsPipeline);

    ShaderUseEventRing eventRing;
    for (uint64_t i = 1; i <= ShaderUseEventRing::Capacity + 1; ++i) {
        ShaderUseKey key{};
        key.compute = (i % 2) == 0;
        key.skipped = (i % 3) == 0;
        eventRing.Record(key, i + 10, i);
    }
    const auto tail = eventRing.Tail(ShaderUseEventRing::Capacity + 1);
    assert(tail.size() == ShaderUseEventRing::Capacity);
    assert(tail.front().ordinal == 2);
    assert(tail.front().presentCall == 12);
    assert(tail.back().ordinal == ShaderUseEventRing::Capacity + 1);
    assert(tail.back().executionCount == ShaderUseEventRing::Capacity + 1);
    assert(!tail.back().key.compute);
    assert(eventRing.Tail(0).empty());

    return 0;
}
