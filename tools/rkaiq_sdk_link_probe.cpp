#include "isp/rkaiq_backend.h"
#include RKAIQ_SYSTEM_HEADER

// Compile/link evidence only. Running this does not initialize an ISP.
int main() {
    auto backend = isp::create_sdk_backend();
    return backend ? 0 : 1;
}
