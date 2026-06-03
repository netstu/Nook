#include "gadget/nook_gadget_config.h"

#if defined(__ANDROID__)
#include "java_hook/JVM.h"
#include "java_hook/deferred/java_hook_loader_resolver.h"

#include <jni.h>
#endif

namespace nook {
namespace gadget {
namespace {

bool ExtractJsonBoolField(const std::string& json,
                          const char* key,
                          bool* value) {
    if (key == nullptr || value == nullptr) {
        return false;
    }

    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    const std::size_t value_pos = json.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (value_pos == std::string::npos) {
        return false;
    }
    if (json.compare(value_pos, 4, "true") == 0) {
        *value = true;
        return true;
    }
    if (json.compare(value_pos, 5, "false") == 0) {
        *value = false;
        return true;
    }
    return false;
}

bool ExtractJsonStringField(const std::string& json,
                            const char* key,
                            std::string* value) {
    if (key == nullptr || value == nullptr) {
        return false;
    }

    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    const std::size_t quote_start = json.find('"', colon_pos + 1);
    if (quote_start == std::string::npos) {
        return false;
    }

    std::string parsed;
    parsed.reserve(json.size() - quote_start);
    bool escape = false;
    for (std::size_t i = quote_start + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (escape) {
            switch (c) {
                case '\\':
                case '"':
                case '/':
                    parsed.push_back(c);
                    break;
                case 'b':
                    parsed.push_back('\b');
                    break;
                case 'f':
                    parsed.push_back('\f');
                    break;
                case 'n':
                    parsed.push_back('\n');
                    break;
                case 'r':
                    parsed.push_back('\r');
                    break;
                case 't':
                    parsed.push_back('\t');
                    break;
                default:
                    parsed.push_back(c);
                    break;
            }
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            *value = parsed;
            return true;
        }
        parsed.push_back(c);
    }
    return false;
}

bool ExtractJsonIntField(const std::string& json,
                         const char* key,
                         int* value) {
    if (key == nullptr || value == nullptr) {
        return false;
    }

    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    const std::size_t value_pos = json.find_first_of("-0123456789", colon_pos + 1);
    if (value_pos == std::string::npos) {
        return false;
    }
    const std::size_t value_end = json.find_first_not_of("0123456789", value_pos + 1);
    try {
        *value = std::stoi(json.substr(value_pos, value_end - value_pos));
    } catch (...) {
        return false;
    }
    return true;
}

bool ExtractJsonObjectField(const std::string& json,
                            const char* key,
                            std::string* value) {
    if (key == nullptr || value == nullptr) {
        return false;
    }

    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    const std::size_t object_start = json.find('{', colon_pos + 1);
    if (object_start == std::string::npos) {
        return false;
    }

    int depth = 0;
    bool in_string = false;
    for (std::size_t i = object_start; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') {
            std::size_t backslash_count = 0;
            for (std::size_t j = i; j > 0 && json[j - 1] == '\\'; --j) {
                ++backslash_count;
            }
            if ((backslash_count % 2) == 0) {
                in_string = !in_string;
                continue;
            }
        }
        if (in_string) {
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                *value = json.substr(object_start, i - object_start + 1);
                return true;
            }
        }
    }
    return false;
}

std::string StartupScriptOnLoadFromStartupMode(const std::string& startup_mode) {
    return startup_mode == "manual" ? "manual" : "auto";
}

std::string StartupModeFromOnLoad(const std::string& on_load) {
    return on_load == "manual" ? "manual" : "auto-start";
}

std::string CanonicalInteractionTransport(const GadgetConfig& config) {
    return config.interaction.transport.empty() ? config.transport_mode
                                                : config.interaction.transport;
}

std::string CanonicalInteractionOnLoad(const GadgetConfig& config) {
    return config.interaction.on_load.empty() ? "resume" : config.interaction.on_load;
}

std::string CanonicalStartupScriptOnLoad(const GadgetConfig& config) {
    if (!config.startup_script.enabled) {
        return StartupScriptOnLoadFromStartupMode(config.startup_mode);
    }
    return config.startup_script.on_load.empty()
               ? StartupScriptOnLoadFromStartupMode(config.startup_mode)
               : config.startup_script.on_load;
}

std::string CanonicalStartupMode(const GadgetConfig& config) {
    if (!config.startup_script.enabled) {
        return config.startup_mode;
    }
    return StartupModeFromOnLoad(CanonicalStartupScriptOnLoad(config));
}

std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

}  // namespace

