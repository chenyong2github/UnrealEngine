#pragma once

#ifdef RL_USE_HALF_FLOATS
    using StorageValueType = std::uint16_t;
#else
    using StorageValueType = float;
#endif
