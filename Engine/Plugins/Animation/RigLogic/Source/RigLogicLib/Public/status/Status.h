#pragma once

#include "status/Defs.h"
#include "status/StatusCode.h"

namespace sc {

class SCAPI Status {
    public:
        static bool isOk();
        static StatusCode get();
};

}  // namespace sc
