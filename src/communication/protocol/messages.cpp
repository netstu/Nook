#include "messages.h"

#include "tlv.h"

namespace nook {
namespace comm {
namespace {

constexpr uint8_t kFieldErrorCode = 1;
constexpr uint8_t kFieldErrorMessage = 2;

constexpr uint8_t kFieldSpawnIdentifier = 1;
constexpr uint8_t kFieldSpawnArgvBase = 2;
constexpr uint8_t kFieldAttachPid = 1;
constexpr uint8_t kFieldAttachIdentifier = 2;
constexpr uint8_t kFieldAttachRespSessionId = 1;
constexpr uint8_t kFieldAttachRespPid = 2;
constexpr uint8_t kFieldAttachRespProcessName = 3;
constexpr uint8_t kFieldAttachRespError = 15;
constexpr uint8_t kFieldSpawnResponsePid = 1;
constexpr uint8_t kFieldSpawnResponseError = 15;
constexpr uint8_t kFieldDetachSessionId = 1;
constexpr uint8_t kFieldDetachRespSessionId = 1;
constexpr uint8_t kFieldDetachRespError = 15;
constexpr uint8_t kFieldResumePid = 1;
constexpr uint8_t kFieldResumeRespPid = 1;
constexpr uint8_t kFieldResumeRespError = 15;

constexpr uint8_t kFieldAgentReadyPid = 1;
constexpr uint8_t kFieldAgentReadyProcessName = 2;
constexpr uint8_t kFieldAgentReadySpawnToken = 3;
constexpr uint8_t kFieldAgentReadyArch = 4;
constexpr uint8_t kFieldAgentReadyVersion = 5;
constexpr uint8_t kFieldAgentReadyStage = 6;

constexpr uint8_t kFieldScriptCreateSessionId = 1;
constexpr uint8_t kFieldScriptCreateSource = 2;
constexpr uint8_t kFieldScriptCreateName = 3;

constexpr uint8_t kFieldScriptCreateRespScriptId = 1;
constexpr uint8_t kFieldScriptCreateRespSuccess = 2;
constexpr uint8_t kFieldScriptCreateRespError = 15;

constexpr uint8_t kFieldScriptLoadScriptId = 1;
constexpr uint8_t kFieldScriptUnloadScriptId = 1;

constexpr uint8_t kFieldScriptMessageScriptId = 1;
constexpr uint8_t kFieldScriptMessageMessage = 2;
constexpr uint8_t kFieldScriptMessageData = 3;

constexpr uint8_t kFieldRpcScriptId = 1;
constexpr uint8_t kFieldRpcMethod = 2;
constexpr uint8_t kFieldRpcArgsJson = 3;
constexpr uint8_t kFieldRpcSuccess = 2;
constexpr uint8_t kFieldRpcResultJson = 3;
constexpr uint8_t kFieldRpcError = 15;

constexpr uint8_t kFieldSpawnInstallTargetPackage = 1;
constexpr uint8_t kFieldSpawnInstallSpawnToken = 2;
constexpr uint8_t kFieldSpawnInstallMode = 3;
constexpr uint8_t kFieldSpawnInstallRespSuccess = 1;
constexpr uint8_t kFieldSpawnInstallRespError = 15;
constexpr uint8_t kFieldSpawnUninstallSpawnToken = 1;
constexpr uint8_t kFieldSpawnUninstallRespSuccess = 1;
constexpr uint8_t kFieldSpawnUninstallRespError = 15;

constexpr uint8_t kFieldProcessListEntry = 1;
constexpr uint8_t kFieldProcessEntryPid = 1;
constexpr uint8_t kFieldProcessEntryName = 2;
constexpr uint8_t kFieldProcessListError = 15;

constexpr uint8_t kFieldAppListEntry = 1;
constexpr uint8_t kFieldAppEntryPackageName = 1;
constexpr uint8_t kFieldAppListError = 15;

}  // namespace

std::vector<uint8_t> EncodeErrorInfo(const ErrorInfo& error) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldErrorCode, static_cast<uint32_t>(error.code));
    if (!error.message.empty()) {
        encoder.PutString(kFieldErrorMessage, error.message);
    }
    return encoder.Build();
}

bool DecodeErrorInfo(const uint8_t* data, size_t len, ErrorInfo* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    uint32_t code = 0;
    decoder.GetUint32(kFieldErrorCode, &code);
    out->code = static_cast<int32_t>(code);
    decoder.GetString(kFieldErrorMessage, &out->message);
    return true;
}

