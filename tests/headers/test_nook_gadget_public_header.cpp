#include "nook/NookGadget.h"

int main() {
    auto init = &NookGadgetInitialize;
    return init != nullptr ? 0 : 1;
}
