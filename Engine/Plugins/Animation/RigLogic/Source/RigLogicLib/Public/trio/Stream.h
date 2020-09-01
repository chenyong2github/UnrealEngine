// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "trio/Concepts.h"
#include "trio/Defs.h"

#include <cstddef>

namespace trio {

class TRIOAPI BoundedIOStream : public Controllable, public Readable, public Writable, public Seekable {
    public:
        virtual ~BoundedIOStream();
        /**
            @brief Obtain size of stream in bytes.
            @return
                Size in bytes.
        */
        virtual std::size_t size() = 0;
};

}  // namespace trio
