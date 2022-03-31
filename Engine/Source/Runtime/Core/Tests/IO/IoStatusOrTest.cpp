// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "IO/IoDispatcher.h"
#include "TestHarness.h"

struct FIoStatusTestType
{
	FIoStatusTestType() { }
	FIoStatusTestType(const FIoStatusTestType& Other)
		: Text(Other.Text) { }
	FIoStatusTestType(FIoStatusTestType&&) = default;

	FIoStatusTestType(const FString& InText)
		: Text(InText) { }
	FIoStatusTestType(FString&& InText)
		: Text(MoveTemp(InText)) { }

	FIoStatusTestType& operator=(const FIoStatusTestType& Other) = default;
	FIoStatusTestType& operator=(FIoStatusTestType&& Other) = default;
	FIoStatusTestType& operator=(const FString& OtherText)
	{
		Text = OtherText;
		return *this;
	}

	FString Text;
};

void TestConstruct(FAutomationTestFixture& Test)
{
	{
		TIoStatusOr<FIoStatusTestType> Result;
		TEST_EQUAL("Default IoStatus is Unknown", Result.Status(), FIoStatus::Unknown);
	}

	{
		const TIoStatusOr<FIoStatusTestType> Other;
		TIoStatusOr<FIoStatusTestType> Result(Other);
		TEST_EQUAL("Copy construct", Result.Status(), FIoStatus::Unknown);
	}

	{
		const FIoStatus IoStatus(EIoErrorCode::InvalidCode);
		TIoStatusOr<FIoStatusTestType> Result(IoStatus);
		TEST_EQUAL("Construct with status", Result.Status().GetErrorCode(), EIoErrorCode::InvalidCode);
	}

	{
		const FString ExpectedText("Unreal");
		const FIoStatusTestType Type(ExpectedText);
		TIoStatusOr<FIoStatusTestType> Result(Type);
		TEST_EQUAL("Construct with value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Result(FIoStatusTestType("Unreal"));
		TEST_EQUAL("Construct with temporary value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		TIoStatusOr<FIoStatusTestType> Result(FString("Unreal"));
		TEST_EQUAL("Construct with value arguments", Result.ValueOrDie().Text, FString("Unreal"));
	}
}

void TestAssignment(FAutomationTestFixture& Test)
{
	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		TIoStatusOr<FIoStatusTestType> Other = FIoStatus(ExpectedErrorCode);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = Other;
		TEST_EQUAL("Assign IoStatusOr with status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		TIoStatusOr<FIoStatusTestType> Result;
		Result = TIoStatusOr<FIoStatusTestType>(FIoStatus(ExpectedErrorCode));
		TEST_EQUAL("Assign temporary IoStatusOr with status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Other = FIoStatusTestType(ExpectedText);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = Other;
		TEST_EQUAL("Assign IoStatusOr with value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Result;
		Result = TIoStatusOr<FIoStatusTestType>(ExpectedText);
		TEST_EQUAL("Assign temporary IoStatusOr with value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		const FIoStatus IoStatus(ExpectedErrorCode);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = IoStatus; 
		TEST_EQUAL("Assign status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		TIoStatusOr<FIoStatusTestType> Result;
		Result = FIoStatus(ExpectedErrorCode);
		TEST_EQUAL("Assign temporary status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const FString ExpectedText("Unreal");
		const FIoStatusTestType Value(ExpectedText);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = Value;
		TEST_EQUAL("Assign value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Result;
		Result = FIoStatusTestType(ExpectedText);
		TEST_EQUAL("Assign temporary value", Result.ValueOrDie().Text, ExpectedText);
	}
}

void TestConsumeValue(FAutomationTestFixture& Test)
{
	const FString ExpectedText("Unreal");
	TIoStatusOr<FIoStatusTestType> Result = FIoStatusTestType(ExpectedText);
	FIoStatusTestType Value = Result.ConsumeValueOrDie();
	TEST_EQUAL("Consume value or die with valid value", Value.Text, ExpectedText);
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::IO::IoStatusOr::Smoke Test", "[Core][IO][Smoke]")
{
	TestConstruct(*this);
	TestAssignment(*this);
	TestConsumeValue(*this);
}

