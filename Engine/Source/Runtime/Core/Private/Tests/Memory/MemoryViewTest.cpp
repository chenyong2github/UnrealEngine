// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/MemoryView.h"

#include "Misc/AutomationTest.h"

#include <type_traits>

static_assert(std::is_trivially_copyable<FMemoryView>::value, "FMemoryView must be trivially copyable");
static_assert(std::is_trivially_copy_constructible<FMemoryView>::value, "FMemoryView must be trivially copy constructible");
static_assert(std::is_trivially_move_constructible<FMemoryView>::value, "FMemoryView must be trivially move constructible");
static_assert(std::is_trivially_copy_assignable<FMemoryView>::value, "FMemoryView must be trivially copy assignable");
static_assert(std::is_trivially_move_assignable<FMemoryView>::value, "FMemoryView must be trivially move assignable");
static_assert(std::is_trivially_destructible<FMemoryView>::value, "FMemoryView must be trivially destructible");

static_assert(std::is_trivially_copyable<FMutableMemoryView>::value, "FMutableMemoryView must be trivially copyable");
static_assert(std::is_trivially_copy_constructible<FMutableMemoryView>::value, "FMutableMemoryView must be trivially copy constructible");
static_assert(std::is_trivially_move_constructible<FMutableMemoryView>::value, "FMutableMemoryView must be trivially move constructible");
static_assert(std::is_trivially_copy_assignable<FMutableMemoryView>::value, "FMutableMemoryView must be trivially copy assignable");
static_assert(std::is_trivially_move_assignable<FMutableMemoryView>::value, "FMutableMemoryView must be trivially move assignable");
static_assert(std::is_trivially_destructible<FMutableMemoryView>::value, "FMutableMemoryView must be trivially destructible");

static_assert(std::is_constructible<FMemoryView, const FMutableMemoryView&>::value, "Missing constructor");
static_assert(!std::is_constructible<FMutableMemoryView, const FMemoryView&>::value, "Invalid constructor");
static_assert(!std::is_constructible<FMutableMemoryView, const void*, uint64>::value, "Invalid constructor");

