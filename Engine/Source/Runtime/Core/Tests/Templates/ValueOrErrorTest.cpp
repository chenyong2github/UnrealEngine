// Copyright Epic Games, Inc. All Rights Reserved.

#include "Templates/ValueOrError.h"
#include "TestHarness.h"


#include <type_traits>

static_assert(!std::is_constructible<TValueOrError<int, int>>::value, "Expected no default constructor.");

static_assert(std::is_copy_constructible<TValueOrError<int, int>>::value, "Missing copy constructor.");
static_assert(std::is_move_constructible<TValueOrError<int, int>>::value, "Missing move constructor.");

static_assert(std::is_copy_assignable<TValueOrError<int, int>>::value, "Missing copy assignment.");
static_assert(std::is_move_assignable<TValueOrError<int, int>>::value, "Missing move assignment.");

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Templates::TValueOrError::Smoke Test", "[Core][Templates][Smoke]")
{
	static int ValueCount = 0;
	static int ErrorCount = 0;

	struct FTestValue
	{
		FTestValue() { Value = ++ValueCount; }
		FTestValue(const FTestValue&) { Value = ++ValueCount; }
		FTestValue(FTestValue&& Other) { Value = Other.Value; ++ValueCount; }
		FTestValue(int InValue1, int InValue2, int InValue3) { Value = InValue1 + InValue2 + InValue3; ++ValueCount; }
		~FTestValue() { --ValueCount; }
		FTestValue& operator=(const FTestValue& Other) = delete;
		FTestValue& operator=(FTestValue&& Other) = delete;
		int Value;
	};

	struct FTestError
	{
		FTestError() { Error = ++ErrorCount; }
		FTestError(const FTestError&) { Error = ++ErrorCount; }
		FTestError(FTestError&& Other) { Error = Other.Error; ++ErrorCount; }
		FTestError(int InError1, int InError2) { Error = InError1 + InError2; ++ErrorCount; }
		~FTestError() { --ErrorCount; }
		FTestError& operator=(const FTestError& Other) = delete;
		FTestError& operator=(FTestError&& Other) = delete;
		int Error;
	};

	struct FTestMoveOnly
	{
		FTestMoveOnly() = default;
		FTestMoveOnly(const FTestMoveOnly&) = delete;
		FTestMoveOnly(FTestMoveOnly&&) = default;
		int Value = 0;
	};

	using FTestType = TValueOrError<FTestValue, FTestError>;

	// Test MakeValue Move
	{
		INFO("Test MakeValue Move");
		FTestType ValueOrError = MakeValue(FTestValue());
		CHECK_EQUAL(ValueCount, 1);
		CHECK_EQUAL(ValueOrError.TryGetValue(), &ValueOrError.GetValue());
		CHECK_EQUAL(ValueOrError.GetValue().Value, 1);
		CHECK_FALSE(ValueOrError.HasError());
		CHECK(ValueOrError.HasValue());
		CHECK(ValueOrError.TryGetError() == nullptr);
	}

	CHECK_EQUAL(ValueCount, 0);

	// Test MakeValue Proxy
	{
		INFO("Test MakeValue Proxy");
		FTestType ValueOrError = MakeValue(2, 6, 8);
		CHECK_EQUAL(ValueCount, 1);
		CHECK_EQUAL(ValueOrError.TryGetValue(), &ValueOrError.GetValue());
		CHECK_EQUAL(ValueOrError.GetValue().Value, 16);
		CHECK_FALSE(ValueOrError.HasError());
		CHECK(ValueOrError.HasValue());
		CHECK(ValueOrError.TryGetError() == nullptr);
	}
	
	CHECK_EQUAL(ValueCount, 0);

	// Test StealValue
	{
		INFO("Test StealValue");
		FTestType ValueOrError = MakeValue(FTestValue());
		FTestValue Value = ValueOrError.StealValue();
		CHECK_EQUAL(ValueCount, 1);
		CHECK_EQUAL(Value.Value, 1);
		CHECK_FALSE(ValueOrError.HasError());
		CHECK_FALSE(ValueOrError.HasValue());
	}
	
	CHECK_EQUAL(ValueCount, 0);

	// Test MakeError Move
	{
		INFO("Test MakeError Move");
		FTestType ValueOrError = MakeError(FTestError());
		CHECK_EQUAL(ErrorCount, 1);
		CHECK_EQUAL(ValueOrError.TryGetError(), &ValueOrError.GetError());
		CHECK_EQUAL(ValueOrError.GetError().Error, 1);
		CHECK_FALSE(ValueOrError.HasValue());
		CHECK(ValueOrError.HasError());
		CHECK(ValueOrError.TryGetValue() == nullptr);
	}
	
	CHECK_EQUAL(ErrorCount, 0);

	// Test MakeError Proxy
	{
		INFO("Test MakeError Proxy");
		FTestType ValueOrError = MakeError(4, 12);
		CHECK_EQUAL(ErrorCount, 1);
		CHECK_EQUAL(ValueOrError.TryGetError(), &ValueOrError.GetError());
		CHECK_EQUAL(ValueOrError.GetError().Error, 16);
		CHECK_FALSE(ValueOrError.HasValue());
		CHECK(ValueOrError.HasError());
		CHECK(ValueOrError.TryGetValue() == nullptr);
	}
	
	CHECK_EQUAL(ErrorCount, 0);

	// Test StealError
	{
		INFO("Test StealError");
		FTestType ValueOrError = MakeError();
		FTestError Error = ValueOrError.StealError();
		CHECK_EQUAL(ErrorCount, 1);
		CHECK_EQUAL(Error.Error, 1);
		CHECK_FALSE(ValueOrError.HasValue());
		CHECK_FALSE(ValueOrError.HasError());
	}

	CHECK_EQUAL(ErrorCount, 0);

	// Test Assignment
	{
		INFO("Test Assignment");
		FTestType ValueOrError = MakeValue();
		ValueOrError = MakeValue();
		CHECK_EQUAL(ValueCount, 1);
		CHECK_EQUAL(ValueOrError.GetValue().Value, 2);
		CHECK_EQUAL(ErrorCount, 0);
		ValueOrError = MakeError();
		CHECK_EQUAL(ValueCount, 0);
		CHECK_EQUAL(ErrorCount, 1);
		ValueOrError = MakeError();
		CHECK_EQUAL(ValueCount, 0);
		CHECK_EQUAL(ValueOrError.GetError().Error, 2);
		CHECK_EQUAL(ErrorCount, 1);
		ValueOrError = MakeValue();
		CHECK_EQUAL(ValueCount, 1);
		CHECK_EQUAL(ErrorCount, 0);
		FTestType UnsetValueOrError = MakeValue();
		UnsetValueOrError.StealValue();
		ValueOrError = MoveTemp(UnsetValueOrError);
		CHECK_EQUAL(ValueCount, 0);
		CHECK_EQUAL(ErrorCount, 0);
		CHECK_FALSE(ValueOrError.HasValue());
		CHECK_FALSE(ValueOrError.HasError());
	}

	CHECK_EQUAL(ValueCount, 0);
	CHECK_EQUAL(ErrorCount, 0);

	// Test Move-Only Value/Error
	{
		INFO("Move-Only Value/Error");
		TValueOrError<FTestMoveOnly, FTestMoveOnly> Value = MakeValue(FTestMoveOnly());
		TValueOrError<FTestMoveOnly, FTestMoveOnly> Error = MakeError(FTestMoveOnly());
		FTestMoveOnly MovedValue = MoveTemp(Value).GetValue();
		FTestMoveOnly MovedError = MoveTemp(Error).GetError();
	}

	// Test Integer Value/Error
	{
		INFO("Test Integer Value/Error");
		TValueOrError<int32, int32> ValueOrError = MakeValue();
		CHECK_EQUAL(ValueOrError.GetValue(), 0);
		ValueOrError = MakeValue(1);
		CHECK_EQUAL(ValueOrError.GetValue(), 1);
		ValueOrError = MakeError();
		CHECK_EQUAL(ValueOrError.GetError(), 0);
		ValueOrError = MakeError(1);
		CHECK_EQUAL(ValueOrError.GetError(), 1);
	}

}