bool ParseGadgetConfigJson(const std::string& json, GadgetConfig* config) {
    if (config == nullptr) {
        return false;
    }

    *config = GadgetConfig{};
    (void)ExtractJsonStringField(json, "gadget_version", &config->gadget_version);
    (void)ExtractJsonStringField(json, "startup_mode", &config->startup_mode);
    (void)ExtractJsonStringField(json, "transport_mode", &config->transport_mode);
    (void)ExtractJsonBoolField(json, "debug_logging", &config->debug_logging);
    config->interaction.transport = config->transport_mode;
    config->startup_script.on_load =
        StartupScriptOnLoadFromStartupMode(config->startup_mode);

    std::string interaction_json;
    if (ExtractJsonObjectField(json, "interaction", &interaction_json)) {
        (void)ExtractJsonStringField(interaction_json, "type", &config->interaction.type);
        if (ExtractJsonStringField(interaction_json, "transport",
                                   &config->interaction.transport)) {
            config->transport_mode = config->interaction.transport;
        }
        (void)ExtractJsonStringField(interaction_json, "on_load", &config->interaction.on_load);
        (void)ExtractJsonStringField(interaction_json, "host", &config->interaction.host);
        (void)ExtractJsonStringField(interaction_json, "address", &config->interaction.address);
        (void)ExtractJsonIntField(interaction_json, "port", &config->interaction.port);
    }

    std::string startup_script_json;
    if (ExtractJsonObjectField(json, "startup_script", &startup_script_json)) {
        if (!ExtractJsonStringField(startup_script_json,
                                    "mode",
                                    &config->startup_script.mode) ||
            !ExtractJsonStringField(startup_script_json,
                                    "path",
                                    &config->startup_script.path)) {
            return false;
        }
        (void)ExtractJsonBoolField(startup_script_json,
                                   "required",
                                   &config->startup_script.required);
        if (ExtractJsonStringField(startup_script_json,
                                   "on_load",
                                   &config->startup_script.on_load)) {
            config->startup_mode =
                StartupModeFromOnLoad(config->startup_script.on_load);
        }

        config->startup_script.enabled = true;
    }
    return true;
}

std::string SerializeGadgetConfigJson(const GadgetConfig& config) {
    const std::string canonical_transport_mode = CanonicalInteractionTransport(config);
    const std::string canonical_startup_mode = CanonicalStartupMode(config);

    std::string json = "{";
    json += "\"gadget_version\":\"" + EscapeJsonString(config.gadget_version) + "\",";
    json += "\"startup_mode\":\"" + EscapeJsonString(canonical_startup_mode) + "\",";
    json += "\"transport_mode\":\"" + EscapeJsonString(canonical_transport_mode) + "\",";
    json += std::string("\"debug_logging\":") + (config.debug_logging ? "true" : "false");
    json += ",\"interaction\":{";
    json += "\"type\":\"" + EscapeJsonString(config.interaction.type) + "\",";
    json += "\"transport\":\"" + EscapeJsonString(canonical_transport_mode) + "\",";
    json += "\"on_load\":\"" + EscapeJsonString(CanonicalInteractionOnLoad(config)) + "\",";
    json += "\"host\":\"" + EscapeJsonString(config.interaction.host) + "\",";
    json += "\"address\":\"" + EscapeJsonString(config.interaction.address) + "\",";
    json += "\"port\":" + std::to_string(config.interaction.port);
    json += "}";
    if (config.startup_script.enabled) {
        const std::string canonical_on_load = CanonicalStartupScriptOnLoad(config);
        json += ",\"startup_script\":{";
        json += "\"mode\":\"" + EscapeJsonString(config.startup_script.mode) + "\",";
        json += "\"path\":\"" + EscapeJsonString(config.startup_script.path) + "\",";
        json += std::string("\"required\":") + (config.startup_script.required ? "true" : "false");
        json += ",\"on_load\":\"" + EscapeJsonString(canonical_on_load) + "\"";
        json += "}";
    }
    json += "}";
    return json;
}

