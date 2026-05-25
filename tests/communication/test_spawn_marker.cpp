#include <cassert>
#include <string>

#include "communication/transport/spawn_marker.h"

using namespace nook::comm;

namespace {

void TestSanitizeSpawnMarkerName() {
    assert(SanitizeSpawnMarkerName("com.demo.target") == "com.demo.target");
    assert(SanitizeSpawnMarkerName("com.demo.target:worker") == "com.demo.target_worker");
    assert(SanitizeSpawnMarkerName("demo target") == "demo_target");
}

void TestCreateAndConsumeSpawnMarker() {
    const std::string base_dir = "build/tmp/test_spawn_marker";
    const std::string process_name = "com.demo.target";
    const std::string marker_path = BuildSpawnMarkerPath(process_name, base_dir);

    RemoveSpawnMarker(process_name, base_dir);
    assert(!HasSpawnMarker(process_name, base_dir));
    assert(CreateSpawnMarker(process_name, base_dir));
    assert(HasSpawnMarker(process_name, base_dir));
    assert(!marker_path.empty());
    assert(ConsumeSpawnMarker(process_name, base_dir));
    assert(!HasSpawnMarker(process_name, base_dir));
    assert(!ConsumeSpawnMarker(process_name, base_dir));
}

void TestConsumeAnySinglePendingSpawnMarker() {
    const std::string base_dir = "build/tmp/test_spawn_marker_any";
    const std::string process_name = "com.demo.target";
    const std::string other_name = "zygote64";

    RemoveSpawnMarker(process_name, base_dir);
    RemoveSpawnMarker(other_name, base_dir);

    assert(CreateSpawnMarker(process_name, base_dir));
    assert(HasSpawnMarker(process_name, base_dir));
    assert(ConsumeAnySingleSpawnMarker(base_dir));
    assert(!HasSpawnMarker(process_name, base_dir));
    assert(!ConsumeAnySingleSpawnMarker(base_dir));
}

void TestConsumeSinglePendingSpawnMarkerWithName() {
    const std::string base_dir = "build/tmp/test_spawn_marker_single_name";
    const std::string process_name = "com.demo.target";
    const std::string other_name = "com.demo.other";
    std::string resolved_name;

    RemoveSpawnMarker(process_name, base_dir);
    RemoveSpawnMarker(other_name, base_dir);

    assert(CreateSpawnMarker(process_name, base_dir));
    assert(FindSinglePendingSpawnMarker(&resolved_name, base_dir));
    assert(resolved_name == process_name);
    assert(ConsumeSinglePendingSpawnMarker(&resolved_name, base_dir));
    assert(resolved_name == process_name);
    assert(!HasSpawnMarker(process_name, base_dir));

    resolved_name.clear();
    assert(CreateSpawnMarker(process_name, base_dir));
    assert(CreateSpawnMarker(other_name, base_dir));
    assert(!ConsumeSinglePendingSpawnMarker(&resolved_name, base_dir));
}

void TestLooksLikeEarlySpawnProcessName() {
    assert(LooksLikeEarlySpawnProcessName("zygote"));
    assert(LooksLikeEarlySpawnProcessName("zygote64"));
    assert(LooksLikeEarlySpawnProcessName("usap32"));
    assert(LooksLikeEarlySpawnProcessName("usap64"));
    assert(LooksLikeEarlySpawnProcessName("<pre-initialized>"));
    assert(LooksLikeEarlySpawnProcessName("pre-initialized"));
    assert(!LooksLikeEarlySpawnProcessName("com.demo.target"));
}

}  // namespace

int main() {
    TestSanitizeSpawnMarkerName();
    TestCreateAndConsumeSpawnMarker();
    TestConsumeAnySinglePendingSpawnMarker();
    TestConsumeSinglePendingSpawnMarkerWithName();
    TestLooksLikeEarlySpawnProcessName();
    return 0;
}
