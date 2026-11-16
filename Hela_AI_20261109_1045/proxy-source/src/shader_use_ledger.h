#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

namespace d3d12on11 {

struct ShaderUseKey {
    bool compute = false;
    uint64_t vsHash = 0;
    uint64_t psHash = 0;
    uint64_t gsHash = 0;
    uint64_t hsHash = 0;
    uint64_t dsHash = 0;
    uint64_t csHash = 0;
    uint32_t missingStageMask = 0;
    bool hasGraphicsPipeline = false;
    bool skipped = false;
    bool backbuffer = false;

    bool operator<(const ShaderUseKey& other) const {
        return std::tie(compute, vsHash, psHash, gsHash, hsHash, dsHash, csHash,
                   missingStageMask, hasGraphicsPipeline, skipped, backbuffer) <
            std::tie(other.compute, other.vsHash, other.psHash, other.gsHash,
                other.hsHash, other.dsHash, other.csHash, other.missingStageMask,
                other.hasGraphicsPipeline, other.skipped, other.backbuffer);
    }
};

struct ShaderUseCount {
    uint64_t executions = 0;
    uint64_t firstPresentCall = 0;
    uint64_t lastPresentCall = 0;
};

struct ShaderUseSnapshot {
    struct Entry {
        ShaderUseKey key;
        ShaderUseCount count;
    };

    std::vector<Entry> entries;
    uint64_t totalDraws = 0;
    uint64_t totalDispatches = 0;
    uint64_t overflowDraws = 0;
    uint64_t overflowDispatches = 0;
    uint64_t omittedGroups = 0;
    uint64_t omittedDraws = 0;
    uint64_t omittedDispatches = 0;
};

class ShaderUseLedger {
public:
    static constexpr size_t MaxTrackedGroups = 512;

    void Record(const ShaderUseKey& key, uint64_t presentCall,
        uint64_t executionCount = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (key.compute)
            totalDispatches_ += executionCount;
        else
            totalDraws_ += executionCount;

        auto found = groups_.find(key);
        if (found == groups_.end()) {
            if (groups_.size() >= MaxTrackedGroups) {
                if (key.compute)
                    overflowDispatches_ += executionCount;
                else
                    overflowDraws_ += executionCount;
                return;
            }
            found = groups_.emplace(key, ShaderUseCount{}).first;
            found->second.firstPresentCall = presentCall;
        }
        found->second.executions += executionCount;
        found->second.lastPresentCall = presentCall;
    }

    ShaderUseSnapshot TakeSnapshot(size_t maxEntries) {
        std::lock_guard<std::mutex> lock(mutex_);
        ShaderUseSnapshot snapshot;
        snapshot.totalDraws = totalDraws_;
        snapshot.totalDispatches = totalDispatches_;
        snapshot.overflowDraws = overflowDraws_;
        snapshot.overflowDispatches = overflowDispatches_;
        snapshot.entries.reserve(groups_.size());
        for (const auto& group : groups_)
            snapshot.entries.push_back({group.first, group.second});

        std::sort(snapshot.entries.begin(), snapshot.entries.end(),
            [](const auto& left, const auto& right) {
                const bool leftPriority =
                    left.key.skipped || left.key.missingStageMask != 0 ||
                    (!left.key.compute && !left.key.hasGraphicsPipeline);
                const bool rightPriority =
                    right.key.skipped || right.key.missingStageMask != 0 ||
                    (!right.key.compute && !right.key.hasGraphicsPipeline);
                if (leftPriority != rightPriority)
                    return leftPriority;
                if (left.count.executions != right.count.executions)
                    return left.count.executions > right.count.executions;
                return left.key < right.key;
            });

        if (snapshot.entries.size() > maxEntries) {
            snapshot.omittedGroups = snapshot.entries.size() - maxEntries;
            for (size_t i = maxEntries; i < snapshot.entries.size(); ++i) {
                const auto& entry = snapshot.entries[i];
                if (entry.key.compute)
                    snapshot.omittedDispatches += entry.count.executions;
                else
                    snapshot.omittedDraws += entry.count.executions;
            }
            snapshot.entries.resize(maxEntries);
        }

        groups_.clear();
        totalDraws_ = 0;
        totalDispatches_ = 0;
        overflowDraws_ = 0;
        overflowDispatches_ = 0;
        return snapshot;
    }

private:
    std::mutex mutex_;
    std::map<ShaderUseKey, ShaderUseCount> groups_;
    uint64_t totalDraws_ = 0;
    uint64_t totalDispatches_ = 0;
    uint64_t overflowDraws_ = 0;
    uint64_t overflowDispatches_ = 0;
};

} // namespace d3d12on11
