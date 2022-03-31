// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/MemoryView.h"
#include <type_traits>
#include "TestHarness.h"

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

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Memory::FMemoryView::MemoryView", "[Core][Memory][Smoke]")
{
	auto TestMemoryView = [this](const FMemoryView& View, const void* Data, uint64 Size)
	{
		TEST_EQUAL(TEXT("MemoryView.GetData()"), View.GetData(), Data);
		TEST_EQUAL(TEXT("MemoryView.GetDataEnd()"), View.GetDataEnd(), static_cast<const void*>(static_cast<const uint8*>(Data) + Size));
		TEST_EQUAL(TEXT("MemoryView.GetSize()"), View.GetSize(), Size);
		TEST_EQUAL(TEXT("MemoryView.IsEmpty()"), View.IsEmpty(), Size == 0);
	};

	auto TestMutableMemoryView = [this](const FMutableMemoryView& View, void* Data, uint64 Size)
	{
		TEST_EQUAL(TEXT("MemoryView.GetData()"), View.GetData(), Data);
		TEST_EQUAL(TEXT("MemoryView.GetDataEnd()"), View.GetDataEnd(), static_cast<void*>(static_cast<uint8*>(Data) + Size));
		TEST_EQUAL(TEXT("MemoryView.GetSize()"), View.GetSize(), Size);
		TEST_EQUAL(TEXT("MemoryView.IsEmpty()"), View.IsEmpty(), Size == 0);
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
		TEST_EQUAL(TEXT("MemoryView.Reset()"), View, FMutableMemoryView());
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
	TEST_EQUAL(TEXT("MemoryView.Right(0)"), MakeMemoryView(IntArray).Right(0), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.Right(1)"), MakeMemoryView(IntArray).Right(1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + sizeof(IntArray) - 1, 1));
	TEST_EQUAL(TEXT("MemoryView.Right(Size)"), MakeMemoryView(IntArray).Right(sizeof(IntArray)), MakeMemoryView(IntArray));
	TEST_EQUAL(TEXT("MemoryView.Right(Size+1)"), MakeMemoryView(IntArray).Right(sizeof(IntArray) + 1), MakeMemoryView(IntArray));
	TEST_EQUAL(TEXT("MemoryView.Right(MaxSize)"), MakeMemoryView(IntArray).Right(MAX_uint64), MakeMemoryView(IntArray));

	// Test RightChop
	TEST_EQUAL(TEXT("MemoryView.RightChop(0)"), MakeMemoryView(IntArray).RightChop(0), MakeMemoryView(IntArray));
	TEST_EQUAL(TEXT("MemoryView.RightChop(1)"), MakeMemoryView(IntArray).RightChop(1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TEST_EQUAL(TEXT("MemoryView.RightChop(Size)"), MakeMemoryView(IntArray).RightChop(sizeof(IntArray)), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.RightChop(Size+1)"), MakeMemoryView(IntArray).RightChop(sizeof(IntArray) + 1), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.RightChop(MaxSize)"), MakeMemoryView(IntArray).RightChop(MAX_uint64), FMutableMemoryView());

	// Test Mid
	TEST_EQUAL(TEXT("MemoryView.Mid(0)"), MakeMemoryView(IntArray).Mid(0), MakeMemoryView(IntArray));
	TEST_EQUAL(TEXT("MemoryView.Mid(1)"), MakeMemoryView(IntArray).Mid(1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TEST_EQUAL(TEXT("MemoryView.Mid(Size)"), MakeMemoryView(IntArray).Mid(sizeof(IntArray)), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.Mid(Size+1)"), MakeMemoryView(IntArray).Mid(sizeof(IntArray) + 1), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.Mid(MaxSize)"), MakeMemoryView(IntArray).Mid(MAX_uint64), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.Mid(0,0)"), MakeMemoryView(IntArray).Mid(0, 0), FMutableMemoryView());
	TEST_EQUAL(TEXT("MemoryView.Mid(0,1)"), MakeMemoryView(IntArray).Mid(0, 1), MakeMemoryView(IntArray, 1));
	TEST_EQUAL(TEXT("MemoryView.Mid(1,Size-2)"), MakeMemoryView(IntArray).Mid(1, sizeof(IntArray) - 2), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 2));
	TEST_EQUAL(TEXT("MemoryView.Mid(1,Size-1)"), MakeMemoryView(IntArray).Mid(1, sizeof(IntArray) - 1), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TEST_EQUAL(TEXT("MemoryView.Mid(1,Size)"), MakeMemoryView(IntArray).Mid(1, sizeof(IntArray)), MakeMemoryView(reinterpret_cast<uint8*>(IntArray) + 1, sizeof(IntArray) - 1));
	TEST_EQUAL(TEXT("MemoryView.Mid(0,MaxSize)"), MakeMemoryView(IntArray).Mid(0, MAX_uint64), MakeMemoryView(IntArray));
	TEST_EQUAL(TEXT("MemoryView.Mid(MaxSize,MaxSize)"), MakeMemoryView(IntArray).Mid(MAX_uint64, MAX_uint64), FMutableMemoryView());

	// Test Contains
	TEST_TRUE(TEXT("MemoryView.Contains(Empty)"), FMemoryView().Contains(FMutableMemoryView()));
	TEST_TRUE(TEXT("MemoryView.Contains(Empty)"), FMutableMemoryView().Contains(FMemoryView()));
	TEST_TRUE(TEXT("MemoryView.Contains(Equal)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray)));
	TEST_TRUE(TEXT("MemoryView.Contains(SmallerBy1Left)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 1, 15)));
	TEST_TRUE(TEXT("MemoryView.Contains(SmallerBy1Right)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray, 15)));
	TEST_TRUE(TEXT("MemoryView.Contains(SmallerBy2Both)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 1, 14)));
	TEST_TRUE(TEXT("MemoryView.Contains(EmptyContained)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray, 0)));
	TEST_TRUE(TEXT("MemoryView.Contains(EmptyContained)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 8, 0)));
	TEST_TRUE(TEXT("MemoryView.Contains(EmptyContained)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 16, 0)));
	TEST_FALSE(TEXT("MemoryView.Contains(EmptyOutside)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 0)));
	TEST_FALSE(TEXT("MemoryView.Contains(EmptyOutside)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.AfterByteArray + 1, 0)));
	TEST_FALSE(TEXT("MemoryView.Contains(OutsideBy1Left)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 1)));
	TEST_FALSE(TEXT("MemoryView.Contains(OutsideBy1Right)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 16, 1)));
	TEST_FALSE(TEXT("MemoryView.Contains(LargerBy1Left)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 17)));
	TEST_FALSE(TEXT("MemoryView.Contains(LargerBy1Right)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray, 17)));
	TEST_FALSE(TEXT("MemoryView.Contains(LargerBy2Both)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 18)));
	TEST_FALSE(TEXT("MemoryView.Contains(SmallerOverlapLeft)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 8)));
	TEST_FALSE(TEXT("MemoryView.Contains(SmallerOverlapRight)"), MakeMemoryView(ByteArray).Contains(MakeMemoryView(ByteArray + 9, 8)));

	// Test Intersects
	TEST_TRUE(TEXT("MemoryView.Intersects(Equal)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray)));
	TEST_TRUE(TEXT("MemoryView.Intersects(SmallerBy1Left)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 1, 15)));
	TEST_TRUE(TEXT("MemoryView.Intersects(SmallerBy1Right)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray, 15)));
	TEST_TRUE(TEXT("MemoryView.Intersects(SmallerBy2Both)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 1, 14)));
	TEST_TRUE(TEXT("MemoryView.Intersects(SmallerOverlapLeft)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 8)));
	TEST_TRUE(TEXT("MemoryView.Intersects(SmallerOverlapRight)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 9, 8)));
	TEST_TRUE(TEXT("MemoryView.Intersects(LargerBy1Left)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 17)));
	TEST_TRUE(TEXT("MemoryView.Intersects(LargerBy1Right)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray, 17)));
	TEST_TRUE(TEXT("MemoryView.Intersects(LargerBy2Both)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 18)));
	TEST_TRUE(TEXT("MemoryView.Intersects(EmptyMiddle)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 8, 0)));
	TEST_FALSE(TEXT("MemoryView.Intersects(Empty)"), FMemoryView().Intersects(FMutableMemoryView()));
	TEST_FALSE(TEXT("MemoryView.Intersects(Empty)"), FMutableMemoryView().Intersects(FMemoryView()));
	TEST_FALSE(TEXT("MemoryView.Intersects(EmptyLeft)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray, 0)));
	TEST_FALSE(TEXT("MemoryView.Intersects(EmptyRight)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 16, 0)));
	TEST_FALSE(TEXT("MemoryView.Intersects(EmptyOutside)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 0)));
	TEST_FALSE(TEXT("MemoryView.Intersects(EmptyOutside)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.AfterByteArray + 1, 0)));
	TEST_FALSE(TEXT("MemoryView.Intersects(OutsideBy1Left)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArrayContainer.BeforeByteArray + 3, 1)));
	TEST_FALSE(TEXT("MemoryView.Intersects(OutsideBy1Right)"), MakeMemoryView(ByteArray).Intersects(MakeMemoryView(ByteArray + 16, 1)));

	// Test CompareBytes
	const uint8 CompareBytes[8] = { 5, 4, 6, 2, 4, 7, 1, 3 };
	TEST_EQUAL(TEXT("MemoryView.CompareBytes(Empty)"), FMemoryView().CompareBytes(FMutableMemoryView()), 0);
	TEST_EQUAL(TEXT("MemoryView.CompareBytes(Empty)"), FMutableMemoryView().CompareBytes(FMemoryView()), 0);
	TEST_EQUAL(TEXT("MemoryView.CompareBytes(EqualView)"), MakeMemoryView(ByteArray).CompareBytes(MakeMemoryView(ByteArray)), 0);
	TEST_EQUAL(TEXT("MemoryView.CompareBytes(EqualBytes)"), MakeMemoryView(ByteArray, 8).CompareBytes(MakeMemoryView(ByteArray + 8, 8)), 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(EmptyLeft)"), FMemoryView().CompareBytes(MakeMemoryView(ByteArray)) < 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(EmptyRight)"), MakeMemoryView(ByteArray).CompareBytes(FMemoryView()) > 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(ShorterLeft)"), MakeMemoryView(ByteArray, 8).CompareBytes(MakeMemoryView(ByteArray)) < 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(ShorterRight)"), MakeMemoryView(ByteArray).CompareBytes(MakeMemoryView(ByteArray, 8)) > 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(ShorterLeft)"), MakeMemoryView(IntArray, 8).CompareBytes(MakeMemoryView(ByteArray)) < 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(ShorterRight)"), MakeMemoryView(ByteArray).CompareBytes(MakeMemoryView(IntArray, 8)) > 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(DifferentSize)"), MakeMemoryView(ByteArray, 4).CompareBytes(MakeMemoryView(ByteArray, 8)) < 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(DifferentSize)"), MakeMemoryView(ByteArray, 8).CompareBytes(MakeMemoryView(ByteArray, 4)) > 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(SameSizeLeftLess)"), MakeMemoryView(CompareBytes, 2).CompareBytes(MakeMemoryView(CompareBytes + 2, 2)) < 0);
	TEST_TRUE(TEXT("MemoryView.CompareBytes(SameSizeLeftGreater)"), MakeMemoryView(CompareBytes, 3).CompareBytes(MakeMemoryView(CompareBytes + 3, 3)) > 0);

	// Test EqualBytes
	TEST_TRUE(TEXT("MemoryView.EqualBytes(Empty)"), FMemoryView().EqualBytes(FMutableMemoryView()));
	TEST_TRUE(TEXT("MemoryView.EqualBytes(Empty)"), FMutableMemoryView().EqualBytes(FMemoryView()));
	TEST_TRUE(TEXT("MemoryView.EqualBytes(EqualView)"), MakeMemoryView(ByteArray).EqualBytes(MakeMemoryView(ByteArray)));
	TEST_TRUE(TEXT("MemoryView.EqualBytes(EqualBytes)"), MakeMemoryView(ByteArray, 8).EqualBytes(MakeMemoryView(ByteArray + 8, 8)));
	TEST_FALSE(TEXT("MemoryView.EqualBytes(DifferentSize)"), MakeMemoryView(ByteArray, 8).EqualBytes(MakeMemoryView(ByteArray, 4)));
	TEST_FALSE(TEXT("MemoryView.EqualBytes(DifferentSize)"), MakeMemoryView(ByteArray, 4).EqualBytes(MakeMemoryView(ByteArray, 8)));
	TEST_FALSE(TEXT("MemoryView.EqualBytes(DifferentBytes)"), MakeMemoryView(CompareBytes, 4).EqualBytes(MakeMemoryView(CompareBytes + 4, 4)));

	// Test Equals
	TEST_TRUE(TEXT("MemoryView.Equals(Empty)"), FMemoryView().Equals(FMemoryView()));
	TEST_TRUE(TEXT("MemoryView.Equals(Empty)"), FMemoryView().Equals(FMutableMemoryView()));
	TEST_TRUE(TEXT("MemoryView.Equals(Empty)"), FMutableMemoryView().Equals(FMemoryView()));
	TEST_TRUE(TEXT("MemoryView.Equals(Empty)"), FMutableMemoryView().Equals(FMutableMemoryView()));
	TEST_TRUE(TEXT("MemoryView.Equals(Equal)"), MakeMemoryView(IntArray).Equals(MakeMemoryView(AsConst(IntArray))));
	TEST_FALSE(TEXT("MemoryView.Equals(DataDiff)"), MakeMemoryView(IntArray).Equals(MakeMemoryView(IntArray + 1, sizeof(IntArray) - sizeof(*IntArray))));
	TEST_FALSE(TEXT("MemoryView.Equals(SizeDiff)"), MakeMemoryView(IntArray).Equals(MakeMemoryView(IntArray, sizeof(*IntArray))));
	TEST_FALSE(TEXT("MemoryView.Equals(BothDiff)"), MakeMemoryView(IntArray).Equals(FMutableMemoryView()));

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
	TEST_EQUAL(TEXT("MemoryView += 0"), MakeMemoryView(ByteArray) += 0, MakeMemoryView(ByteArray));
	TEST_EQUAL(TEXT("MemoryView += Half"), MakeMemoryView(ByteArray) += 8, MakeMemoryView(ByteArray + 8, 8));
	TEST_EQUAL(TEXT("MemoryView += Size"), MakeMemoryView(ByteArray) += 16, MakeMemoryView(ByteArray + 16, 0));
	TEST_EQUAL(TEXT("MemoryView += OutOfBounds"), MakeMemoryView(ByteArray) += 32, MakeMemoryView(ByteArray + 16, 0));

	// Test operator+
	TEST_EQUAL(TEXT("MemoryView + 0"), MakeMemoryView(ByteArray) + 0, MakeMemoryView(ByteArray));
	TEST_EQUAL(TEXT("0 + MemoryView"), 0 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray));
	TEST_EQUAL(TEXT("MemoryView + Half"), MakeMemoryView(ByteArray) + 8, MakeMemoryView(ByteArray + 8, 8));
	TEST_EQUAL(TEXT("Half + MemoryView"), 8 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray + 8, 8));
	TEST_EQUAL(TEXT("MemoryView + Size"), MakeMemoryView(ByteArray) + 16, MakeMemoryView(ByteArray + 16, 0));
	TEST_EQUAL(TEXT("Size + MemoryView"), 16 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray + 16, 0));
	TEST_EQUAL(TEXT("MemoryView + OutOfBounds"), MakeMemoryView(ByteArray) + 32, MakeMemoryView(ByteArray + 16, 0));
	TEST_EQUAL(TEXT("OutOfBounds + MemoryView"), 32 + MakeMemoryView(ByteArray), MakeMemoryView(ByteArray + 16, 0));
}
