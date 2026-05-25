#include <cassert>
#include <string>

#include "communication/transport/path_utils.h"

using namespace nook::comm;

namespace {

void TestGetParentDirectory() {
    assert(GetParentDirectory("/data/local/tmp/nook/nook.sock") == "/data/local/tmp/nook");
    assert(GetParentDirectory("/tmp/nook.sock") == "/tmp");
    assert(GetParentDirectory("nook.sock").empty());
}

void TestEnsureDirectoryRecursive() {
    const std::string path = "build/tmp/path_utils/a/b/c";
    assert(EnsureDirectoryRecursive(path));
    assert(EnsureDirectoryRecursive(path));
}

}  // namespace

int main() {
    TestGetParentDirectory();
    TestEnsureDirectoryRecursive();
    return 0;
}
