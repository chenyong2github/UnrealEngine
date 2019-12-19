// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditConditionParserTests.h"
#include "EditConditionParser.h"
#include "EditConditionContext.h"
#include "ObjectPropertyNode.h"
#include "Misc/AutomationTest.h"

UEditConditionTestObject::UEditConditionTestObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_DEV_AUTOMATION_TESTS
struct FTestEditConditionContext : IEditConditionContext
{
	TMap<FString, bool> BoolValues;
	TMap<FString, double> DoubleValues;
	TMap<FString, FString> EnumValues;
	FString EnumTypeName;

	FTestEditConditionContext(){}
	virtual ~FTestEditConditionContext() {}

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
		const EditConditionParserTokens::FPropertyToken* PropertyToken = Token.Node.Cast<EditConditionParserTokens::FPropertyToken>();
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

	bResult &= CanParse(Parser, TEXT("BoolProperty"), 1, 1);
	bResult &= CanParse(Parser, TEXT("!BoolProperty"), 2, 1);
	bResult &= CanParse(Parser, TEXT("BoolProperty == true"), 3, 1);
	bResult &= CanParse(Parser, TEXT("BoolProperty == false"), 3, 1);
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
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value && BoolProperty"), 5, 2);
	bResult &= CanParse(Parser, TEXT("Enum == EType::Value || BoolProperty == false"), 7, 2);
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value || BoolProperty == bFoo"), 7, 3);
	bResult &= CanParse(Parser, TEXT("Enum == EType::Value && Foo != 5"), 7, 2);
	bResult &= CanParse(Parser, TEXT("Enum != EType::Value && Foo == Bar"), 7, 3);

	return bResult;
}

static bool CanEvaluate(const FEditConditionParser& Parser, const IEditConditionContext& Context, const FString& Expression, bool Expected)
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

