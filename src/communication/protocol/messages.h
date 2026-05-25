#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nook {
namespace comm {

struct ErrorInfo {
    int32_t code = 0;
    std::string message;
};

struct SpawnRequest {
    std::string identifier;
    std::vector<std::string> argv;
};

struct AttachRequest {
    uint32_t pid = 0;
    std::string identifier;
};

struct AttachResponse {
    uint32_t session_id = 0;
    uint32_t pid = 0;
    std::string process_name;
    ErrorInfo error;
};

struct SpawnResponse {
    uint32_t pid = 0;
    ErrorInfo error;
};

struct DetachRequest {
    uint32_t session_id = 0;
};

struct DetachResponse {
    uint32_t session_id = 0;
    ErrorInfo error;
};

struct ResumeRequest {
    uint32_t pid = 0;
};

struct ResumeResponse {
    uint32_t pid = 0;
    ErrorInfo error;
};

enum class AgentReadyStage : uint8_t {
    kRuntime = 0,
    kControl = 1,
};

struct AgentReady {
    uint32_t pid = 0;
    std::string process_name;
    std::string spawn_token;
    std::string arch;
    std::string version;
    AgentReadyStage stage = AgentReadyStage::kRuntime;
};

struct ScriptCreate {
    uint32_t session_id = 0;
    std::string source;
    std::string name;
};

struct ScriptCreateResponse {
    uint32_t script_id = 0;
    bool success = false;
    ErrorInfo error;
};

struct ScriptResponse {
    uint32_t script_id = 0;
    bool success = false;
    ErrorInfo error;
};

struct ScriptLoad {
    uint32_t script_id = 0;
};

struct ScriptUnload {
    uint32_t script_id = 0;
};

struct ScriptMessage {
    uint32_t script_id = 0;
    std::string message;
    std::vector<uint8_t> data;
};

struct ScriptPost {
    uint32_t script_id = 0;
    std::string message;
    std::vector<uint8_t> data;
};

struct RpcRequest {
    uint32_t script_id = 0;
    std::string method;
    std::string args_json;
};

struct RpcResponse {
    uint32_t script_id = 0;
    bool success = false;
    std::string result_json;
    ErrorInfo error;
};

struct SpawnInstallRequest {
    std::string target_package;
    std::string spawn_token;
    std::string mode;
};

struct SpawnInstallResponse {
    bool success = false;
    ErrorInfo error;
};

struct SpawnUninstallRequest {
    std::string spawn_token;
};

struct SpawnUninstallResponse {
    bool success = false;
    ErrorInfo error;
};

struct ProcessEntry {
    uint32_t pid = 0;
    std::string name;
};

struct ProcessListRequest {};

struct ProcessListResponse {
    std::vector<ProcessEntry> processes;
    ErrorInfo error;
};

struct AppEntry {
    std::string package_name;
};

struct AppListRequest {};

struct AppListResponse {
    std::vector<AppEntry> apps;
    ErrorInfo error;
};

std::vector<uint8_t> EncodeErrorInfo(const ErrorInfo& error);
bool DecodeErrorInfo(const uint8_t* data, size_t len, ErrorInfo* out);

std::vector<uint8_t> EncodeSpawnRequest(const SpawnRequest& request);
bool DecodeSpawnRequest(const uint8_t* data, size_t len, SpawnRequest* out);

std::vector<uint8_t> EncodeAttachRequest(const AttachRequest& request);
bool DecodeAttachRequest(const uint8_t* data, size_t len, AttachRequest* out);

std::vector<uint8_t> EncodeAttachResponse(const AttachResponse& response);
bool DecodeAttachResponse(const uint8_t* data, size_t len, AttachResponse* out);

std::vector<uint8_t> EncodeSpawnResponse(const SpawnResponse& response);
bool DecodeSpawnResponse(const uint8_t* data, size_t len, SpawnResponse* out);

std::vector<uint8_t> EncodeDetachRequest(const DetachRequest& request);
bool DecodeDetachRequest(const uint8_t* data, size_t len, DetachRequest* out);

std::vector<uint8_t> EncodeDetachResponse(const DetachResponse& response);
bool DecodeDetachResponse(const uint8_t* data, size_t len, DetachResponse* out);

std::vector<uint8_t> EncodeResumeRequest(const ResumeRequest& request);
bool DecodeResumeRequest(const uint8_t* data, size_t len, ResumeRequest* out);

std::vector<uint8_t> EncodeResumeResponse(const ResumeResponse& response);
bool DecodeResumeResponse(const uint8_t* data, size_t len, ResumeResponse* out);

std::vector<uint8_t> EncodeAgentReady(const AgentReady& ready);
bool DecodeAgentReady(const uint8_t* data, size_t len, AgentReady* out);

std::vector<uint8_t> EncodeScriptCreate(const ScriptCreate& create);
bool DecodeScriptCreate(const uint8_t* data, size_t len, ScriptCreate* out);

std::vector<uint8_t> EncodeScriptCreateResponse(const ScriptCreateResponse& response);
bool DecodeScriptCreateResponse(const uint8_t* data, size_t len, ScriptCreateResponse* out);

std::vector<uint8_t> EncodeScriptResponse(const ScriptResponse& response);
bool DecodeScriptResponse(const uint8_t* data, size_t len, ScriptResponse* out);

std::vector<uint8_t> EncodeScriptLoad(const ScriptLoad& load);
bool DecodeScriptLoad(const uint8_t* data, size_t len, ScriptLoad* out);

std::vector<uint8_t> EncodeScriptUnload(const ScriptUnload& unload);
bool DecodeScriptUnload(const uint8_t* data, size_t len, ScriptUnload* out);

std::vector<uint8_t> EncodeScriptMessage(const ScriptMessage& message);
bool DecodeScriptMessage(const uint8_t* data, size_t len, ScriptMessage* out);

std::vector<uint8_t> EncodeScriptPost(const ScriptPost& post);
bool DecodeScriptPost(const uint8_t* data, size_t len, ScriptPost* out);

std::vector<uint8_t> EncodeRpcRequest(const RpcRequest& request);
bool DecodeRpcRequest(const uint8_t* data, size_t len, RpcRequest* out);

std::vector<uint8_t> EncodeRpcResponse(const RpcResponse& response);
bool DecodeRpcResponse(const uint8_t* data, size_t len, RpcResponse* out);

std::vector<uint8_t> EncodeSpawnInstallRequest(const SpawnInstallRequest& request);
bool DecodeSpawnInstallRequest(const uint8_t* data, size_t len, SpawnInstallRequest* out);

std::vector<uint8_t> EncodeSpawnInstallResponse(const SpawnInstallResponse& response);
bool DecodeSpawnInstallResponse(const uint8_t* data, size_t len, SpawnInstallResponse* out);

std::vector<uint8_t> EncodeSpawnUninstallRequest(const SpawnUninstallRequest& request);
bool DecodeSpawnUninstallRequest(const uint8_t* data, size_t len, SpawnUninstallRequest* out);

std::vector<uint8_t> EncodeSpawnUninstallResponse(const SpawnUninstallResponse& response);
bool DecodeSpawnUninstallResponse(const uint8_t* data, size_t len, SpawnUninstallResponse* out);

std::vector<uint8_t> EncodeProcessListRequest(const ProcessListRequest& request);
bool DecodeProcessListRequest(const uint8_t* data, size_t len, ProcessListRequest* out);

std::vector<uint8_t> EncodeProcessListResponse(const ProcessListResponse& response);
bool DecodeProcessListResponse(const uint8_t* data, size_t len, ProcessListResponse* out);

std::vector<uint8_t> EncodeAppListRequest(const AppListRequest& request);
bool DecodeAppListRequest(const uint8_t* data, size_t len, AppListRequest* out);

std::vector<uint8_t> EncodeAppListResponse(const AppListResponse& response);
bool DecodeAppListResponse(const uint8_t* data, size_t len, AppListResponse* out);

}  // namespace comm
}  // namespace nook
