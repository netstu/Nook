#include <cassert>
#include <string>

#include "framework/NookCommInternal.h"

int main() {
    using nook::framework::MakeScriptCallbackErrorMessage;
    using nook::framework::SetPendingScriptCallbackError;
    using nook::framework::TakePendingScriptCallbackError;

    assert(TakePendingScriptCallbackError().empty());

    SetPendingScriptCallbackError("script not found");
    assert(TakePendingScriptCallbackError() == "script not found");
    assert(TakePendingScriptCallbackError().empty());

    SetPendingScriptCallbackError("compile error");
    assert(MakeScriptCallbackErrorMessage("script create callback failed") == "compile error");
    assert(TakePendingScriptCallbackError().empty());

    assert(MakeScriptCallbackErrorMessage("script load callback failed") ==
           "script load callback failed");

    return 0;
}
