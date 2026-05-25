#include <cassert>
#include <string>

namespace nook {
namespace framework {

bool ShouldAutoInitializeNookAgent(const std::string& process_name);
bool ShouldActivateInheritedNookAgent(const std::string& process_name, bool has_spawn_marker);

}  // namespace framework
}  // namespace nook

int main() {
    using nook::framework::ShouldActivateInheritedNookAgent;
    using nook::framework::ShouldAutoInitializeNookAgent;

    assert(!ShouldAutoInitializeNookAgent("zygote64"));
    assert(!ShouldAutoInitializeNookAgent("usap64"));
    assert(ShouldAutoInitializeNookAgent("com.demo.target"));

    assert(!ShouldActivateInheritedNookAgent("zygote64", true));
    assert(ShouldActivateInheritedNookAgent("com.demo.target", false));
    assert(ShouldActivateInheritedNookAgent("com.demo.target", true));
    return 0;
}