std::vector<uint8_t> EncodeSpawnRequest(const SpawnRequest& request) {
    TlvEncoder encoder;
    if (!request.identifier.empty()) {
        encoder.PutString(kFieldSpawnIdentifier, request.identifier);
    }
    for (size_t i = 0; i < request.argv.size() && i < 14; ++i) {
        encoder.PutString(static_cast<uint8_t>(kFieldSpawnArgvBase + i), request.argv[i]);
    }
    return encoder.Build();
}

bool DecodeSpawnRequest(const uint8_t* data, size_t len, SpawnRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    out->identifier.clear();
    decoder.GetString(kFieldSpawnIdentifier, &out->identifier);
    out->argv.clear();
    for (uint8_t field_id = kFieldSpawnArgvBase; field_id <= 15; ++field_id) {
        std::string arg;
        if (decoder.GetString(field_id, &arg)) {
            out->argv.push_back(arg);
        }
    }
    return true;
}

std::vector<uint8_t> EncodeAttachRequest(const AttachRequest& request) {
    TlvEncoder encoder;
    if (request.pid != 0) {
        encoder.PutUint32(kFieldAttachPid, request.pid);
    }
    if (!request.identifier.empty()) {
        encoder.PutString(kFieldAttachIdentifier, request.identifier);
    }
    return encoder.Build();
}

bool DecodeAttachRequest(const uint8_t* data, size_t len, AttachRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    out->pid = 0;
    out->identifier.clear();
    decoder.GetUint32(kFieldAttachPid, &out->pid);
    decoder.GetString(kFieldAttachIdentifier, &out->identifier);
    return true;
}

std::vector<uint8_t> EncodeAttachResponse(const AttachResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldAttachRespSessionId, response.session_id);
    encoder.PutUint32(kFieldAttachRespPid, response.pid);
    if (!response.process_name.empty()) {
        encoder.PutString(kFieldAttachRespProcessName, response.process_name);
    }
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldAttachRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeAttachResponse(const uint8_t* data, size_t len, AttachResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldAttachRespSessionId, &out->session_id) ||
        !decoder.GetUint32(kFieldAttachRespPid, &out->pid)) {
        return false;
    }
    out->process_name.clear();
    out->error = {};
    decoder.GetString(kFieldAttachRespProcessName, &out->process_name);

    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldAttachRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeSpawnResponse(const SpawnResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldSpawnResponsePid, response.pid);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldSpawnResponseError, error_enc);
    }
    return encoder.Build();
}

bool DecodeSpawnResponse(const uint8_t* data, size_t len, SpawnResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldSpawnResponsePid, &out->pid)) {
        return false;
    }
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldSpawnResponseError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeDetachRequest(const DetachRequest& request) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldDetachSessionId, request.session_id);
    return encoder.Build();
}

bool DecodeDetachRequest(const uint8_t* data, size_t len, DetachRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    return decoder.GetUint32(kFieldDetachSessionId, &out->session_id);
}

std::vector<uint8_t> EncodeDetachResponse(const DetachResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldDetachRespSessionId, response.session_id);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldDetachRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeDetachResponse(const uint8_t* data, size_t len, DetachResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldDetachRespSessionId, &out->session_id)) {
        return false;
    }
    out->error = {};
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldDetachRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeResumeRequest(const ResumeRequest& request) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldResumePid, request.pid);
    return encoder.Build();
}

bool DecodeResumeRequest(const uint8_t* data, size_t len, ResumeRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    return decoder.GetUint32(kFieldResumePid, &out->pid);
}

std::vector<uint8_t> EncodeResumeResponse(const ResumeResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldResumeRespPid, response.pid);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldResumeRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeResumeResponse(const uint8_t* data, size_t len, ResumeResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldResumeRespPid, &out->pid)) {
        return false;
    }
    out->error = {};
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldResumeRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeAgentReady(const AgentReady& ready) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldAgentReadyPid, ready.pid);
    encoder.PutString(kFieldAgentReadyProcessName, ready.process_name);
    if (!ready.spawn_token.empty()) {
        encoder.PutString(kFieldAgentReadySpawnToken, ready.spawn_token);
    }
    encoder.PutString(kFieldAgentReadyArch, ready.arch);
    encoder.PutString(kFieldAgentReadyVersion, ready.version);
    encoder.PutUint8(kFieldAgentReadyStage, static_cast<uint8_t>(ready.stage));
    return encoder.Build();
}

