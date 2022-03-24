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

void TestConstruct()
{
	{
		TIoStatusOr<FIoStatusTestType> Result;
		TestEqual("Default IoStatus is Unknown", Result.Status(), FIoStatus::Unknown);
	}

	{
		const TIoStatusOr<FIoStatusTestType> Other;
		TIoStatusOr<FIoStatusTestType> Result(Other);
		TestEqual("Copy construct", Result.Status(), FIoStatus::Unknown);
	}

	{
		const FIoStatus IoStatus(EIoErrorCode::InvalidCode);
		TIoStatusOr<FIoStatusTestType> Result(IoStatus);
		TestEqual("Construct with status", Result.Status().GetErrorCode(), EIoErrorCode::InvalidCode);
	}

	{
		const FString ExpectedText("Unreal");
		const FIoStatusTestType Type(ExpectedText);
		TIoStatusOr<FIoStatusTestType> Result(Type);
		TestEqual("Construct with value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Result(FIoStatusTestType("Unreal"));
		TestEqual("Construct with temporary value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		TIoStatusOr<FIoStatusTestType> Result(FString("Unreal"));
		TestEqual("Construct with value arguments", Result.ValueOrDie().Text, FString("Unreal"));
	}
}

void TestAssignment()
{
	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		TIoStatusOr<FIoStatusTestType> Other = FIoStatus(ExpectedErrorCode);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = Other;
		TestEqual("Assign IoStatusOr with status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		TIoStatusOr<FIoStatusTestType> Result;
		Result = TIoStatusOr<FIoStatusTestType>(FIoStatus(ExpectedErrorCode));
		TestEqual("Assign temporary IoStatusOr with status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Other = FIoStatusTestType(ExpectedText);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = Other;
		TestEqual("Assign IoStatusOr with value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Result;
		Result = TIoStatusOr<FIoStatusTestType>(ExpectedText);
		TestEqual("Assign temporary IoStatusOr with value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		const FIoStatus IoStatus(ExpectedErrorCode);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = IoStatus; 
		TestEqual("Assign status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const EIoErrorCode ExpectedErrorCode = EIoErrorCode::InvalidCode;
		TIoStatusOr<FIoStatusTestType> Result;
		Result = FIoStatus(ExpectedErrorCode);
		TestEqual("Assign temporary status", Result.Status().GetErrorCode(), ExpectedErrorCode);
	}

	{
		const FString ExpectedText("Unreal");
		const FIoStatusTestType Value(ExpectedText);
		TIoStatusOr<FIoStatusTestType> Result;
		Result = Value;
		TestEqual("Assign value", Result.ValueOrDie().Text, ExpectedText);
	}

	{
		const FString ExpectedText("Unreal");
		TIoStatusOr<FIoStatusTestType> Result;
		Result = FIoStatusTestType(ExpectedText);
		TestEqual("Assign temporary value", Result.ValueOrDie().Text, ExpectedText);
	}
}

void TestConsumeValue()
{
	const FString ExpectedText("Unreal");
	TIoStatusOr<FIoStatusTestType> Result = FIoStatusTestType(ExpectedText);
	FIoStatusTestType Value = Result.ConsumeValueOrDie();
	TestEqual("Consume value or die with valid value", Value.Text, ExpectedText);
}

TEST_CASE("Core::IO::IoStatusOr::Smoke Test", "[Core][IO][Smoke]")
{
	TestConstruct();
	TestAssignment();
	TestConsumeValue();
}

