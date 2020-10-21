// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"

#include "Misc/AutomationTest.h"

#include <type_traits>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(!std::is_constructible<FSharedBufferRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferRef, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(!std::is_constructible<FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(std::is_constructible<FSharedBufferConstRef, FSharedBufferRef>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferConstRef, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(std::is_constructible<FSharedBufferPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferPtr, FSharedBufferRef>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferPtr, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(std::is_constructible<FSharedBufferConstPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstPtr, FSharedBufferRef>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstPtr, FSharedBufferPtr>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferConstPtr, FSharedBufferWeakPtr>::value, "Invalid constructor");
static_assert(std::is_constructible<FSharedBufferConstPtr, FSharedBufferConstRef>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferConstPtr, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(std::is_constructible<FSharedBufferWeakPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferWeakPtr, FSharedBufferRef>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferWeakPtr, FSharedBufferPtr>::value, "Missing constructor");
static_assert(!std::is_constructible<FSharedBufferWeakPtr, FSharedBufferConstRef>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferWeakPtr, FSharedBufferConstPtr>::value, "Invalid constructor");
static_assert(!std::is_constructible<FSharedBufferWeakPtr, FSharedBufferConstWeakPtr>::value, "Invalid constructor");

static_assert(std::is_constructible<FSharedBufferConstWeakPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferRef>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferWeakPtr>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferConstRef>::value, "Missing constructor");
static_assert(std::is_constructible<FSharedBufferConstWeakPtr, FSharedBufferConstPtr>::value, "Missing constructor");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_assignable<FSharedBufferRef, FSharedBufferRef>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferConstRef>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferRef, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert(std::is_assignable<FSharedBufferConstRef, FSharedBufferRef>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert(std::is_assignable<FSharedBufferConstRef, FSharedBufferConstRef>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferConstRef, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert(std::is_assignable<FSharedBufferPtr, FSharedBufferRef>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferPtr, FSharedBufferPtr>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferConstRef>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferPtr, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert(std::is_assignable<FSharedBufferConstPtr, FSharedBufferRef>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstPtr, FSharedBufferPtr>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstPtr, FSharedBufferWeakPtr>::value, "Invalid assignment");
static_assert(std::is_assignable<FSharedBufferConstPtr, FSharedBufferConstRef>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstPtr, FSharedBufferConstPtr>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferConstPtr, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert(std::is_assignable<FSharedBufferWeakPtr, FSharedBufferRef>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferWeakPtr, FSharedBufferPtr>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferWeakPtr, FSharedBufferWeakPtr>::value, "Missing assignment");
static_assert(!std::is_assignable<FSharedBufferWeakPtr, FSharedBufferConstRef>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferWeakPtr, FSharedBufferConstPtr>::value, "Invalid assignment");
static_assert(!std::is_assignable<FSharedBufferWeakPtr, FSharedBufferConstWeakPtr>::value, "Invalid assignment");

static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferRef>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferPtr>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferWeakPtr>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferConstRef>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferConstPtr>::value, "Missing assignment");
static_assert(std::is_assignable<FSharedBufferConstWeakPtr, FSharedBufferConstWeakPtr>::value, "Missing assignment");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer())>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(DeclVal<uint64>()))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::AssumeOwnership, DeclVal<void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstRef, decltype(MakeSharedBuffer(FSharedBuffer::AssumeOwnership, DeclVal<const void*>(), 0))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::Clone, DeclVal<void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::Clone, DeclVal<const void*>(), 0))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::Wrap, DeclVal<void*>(), 0))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstRef, decltype(MakeSharedBuffer(FSharedBuffer::Wrap, DeclVal<const void*>(), 0))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::AssumeOwnership, FMutableMemoryView()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstRef, decltype(MakeSharedBuffer(FSharedBuffer::AssumeOwnership, FConstMemoryView()))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::Clone, FMutableMemoryView()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::Clone, FConstMemoryView()))>::value, "Invalid constructor");

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBuffer(FSharedBuffer::Wrap, FMutableMemoryView()))>::value, "Invalid constructor");
static_assert(std::is_same<FSharedBufferConstRef, decltype(MakeSharedBuffer(FSharedBuffer::Wrap, FConstMemoryView()))>::value, "Invalid constructor");

static_assert(std::is_same<void*, decltype(DeclVal<FSharedBuffer>().GetData())>::value, "Invalid accessor");
static_assert(std::is_same<const void*, decltype(DeclVal<const FSharedBuffer>().GetData())>::value, "Invalid accessor");

static_assert(std::is_same<FMutableMemoryView, decltype(DeclVal<FSharedBuffer>().GetView())>::value, "Invalid accessor");
static_assert(std::is_same<FConstMemoryView, decltype(DeclVal<const FSharedBuffer>().GetView())>::value, "Invalid accessor");

