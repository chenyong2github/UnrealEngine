// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"

#include "Misc/AutomationTest.h"

#include <type_traits>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(!std::is_constructible<FSharedBuffer>::value, "Invalid constructor");

static_assert(!std::is_constructible<FSharedBufferRef>::value, "Invalid constructor");
static_assert( std::is_constructible<FSharedBufferRef, const FSharedBufferRef&>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(!std::is_constructible<FSharedBufferConstRef>::value, "Invalid constructor");
static_assert( std::is_constructible<FSharedBufferConstRef, const FSharedBufferRef&>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert( std::is_constructible<FSharedBufferConstRef, const FSharedBufferConstRef&>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert( std::is_constructible<FSharedBufferPtr>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferPtr, const FSharedBufferRef&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferPtr, const FSharedBufferPtr&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferPtr, FSharedBufferPtr&&>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert( std::is_constructible<FSharedBufferConstPtr>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferConstPtr, const FSharedBufferRef&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferConstPtr, const FSharedBufferPtr&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferConstPtr, FSharedBufferPtr&&>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferConstPtr, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert( std::is_constructible<FSharedBufferConstPtr, const FSharedBufferConstRef&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferConstPtr, const FSharedBufferConstPtr&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferConstPtr, FSharedBufferConstPtr&&>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferConstPtr, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert( std::is_constructible<FSharedBufferWeakPtr>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferWeakPtr, FSharedBufferRef>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferWeakPtr, FSharedBufferPtr>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferWeakPtr, const FSharedBufferWeakPtr&>::value, "Missing constructor");
static_assert( std::is_constructible<FSharedBufferWeakPtr, FSharedBufferWeakPtr&&>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferWeakPtr, FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferWeakPtr, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferWeakPtr, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(std::is_constructible<FSharedBufferConstWeakPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferRef>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, const FSharedBufferWeakPtr&>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferWeakPtr&&>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferConstRef>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferConstPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, const FSharedBufferConstWeakPtr&>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferConstWeakPtr&&>::value, "Missing constructor");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert( std::is_assignable<FSharedBufferRef, const FSharedBufferRef&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferConstRef>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert( std::is_assignable<FSharedBufferConstRef, const FSharedBufferRef&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert( std::is_assignable<FSharedBufferConstRef, const FSharedBufferConstRef&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert( std::is_assignable<FSharedBufferPtr, const FSharedBufferRef&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferPtr, const FSharedBufferPtr&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferPtr, FSharedBufferPtr&&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferConstRef>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert( std::is_assignable<FSharedBufferConstPtr, const FSharedBufferRef&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferConstPtr, const FSharedBufferPtr&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferConstPtr, FSharedBufferPtr&&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstPtr, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert( std::is_assignable<FSharedBufferConstPtr, const FSharedBufferConstRef&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferConstPtr, const FSharedBufferConstPtr&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferConstPtr, FSharedBufferConstPtr&&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstPtr, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert( std::is_assignable<FSharedBufferWeakPtr, const FSharedBufferRef&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferWeakPtr, const FSharedBufferPtr&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferWeakPtr, const FSharedBufferWeakPtr&>::value, "Missing assignment");
static_assert( std::is_assignable<FSharedBufferWeakPtr, FSharedBufferWeakPtr&&>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferWeakPtr, FSharedBufferConstRef>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferWeakPtr, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferWeakPtr, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert(std::is_assignable<FSharedBufferConstWeakPtr, const FSharedBufferRef&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, const FSharedBufferPtr&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, const FSharedBufferWeakPtr&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferWeakPtr&&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, const FSharedBufferConstRef&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, const FSharedBufferConstPtr&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferConstPtr&&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, const FSharedBufferConstWeakPtr&>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferConstWeakPtr&&>::value, "Missing assignment");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Alloc(DeclVal<uint64>()))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Clone(DeclVal<void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Clone(DeclVal<const void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Clone(*DeclVal<FSharedBufferRef>()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Clone(*DeclVal<FSharedBufferConstRef>()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Clone(DeclVal<FMutableMemoryView>()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::Clone(DeclVal<FConstMemoryView>()))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::MakeView(DeclVal<FMutableMemoryView>()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::MakeView(DeclVal<FConstMemoryView>()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::MakeView(DeclVal<void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::MakeView(DeclVal<const void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::MakeView(DeclVal<FConstMemoryView>(), DeclVal<const FSharedBuffer&>()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::MakeView(DeclVal<const void*>(), 0, DeclVal<const FSharedBuffer&>()))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::TakeOwnership(DeclVal<void*>(), 0, FMemory::Free))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::TakeOwnership(DeclVal<const void*>(), 0, FMemory::Free))>::value, "Invalid constructor");

static_assert(std::is_same<void*, decltype(DeclVal<FSharedBuffer>().GetData())>::value, "Invalid accessor");
static_assert(std::is_same<const void*, decltype(DeclVal<const FSharedBuffer>().GetData())>::value, "Invalid accessor");
static_assert(std::is_same<uint64, decltype(DeclVal<FSharedBuffer>().GetSize())>::value, "Invalid accessor");

static_assert(std::is_same<FMutableMemoryView, decltype(DeclVal<FSharedBuffer>().GetView())>::value, "Invalid accessor");
static_assert(std::is_same<FConstMemoryView, decltype(DeclVal<const FSharedBuffer>().GetView())>::value, "Invalid accessor");

static_assert(std::is_convertible<FSharedBuffer, FMutableMemoryView>::value, "Missing conversion");
static_assert(std::is_convertible<FSharedBuffer, FConstMemoryView>::value, "Missing conversion");
static_assert(!std::is_convertible<const FSharedBuffer, FMutableMemoryView>::value, "Invalid conversion");
static_assert(std::is_convertible<const FSharedBuffer, FConstMemoryView>::value, "Missing conversion");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<FSharedBufferRef, decltype(FSharedBuffer::MakeOwned(DeclVal<const FSharedBufferRef&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferRef, decltype(FSharedBuffer::MakeOwned(DeclVal<FSharedBufferRef&&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::MakeOwned(DeclVal<const FSharedBufferPtr&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferPtr, decltype(FSharedBuffer::MakeOwned(DeclVal<FSharedBufferPtr&&>()))>::value, "Invalid conversion");

static_assert(std::is_same<FSharedBufferConstRef, decltype(FSharedBuffer::MakeOwned(DeclVal<const FSharedBufferConstRef&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferConstRef, decltype(FSharedBuffer::MakeOwned(DeclVal<FSharedBufferConstRef&&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::MakeOwned(DeclVal<const FSharedBufferConstPtr&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(FSharedBuffer::MakeOwned(DeclVal<FSharedBufferConstPtr&&>()))>::value, "Invalid conversion");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() == DeclVal<const FSharedBufferRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() == DeclVal<const FSharedBufferPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() == DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() == DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() == DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() == DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid equality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() == DeclVal<const FSharedBufferRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() == DeclVal<const FSharedBufferPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() == DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() == DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() == DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() == DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid equality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() == DeclVal<const FSharedBufferRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() == DeclVal<const FSharedBufferPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() == DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() == DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() == DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() == DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid equality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() == DeclVal<const FSharedBufferRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() == DeclVal<const FSharedBufferPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() == DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() == DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() == DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() == DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid equality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() == DeclVal<const FSharedBufferRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() == DeclVal<const FSharedBufferPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() == DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() == DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() == DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() == DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid equality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() == DeclVal<const FSharedBufferRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() == DeclVal<const FSharedBufferPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() == DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() == DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() == DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid equality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() == DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid equality");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() != DeclVal<const FSharedBufferRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() != DeclVal<const FSharedBufferPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() != DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() != DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() != DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferRef&>() != DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid inequality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() != DeclVal<const FSharedBufferRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() != DeclVal<const FSharedBufferPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() != DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() != DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() != DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferPtr&>() != DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid inequality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() != DeclVal<const FSharedBufferRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() != DeclVal<const FSharedBufferPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() != DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() != DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() != DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferWeakPtr&>() != DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid inequality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() != DeclVal<const FSharedBufferRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() != DeclVal<const FSharedBufferPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() != DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() != DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() != DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstRef&>() != DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid inequality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() != DeclVal<const FSharedBufferRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() != DeclVal<const FSharedBufferPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() != DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() != DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() != DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstPtr&>() != DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid inequality");

static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() != DeclVal<const FSharedBufferRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() != DeclVal<const FSharedBufferPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() != DeclVal<const FSharedBufferWeakPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() != DeclVal<const FSharedBufferConstRef&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() != DeclVal<const FSharedBufferConstPtr&>())>::value, "Invalid inequality");
static_assert(std::is_same<bool, decltype(DeclVal<const FSharedBufferConstWeakPtr&>() != DeclVal<const FSharedBufferConstWeakPtr&>())>::value, "Invalid inequality");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<uint32, decltype(GetTypeHash(DeclVal<const FSharedBufferRef>()))>::value, "Missing or invalid hash function");
static_assert(std::is_same<uint32, decltype(GetTypeHash(DeclVal<const FSharedBufferConstRef>()))>::value, "Missing or invalid hash function");
static_assert(std::is_same<uint32, decltype(GetTypeHash(DeclVal<const FSharedBufferPtr>()))>::value, "Missing or invalid hash function");
static_assert(std::is_same<uint32, decltype(GetTypeHash(DeclVal<const FSharedBufferConstPtr>()))>::value, "Missing or invalid hash function");
static_assert(std::is_same<uint32, decltype(GetTypeHash(DeclVal<const FSharedBufferWeakPtr>()))>::value, "Missing or invalid hash function");
static_assert(std::is_same<uint32, decltype(GetTypeHash(DeclVal<const FSharedBufferConstWeakPtr>()))>::value, "Missing or invalid hash function");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedBufferTest, "System.Core.Memory.SharedBuffer", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FSharedBufferTest::RunTest(const FString& Parameters)
{
	// Test Size
	{
		constexpr uint64 Size = 64;
		FSharedBufferRef Ref = FSharedBuffer::Alloc(64).ToSharedRef();
		TestTrue(TEXT("FSharedBuffer(Size).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer(Size).GetSize()"), Ref->GetSize(), Size);
	}

	// Test Clone
	{
		constexpr uint64 Size = 64;
		const uint8 Data[Size]{};
		FSharedBufferRef Ref = FSharedBuffer::Clone(Data, Size).ToSharedRef();
		TestTrue(TEXT("FSharedBuffer::Clone().IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer::Clone().GetSize()"), Ref->GetSize(), Size);
		TestNotEqual(TEXT("FSharedBuffer::Clone().GetData()"), static_cast<const void*>(Ref->GetData()), static_cast<const void*>(Data));
	}

	// Test MakeView
	{
		constexpr uint64 Size = 64;
		uint8 Data[Size]{};
		FSharedBufferRef Ref = FSharedBuffer::MakeView(Data, Size).ToSharedRef();
		TestFalse(TEXT("FSharedBuffer::MakeView().IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer::MakeView().GetSize()"), Ref->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::MakeView().GetData()"), Ref->GetData(), static_cast<void*>(Data));
	}

	// Test MakeView Outer
	{
		constexpr uint64 Size = 64;
		FSharedBufferWeakPtr WeakPtr;
		{
			FSharedBufferConstPtr Ptr;
			{
				FSharedBufferRef OuterRef = FSharedBuffer::Alloc(Size).ToSharedRef();
				Ptr = FSharedBuffer::MakeView(OuterRef->GetData(), OuterRef->GetSize() / 2, *OuterRef);
				WeakPtr = OuterRef;
			}
			TestFalse(TEXT("FSharedBuffer::MakeView(Outer).IsOwned()"), Ptr->IsOwned());
			TestFalse(TEXT("FSharedBuffer::MakeView(Outer).IsReadOnly()"), Ptr->IsReadOnly());
			TestTrue(TEXT("FSharedBuffer::MakeView(Outer) Outer Valid"), WeakPtr.Pin().IsValid());
		}
		TestFalse(TEXT("FSharedBuffer::MakeView(Outer) Outer Not Valid"), WeakPtr.Pin().IsValid());
	}

	// Test MakeView Outer Read-Only
	{
		constexpr uint64 Size = 64;
		FSharedBufferConstPtr Ptr;
		{
			FSharedBufferConstRef OuterRef = FSharedBuffer::MakeReadOnly(FSharedBuffer::Alloc(Size)).ToSharedRef();
			Ptr = FSharedBuffer::MakeView(OuterRef->GetData(), OuterRef->GetSize() / 2, *OuterRef);
		}
		TestFalse(TEXT("FSharedBuffer::MakeView(Outer ReadOnly).IsOwned()"), Ptr->IsOwned());
		TestTrue(TEXT("FSharedBuffer::MakeView(Outer ReadOnly).IsReadOnly()"), Ptr->IsReadOnly());
	}

	// Test TakeOwnership FMemory::Free
	{
		constexpr uint64 Size = 64;
		void* const Data = FMemory::Malloc(Size);
		FSharedBufferRef Ref = FSharedBuffer::TakeOwnership(Data, Size, FMemory::Free).ToSharedRef();
		TestTrue(TEXT("FSharedBuffer::TakeOwnership(FMemory::Free).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer::TakeOwnership(FMemory::Free).GetSize()"), Ref->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::TakeOwnership(FMemory::Free).GetData()"), Ref->GetData(), Data);
	}

	// Test TakeOwnership Lambda
	{
		bool bDeleted = false;
		auto Deleter = [&bDeleted](void* Data)
		{
			bDeleted = true;
			delete[] static_cast<uint8*>(Data);
		};
		constexpr uint64 Size = 64;
		FSharedBuffer::TakeOwnership(new uint8[Size], Size, Deleter);
		TestTrue(TEXT("FSharedBuffer::TakeOwnership(Lambda) Deleted"), bDeleted);
	}

	// Test TakeOwnership BufferOwner
	{
		bool bDeleted = false;
		class FTestBufferOwner final : public FSharedBuffer::FBufferOwner
		{
		public:
			FTestBufferOwner(bool& bInDeletedRef)
				: bDeletedRef(bInDeletedRef)
			{
			}

		private:
			virtual void Free(void* Data, uint64 Size) final
			{
				bDeletedRef = true;
			}

			bool& bDeletedRef;
		};
		constexpr uint64 Size = 64;
		FSharedBuffer::TakeOwnership(nullptr, 0, FTestBufferOwner(bDeleted));
		TestTrue(TEXT("FSharedBuffer::TakeOwnership(BufferOwner) Deleted"), bDeleted);
	}

	// Test MakeOwned
	{
		constexpr uint64 Size = 64;
		uint8 Data[Size]{};
		FSharedBufferRef Ref = FSharedBuffer::MakeOwned(FSharedBuffer::MakeView(Data, Size).ToSharedRef());
		TestTrue(TEXT("FSharedBuffer::MakeOwned(Wrap).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer::MakeOwned(Wrap).GetSize()"), Ref->GetSize(), Size);
		TestNotEqual(TEXT("FSharedBuffer::MakeOwned(Wrap).GetData()"), Ref->GetData(), static_cast<void*>(Data));
		FSharedBufferRef RefCopy = FSharedBuffer::MakeOwned(Ref);
		TestTrue(TEXT("FSharedBuffer::MakeOwned(Owned).IsOwned()"), RefCopy->IsOwned());
		TestEqual(TEXT("FSharedBuffer::MakeOwned(Owned).GetSize()"), RefCopy->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::MakeOwned(Owned).GetData()"), RefCopy->GetData(), Ref->GetData());
	}
	{
		FSharedBufferPtr Ptr;
		TestFalse(TEXT("FSharedBuffer::MakeOwned(Null)"), FSharedBuffer::MakeOwned(Ptr).IsValid());
		TestFalse(TEXT("FSharedBuffer::MakeOwned(Null)"), FSharedBuffer::MakeOwned(AsConst(Ptr)).IsValid());
		TestFalse(TEXT("FSharedBuffer::MakeOwned(Null)"), FSharedBuffer::MakeOwned(FSharedBufferPtr()).IsValid());
		TestFalse(TEXT("FSharedBuffer::MakeOwned(Null)"), FSharedBuffer::MakeOwned(FSharedBufferConstPtr()).IsValid());
	}

	// Test MakeReadOnly
	{
		// MakeReadOnly from a new reference.
		constexpr uint64 Size = 64;
		FSharedBufferConstRef Ref = FSharedBuffer::MakeReadOnly(FSharedBuffer::Alloc(Size)).ToSharedRef();
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(AllocRef).IsOwned()"), Ref->IsOwned());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(AllocRef).IsReadOnly()"), Ref->IsReadOnly());
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(AllocRef).GetSize()"), Ref->GetSize(), Size);
		FSharedBufferConstRef OtherRef = Ref;
		FSharedBufferConstRef ReadOnlyRef = FSharedBuffer::MakeReadOnly(MoveTemp(Ref));
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(AllocRef).GetData()"), Ref->GetData(), ReadOnlyRef->GetData());
	}
	{
		// MakeReadOnly from a new pointer.
		constexpr uint64 Size = 64;
		FSharedBufferPtr MutablePtr = FSharedBuffer::Alloc(Size);
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).IsReadOnly()"), MutablePtr->IsReadOnly());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).IsOwned()"), MutablePtr->IsOwned());
		const void* const Data = MutablePtr->GetData();
		FSharedBufferConstPtr Ptr = FSharedBuffer::MakeReadOnly(MoveTemp(MutablePtr));
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).IsOwned()"), Ptr->IsOwned());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).IsReadOnly()"), Ptr->IsReadOnly());
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).GetSize()"), Ptr->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).GetData()"), Ptr->GetData(), Data);
	}
	{
		// MakeReadOnly from a view.
		constexpr uint64 Size = 64;
		uint8 Data[Size]{};
		FSharedBufferPtr MutablePtr = FSharedBuffer::MakeView(Data, Size);
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(View).IsOwned()"), MutablePtr->IsOwned());
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(View).IsReadOnly()"), MutablePtr->IsReadOnly());
		FSharedBufferConstPtr Ptr = FSharedBuffer::MakeReadOnly(MoveTemp(MutablePtr));
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(View).IsOwned()"), Ptr->IsOwned());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(View).IsReadOnly()"), Ptr->IsReadOnly());
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(View).GetSize()"), Ptr->GetSize(), Size);
		TestNotEqual(TEXT("FSharedBuffer::MakeReadOnly(View).GetData()"), Ptr->GetData(), static_cast<const void*>(Data));
	}
	{
		// MakeReadOnly with another shared reference.
		constexpr uint64 Size = 64;
		FSharedBufferPtr MutablePtr = FSharedBuffer::Alloc(Size);
		FSharedBufferConstPtr SharedPtr = MutablePtr;
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(SharedRef).IsReadOnly()"), MutablePtr->IsReadOnly());
		const void* const Data = MutablePtr->GetData();
		FSharedBufferConstPtr Ptr = FSharedBuffer::MakeReadOnly(MoveTemp(MutablePtr));
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(SharedRef).IsOwned()"), Ptr->IsOwned());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(SharedRef).IsReadOnly()"), Ptr->IsReadOnly());
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(SharedRef).IsReadOnly(SharedPtr)"), SharedPtr->IsReadOnly());
		TestNotEqual(TEXT("FSharedBuffer::MakeReadOnly(SharedRef).GetData()"), Ptr->GetData(), Data);
	}
	{
		// MakeReadOnly with another weak reference.
		constexpr uint64 Size = 64;
		FSharedBufferPtr MutablePtr = FSharedBuffer::Alloc(Size);
		FSharedBufferConstWeakPtr WeakPtr = MutablePtr;
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(WeakRef).IsReadOnly()"), MutablePtr->IsReadOnly());
		const void* const Data = MutablePtr->GetData();
		FSharedBufferConstPtr Ptr = FSharedBuffer::MakeReadOnly(MoveTemp(MutablePtr));
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(WeakRef).IsOwned()"), Ptr->IsOwned());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(WeakRef).IsReadOnly()"), Ptr->IsReadOnly());
		TestNotEqual(TEXT("FSharedBuffer::MakeReadOnly(WeakRef).GetData()"), Ptr->GetData(), Data);
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(WeakRef).Pin().IsValid()"), WeakPtr.Pin().IsValid());
	}

	// Test MakeWritable
	{
		// MakeWritable from a new reference.
		constexpr uint64 Size = 64;
		FSharedBufferConstRef ConstRef = FSharedBuffer::MakeReadOnly(FSharedBuffer::Alloc(Size)).ToSharedRef();
		FSharedBufferRef Ref = FSharedBuffer::MakeWritable(MoveTemp(ConstRef));
		TestTrue(TEXT("FSharedBuffer::MakeWritable(AllocRef).IsOwned()"), Ref->IsOwned());
		TestFalse(TEXT("FSharedBuffer::MakeWritable(AllocRef).IsReadOnly()"), Ref->IsReadOnly());
		TestEqual(TEXT("FSharedBuffer::MakeWritable(AllocRef).GetSize()"), Ref->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::MakeWritable(AllocRef).GetData()"), const_cast<const void*>(Ref->GetData()), ConstRef->GetData());
		FSharedBufferRef MutableRef = FSharedBuffer::MakeWritable(MoveTemp(Ref));
		TestEqual(TEXT("FSharedBuffer::MakeWritable(AllocRef).GetData()"), Ref->GetData(), MutableRef->GetData());
	}
	{
		// MakeWritable from a new pointer.
		constexpr uint64 Size = 64;
		FSharedBufferConstPtr ConstPtr = FSharedBuffer::MakeReadOnly(FSharedBuffer::Alloc(Size));
		const void* const Data = ConstPtr->GetData();
		FSharedBufferPtr Ptr = FSharedBuffer::MakeWritable(MoveTemp(ConstPtr));
		TestFalse(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).IsReadOnly()"), Ptr->IsReadOnly());
		TestTrue(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).IsOwned()"), Ptr->IsOwned());
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).GetSize()"), Ptr->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::MakeReadOnly(MovePtr).GetData()"), const_cast<const void*>(Ptr->GetData()), Data);
	}
	{
		// MakeWritable from a mutable view.
		constexpr uint64 Size = 64;
		uint8 Data[Size]{};
		FSharedBufferPtr Ptr = FSharedBuffer::MakeWritable(FSharedBuffer::MakeView(Data, Size));
		TestFalse(TEXT("FSharedBuffer::MakeWritable(View).IsOwned()"), Ptr->IsOwned());
		TestFalse(TEXT("FSharedBuffer::MakeWritable(View).IsReadOnly()"), Ptr->IsReadOnly());
		TestEqual(TEXT("FSharedBuffer::MakeWritable(View).GetSize()"), Ptr->GetSize(), Size);
		TestEqual(TEXT("FSharedBuffer::MakeWritable(View).GetData()"), const_cast<const void*>(Ptr->GetData()), static_cast<const void*>(Data));
	}
	{
		// MakeWritable from a const view.
		constexpr uint64 Size = 64;
		const uint8 Data[Size]{};
		FSharedBufferPtr Ptr = FSharedBuffer::MakeWritable(FSharedBuffer::MakeView(Data, Size));
		TestTrue(TEXT("FSharedBuffer::MakeWritable(View).IsOwned()"), Ptr->IsOwned());
		TestFalse(TEXT("FSharedBuffer::MakeWritable(View).IsReadOnly()"), Ptr->IsReadOnly());
		TestEqual(TEXT("FSharedBuffer::MakeWritable(View).GetSize()"), Ptr->GetSize(), Size);
		TestNotEqual(TEXT("FSharedBuffer::MakeWritable(View).GetData()"), const_cast<const void*>(Ptr->GetData()), static_cast<const void*>(Data));
	}
	{
		// MakeWritable with another shared reference.
		constexpr uint64 Size = 64;
		FSharedBufferConstPtr ConstPtr = FSharedBuffer::MakeReadOnly(FSharedBuffer::Alloc(Size));
		FSharedBufferConstPtr SharedPtr = ConstPtr;
		const void* const Data = ConstPtr->GetData();
		FSharedBufferPtr Ptr = FSharedBuffer::MakeWritable(MoveTemp(ConstPtr));
		TestTrue(TEXT("FSharedBuffer::MakeWritable(SharedRef).IsOwned()"), Ptr->IsOwned());
		TestFalse(TEXT("FSharedBuffer::MakeWritable(SharedRef).IsReadOnly()"), Ptr->IsReadOnly());
		TestNotEqual(TEXT("FSharedBuffer::MakeWritable(SharedRef).GetData()"), const_cast<const void*>(Ptr->GetData()), Data);
	}
	{
		// MakeWritable with another weak reference.
		constexpr uint64 Size = 64;
		FSharedBufferConstPtr ConstPtr = FSharedBuffer::MakeReadOnly(FSharedBuffer::Alloc(Size));
		FSharedBufferConstWeakPtr WeakPtr = ConstPtr;
		const void* const Data = ConstPtr->GetData();
		FSharedBufferPtr Ptr = FSharedBuffer::MakeWritable(MoveTemp(ConstPtr));
		TestTrue(TEXT("FSharedBuffer::MakeWritable(WeakRef).IsOwned()"), Ptr->IsOwned());
		TestFalse(TEXT("FSharedBuffer::MakeWritable(WeakRef).IsReadOnly()"), Ptr->IsReadOnly());
		TestNotEqual(TEXT("FSharedBuffer::MakeWritable(WeakRef).GetData()"), const_cast<const void*>(Ptr->GetData()), Data);
		TestTrue(TEXT("FSharedBuffer::MakeWritable(WeakRef).Pin().IsValid()"), WeakPtr.Pin().IsValid());
	}

	// Test WeakPtr
	{
		FSharedBufferWeakPtr WeakPtr;
		{
			FSharedBufferRef Ref = FSharedBuffer::Alloc(0).ToSharedRef();
			WeakPtr = Ref;
			TestTrue(TEXT("FSharedBufferWeakPtr(Ref).Pin().IsValid()"), WeakPtr.Pin().IsValid());
		}
		TestFalse(TEXT("FSharedBufferWeakPtr(Ref).Pin().IsValid()"), WeakPtr.Pin().IsValid());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
