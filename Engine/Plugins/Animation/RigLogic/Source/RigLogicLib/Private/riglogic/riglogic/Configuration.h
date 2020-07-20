#pragma once

#include "riglogic/riglogic/CalculationType.h"

namespace rl4 {

struct Configuration {
    CalculationType calculationType;

    template<class Archive>
    void serialize(Archive& archive) {
        archive(calculationType);
    }

};

}  // namespace rl4
