// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// MemoryView

template <typename DataType>
class TMemoryView;

/** A non-owning view of a contiguous region of memory. */
using FMemoryView = TMemoryView<const void>;

/** A non-owning mutable view of a contiguous region of memory. */
using FMutableMemoryView = TMemoryView<void>;

// SharedBuffer

class FUniqueBuffer;
class FSharedBuffer;
class FWeakSharedBuffer;