static_assert(std::is_convertible<FSharedBuffer, FMutableMemoryView>::value, "Missing conversion");
static_assert(std::is_convertible<FSharedBuffer, FConstMemoryView>::value, "Missing conversion");
static_assert(!std::is_convertible<const FSharedBuffer, FMutableMemoryView>::value, "Invalid conversion");
static_assert(std::is_convertible<const FSharedBuffer, FConstMemoryView>::value, "Missing conversion");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(std::is_same<FSharedBufferRef, decltype(MakeSharedBufferOwned(DeclVal<const FSharedBufferRef&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferPtr, decltype(MakeSharedBufferOwned(DeclVal<const FSharedBufferPtr&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferPtr, decltype(MakeSharedBufferOwned(DeclVal<FSharedBufferPtr&&>()))>::value, "Invalid conversion");

static_assert(std::is_same<FSharedBufferConstRef, decltype(MakeSharedBufferOwned(DeclVal<const FSharedBufferConstRef&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(MakeSharedBufferOwned(DeclVal<const FSharedBufferConstPtr&>()))>::value, "Invalid conversion");
static_assert(std::is_same<FSharedBufferConstPtr, decltype(MakeSharedBufferOwned(DeclVal<FSharedBufferConstPtr&&>()))>::value, "Invalid conversion");

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
	// Test Default
	{
		FSharedBufferRef Ref = MakeSharedBuffer();
		TestFalse(TEXT("FSharedBuffer().IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer().Size()"), Ref->Size(), uint64(0));
	}

	// Test Size
	{
		constexpr uint64 Size = 64;
		FSharedBufferRef Ref = MakeSharedBuffer(64);
		TestTrue(TEXT("FSharedBuffer(Size).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer(Size).Size()"), Ref->Size(), Size);
	}

	// Test AssumeOwnership
	{
		constexpr uint64 Size = 64;
		void* const Data = FMemory::Malloc(Size);
		FSharedBufferRef Ref = MakeSharedBuffer(FSharedBuffer::AssumeOwnership, Data, Size);
		TestTrue(TEXT("FSharedBuffer(AssumeOwnership).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer(AssumeOwnership).Size()"), Ref->Size(), Size);
		TestEqual(TEXT("FSharedBuffer(AssumeOwnership).GetData()"), Ref->GetData(), Data);
	}

	// Test Clone
	{
		constexpr uint64 Size = 64;
		const uint8 Data[Size]{};
		FSharedBufferRef Ref = MakeSharedBuffer(FSharedBuffer::Clone, Data, Size);
		TestTrue(TEXT("FSharedBuffer(Clone).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer(Clone).Size()"), Ref->Size(), Size);
		TestNotEqual(TEXT("FSharedBuffer(Clone).GetData()"), static_cast<const void*>(Ref->GetData()), static_cast<const void*>(Data));
	}

	// Test Wrap
	{
		constexpr uint64 Size = 64;
		uint8 Data[Size]{};
		FSharedBufferRef Ref = MakeSharedBuffer(FSharedBuffer::Wrap, Data, Size);
		TestFalse(TEXT("FSharedBuffer(Wrap).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("FSharedBuffer(Wrap).Size()"), Ref->Size(), Size);
		TestEqual(TEXT("FSharedBuffer(Wrap).GetData()"), Ref->GetData(), static_cast<void*>(Data));
	}

	// Test MakeSharedBufferOwned
	{
		constexpr uint64 Size = 64;
		uint8 Data[Size]{};
		FSharedBufferRef Ref = MakeSharedBufferOwned(MakeSharedBuffer(FSharedBuffer::Wrap, Data, Size));
		TestTrue(TEXT("MakeSharedBufferOwned(Wrap).IsOwned()"), Ref->IsOwned());
		TestEqual(TEXT("MakeSharedBufferOwned(Wrap).Size()"), Ref->Size(), Size);
		TestNotEqual(TEXT("MakeSharedBufferOwned(Wrap).GetData()"), Ref->GetData(), static_cast<void*>(Data));
		FSharedBufferRef RefCopy = MakeSharedBufferOwned(Ref);
		TestTrue(TEXT("MakeSharedBufferOwned(Owned).IsOwned()"), RefCopy->IsOwned());
		TestEqual(TEXT("MakeSharedBufferOwned(Owned).Size()"), RefCopy->Size(), Size);
		TestEqual(TEXT("MakeSharedBufferOwned(Owned).GetData()"), RefCopy->GetData(), Ref->GetData());
	}
	{
		FSharedBufferPtr Ptr;
		TestFalse(TEXT("MakeSharedBufferOwned(Null)"), MakeSharedBufferOwned(Ptr).IsValid());
		TestFalse(TEXT("MakeSharedBufferOwned(Null)"), MakeSharedBufferOwned(AsConst(Ptr)).IsValid());
		TestFalse(TEXT("MakeSharedBufferOwned(Null)"), MakeSharedBufferOwned(FSharedBufferPtr()).IsValid());
		TestFalse(TEXT("MakeSharedBufferOwned(Null)"), MakeSharedBufferOwned(FSharedBufferConstPtr()).IsValid());
	}

	// Test WeakPtr
	{
		FSharedBufferWeakPtr WeakPtr;
		{
			FSharedBufferRef Ref = MakeSharedBuffer();
			WeakPtr = Ref;
			TestTrue(TEXT("FSharedBufferWeakPtr(Ref).Pin().IsValid()"), WeakPtr.Pin().IsValid());
		}
		TestFalse(TEXT("FSharedBufferWeakPtr(Ref).Pin().IsValid()"), WeakPtr.Pin().IsValid());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
