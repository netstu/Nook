#include "pending_java_hook_registry.h"

#include "java_hook_loader_resolver.h"

#include <mutex>
#include <new>

PendingJavaHookRegistry& PendingJavaHookRegistry::Instance() {
    static PendingJavaHookRegistry instance;
    return instance;
}

int PendingJavaHookRegistry::Register(const char* class_name,
                                      const char* method_name,
                                      const char* signature,
                                      int is_static,
                                      uint64_t loader_handle,
                                      NookJavaHookCallback callback) {
    if (class_name == nullptr || method_name == nullptr || signature == nullptr || callback == nullptr) {
        return static_cast<int>(NOOK_STATUS_INVALID_ARGUMENT);
    }

    const std::string normalized_class_name = JavaHookLoaderResolver::NormalizeSlashClassName(class_name);
    const std::string dot_class_name = JavaHookLoaderResolver::NormalizeDotClassName(class_name);

    std::lock_guard<std::mutex> lock(mutex_);
    for (const Request& request : requests_) {
        if (!request.active) {
            continue;
        }
        if (request.class_name == normalized_class_name &&
            request.method_name == method_name &&
            request.signature == signature &&
            request.is_static == is_static &&
            request.loader_handle == loader_handle &&
            request.callback == callback) {
            return request.request_id;
        }
    }

    Request request = {};
    request.request_id = next_request_id_++;
    request.class_name = normalized_class_name;
    request.dot_class_name = dot_class_name;
    request.method_name = method_name;
    request.signature = signature;
    request.resolved_signature.clear();
    request.is_static = is_static;
    request.loader_handle = loader_handle;
    request.callback = callback;
    request.hook_id = -1;
    request.installed = false;
    request.active = true;
    request.installing = false;
    request.retry_scheduled = false;
    requests_.push_back(request);
    return request.request_id;
}

bool PendingJavaHookRegistry::TryBeginInstall(int request_id, Request* out_request) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Request& request : requests_) {
        if (request.request_id != request_id) {
            continue;
        }
        if (!request.active || request.installed || request.installing) {
            return false;
        }
        request.installing = true;
        if (out_request != nullptr) {
            *out_request = request;
        }
        return true;
    }
    return false;
}

void PendingJavaHookRegistry::FinishInstall(int request_id, int hook_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Request& request : requests_) {
        if (request.request_id != request_id) {
            continue;
        }
        request.installing = false;
        if (hook_id >= 0) {
            request.installed = true;
            request.hook_id = hook_id;
        }
        return;
    }
}

std::vector<int> PendingJavaHookRegistry::CollectPendingRequestIds(const char* class_name) const {
    std::vector<int> request_ids;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Request& request : requests_) {
        if (!request.active || request.installed) {
            continue;
        }
        if (MatchesClassName(request, class_name)) {
            request_ids.push_back(request.request_id);
        }
    }
    return request_ids;
}

std::vector<int> PendingJavaHookRegistry::MarkRetryScheduled(const char* class_name) {
    std::vector<int> request_ids;
    std::lock_guard<std::mutex> lock(mutex_);
    for (Request& request : requests_) {
        if (!request.active || request.installed || request.installing || request.retry_scheduled) {
            continue;
        }
        if (!MatchesClassName(request, class_name)) {
            continue;
        }
        request.retry_scheduled = true;
        request_ids.push_back(request.request_id);
    }
    return request_ids;
}

void PendingJavaHookRegistry::ClearRetryScheduled(const std::vector<int>& request_ids) {
    if (request_ids.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (int request_id : request_ids) {
        for (Request& request : requests_) {
            if (request.request_id == request_id) {
                request.retry_scheduled = false;
                break;
            }
        }
    }
}

bool PendingJavaHookRegistry::HasAnyPending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Request& request : requests_) {
        if (request.active && !request.installed) {
            return true;
        }
    }
    return false;
}

bool PendingJavaHookRegistry::HasPendingForClass(const char* class_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Request& request : requests_) {
        if (!request.active || request.installed) {
            continue;
        }
        if (MatchesClassName(request, class_name)) {
            return true;
        }
    }
    return false;
}

bool PendingJavaHookRegistry::TryGetInstalledHookId(int request_id, int* installed_hook_id) const {
    if (installed_hook_id == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const Request& request : requests_) {
        if (request.request_id != request_id) {
            continue;
        }
        if (!request.installed || request.hook_id < 0) {
            return false;
        }
        *installed_hook_id = request.hook_id;
        return true;
    }
    return false;
}

bool PendingJavaHookRegistry::TryGetResolvedSignature(int request_id,
                                                      std::string* resolved_signature) const {
    if (resolved_signature == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const Request& request : requests_) {
        if (request.request_id != request_id) {
            continue;
        }
        if (request.resolved_signature.empty()) {
            return false;
        }
        *resolved_signature = request.resolved_signature;
        return true;
    }
    return false;
}

void PendingJavaHookRegistry::SetResolvedSignature(int request_id,
                                                   const std::string& resolved_signature) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Request& request : requests_) {
        if (request.request_id == request_id) {
            request.resolved_signature = resolved_signature;
            return;
        }
    }
}

bool PendingJavaHookRegistry::RemoveRequest(int request_id, int* installed_hook_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Request& request : requests_) {
        if (request.request_id != request_id) {
            continue;
        }
        request.active = false;
        request.installing = false;
        request.retry_scheduled = false;
        if (installed_hook_id != nullptr) {
            *installed_hook_id = request.installed ? request.hook_id : -1;
        }
        return true;
    }
    return false;
}

void PendingJavaHookRegistry::Clear(void) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.clear();
    next_request_id_ = 0x40000000;
}

void PendingJavaHookRegistry::ResetInheritedStateForChild() {
    new (&mutex_) std::mutex();
    requests_.clear();
    next_request_id_ = 0x40000000;
}

bool PendingJavaHookRegistry::MatchesClassName(const Request& request, const char* class_name) {
    if (class_name == nullptr) {
        return true;
    }

    return request.class_name == JavaHookLoaderResolver::NormalizeSlashClassName(class_name) ||
           request.dot_class_name == JavaHookLoaderResolver::NormalizeDotClassName(class_name);
}