bool DecodeAgentReady(const uint8_t* data, size_t len, AgentReady* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    out->spawn_token.clear();
    out->stage = AgentReadyStage::kRuntime;
    uint8_t stage = static_cast<uint8_t>(AgentReadyStage::kRuntime);
    (void) decoder.GetUint8(kFieldAgentReadyStage, &stage);
    return decoder.GetUint32(kFieldAgentReadyPid, &out->pid) &&
           decoder.GetString(kFieldAgentReadyProcessName, &out->process_name) &&
           decoder.GetString(kFieldAgentReadyArch, &out->arch) &&
           decoder.GetString(kFieldAgentReadyVersion, &out->version) &&
           (decoder.GetString(kFieldAgentReadySpawnToken, &out->spawn_token) || true) &&
           ((out->stage = static_cast<AgentReadyStage>(stage)), true);
}

std::vector<uint8_t> EncodeScriptCreate(const ScriptCreate& create) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptCreateSessionId, create.session_id);
    encoder.PutString(kFieldScriptCreateSource, create.source);
    if (!create.name.empty()) {
        encoder.PutString(kFieldScriptCreateName, create.name);
    }
    return encoder.Build();
}

bool DecodeScriptCreate(const uint8_t* data, size_t len, ScriptCreate* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldScriptCreateSessionId, &out->session_id) ||
        !decoder.GetString(kFieldScriptCreateSource, &out->source)) {
        return false;
    }
    decoder.GetString(kFieldScriptCreateName, &out->name);
    return true;
}

std::vector<uint8_t> EncodeScriptCreateResponse(const ScriptCreateResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptCreateRespScriptId, response.script_id);
    encoder.PutUint8(kFieldScriptCreateRespSuccess, response.success ? 1 : 0);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldScriptCreateRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeScriptCreateResponse(const uint8_t* data, size_t len, ScriptCreateResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    uint8_t success = 0;
    if (!decoder.GetUint32(kFieldScriptCreateRespScriptId, &out->script_id) ||
        !decoder.GetUint8(kFieldScriptCreateRespSuccess, &success)) {
        return false;
    }
    out->success = success != 0;
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldScriptCreateRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeScriptResponse(const ScriptResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptCreateRespScriptId, response.script_id);
    encoder.PutUint8(kFieldScriptCreateRespSuccess, response.success ? 1 : 0);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldScriptCreateRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeScriptResponse(const uint8_t* data, size_t len, ScriptResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    uint8_t success = 0;
    if (!decoder.GetUint32(kFieldScriptCreateRespScriptId, &out->script_id) ||
        !decoder.GetUint8(kFieldScriptCreateRespSuccess, &success)) {
        return false;
    }
    out->success = success != 0;
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldScriptCreateRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeScriptLoad(const ScriptLoad& load) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptLoadScriptId, load.script_id);
    return encoder.Build();
}

bool DecodeScriptLoad(const uint8_t* data, size_t len, ScriptLoad* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    return decoder.GetUint32(kFieldScriptLoadScriptId, &out->script_id);
}

std::vector<uint8_t> EncodeScriptUnload(const ScriptUnload& unload) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptUnloadScriptId, unload.script_id);
    return encoder.Build();
}

bool DecodeScriptUnload(const uint8_t* data, size_t len, ScriptUnload* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    return decoder.GetUint32(kFieldScriptUnloadScriptId, &out->script_id);
}

std::vector<uint8_t> EncodeScriptMessage(const ScriptMessage& message) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptMessageScriptId, message.script_id);
    encoder.PutString(kFieldScriptMessageMessage, message.message);
    if (!message.data.empty()) {
        encoder.PutBytes(kFieldScriptMessageData, message.data.data(), message.data.size());
    }
    return encoder.Build();
}

bool DecodeScriptMessage(const uint8_t* data, size_t len, ScriptMessage* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldScriptMessageScriptId, &out->script_id) ||
        !decoder.GetString(kFieldScriptMessageMessage, &out->message)) {
        return false;
    }
    out->data.clear();
    decoder.GetBytes(kFieldScriptMessageData, &out->data);
    return true;
}

