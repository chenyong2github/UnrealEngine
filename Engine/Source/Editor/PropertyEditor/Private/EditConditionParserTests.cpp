// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "EditConditionParser.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

PRAGMA_DISABLE_OPTIMIZATION

struct TestEditConditionContext : IEditConditionContext
{
	TMap<FString, bool> BoolValues;
	TMap<FString, double> DoubleValues;
	TMap<FString, FString> EnumValues;
	FString EnumTypeName;

	TestEditConditionContext(){}
	virtual ~TestEditConditionContext() {}

	virtual TOptional<bool> GetBoolValue(const FString& PropertyName) const override
	{
		TOptional<bool> Result;
		if (const bool* Value = BoolValues.Find(PropertyName))
		{
			Result = *Value;
		}
		return Result;
	}

	virtual TOptional<double> GetNumericValue(const FString& PropertyName) const override
	{
		TOptional<double> Result;
		if (const double* Value = DoubleValues.Find(PropertyName))
		{
			Result = *Value;
		}
		return Result;
	}

	virtual TOptional<FString> GetEnumValue(const FString& PropertyName) const override
	{
		TOptional<FString> Result;
		if (const FString* Value = EnumValues.Find(PropertyName))
		{
			Result = *Value;
		}
		return Result;
	}

	virtual TOptional<FString> GetTypeName(const FString& PropertyName) const override
	{
		TOptional<FString> Result;

		if (BoolValues.Find(PropertyName) != nullptr)
		{
			Result = TEXT("bool");
		}
		else if (DoubleValues.Find(PropertyName) != nullptr)
		{
			Result = TEXT("double");
		}
		else if (EnumValues.Find(PropertyName) != nullptr)
		{
			Result = EnumTypeName;
		}

		return Result;
	}

	void SetupBool(const FString& PropertyName, bool Value)
	{
		BoolValues.Add(PropertyName, Value);
	}

	void SetupDouble(const FString& PropertyName, double Value)
	{
		DoubleValues.Add(PropertyName, Value);
	}

	void SetupEnum(const FString& PropertyName, const FString& Value)
	{
		EnumValues.Add(PropertyName, Value);
	}

	void SetupEnumType(const FString& EnumType)
	{
		EnumTypeName = EnumType;
	}
};

