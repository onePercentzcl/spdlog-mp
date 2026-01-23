#pragma once

#include "main.h"
#include <iostream>

namespace onep::dpu {
    bool StartDPU(const SystemConfig &systemConfig);

    bool InitializeLogger(const onep::SystemConfig &systemConfig);


}