static_assert(std::is_assignable<FMemoryView, const FMutableMemoryView&>::value, "Missing assignment");
static_assert(!std::is_assignable<FMutableMemoryView, const FMemoryView&>::value, "Invalid assignment");

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMemoryViewTest, "System.Core.Memory.MemoryView", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FMemoryViewTest::RunTest(const FString& Parameters)
{
	auto TestMemoryView = [this](const FMemoryView& View, const void* Data, uint64 Size)
	{
		TestEqual(TEXT("MemoryView.GetData()"), View.GetData(), Data);
		TestEqual(TEXT("MemoryView.GetDataEnd()"), View.GetDataEnd(), static_cast<const void*>(static_cast<const uint8*>(Data) + Size));
		TestEqual(TEXT("MemoryView.GetSize()"), View.GetSize(), Size);
		TestEqual(TEXT("MemoryView.IsEmpty()"), View.IsEmpty(), Size == 0);
	};

	auto TestMutableMemoryView = [this](const FMutableMemoryView& View, void* Data, uint64 Size)
	{
		TestEqual(TEXT("MemoryView.GetData()"), View.GetData(), Data);
		TestEqual(TEXT("MemoryView.GetDataEnd()"), View.GetDataEnd(), static_cast<void*>(static_cast<uint8*>(Data) + Size));
		TestEqual(TEXT("MemoryView.GetSize()"), View.GetSize(), Size);
		TestEqual(TEXT("MemoryView.IsEmpty()"), View.IsEmpty(), Size == 0);
	};

	struct
	{
		uint8 BeforeByteArray[4];
		uint8 ByteArray[16]{};
		uint8 AfterByteArray[4];
	} ByteArrayContainer;

	uint8 (&ByteArray)[16] = ByteArrayContainer.ByteArray;
	uint32 IntArray[12]{};

	// Test Empty Views
	TestMemoryView(FMemoryView(), nullptr, 0);
	TestMemoryView(FMutableMemoryView(), nullptr, 0);
	TestMutableMemoryView(FMutableMemoryView(), nullptr, 0);

	// Test Construction from Type[], TArrayView, (Type*, uint64), (Type*, Type*)
	TestMemoryView(MakeMemoryView(AsConst(IntArray)), IntArray, sizeof(IntArray));
	TestMemoryView(MakeMemoryView(MakeArrayView(AsConst(IntArray))), IntArray, sizeof(IntArray));
	TestMemoryView(MakeMemoryView(AsConst(IntArray), sizeof(IntArray)), IntArray, sizeof(IntArray));
	TestMemoryView(MakeMemoryView(AsConst(IntArray), AsConst(IntArray) + 6), IntArray, sizeof(*IntArray) * 6);
	TestMutableMemoryView(MakeMemoryView(IntArray), IntArray, sizeof(IntArray));
	TestMutableMemoryView(MakeMemoryView(MakeArrayView(IntArray)), IntArray, sizeof(IntArray));
	TestMutableMemoryView(MakeMemoryView(IntArray, sizeof(IntArray)), IntArray, sizeof(IntArray));
	TestMutableMemoryView(MakeMemoryView(IntArray, IntArray + 6), IntArray, sizeof(*IntArray) * 6);

	// Test Construction from std::initializer_list
	//MakeMemoryView({1, 2, 3}); // fail because the type must be deduced
	std::initializer_list<uint8> InitializerList{1, 2, 3};
	TestMemoryView(MakeMemoryView(InitializerList), GetData(InitializerList), GetNum(InitializerList) * sizeof(uint8));

	// Test Reset
	{
		FMutableMemoryView View = MakeMemoryView(IntArray);
		View.Reset();
		TestEqual(TEXT("MemoryView.Reset()"), View, FMutableMemoryView());
	}

	// Test Left
	static_assert(MakeMemoryView(IntArray).Left(0).IsEmpty(), "Error in Left");
	static_assert(MakeMemoryView(IntArray).Left(1) == MakeMemoryView(IntArray, 1), "Error in Left");
	static_assert(MakeMemoryView(IntArray).Left(sizeof(IntArray)) == MakeMemoryView(IntArray), "Error in Left");
	static_assert(MakeMemoryView(IntArray).Left(sizeof(IntArray) + 1) == MakeMemoryView(IntArray), "Error in Left");
	static_assert(MakeMemoryView(IntArray).Left(MAX_uint64) == MakeMemoryView(IntArray), "Error in Left");

	// Test LeftChop
	static_assert(MakeMemoryView(IntArray).LeftChop(0) == MakeMemoryView(IntArray), "Error in LeftChop");
	static_assert(MakeMemoryView(IntArray).LeftChop(1) == MakeMemoryView(IntArray, sizeof(IntArray) - 1), "Error in LeftChop");
	static_assert(MakeMemoryView(IntArray).LeftChop(sizeof(IntArray)).IsEmpty(), "Error in LeftChop");
	static_assert(MakeMemoryView(IntArray).LeftChop(sizeof(IntArray) + 1).IsEmpty(), "Error in LeftChop");
	static_assert(MakeMemoryView(IntArray).LeftChop(MAX_uint64).IsEmpty(), "Error in LeftChop");

	// Test Right
	TestEqual(TEXT("MemoryView.Right(0)"), MakeMemoryView(IntArray).Right(0), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.Right(1)"), MakeMemoryView(IntArray).Right(1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + sizeof(IntArray) - 1, 1));
	TestEqual(TEXT("MemoryView.Right(Size)"), MakeMemoryView(IntArray).Right(sizeof(IntArray)), MakeMemoryView(IntArray));
	TestEqual(TEXT("MemoryView.Right(Size+1)"), MakeMemoryView(IntArray).Right(sizeof(IntArray) + 1), MakeMemoryView(IntArray));
	TestEqual(TEXT("MemoryView.Right(MaxSize)"), MakeMemoryView(IntArray).Right(MAX_uint64), MakeMemoryView(IntArray));

	// Test RightChop
	TestEqual(TEXT("MemoryView.RightChop(0)"), MakeMemoryView(IntArray).RightChop(0), MakeMemoryView(IntArray));
	TestEqual(TEXT("MemoryView.RightChop(1)"), MakeMemoryView(IntArray).RightChop(1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TestEqual(TEXT("MemoryView.RightChop(Size)"), MakeMemoryView(IntArray).RightChop(sizeof(IntArray)), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.RightChop(Size+1)"), MakeMemoryView(IntArray).RightChop(sizeof(IntArray) + 1), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.RightChop(MaxSize)"), MakeMemoryView(IntArray).RightChop(MAX_uint64), FMutableMemoryView());

	// Test Mid
	TestEqual(TEXT("MemoryView.Mid(0)"), MakeMemoryView(IntArray).Mid(0), MakeMemoryView(IntArray));
	TestEqual(TEXT("MemoryView.Mid(1)"), MakeMemoryView(IntArray).Mid(1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TestEqual(TEXT("MemoryView.Mid(Size)"), MakeMemoryView(IntArray).Mid(sizeof(IntArray)), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.Mid(Size+1)"), MakeMemoryView(IntArray).Mid(sizeof(IntArray) + 1), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.Mid(MaxSize)"), MakeMemoryView(IntArray).Mid(MAX_uint64), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.Mid(0,0)"), MakeMemoryView(IntArray).Mid(0, 0), FMutableMemoryView());
	TestEqual(TEXT("MemoryView.Mid(0,1)"), MakeMemoryView(IntArray).Mid(0, 1), MakeMemoryView(IntArray, 1));
	TestEqual(TEXT("MemoryView.Mid(1,Size-2)"), MakeMemoryView(IntArray).Mid(1, sizeof(IntArray) - 2), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 2));
	TestEqual(TEXT("MemoryView.Mid(1,Size-1)"), MakeMemoryView(IntArray).Mid(1, sizeof(IntArray) - 1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TestEqual(TEXT("MemoryView.Mid(1,Size)"), MakeMemoryView(IntArray).Mid(1, sizeof(IntArray)), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TestEqual(TEXT("MemoryView.Mid(0,MaxSize)"), MakeMemoryView(IntArray).Mid(0, MAX_uint64), MakeMemoryView(IntArray));
	TestEqual(TEXT("MemoryView.Mid(MaxSize,MaxSize)"), MakeMemoryView(IntArray).Mid(MAX_uint64, MAX_uint64), FMutableMemoryView());

	// Test Contains
	TestTrue(TEXT("MemoryView.Contains(Empty)"), FMemoryView().Contains(FMutableMemoryView()));
	TestTrue(TEXT("MemoryView.Contains(Empty)"), FMutableMemoryView().Contains(FMemoryView()));
	TestTrue(TEXT("MemoryView.Contains(Equal)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray)));
	TestTrue(TEXT("MemoryView.Contains(SmallerBy1Left)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 1, 15)));
	TestTrue(TEXT("MemoryView.Contains(SmallerBy1Right)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray, 15)));
	TestTrue(TEXT("MemoryView.Contains(SmallerBy2Both)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 1, 14)));
	TestTrue(TEXT("MemoryView.Contains(EmptyContained)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray, 0)));
	TestTrue(TEXT("MemoryView.Contains(EmptyContained)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 8, 0)));
	TestTrue(TEXT("MemoryView.Contains(EmptyContained)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 16, 0)));
	TestFalse(TEXT("MemoryView.Contains(EmptyOutside)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 0)));
	TestFalse(TEXT("MemoryView.Contains(EmptyOutside)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.AfterByteArray + 1, 0)));
	TestFalse(TEXT("MemoryView.Contains(OutsideBy1Left)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 1)));
	TestFalse(TEXT("MemoryView.Contains(OutsideBy1Right)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 16, 1)));
	TestFalse(TEXT("MemoryView.Contains(LargerBy1Left)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 17)));
	TestFalse(TEXT("MemoryView.Contains(LargerBy1Right)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray, 17)));
	TestFalse(TEXT("MemoryView.Contains(LargerBy2Both)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 18)));
	TestFalse(TEXT("MemoryView.Contains(SmallerOverlapLeft)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 8)));
	TestFalse(TEXT("MemoryView.Contains(SmallerOverlapRight)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 9, 8)));

	// Test Intersects
	TestTrue(TEXT("MemoryView.Intersects(Equal)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray)));
	TestTrue(TEXT("MemoryView.Intersects(SmallerBy1Left)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 1, 15)));
	TestTrue(TEXT("MemoryView.Intersects(SmallerBy1Right)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray, 15)));
	TestTrue(TEXT("MemoryView.Intersects(SmallerBy2Both)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 1, 14)));
	TestTrue(TEXT("MemoryView.Intersects(SmallerOverlapLeft)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 8)));
	TestTrue(TEXT("MemoryView.Intersects(SmallerOverlapRight)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 9, 8)));
	TestTrue(TEXT("MemoryView.Intersects(LargerBy1Left)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 17)));
	TestTrue(TEXT("MemoryView.Intersects(LargerBy1Right)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray, 17)));
	TestTrue(TEXT("MemoryView.Intersects(LargerBy2Both)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 18)));
	TestTrue(TEXT("MemoryView.Intersects(EmptyMiddle)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 8, 0)));
	TestFalse(TEXT("MemoryView.Intersects(Empty)"), FMemoryView().Intersects(FMutableMemoryView()));
	TestFalse(TEXT("MemoryView.Intersects(Empty)"), FMutableMemoryView().Intersects(FMemoryView()));
	TestFalse(TEXT("MemoryView.Intersects(EmptyLeft)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray, 0)));
	TestFalse(TEXT("MemoryView.Intersects(EmptyRight)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 16, 0)));
	TestFalse(TEXT("MemoryView.Intersects(EmptyOutside)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 0)));
	TestFalse(TEXT("MemoryView.Intersects(EmptyOutside)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.AfterByteArray + 1, 0)));
	TestFalse(TEXT("MemoryView.Intersects(OutsideBy1Left)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 1)));
	TestFalse(TEXT("MemoryView.Intersects(OutsideBy1Right)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 16, 1)));

	// Test CompareBytes
	const uint8 CompareBytes[8] = { 5, 4, 6, 2, 4, 7, 1, 3 };
	TestEqual(TEXT("MemoryView.CompareBytes(Empty)"), FMemoryView().CompareBytes(FMutableMemoryView()), 0);
	TestEqual(TEXT("MemoryView.CompareBytes(Empty)"), FMutableMemoryView().CompareBytes(FMemoryView()), 0);
	TestEqual(TEXT("MemoryView.CompareBytes(EqualView)"), MakeMemoryView(ByteArray).CompareBytes(MakeMemoryView(ByteArray)), 0);
	TestEqual(TEXT("MemoryView.CompareBytes(EqualBytes)"), MakeMemoryView(ByteArray, 8).CompareBytes(MakeMemoryView(ByteArray + 8, 8)), 0);
	TestTrue(TEXT("MemoryView.CompareBytes(EmptyLeft)"), FMemoryView().CompareBytes(MakeMemoryView(ByteArray)) < 0);
	TestTrue(TEXT("MemoryView.CompareBytes(EmptyRight)"), MakeMemoryView(ByteArray).CompareBytes(FMemoryView()) > 0);
	TestTrue(TEXT("MemoryView.CompareBytes(ShorterLeft)"), MakeMemoryView(ByteArray, 8).CompareBytes(MakeMemoryView(ByteArray)) < 0);
	TestTrue(TEXT("MemoryView.CompareBytes(ShorterRight)"), MakeMemoryView(ByteArray).CompareBytes(MakeMemoryView(ByteArray, 8)) > 0);
	TestTrue(TEXT("MemoryView.CompareBytes(ShorterLeft)"), MakeMemoryView(IntArray, 8).CompareBytes(MakeMemoryView(ByteArray)) < 0);
	TestTrue(TEXT("MemoryView.CompareBytes(ShorterRight)"), MakeMemoryView(ByteArray).CompareBytes(MakeMemoryView(IntArray, 8)) > 0);
	TestTrue(TEXT("MemoryView.CompareBytes(DifferentSize)"), MakeMemoryView(ByteArray, 4).CompareBytes(MakeMemoryView(ByteArray, 8)) < 0);
	TestTrue(TEXT("MemoryView.CompareBytes(DifferentSize)"), MakeMemoryView(ByteArray, 8).CompareBytes(MakeMemoryView(ByteArray, 4)) > 0);
	TestTrue(TEXT("MemoryView.CompareBytes(SameSizeLeftLess)"), MakeMemoryView(CompareBytes, 2).CompareBytes(MakeMemoryView(CompareBytes + 2, 2)) < 0);
	TestTrue(TEXT("MemoryView.CompareBytes(SameSizeLeftGreater)"), MakeMemoryView(CompareBytes, 3).CompareBytes(MakeMemoryView(CompareBytes + 3, 3)) > 0);

	// Test EqualBytes
	TestTrue(TEXT("MemoryView.EqualBytes(Empty)"), FMemoryView().EqualBytes(FMutableMemoryView()));
	TestTrue(TEXT("MemoryView.EqualBytes(Empty)"), FMutableMemoryView().EqualBytes(FMemoryView()));
	TestTrue(TEXT("MemoryView.EqualBytes(EqualView)"), MakeMemoryView(ByteArray).EqualBytes(MakeMemoryView(ByteArray)));
	TestTrue(TEXT("MemoryView.EqualBytes(EqualBytes)"), MakeMemoryView(ByteArray, 8).EqualBytes(MakeMemoryView(ByteArray + 8, 8)));
	TestFalse(TEXT("MemoryView.EqualBytes(DifferentSize)"), MakeMemoryView(ByteArray, 8).EqualBytes(MakeMemoryView(ByteArray, 4)));
	TestFalse(TEXT("MemoryView.EqualBytes(DifferentSize)"), MakeMemoryView(ByteArray, 4).EqualBytes(MakeMemoryView(ByteArray, 8)));
	TestFalse(TEXT("MemoryView.EqualBytes(DifferentBytes)"), MakeMemoryView(CompareBytes, 4).EqualBytes(MakeMemoryView(CompareBytes + 4, 4)));

	// Test Equals
	TestTrue(TEXT("MemoryView.Equals(Empty)"), FMemoryView().Equals(FMemoryView()));
	TestTrue(TEXT("MemoryView.Equals(Empty)"), FMemoryView().Equals(FMutableMemoryView()));
	TestTrue(TEXT("MemoryView.Equals(Empty)"), FMutableMemoryView().Equals(FMemoryView()));
	TestTrue(TEXT("MemoryView.Equals(Empty)"), FMutableMemoryView().Equals(FMutableMemoryView()));
	TestTrue(TEXT("MemoryView.Equals(Equal)"), MakeMemoryView(IntArray).Equals(MakeMemoryView(AsConst(IntArray))));
	TestFalse(TEXT("MemoryView.Equals(DataDiff)"), MakeMemoryView(IntArray).Equals(MakeMemoryView(IntArray + 1, sizeof(IntArray) - sizeof(*IntArray))));
	TestFalse(TEXT("MemoryView.Equals(SizeDiff)"), MakeMemoryView(IntArray).Equals(MakeMemoryView(IntArray, sizeof(*IntArray))));
	TestFalse(TEXT("MemoryView.Equals(BothDiff)"), MakeMemoryView(IntArray).Equals(FMutableMemoryView()));

	// Test operator==
	static_assert(MakeMemoryView(ByteArrayContainer.ByteArray) == MakeMemoryView(ByteArrayContainer.ByteArray), "Error in MemoryView == MemoryView"); //-V501
	static_assert(MakeMemoryView(ByteArrayContainer.ByteArray) == MakeMemoryView(AsConst(ByteArrayContainer.ByteArray)), "Error in MemoryView == MemoryView");
	static_assert(MakeMemoryView(AsConst(ByteArrayContainer.ByteArray)) == MakeMemoryView(ByteArrayContainer.ByteArray), "Error in MemoryView == MemoryView");
	static_assert(MakeMemoryView(AsConst(ByteArrayContainer.ByteArray)) == MakeMemoryView(AsConst(ByteArrayContainer.ByteArray)), "Error in MemoryView == MemoryView"); //-V501

	// Test operator!=
	static_assert(MakeMemoryView(ByteArrayContainer.ByteArray) != MakeMemoryView(IntArray), "Error in MemoryView != MemoryView");
	static_assert(MakeMemoryView(ByteArrayContainer.ByteArray) != MakeMemoryView(AsConst(IntArray)), "Error in MemoryView != MemoryView");
	static_assert(MakeMemoryView(AsConst(ByteArrayContainer.ByteArray)) != MakeMemoryView(IntArray), "Error in MemoryView != MemoryView");
	static_assert(MakeMemoryView(AsConst(ByteArrayContainer.ByteArray)) != MakeMemoryView(AsConst(IntArray)), "Error in MemoryView != MemoryView");

	// Test operator+=
	TestEqual(TEXT("MemoryView += 0"), MakeMemoryView(ByteArray) += 0, MakeMemoryView(ByteArray));
	TestEqual(TEXT("MemoryView += Half"), MakeMemoryView(ByteArray) += 8, MakeMemoryView(ByteArray + 8, 8));
	TestEqual(TEXT("MemoryView += Size"), MakeMemoryView(ByteArray) += 16, MakeMemoryView(ByteArray + 16, 0));
	TestEqual(TEXT("MemoryView += OutOfBounds"), MakeMemoryView(ByteArray) += 32, MakeMemoryView(ByteArray + 16, 0));

	// Test operator+
	TestEqual(TEXT("MemoryView + 0"), MakeMemoryView(ByteArray) + 0, MakeMemoryView(ByteArray));
	TestEqual(TEXT("0 + MemoryView"), 0 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray));
	TestEqual(TEXT("MemoryView + Half"), MakeMemoryView(ByteArray) + 8, MakeMemoryView(ByteArray + 8, 8));
	TestEqual(TEXT("Half + MemoryView"), 8 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray + 8, 8));
	TestEqual(TEXT("MemoryView + Size"), MakeMemoryView(ByteArray) + 16, MakeMemoryView(ByteArray + 16, 0));
	TestEqual(TEXT("Size + MemoryView"), 16 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray + 16, 0));
	TestEqual(TEXT("MemoryView + OutOfBounds"), MakeMemoryView(ByteArray) + 32, MakeMemoryView(ByteArray + 16, 0));
	TestEqual(TEXT("OutOfBounds + MemoryView"), 32 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray + 16, 0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
