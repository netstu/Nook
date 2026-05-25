#pragma once

namespace JavaHookClassObserver {

class ScopedSuppression {
public:
    ScopedSuppression();
    ~ScopedSuppression();

    ScopedSuppression(const ScopedSuppression&) = delete;
    ScopedSuppression& operator=(const ScopedSuppression&) = delete;
};

bool IsSuppressed();
bool EnsureInstalled();
void SchedulePendingRetry(const char* class_name, int delay_ms);
void Reset();
void ResetInheritedStateForChild();

}
