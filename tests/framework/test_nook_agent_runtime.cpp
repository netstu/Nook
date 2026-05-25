#include <cassert>
#include <string>

#include "framework/nook_agent_runtime.h"

using namespace nook::framework;

namespace {

void TestResolveRuntimeDirectoryFromAgentPathUsesDirectory() {
    const std::string runtime_dir =
        ResolveRuntimeDirectoryFromAgentPath("/data/local/tmp/nook-test/libnook-agent.so");
    assert(runtime_dir == "/data/local/tmp/nook-test");
}

void TestResolveRuntimeDirectoryFromAgentPathHandlesRoot() {
    const std::string runtime_dir =
        ResolveRuntimeDirectoryFromAgentPath("/libnook-agent.so");
    assert(runtime_dir == "/");
}

void TestResolveRuntimeDirectoryFromAgentPathHandlesMissingDirectory() {
    const std::string runtime_dir =
        ResolveRuntimeDirectoryFromAgentPath("libnook-agent.so");
    assert(runtime_dir.empty());
}

void TestResolveRuntimeDirectoryFromAgentPathHandlesMemfdPath() {
    const std::string runtime_dir =
        ResolveRuntimeDirectoryFromAgentPath("/memfd:libnook-agent (deleted)");
    assert(runtime_dir.empty());
}

}  // namespace

int main() {
    TestResolveRuntimeDirectoryFromAgentPathUsesDirectory();
    TestResolveRuntimeDirectoryFromAgentPathHandlesRoot();
    TestResolveRuntimeDirectoryFromAgentPathHandlesMissingDirectory();
    TestResolveRuntimeDirectoryFromAgentPathHandlesMemfdPath();
    return 0;
}
