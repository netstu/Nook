# 从0到1构建一个Hook工具之gadget与重打包

## 前言

到目前为止，Nook在功能上已经可以实现正常的Hook了，接下来就是对其他功能的一些拓展，首先是gadget的使用，使得Nook可以在非Root环境下使用，以及提供了重打包的功能。

项目地址：

## 目标

这里的目标主要就三点：

1. gadget script的使用方式，即将Hook脚本和gadget一起打包进apk
2. gadget listen的使用方式，即只打包gadget进入apk，启动后等待host的启动命令
3. 提供patchapk命令，实现重打包

## 了解这些概念可以更好的理解下文

**Application和Activity**

Application 是整个应用进程级的入口，通常一个进程只会有一个；Activity 则是界面层入口，尤其是 launcher Activity，它往往是用户点开 App 后最早执行的业务代码之一。所以重打包想“尽早插入自己的逻辑”，最常见就是从这两个点下手。

在 Nook 里，这就是 minimal 模式的思路：优先把 System.loadLibrary("nook-gadget") 插进 Application.onCreate()，不行再退到 launcher Activity.onCreate()；如果目标有自定义 Application，就用 proxy-loader 直接接管它。

**assets/**

assets/ 适合放“需要跟 APK 一起分发、但又不参与 Java 资源 ID 编译”的内容，比如配置文件、脚本、二进制模板。对 gadget 来说，这放的是 config.json 和 startup.js。

**System.loadLibrary(...)**

这个方法在之前的文章就有提到过，在这里System.loadLibrary("nook-gadget") 的作用，是让 Java 层主动把对应的 native 库 libnook-gadget.so 加载到当前应用进程里。把 loadLibrary 插进足够早的启动点，后面 libnook-gadget.so 自己就会完成 runtime 初始化。

**smali**

其实就是java/kotlin的汇编，更准确的说是dex的汇编，Java/Kotlin 源码编译后，不是直接进 APK 里，而是先变成 class，再变成 Android 虚拟机使用的 dex。apktool 这类工具把 dex反汇编后就是smali文件，Nook的minimal重打包方案就是通过修改samli文件实现的。

**manifest**

AndroidManifest.xml 可以理解成 APK 的“总配置入口”或者“应用声明文件”。它决定了这个 APK 是什么、入口在哪、有哪些组件、要哪些权限、系统应该怎么启动它。对重打包来说，manifest 的关键在于重打包其实是在改“系统如何认识和启动这个 App”。

**proxy-loader**

上文提到Nook有两种重打包的模式，minimal和proxy-loader，minimal 的前提是：目标 APK 里要存在一个“你能安全改进去”的启动方法，比如标准的 Application.onCreate() 或 launcher Activity.onCreate()。而proxy-loader是直接从 manifest 层接管 Application，生成一个代理 Application。

## 重打包部分

Nook的重打包有两种bootstrap方案：

1. minimal
2. proxy-loader

minimal的做法是直接在app的启动方法中插入一段smali，加载gadget

### 从一个最小例子来理解重打包

### 具体实现细节

## gadget部分

### 从一个最小例子来理解

最小例子可以先看这条命令：

```powershell
nook-gadget patchapk --source .\target.apk --startup-script .\startup.js --on-load wait --output .\target-gadget.apk
```

这一步做的事其实就是：把 `libnook-gadget.so` 塞进 APK，把 `assets/nook-gadget/config.json` 写进去，再把 `startup.js` 打包成资源。然后再补一个最早能执行的启动点，让应用一启动就会加载 gadget。

如果是 `minimal` 模式，工具会优先往现有 `Application` 的 `onCreate()` 里插 `System.loadLibrary("nook-gadget")`；找不到的话，就退到 launcher `Activity` 的 `onCreate()`。如果是 `proxy-loader`，它会改 manifest 里的 `android:name`，生成一个代理 `Application` 来接管启动。

应用真正跑起来以后，`libnook-gadget.so` 的构造器会先触发初始化，runtime 再去读 `assets/nook-gadget/config.json`，按里面的 `interaction`、`startup_mode`、`startup_script` 决定后续怎么连、怎么加载脚本、要不要等 host 放行。

对应代码主要在 [src/gadget/nook_gadget_entry.cpp](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/gadget/nook_gadget_entry.cpp) 和 [tools/nook_patchapk.py](E:/Learn/my_program/all_my_hook/kanxue/Nook/tools/nook_patchapk.py)。

### 一次 gadget 的完整工作链路

1. 先 patch APK。工具把 gadget 库、配置和可选脚本写进包里，同时改 manifest 和启动点。
2. 再安装并启动应用。此时 gadget 已经在目标进程里，不需要先连外部 server。
3. 进程加载 `libnook-gadget.so` 后，构造器进入 `NookGadgetInitialize()`，再转到 `InitializeRuntime()`。
4. runtime 先初始化控制通道。`listen` 走本地等待/监听逻辑，`connect` 则主动向外连。
5. 之后初始化 JS/脚本桥，再决定是否自动加载打包进去的 `startup.js`。
6. 如果配置了 `on_load=wait`，进程会先卡住，等 host 连上后再 resume。

所以可以把 gadget 理解成“被打进 APK 里的自启动 runtime”：它不是等你 attach 才开始工作，而是应用一启动，它就已经在进程里把自己立起来了。

对应实现看 [src/gadget/nook_gadget_runtime.cpp](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/gadget/nook_gadget_runtime.cpp)。

### 具体实现细节

#### 1. 运行时入口

`NookGadgetInitialize()` 只负责把几个默认初始化器挂进去，然后调用 `InitializeRuntime()`。真正的流程控制都在 runtime 里完成。

#### 2. 配置驱动

`config.json` 是整个 gadget 的控制面。它不是靠命令行直接驱动，而是靠 APK 内资源里的配置决定行为。配置解析和序列化在 [src/gadget/nook_gadget_config.cpp](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/gadget/nook_gadget_config.cpp)。

#### 3. 启动脚本

如果 `startup_mode=auto-start`，runtime 会自动读取 `assets/nook-gadget/startup.js` 并加载。若是 `manual`，则可以通过内部 RPC `nook.gadget.load-configured-startup` 触发。为了避免太早跑 `Java.perform()`，脚本还会被包一层延迟逻辑，等 Java 生命周期真正 ready 之后再执行。

#### 4. 重打包策略

`minimal` 更像“在现有启动链路上插一刀”，轻，但对目标结构有要求。`proxy-loader` 更像“接管 Application”，适合自定义 `Application` 的应用。前者偏简单，后者偏稳。

#### 5. host 侧只做薄封装

`host/nook-py/nook/patchapk.py` 主要负责补默认值、找工具、拼 argv，然后调用 `tools/nook_patchapk.py`。真正的补包逻辑还是在工具脚本里。

对应代码建议重点看：

- [src/gadget/nook_gadget_entry.cpp](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/gadget/nook_gadget_entry.cpp)
- [src/gadget/nook_gadget_runtime.cpp](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/gadget/nook_gadget_runtime.cpp)
- [src/gadget/nook_gadget_config.cpp](E:/Learn/my_program/all_my_hook/kanxue/Nook/src/gadget/nook_gadget_config.cpp)
- [tools/nook_patchapk.py](E:/Learn/my_program/all_my_hook/kanxue/Nook/tools/nook_patchapk.py)
- [host/nook-py/nook/patchapk.py](E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/patchapk.py)

