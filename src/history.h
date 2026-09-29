#pragma once
#include <algorithm>
#include <vector>
#include <cstdint>

// Store complete layout handles: two layouts of the same language are distinct.
class LayoutHistory {
public:
    using Id = uintptr_t;
    void SetAvailable(std::vector<Id> available) {
        available_ = std::move(available);
        recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
            [this](Id id) { return !Contains(id); }), recent_.end());
        for (Id id : available_) if (std::find(recent_.begin(), recent_.end(), id) == recent_.end())
            recent_.push_back(id);
    }
    bool Observe(Id id) {
        if (!Contains(id)) return false;
        if (!recent_.empty() && recent_[0] == id) return false;
        recent_.erase(std::remove(recent_.begin(), recent_.end(), id), recent_.end());
        recent_.insert(recent_.begin(), id);
        return true;
    }
    Id Current() const { return recent_.empty() ? 0 : recent_[0]; }
    Id Previous() const { return recent_.size() < 2 ? 0 : recent_[1]; }
private:
    bool Contains(Id id) const { return std::find(available_.begin(), available_.end(), id) != available_.end(); }
    std::vector<Id> available_, recent_;
};