bool ReadGadgetAssetFile(const char* asset_path, std::string* contents) {
#if defined(__ANDROID__)
    if (asset_path == nullptr || contents == nullptr) {
        return false;
    }

    JavaEnv jenv;
    JNIEnv* env = jenv.get();
    if (env == nullptr) {
        return false;
    }

    jobject application = JavaHookLoaderResolver::GetCurrentApplication(env);
    if (application == nullptr) {
        return false;
    }

    jclass context_class = env->GetObjectClass(application);
    if (context_class == nullptr) {
        env->DeleteLocalRef(application);
        return false;
    }

    jmethodID get_assets_method =
        env->GetMethodID(context_class, "getAssets", "()Landroid/content/res/AssetManager;");
    env->DeleteLocalRef(context_class);
    if (get_assets_method == nullptr) {
        env->DeleteLocalRef(application);
        return false;
    }

    jobject asset_manager = env->CallObjectMethod(application, get_assets_method);
    env->DeleteLocalRef(application);
    if (asset_manager == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass asset_manager_class = env->GetObjectClass(asset_manager);
    if (asset_manager_class == nullptr) {
        env->DeleteLocalRef(asset_manager);
        return false;
    }

    jmethodID open_method = env->GetMethodID(
        asset_manager_class,
        "open",
        "(Ljava/lang/String;)Ljava/io/InputStream;");
    env->DeleteLocalRef(asset_manager_class);
    if (open_method == nullptr) {
        env->DeleteLocalRef(asset_manager);
        return false;
    }

    std::string relative_asset_path(asset_path);
    constexpr const char* kAssetsPrefix = "assets/";
    if (relative_asset_path.rfind(kAssetsPrefix, 0) == 0) {
        relative_asset_path.erase(0, std::char_traits<char>::length(kAssetsPrefix));
    }

    jstring path_string = env->NewStringUTF(relative_asset_path.c_str());
    jobject input_stream = env->CallObjectMethod(asset_manager, open_method, path_string);
    env->DeleteLocalRef(path_string);
    env->DeleteLocalRef(asset_manager);
    if (input_stream == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass input_stream_class = env->GetObjectClass(input_stream);
    if (input_stream_class == nullptr) {
        env->DeleteLocalRef(input_stream);
        return false;
    }

    jmethodID read_method = env->GetMethodID(input_stream_class, "read", "([B)I");
    jmethodID close_method = env->GetMethodID(input_stream_class, "close", "()V");
    env->DeleteLocalRef(input_stream_class);
    if (read_method == nullptr || close_method == nullptr) {
        env->DeleteLocalRef(input_stream);
        return false;
    }

    jbyteArray buffer = env->NewByteArray(4096);
    if (buffer == nullptr) {
        env->DeleteLocalRef(input_stream);
        return false;
    }

    std::string data;
    while (true) {
        const jint read_count = env->CallIntMethod(input_stream, read_method, buffer);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->CallVoidMethod(input_stream, close_method);
            env->DeleteLocalRef(buffer);
            env->DeleteLocalRef(input_stream);
            return false;
        }
        if (read_count <= 0) {
            break;
        }

        jbyte* raw = env->GetByteArrayElements(buffer, nullptr);
        if (raw == nullptr) {
            env->CallVoidMethod(input_stream, close_method);
            env->DeleteLocalRef(buffer);
            env->DeleteLocalRef(input_stream);
            return false;
        }
        data.append(reinterpret_cast<const char*>(raw), static_cast<size_t>(read_count));
        env->ReleaseByteArrayElements(buffer, raw, JNI_ABORT);
    }

    env->CallVoidMethod(input_stream, close_method);
    env->DeleteLocalRef(buffer);
    env->DeleteLocalRef(input_stream);
    *contents = data;
    return true;
#else
    (void)asset_path;
    (void)contents;
    return false;
#endif
}

}  // namespace gadget
}  // namespace nook
