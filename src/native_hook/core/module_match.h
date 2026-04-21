#ifndef INJECTDEMO_MODULE_MATCH_H
#define INJECTDEMO_MODULE_MATCH_H

namespace ElfHooker {

bool module_path_matches(const char* mapped_path, const char* module_name);

}  // namespace ElfHooker

#endif // INJECTDEMO_MODULE_MATCH_H