std::vector<uint8_t> EncodeScriptPost(const ScriptPost& post) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldScriptMessageScriptId, post.script_id);
    encoder.PutString(kFieldScriptMessageMessage, post.message);
    if (!post.data.empty()) {
        encoder.PutBytes(kFieldScriptMessageData, post.data.data(), post.data.size());
    }
    return encoder.Build();
}

bool DecodeScriptPost(const uint8_t* data, size_t len, ScriptPost* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldScriptMessageScriptId, &out->script_id) ||
        !decoder.GetString(kFieldScriptMessageMessage, &out->message)) {
        return false;
    }
    out->data.clear();
    decoder.GetBytes(kFieldScriptMessageData, &out->data);
    return true;
}

std::vector<uint8_t> EncodeRpcRequest(const RpcRequest& request) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldRpcScriptId, request.script_id);
    encoder.PutString(kFieldRpcMethod, request.method);
    if (!request.args_json.empty()) {
        encoder.PutString(kFieldRpcArgsJson, request.args_json);
    }
    return encoder.Build();
}

bool DecodeRpcRequest(const uint8_t* data, size_t len, RpcRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    if (!decoder.GetUint32(kFieldRpcScriptId, &out->script_id) ||
        !decoder.GetString(kFieldRpcMethod, &out->method)) {
        return false;
    }
    decoder.GetString(kFieldRpcArgsJson, &out->args_json);
    return true;
}

std::vector<uint8_t> EncodeRpcResponse(const RpcResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint32(kFieldRpcScriptId, response.script_id);
    encoder.PutUint8(kFieldRpcSuccess, response.success ? 1 : 0);
    if (!response.result_json.empty()) {
        encoder.PutString(kFieldRpcResultJson, response.result_json);
    }
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldRpcError, error_enc);
    }
    return encoder.Build();
}

bool DecodeRpcResponse(const uint8_t* data, size_t len, RpcResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    uint8_t success = 0;
    if (!decoder.GetUint32(kFieldRpcScriptId, &out->script_id) ||
        !decoder.GetUint8(kFieldRpcSuccess, &success)) {
        return false;
    }
    out->success = success != 0;
    out->result_json.clear();
    out->error = {};
    decoder.GetString(kFieldRpcResultJson, &out->result_json);

    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldRpcError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeSpawnInstallRequest(const SpawnInstallRequest& request) {
    TlvEncoder encoder;
    if (!request.target_package.empty()) {
        encoder.PutString(kFieldSpawnInstallTargetPackage, request.target_package);
    }
    if (!request.spawn_token.empty()) {
        encoder.PutString(kFieldSpawnInstallSpawnToken, request.spawn_token);
    }
    if (!request.mode.empty()) {
        encoder.PutString(kFieldSpawnInstallMode, request.mode);
    }
    return encoder.Build();
}

bool DecodeSpawnInstallRequest(const uint8_t* data, size_t len, SpawnInstallRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    out->target_package.clear();
    out->spawn_token.clear();
    out->mode.clear();
    return decoder.GetString(kFieldSpawnInstallTargetPackage, &out->target_package) &&
           decoder.GetString(kFieldSpawnInstallSpawnToken, &out->spawn_token) &&
           decoder.GetString(kFieldSpawnInstallMode, &out->mode);
}

std::vector<uint8_t> EncodeSpawnInstallResponse(const SpawnInstallResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint8(kFieldSpawnInstallRespSuccess, response.success ? 1 : 0);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldSpawnInstallRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeSpawnInstallResponse(const uint8_t* data, size_t len, SpawnInstallResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    uint8_t success = 0;
    out->error = {};
    if (!decoder.GetUint8(kFieldSpawnInstallRespSuccess, &success)) {
        return false;
    }
    out->success = success != 0;
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldSpawnInstallRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeSpawnUninstallRequest(const SpawnUninstallRequest& request) {
    TlvEncoder encoder;
    if (!request.spawn_token.empty()) {
        encoder.PutString(kFieldSpawnUninstallSpawnToken, request.spawn_token);
    }
    return encoder.Build();
}

bool DecodeSpawnUninstallRequest(const uint8_t* data, size_t len, SpawnUninstallRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    out->spawn_token.clear();
    return decoder.GetString(kFieldSpawnUninstallSpawnToken, &out->spawn_token);
}

std::vector<uint8_t> EncodeSpawnUninstallResponse(const SpawnUninstallResponse& response) {
    TlvEncoder encoder;
    encoder.PutUint8(kFieldSpawnUninstallRespSuccess, response.success ? 1 : 0);
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldSpawnUninstallRespError, error_enc);
    }
    return encoder.Build();
}

bool DecodeSpawnUninstallResponse(const uint8_t* data, size_t len, SpawnUninstallResponse* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    uint8_t success = 0;
    out->error = {};
    if (!decoder.GetUint8(kFieldSpawnUninstallRespSuccess, &success)) {
        return false;
    }
    out->success = success != 0;
    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldSpawnUninstallRespError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }
    return true;
}