static bool CanParse(const FEditConditionParser& Parser, const FString& Expression, int32 ExpectedTokens, int32 ExpectedProperties)
{
	 TSharedPtr<FEditConditionExpression> Parsed = Parser.Parse(Expression);

	 if (!Parsed.IsValid())
	 {
		ensureMsgf(false, TEXT("Failed to parse expression: %s"), *Expression);
		return false;
	 }
	 
	 int PropertyCount = 0;

	 for (const auto& Token : Parsed->Tokens)
	 {
		const EditConditionParserNamespace::FPropertyToken* PropertyToken = Token.Node.Cast<EditConditionParserNamespace::FPropertyToken>();
		if (PropertyToken != nullptr)
		{
			++PropertyCount;
		}
	 }

	 return Parsed->Tokens.Num() == ExpectedTokens &&
		 PropertyCount == ExpectedProperties;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_Parse, "EditConditionParser.Parse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_Parse::RunTest(const FString& Parameters)
{
	FEditConditionParser Parser;
	bool bResult = true;

	bResult &= CanParse(Parser, TEXT("bProperty"), 1, 1);
	bResult &= CanParse(Parser, TEXT("!bProperty"), 2, 1);
	bResult &= CanParse(Parser, TEXT("bProperty == true"), 3, 1);
	bResult &= CanParse(Parser, TEXT("bProperty == false"), 3, 1);
	bResult &= CanParse(Parser, TEXT("IntProperty == 0"), 3, 1);
	bResult &= CanParse(Parser, TEXT("IntProperty != 0"), 3, 1);
	bResult &= CanParse(Parser, TEXT("IntProperty > 0"), 3, 1);
	bResult &= CanParse(Parser, TEXT("IntProperty < 0"), 3, 1);
	bResult &= CanParse(Parser, TEXT("IntProperty <= 0"), 3, 1);
	bResult &= CanParse(Parser, TEXT("IntProperty >= 0"), 3, 1);
	bResult &= CanParse(Parser, TEXT("Foo > Bar"), 3, 2);
	bResult &= CanParse(Parser, TEXT("Foo && Bar"), 3, 2);
	bResult &= CanParse(Parser, TEXT("Foo || Bar"), 3, 2);
	bResult &= CanParse(Parser, TEXT("Foo == Bar + 5"), 5, 2);
	bResult &= CanParse(Parser, TEXT("Foo == Bar - 5"), 5, 2);
	bResult &= CanParse(Parser, TEXT("Foo == Bar * 5"), 5, 2);
	bResult &= CanParse(Parser, TEXT("Foo == Bar / 5"), 5, 2);
	bResult &= CanParse(Parser, TEXT("Enum == EType::Value"), 3, 1);
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value"), 3, 1);
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value && bProperty"), 5, 2);
	bResult &= CanParse(Parser, TEXT("Enum == EType::Value || bProperty == false"), 7, 2);
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value || bProperty == bFoo"), 7, 3);
	bResult &= CanParse(Parser, TEXT("Enum == EType::Value && Foo != 5"), 7, 2);
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value && Foo == Bar"), 7, 3);

	return bResult;
}

bool CanEvaluate(const FEditConditionParser& Parser, const IEditConditionContext& Context, const FString& Expression, bool Expected)
{
	TSharedPtr<FEditConditionExpression> Parsed = Parser.Parse(Expression);
	if (!Parsed.IsValid())
	{
		ensureMsgf(false, TEXT("Failed to parse expression: %s"), *Expression);
		return false;
	}

	TOptional<bool> Result = Parser.Evaluate(*Parsed.Get(), Context);
	if (!Result.IsSet())
	{
		ensureMsgf(false, TEXT("Expression failed to evaluate: %s"), *Expression);
		return false;
	}

	if (Result.GetValue() != Expected)
	{
		ensureMsgf(false, TEXT("Expression evaluated to unexpected value."), *Expression);
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateBool, "EditConditionParser.EvaluateBool", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateBool::RunTest(const FString& Parameters)
{
	FEditConditionParser Parser;

	TestEditConditionContext TestContext;
	TestContext.SetupBool(TEXT("bProperty"), true);

	bool bResult = true;

	bResult &= CanEvaluate(Parser, TestContext, TEXT("true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("!true"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("!false"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("!bProperty"), false);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty == true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty == false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty == bProperty"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty != true"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty != false"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("true && true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("true && false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false && true"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false && false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("true && true && true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("true && true && false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty && bProperty"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty && false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false && bProperty"), false);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("true || true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("true || false"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false || true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false || false"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("true || true || true"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("true || true || false"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty || bProperty"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("bProperty || false"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("false || bProperty"), true);

	return bResult;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateDouble, "EditConditionParser.EvaluateDouble", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateDouble::RunTest(const FString& Parameters)
{
	FEditConditionParser Parser;

	TestEditConditionContext TestContext;
	TestContext.SetupDouble(TEXT("Property"), 5.0);

	bool bResult = true;

	bResult &= CanEvaluate(Parser, TestContext, TEXT("5 == 5"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("5.0 == 5.0"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == 5.0"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == 5"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == Property"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property != 5.0"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property != 6.0"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property != 6"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property > 4.5"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property > 5"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property > 6"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property > Property"), false);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property < 4.5"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property < 5"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property < 6"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property < Property"), false);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property >= 4.5"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property >= 5"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property >= 6"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property >= Property"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property <= 4.5"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property <= 5"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property <= 6"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property <= Property"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == 2 + 3"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == 6 - 1"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == 2.5 * 2"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == 10 / 2"), true);

	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateEnum, "EditConditionParser.EvaluateEnum", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateEnum::RunTest(const FString& Parameters)
{
	FEditConditionParser Parser;

	TestEditConditionContext TestContext;
	TestContext.SetupEnumType(TEXT("MyEnum"));
	TestContext.SetupEnum(TEXT("Property"), TEXT("First"));

	bool bResult = true;
	
	bResult &= CanEvaluate(Parser, TestContext, TEXT("MyEnum::First == MyEnum::First"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("MyEnum::First == MyEnum::Second"), false);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("MyEnum::First != MyEnum::First"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("MyEnum::First != MyEnum::Second"), true);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == MyEnum::First"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("MyEnum::First == Property"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property == MyEnum::Second"), false);

	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property != MyEnum::Second"), true);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("Property != MyEnum::First"), false);
	bResult &= CanEvaluate(Parser, TestContext, TEXT("MyEnum::Second != Property"), true);

	return bResult;
}

#endif // WITH_DEV_AUTOMATION_TESTS