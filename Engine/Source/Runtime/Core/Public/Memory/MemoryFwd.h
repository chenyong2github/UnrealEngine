// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// MemoryView

template <typename DataType>
class TMemoryView;

/** A non-owning mutable view of a contiguous region of memory. */
using FMutableMemoryView = TMemoryView<void>;

/** A non-owning const view of a contiguous region of memory. */
using FConstMemoryView = TMemoryView<const void>;

// SharedBuffer

class FSharedBufferRef;
class FSharedBufferConstRef;
class FSharedBufferPtr;
class FSharedBufferConstPtr;
class FSharedBufferWeakPtr;
class FSharedBufferConstWeakPtr;