std::vector<uint8_t> EncodeProcessListRequest(const ProcessListRequest&) {
    TlvEncoder encoder;
    return encoder.Build();
}

bool DecodeProcessListRequest(const uint8_t* data, size_t len, ProcessListRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    (void)decoder;
    *out = ProcessListRequest{};
    return true;
}

std::vector<uint8_t> EncodeProcessListResponse(const ProcessListResponse& response) {
    TlvEncoder encoder;
    for (const ProcessEntry& process : response.processes) {
        TlvEncoder process_enc;
        process_enc.PutUint32(kFieldProcessEntryPid, process.pid);
        process_enc.PutString(kFieldProcessEntryName, process.name);
        encoder.PutNested(kFieldProcessListEntry, process_enc);
    }
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldProcessListError, error_enc);
    }
    return encoder.Build();
}

bool DecodeProcessListResponse(const uint8_t* data, size_t len, ProcessListResponse* out) {
    if (out == nullptr) {
        return false;
    }

    TlvDecoder decoder(data, len);
    out->processes.clear();
    out->error = {};

    std::vector<TlvDecoder> process_decoders;
    if (!decoder.GetAllNested(kFieldProcessListEntry, &process_decoders)) {
        return false;
    }

    for (const TlvDecoder& process_decoder : process_decoders) {
        ProcessEntry process;
        if (!process_decoder.GetUint32(kFieldProcessEntryPid, &process.pid) ||
            !process_decoder.GetString(kFieldProcessEntryName, &process.name)) {
            return false;
        }
        out->processes.push_back(std::move(process));
    }

    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldProcessListError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }

    return true;
}

std::vector<uint8_t> EncodeAppListRequest(const AppListRequest&) {
    TlvEncoder encoder;
    return encoder.Build();
}

bool DecodeAppListRequest(const uint8_t* data, size_t len, AppListRequest* out) {
    if (out == nullptr) {
        return false;
    }
    TlvDecoder decoder(data, len);
    (void)decoder;
    *out = AppListRequest{};
    return true;
}

std::vector<uint8_t> EncodeAppListResponse(const AppListResponse& response) {
    TlvEncoder encoder;
    for (const AppEntry& app : response.apps) {
        TlvEncoder app_enc;
        app_enc.PutString(kFieldAppEntryPackageName, app.package_name);
        encoder.PutNested(kFieldAppListEntry, app_enc);
    }
    if (response.error.code != 0 || !response.error.message.empty()) {
        TlvEncoder error_enc;
        error_enc.PutUint32(kFieldErrorCode, static_cast<uint32_t>(response.error.code));
        if (!response.error.message.empty()) {
            error_enc.PutString(kFieldErrorMessage, response.error.message);
        }
        encoder.PutNested(kFieldAppListError, error_enc);
    }
    return encoder.Build();
}

bool DecodeAppListResponse(const uint8_t* data, size_t len, AppListResponse* out) {
    if (out == nullptr) {
        return false;
    }

    TlvDecoder decoder(data, len);
    out->apps.clear();
    out->error = {};

    std::vector<TlvDecoder> app_decoders;
    if (!decoder.GetAllNested(kFieldAppListEntry, &app_decoders)) {
        return false;
    }

    for (const TlvDecoder& app_decoder : app_decoders) {
        AppEntry app;
        if (!app_decoder.GetString(kFieldAppEntryPackageName, &app.package_name)) {
            return false;
        }
        out->apps.push_back(std::move(app));
    }

    TlvDecoder error_decoder;
    if (decoder.GetNested(kFieldAppListError, &error_decoder)) {
        uint32_t code = 0;
        error_decoder.GetUint32(kFieldErrorCode, &code);
        out->error.code = static_cast<int32_t>(code);
        error_decoder.GetString(kFieldErrorMessage, &out->error.message);
    }

    return true;
}

}  // namespace comm
}  // namespace nook
