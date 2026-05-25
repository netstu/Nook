#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadAll(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

}  // namespace

int main() {
    const std::string local_injector = ReadAll("server/symbi_injector_local.cpp");

    assert(local_injector.find("enum class SymbiHandoffState") != std::string::npos);
    assert(local_injector.find("kGateInstalled") != std::string::npos);
    assert(local_injector.find("kTargetAppStarted") != std::string::npos);
    assert(local_injector.find("kCallbackObserved") != std::string::npos);
    assert(local_injector.find("kPrimaryRestoreAttempted") != std::string::npos);
    assert(local_injector.find("kPtraceRestoreAttempted") != std::string::npos);
    assert(local_injector.find("kRestoreCompleted") != std::string::npos);
    assert(local_injector.find("AdvanceSymbiHandoffState") != std::string::npos);
    assert(local_injector.find("SymbiHandoffStateName") != std::string::npos);
    assert(local_injector.find("struct RestoreDriverOps") != std::string::npos);
    assert(local_injector.find("run_restore_attempt") != std::string::npos);
    assert(local_injector.find("primary_restore_ops") != std::string::npos);
    assert(local_injector.find("ptrace_restore_ops") != std::string::npos);

    return 0;
}
