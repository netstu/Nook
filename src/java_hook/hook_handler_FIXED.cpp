// 完整修复的 hook_handler 版本
// 替换 core/hook/JavaHook.cpp 中的 hook_handler 函数

extern "C" uint64_t hook_handler(JNIEnv* env, jobject thiz,
                                 uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5,
                                 uint64_t x6, uint64_t x7) {
    // 获取 hook_id
    uint64_t tmpHookid;
    asm volatile("mov %0, x15" : "=r"(tmpHookid));
    uint32_t hookID = (uint32_t)tmpHookid;

    // 获取浮点寄存器
    double v0, v1, v2, v3, v4, v5, v6, v7;
    asm volatile(
        "mov %0, v0.d[0]\n"
        "mov %1, v1.d[0]\n"
        "mov %2, v2.d[0]\n"
        "mov %3, v3.d[0]\n"
        "mov %4, v4.d[0]\n"
        "mov %5, v5.d[0]\n"
        "mov %6, v6.d[0]\n"
        "mov %7, v7.d[0]\n"
        : "=r"(v0), "=r"(v1), "=r"(v2), "=r"(v3),
          "=r"(v4), "=r"(v5), "=r"(v6), "=r"(v7)
        :
        :);

    // 获取栈参数
    uint64_t origin_sp;
    asm volatile("mov %0, x29" : "=r"(origin_sp));
    void* args_in = (void*)(origin_sp + 0x20);

    // 获取 Hook 信息
    HookInfo hookInfo = HookStore<HookInfo>::Instance().CopyByIndex(hookID);
    std::mutex& mtx = HookIdLockManager::Instance().GetMutex(hookID);
    std::lock_guard<std::mutex> lock(mtx);

    if (!hookInfo.valid) {
        LOGE("Hook %d is invalid", hookID);
        return 0;
    }

    // 解析参数
    size_t paramCount = hookInfo.shorty.size() - 1;
    HookValue* args = new HookValue[paramCount];

    // 修复问题 2: 标记哪些参数是 jobject LocalRef
    bool* needsLocalRef = new bool[paramCount];
    memset(needsLocalRef, 0, sizeof(bool) * paramCount);

    // 修复问题 3: 保存原始 StackReference* 指针，用于检测是否被修改
    uint64_t* original_obj_ptrs = new uint64_t[paramCount];
    memset(original_obj_ptrs, 0, sizeof(uint64_t) * paramCount);

    int x_reg_count = 2;  // x0(env), x1(thiz) 已用
    int v_reg_count = 0;
    int stack_reg_count = 0;

    for (size_t i = 0; i < paramCount; i++) {
        char type = hookInfo.shorty[i + 1];

        switch (type) {
            case 'F':
                if (v_reg_count < 8) {
                    double* vregs[] = {&v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7};
                    args[i].f = *(float*)vregs[v_reg_count];
                } else {
                    args[i].f = *(float*)((uint64_t)args_in + stack_reg_count * 8);
                    stack_reg_count++;
                }
                v_reg_count++;
                break;

            case 'D':
                if (v_reg_count < 8) {
                    double* vregs[] = {&v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7};
                    args[i].d = *vregs[v_reg_count];
                } else {
                    args[i].d = *(double*)((uint64_t)args_in + stack_reg_count * 8);
                    stack_reg_count++;
                }
                v_reg_count++;
                break;

            case 'L':  // 修复问题 2: 对象引用 - 转换 StackReference* 为 jobject LocalRef
                {
                    uint64_t obj_ptr;
                    if (x_reg_count <= 7) {
                        uint64_t* xregs[] = {&x2, &x3, &x4, &x5, &x6, &x7};
                        obj_ptr = *xregs[x_reg_count - 2];
                    } else {
                        obj_ptr = *(uint64_t*)((uint64_t)args_in + stack_reg_count * 8);
                        stack_reg_count++;
                    }

                    // 保存原始 StackReference* 指针
                    original_obj_ptrs[i] = obj_ptr;

                    // 将 StackReference* 转换为 jobject LocalRef
                    jobject obj = (jobject)ArtInternals::newlocalrefFn(env, (void*)obj_ptr);
                    args[i].l = obj;
                    needsLocalRef[i] = true;  // 标记需要清理
                    x_reg_count++;
                }
                break;

            default:
                if (x_reg_count <= 7) {
                    uint64_t* xregs[] = {&x2, &x3, &x4, &x5, &x6, &x7};
                    args[i].u = *xregs[x_reg_count - 2];
                } else {
                    args[i].u = *(uint64_t*)((uint64_t)args_in + stack_reg_count * 8);
                    stack_reg_count++;
                }
                x_reg_count++;
        }
    }

    // 调用用户回调
    HookValue directRet = {0};
    bool callOriginal = hookInfo.callback(env, thiz, args, paramCount, &directRet);

    // 清理 LocalRef
    for (size_t i = 0; i < paramCount; i++) {
        if (needsLocalRef[i] && args[i].l != nullptr) {
            env->DeleteLocalRef((jobject)args[i].l);
        }
    }
    delete[] needsLocalRef;
    delete[] original_obj_ptrs;

    // 修复问题 4: 处理对象返回值
    if (!callOriginal) {
        delete[] args;
        switch (hookInfo.shorty[0]) {
            case 'F': {
                asm volatile("fmov s0, %s0" : : "w"(directRet.f));
                return 0;
            }
            case 'D': {
                asm volatile("fmov d0, %d0" : : "w"(directRet.d));
                return 0;
            }
            case 'L': {
                // 对象返回值 - 将 jobject 转换为压缩引用
                jobject obj = (jobject)directRet.l;
                if (obj != nullptr) {
                    // 获取压缩引用（假设对象已被正确转换为 LocalRef）
                    // 这里直接返回指针，ART 会处理
                    return (uint64_t)obj;
                }
                return 0;
            }
            default:
                return directRet.u;
        }
    }

    // 调用原函数
    void* thread = ArtInternals::GetCurrentThread();
    if (!thread) {
        LOGE("Failed to get current thread");
        delete[] args;
        return 0;
    }

    // GC 保护
    if (!ArtInternals::SGCFn || !ArtInternals::DestroyGCFn) {
        LOGE("ScopedGCCriticalSection helpers are unavailable");
        delete[] args;
        return 0;
    }

    char gcScope[256] = {};
    ArtInternals::SGCFn(gcScope, thread, kGcCauseDebugger, kCollectorTypeDebugger);

    // 构造参数数组
    auto argsArray = new uint32_t[(paramCount + 2) * 8];
    memset(argsArray, 0, sizeof(uint32_t) * (paramCount + 2) * 8);
    uint32_t argsize = 0;

    if (!hookInfo.isStatic) {
        argsArray[0] = *(uint32_t*)thiz;
        argsize += 4;
    }

    // 填充参数
    for (size_t i = 0; i < paramCount; i++) {
        char type = hookInfo.shorty[i + 1];
        switch (type) {
            case 'F':
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].f, sizeof(float));
                argsize += 4;
                break;
            case 'D':
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].d, sizeof(double));
                argsize += 8;
                break;
            case 'J':
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].j, sizeof(int64_t));  // 修复问题 1: 64 位
                argsize += 8;
                break;
            case 'L':  // 修复问题 3: 处理修改后的对象参数
                {
                    uint64_t obj_ptr = args[i].u;
                    // 检查是否被修改
                    if (obj_ptr != original_obj_ptrs[i]) {
                        // 用户修改了对象，需要转换回压缩引用
                        obj_ptr &= (~(1));  // 对齐
                        uint32_t compressed_ref = *(uint32_t*)obj_ptr;
                        memcpy((void*)((uint64_t)argsArray + argsize), &compressed_ref, sizeof(uint32_t));
                    } else {
                        // 未修改，使用原始压缩引用
                        uint32_t compressed_ref = *(uint32_t*)original_obj_ptrs[i];
                        memcpy((void*)((uint64_t)argsArray + argsize), &compressed_ref, sizeof(uint32_t));
                    }
                    argsize += 4;
                }
                break;
            default:
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].i, sizeof(int32_t));
                argsize += 4;
                break;
        }
    }

    delete[] args;
    delete[] original_obj_ptrs;

    // 拷贝 ArtMethod 并调用
    auto tocallOrigin = new uint8_t[hookInfo.layout.art_method_size];
    memcpy(tocallOrigin, hookInfo.artMethod, hookInfo.layout.art_method_size);
    recover_artmethod(tocallOrigin, hookInfo, true);

    jvalue result;
    ArtInternals::Invoke(tocallOrigin, thread, argsArray, argsize, &result, hookInfo.shorty.c_str());

    delete[] tocallOrigin;
    delete[] argsArray;
    ArtInternals::DestroyGCFn(gcScope);

    // 返回结果
    uint64_t ret = 0;
    switch (hookInfo.shorty[0]) {
        case 'F':
            asm volatile("fmov s0, %s0" : : "w"(result.f));
            return 0;
        case 'D':
            asm volatile("fmov d0, %d0" : : "w"(result.d));
            return 0;
        case 'Z': ret = result.z; break;
        case 'B': ret = result.b; break;
        case 'C': ret = result.c; break;
        case 'S': ret = result.s; break;
        case 'I': ret = result.i; break;
        case 'J': ret = result.j; break;
        case 'L':
            // 对象返回值 - 需要转换为 LocalRef
            if (result.l != nullptr) {
                jobject localRef = (jobject)ArtInternals::newlocalrefFn(env, result.l);
                ret = (uint64_t)localRef;
            }
            break;
        case 'V':
            return 0;
    }
    return ret;
}
