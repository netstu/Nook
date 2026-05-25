#include "nook/NookInlineHook.h"
#include "nook/NookPltHook.h"
#include "nook/NookNativeHook.h"

int main() {
    int available = 1;
    int plt_available = 1;
    int inline_available = 1;
    NookStatus available_status = NookNativeHookIsAvailable(&available);
    NookStatus plt_status = NookPltHookIsAvailable(&plt_available);
    NookStatus inline_status = NookInlineHookIsAvailable(&inline_available);
    return (available_status <= 0 &&
            plt_status <= 0 &&
            inline_status <= 0 &&
            available == 1 &&
            plt_available == 1 &&
            inline_available == 1) ? 0 : 1;
}
