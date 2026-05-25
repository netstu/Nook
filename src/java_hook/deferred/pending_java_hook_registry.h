#pragma once

#include "nook/NookJavaHook.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class PendingJavaHookRegistry {
public:
    struct Request {
        int request_id;
        std::string class_name;
        std::string dot_class_name;
        std::string method_name;
        std::string signature;
        std::string resolved_signature;
        int is_static;
        uint64_t loader_handle = 0u;
        NookJavaHookCallback callback;
        int hook_id;
        bool installed;
        bool active;
        bool installing;
        bool retry_scheduled;
    };

    static PendingJavaHookRegistry& Instance();

    int Register(const char* class_name,
                 const char* method_name,
                 const char* signature,
                 int is_static,
                 uint64_t loader_handle,
                 NookJavaHookCallback callback);

    bool TryBeginInstall(int request_id, Request* out_request);
    void FinishInstall(int request_id, int hook_id);

    std::vector<int> CollectPendingRequestIds(const char* class_name) const;
    std::vector<int> MarkRetryScheduled(const char* class_name);
    void ClearRetryScheduled(const std::vector<int>& request_ids);

    bool HasAnyPending() const;
    bool HasPendingForClass(const char* class_name) const;
    bool TryGetInstalledHookId(int request_id, int* installed_hook_id) const;
    bool TryGetResolvedSignature(int request_id, std::string* resolved_signature) const;
    void SetResolvedSignature(int request_id, const std::string& resolved_signature);
    bool RemoveRequest(int request_id, int* installed_hook_id);
    void Clear(void);
    void ResetInheritedStateForChild();

private:
    PendingJavaHookRegistry() = default;

    static bool MatchesClassName(const Request& request, const char* class_name);

    mutable std::mutex mutex_;
    std::vector<Request> requests_;
    int next_request_id_ = 0x40000000;
};