static bool RunBoolTests(const IEditConditionContext& Context)
{
	FEditConditionParser Parser;
	bool bResult = true;

	bResult &= CanEvaluate(Parser, Context, TEXT("true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("false"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("!true"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("!false"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("!BoolProperty"), false);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty == true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty == false"), false);

	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty == BoolProperty"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty != BoolProperty"), false);

	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty != true"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty != false"), true);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("true && true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("true && false"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("false && true"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("false && false"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("true && true && true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("true && true && false"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty && BoolProperty"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty && false"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("false && BoolProperty"), false);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("true || true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("true || false"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("false || true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("false || false"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("true || true || true"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("true || true || false"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty || BoolProperty"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("BoolProperty || false"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("false || BoolProperty"), true);

	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateBool, "EditConditionParser.EvaluateBool", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateBool::RunTest(const FString& Parameters)
{
	FTestEditConditionContext TestContext;
	TestContext.SetupBool(TEXT("BoolProperty"), true);

	return RunBoolTests(TestContext);
}

static bool RunNumericTests(const IEditConditionContext& Context)
{
	FEditConditionParser Parser;
	bool bResult = true;

	bResult &= CanEvaluate(Parser, Context, TEXT("5 == 5"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("5.0 == 5.0"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == 5.0"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == 5"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == DoubleProperty"), true);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty != 5.0"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty != 6.0"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty != 6"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty != DoubleProperty"), false);

	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty > 4.5"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty > 5"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty > 6"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty > DoubleProperty"), false);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty < 4.5"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty < 5"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty < 6"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty < DoubleProperty"), false);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty >= 4.5"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty >= 5"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty >= 6"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty >= DoubleProperty"), true);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty <= 4.5"), false);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty <= 5"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty <= 6"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty <= DoubleProperty"), true);
	
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == 2 + 3"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == 6 - 1"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == 2.5 * 2"), true);
	bResult &= CanEvaluate(Parser, Context, TEXT("DoubleProperty == 10 / 2"), true);

	return bResult;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateDouble, "EditConditionParser.EvaluateDouble", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateDouble::RunTest(const FString& Parameters)
{
	FTestEditConditionContext TestContext;
	TestContext.SetupDouble(TEXT("DoubleProperty"), 5.0);

	return RunNumericTests(TestContext);
}

static bool RunEnumTests(const IEditConditionContext& Context, const FString& EnumName, const FString& PropertyName)
{
	FEditConditionParser Parser;
	bool bResult = true;

	bResult &= CanEvaluate(Parser, Context, EnumName + TEXT("::First == ") + EnumName + TEXT("::First"), true);
	bResult &= CanEvaluate(Parser, Context, EnumName + TEXT("::First == ") + EnumName + TEXT("::Second"), false);

	bResult &= CanEvaluate(Parser, Context, EnumName + TEXT("::First != ") + EnumName + TEXT("::First"), false);
	bResult &= CanEvaluate(Parser, Context, EnumName + TEXT("::First != ") + EnumName + TEXT("::Second"), true);

	bResult &= CanEvaluate(Parser, Context, PropertyName + TEXT(" == ") + PropertyName, true);
	bResult &= CanEvaluate(Parser, Context, PropertyName + TEXT(" != ") + PropertyName, false);

	bResult &= CanEvaluate(Parser, Context, PropertyName + TEXT(" == ") + EnumName + TEXT("::First"), true);
	bResult &= CanEvaluate(Parser, Context, EnumName + TEXT("::First == ") + PropertyName, true);
	bResult &= CanEvaluate(Parser, Context, PropertyName + TEXT(" == ") + EnumName + TEXT("::Second"), false);

	bResult &= CanEvaluate(Parser, Context, PropertyName + TEXT(" != ") + EnumName + TEXT("::Second"), true);
	bResult &= CanEvaluate(Parser, Context, PropertyName + TEXT(" != ") + EnumName + TEXT("::First"), false);
	bResult &= CanEvaluate(Parser, Context, EnumName + TEXT("::Second != ") + PropertyName, true);

	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateEnum, "EditConditionParser.EvaluateEnum", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateEnum::RunTest(const FString& Parameters)
{
	FTestEditConditionContext TestContext;

	const FString EnumType = TEXT("EditConditionTestEnum");
	TestContext.SetupEnumType(EnumType);

	const FString PropertyName = TEXT("EnumProperty");
	TestContext.SetupEnum(PropertyName, TEXT("First"));

	return RunEnumTests(TestContext, EnumType, PropertyName);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_EvaluateUObject, "EditConditionParser.EvaluateUObject", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_EvaluateUObject::RunTest(const FString& Parameters)
{
	UEditConditionTestObject* TestObject = NewObject<UEditConditionTestObject>();
	TestObject->AddToRoot();

	TSharedPtr<FObjectPropertyNode> ObjectNode(new FObjectPropertyNode);
	ObjectNode->AddObject(TestObject);

	FPropertyNodeInitParams InitParams;
	ObjectNode->InitNode(InitParams);

	TOptional<bool> bResult;
	bool bAllResults = true;

	// enum comparison
	{
		static const FName EnumPropertyName = TEXT("EnumProperty");
		TSharedPtr<FPropertyNode> PropertyNode = ObjectNode->FindChildPropertyNode(EnumPropertyName, true);
		FEditConditionContext Context(*PropertyNode.Get());

		TestObject->EnumProperty = EditConditionTestEnum::First;
		TestObject->ByteEnumProperty = EditConditionByteEnum::First;

		static const FString EnumType = TEXT("EditConditionTestEnum");
		bAllResults &= RunEnumTests(Context, EnumType, EnumPropertyName.ToString());

		static const FString ByteEnumType = TEXT("EditConditionByteEnum");
		static const FString ByteEnumPropertyName = TEXT("ByteEnumProperty");
		bAllResults &= RunEnumTests(Context, ByteEnumType, ByteEnumPropertyName);
	}

	// bool comparison
	{
		static const FName BoolPropertyName(TEXT("BoolProperty"));
		TSharedPtr<FPropertyNode> PropertyNode = ObjectNode->FindChildPropertyNode(BoolPropertyName, true);
		FEditConditionContext Context(*PropertyNode.Get());

		TestObject->BoolProperty = true;

		bAllResults &= RunBoolTests(Context);
	}

	// double comparison
	{
		static const FName DoublePropertyName(TEXT("DoubleProperty"));
		TSharedPtr<FPropertyNode> PropertyNode = ObjectNode->FindChildPropertyNode(DoublePropertyName, true);
		FEditConditionContext Context(*PropertyNode.Get());

		TestObject->DoubleProperty = 5.0;

		bAllResults &= RunNumericTests(Context);
	}

	// integer comparison
	{
		static const FName IntegerPropertyName(TEXT("IntegerProperty"));
		TSharedPtr<FPropertyNode> PropertyNode = ObjectNode->FindChildPropertyNode(IntegerPropertyName, true);
		FEditConditionContext Context(*PropertyNode.Get());

		TestObject->IntegerProperty = 5;

		bAllResults &= RunNumericTests(Context);
	}

	{
		static const FName DoublePropertyName(TEXT("DoubleProperty"));
		TSharedPtr<FPropertyNode> PropertyNode = ObjectNode->FindChildPropertyNode(DoublePropertyName, true);
		FEditConditionContext Context(*PropertyNode.Get());

		TestEqual(TEXT("Boolean Type Name"), Context.GetTypeName(TEXT("BoolProperty")).GetValue(), TEXT("bool"));
		TestEqual(TEXT("Enum Type Name"), Context.GetTypeName(TEXT("EnumProperty")).GetValue(), TEXT("EditConditionTestEnum"));
		TestEqual(TEXT("Byte Enum Type Name"), Context.GetTypeName(TEXT("ByteEnumProperty")).GetValue(), TEXT("EditConditionByteEnum"));
		TestEqual(TEXT("Double Type Name"), Context.GetTypeName(TEXT("DoubleProperty")).GetValue(), TEXT("double"));
	}

	TestObject->RemoveFromRoot();

	return bAllResults;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditConditionParser_SingleBool, "EditConditionParser.SingleBool", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEditConditionParser_SingleBool::RunTest(const FString& Parameters)
{
	UEditConditionTestObject* TestObject = NewObject<UEditConditionTestObject>();
	TestObject->AddToRoot();

	TSharedPtr<FObjectPropertyNode> ObjectNode(new FObjectPropertyNode);
	ObjectNode->AddObject(TestObject);

	FPropertyNodeInitParams InitParams;
	ObjectNode->InitNode(InitParams);

	static const FName BoolPropertyName(TEXT("BoolProperty"));
	TSharedPtr<FPropertyNode> PropertyNode = ObjectNode->FindChildPropertyNode(BoolPropertyName, true);
	FEditConditionContext Context(*PropertyNode.Get());

	FEditConditionParser Parser;

	{
		TSharedPtr<FEditConditionExpression> Expression = Parser.Parse(FString(TEXT("BoolProperty")));
		const FBoolProperty* Property = Context.GetSingleBoolProperty(Expression);
		TestNotNull(TEXT("Bool"), Property);
	}

	{
		TSharedPtr<FEditConditionExpression> Expression = Parser.Parse(FString(TEXT("UintBitfieldProperty")));
		const FBoolProperty* Property = Context.GetSingleBoolProperty(Expression);
		TestNotNull(TEXT("Uint Bitfield"), Property);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS