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
    const std::string stub_header = ReadAll("server/symbi/stub_src/stub.h");
    const std::string stub_source = ReadAll("server/symbi/stub_src/stub.c");
    const std::string local_injector = ReadAll("server/symbi_injector_local.cpp");

    // The child stop handshake should keep only the helpers required for:
    // 1. package-name matching
    // 2. callback socket handshake
    // 3. SIGSTOP after host ack
    assert(stub_header.find("getppid") == std::string::npos);
    assert(stub_source.find("getppid") == std::string::npos);
    assert(local_injector.find("remote_getppid") == std::string::npos);
    assert(stub_header.find("read)(") == std::string::npos);
    assert(stub_source.find("stubApi.read") == std::string::npos);
    assert(stub_header.find("getuid") == std::string::npos);
    assert(stub_source.find("stubApi.getuid") == std::string::npos);
    assert(local_injector.find("remote_getuid") == std::string::npos);
    assert(local_injector.find("target_uid") == std::string::npos);

    // The callback protocol no longer needs to echo package payload back to host.
    assert(stub_source.find("package_name_len") == std::string::npos);
    assert(local_injector.find("package_name_len") == std::string::npos);
    assert(local_injector.find("callback ack write failed") == std::string::npos);

    return 0;
}
