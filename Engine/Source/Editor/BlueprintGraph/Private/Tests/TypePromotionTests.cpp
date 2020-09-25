// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"
#include "BlueprintTypePromotion.h"
#include "EdGraph/EdGraphPin.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypePromotionTest, "Blueprints.Compiler.TypePromotion", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTypePromotionTest::RunTest(const FString& Parameters)
{
	FEdGraphPinType DoublePin = {};		DoublePin.PinCategory	= UEdGraphSchema_K2::PC_Double;
	FEdGraphPinType FloatPin = {};		FloatPin.PinCategory	= UEdGraphSchema_K2::PC_Float;
	FEdGraphPinType IntPin = {};		IntPin.PinCategory		= UEdGraphSchema_K2::PC_Int;
	FEdGraphPinType Int64Pin = {};		Int64Pin.PinCategory	= UEdGraphSchema_K2::PC_Int64;
	FEdGraphPinType BytePin = {};		BytePin.PinCategory		= UEdGraphSchema_K2::PC_Byte;

	// Test promotions that should happen
	TestEqual(TEXT("Testing float to double"), FTypePromotion::GetHigherType(FloatPin, DoublePin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	
	TestEqual(TEXT("Testing int to float"), FTypePromotion::GetHigherType(IntPin, FloatPin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	TestEqual(TEXT("Testing int to double"), FTypePromotion::GetHigherType(IntPin, DoublePin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	TestEqual(TEXT("Testing int to int64"), FTypePromotion::GetHigherType(IntPin, Int64Pin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	
	TestEqual(TEXT("Testing Byte to int"), FTypePromotion::GetHigherType(BytePin, IntPin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	TestEqual(TEXT("Testing Byte to int64"), FTypePromotion::GetHigherType(BytePin, Int64Pin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	
	TestEqual(TEXT("Testing Double to int64"), FTypePromotion::GetHigherType(DoublePin, Int64Pin), FTypePromotion::ETypeComparisonResult::TypeBHigher);

	// Test Equality of pins
	TestEqual(TEXT("Testing Byte == Byte"), FTypePromotion::GetHigherType(BytePin, BytePin), FTypePromotion::ETypeComparisonResult::TypesEqual);
	TestEqual(TEXT("Testing float == float"), FTypePromotion::GetHigherType(FloatPin, FloatPin), FTypePromotion::ETypeComparisonResult::TypesEqual);
	TestEqual(TEXT("Testing double == double"), FTypePromotion::GetHigherType(DoublePin, DoublePin), FTypePromotion::ETypeComparisonResult::TypesEqual);
	TestEqual(TEXT("Testing int == int"), FTypePromotion::GetHigherType(IntPin, IntPin), FTypePromotion::ETypeComparisonResult::TypesEqual);
	TestEqual(TEXT("Testing int64 == int64"), FTypePromotion::GetHigherType(Int64Pin, Int64Pin), FTypePromotion::ETypeComparisonResult::TypesEqual);


	// Test promotions that should not happen
	TestEqual(TEXT("Testing int64 cannot go to byte"), FTypePromotion::GetHigherType(Int64Pin, BytePin), FTypePromotion::ETypeComparisonResult::TypeAHigher);
	TestEqual(TEXT("Testing int64 cannot go to int"), FTypePromotion::GetHigherType(Int64Pin, IntPin), FTypePromotion::ETypeComparisonResult::TypeAHigher);
	TestEqual(TEXT("Testing int64 cannot go to float"), FTypePromotion::GetHigherType(Int64Pin, FloatPin), FTypePromotion::ETypeComparisonResult::TypeAHigher);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFindPromotedFunc, "Blueprints.Compiler.FindPromotedFunc", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

#define MakeTestPin(OwningNode, PinArray, InPinName, InPinType)		UEdGraphPin* InPinName = UEdGraphPin::CreatePin(OwningNode);		\
																	InPinName->PinType.PinCategory = InPinType;							\
																	PinArray.Add(InPinName);		

bool FFindPromotedFunc::RunTest(const FString& Parameters)
{
	TArray<UEdGraphPin*> PinTypes = {};

	UEdGraphNode* TestNode = NewObject<UEdGraphNode>();			
	
	const FTypePromotion& TypePromo = FTypePromotion::Get();

	MakeTestPin(TestNode, PinTypes, DoublePin, UEdGraphSchema_K2::PC_Double);
	MakeTestPin(TestNode, PinTypes, FloatPin, UEdGraphSchema_K2::PC_Float);
	MakeTestPin(TestNode, PinTypes, Int32Pin, UEdGraphSchema_K2::PC_Int);
	MakeTestPin(TestNode, PinTypes, Int64Pin, UEdGraphSchema_K2::PC_Int64);

	// Add Operation
	{
		TArray<UEdGraphPin*> TestPins =
		{
			DoublePin, FloatPin
		};
		UFunction* AddDoubleFunc = FTypePromotion::GetOperatorFunction(TEXT("add"), TestPins);
		TestNotNull(TEXT("Add Double Float function"), AddDoubleFunc);
	}

	// Add Operation
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPin, DoublePin
		};
		UFunction* AddDoubleFunc = FTypePromotion::GetOperatorFunction(TEXT("add"), TestPins);
		TestNotNull(TEXT("Add Float Double function"), AddDoubleFunc);
	}

	// Multiply
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPin, Int32Pin
		};
		UFunction* ResultFunc = FTypePromotion::GetOperatorFunction(TEXT("multiply"), TestPins);
		TestNotNull(TEXT("multiply Float Int32 function"), ResultFunc);
	}

	// Divide
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPin, Int32Pin
		};

		UFunction* ResultFunc = FTypePromotion::GetOperatorFunction(TEXT("divide"), TestPins);
		TestNotNull(TEXT("divide Float Int32 function"), ResultFunc);
	}

	// Clear our test pins
	for(UEdGraphPin* TestPin : PinTypes)
	{
		if(TestPin)
		{
			TestPin->MarkPendingKill();
		}
	}
	PinTypes.Empty();
	TestNode->MarkPendingKill();

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFindBestMatchingFunc, "Blueprints.Compiler.FindBestMatchingFunc", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FFindBestMatchingFunc::RunTest(const FString& Parameters)
{
	TArray<UEdGraphPin*> PinTypes = {};

	UEdGraphNode* TestNode = NewObject<UEdGraphNode>();

	MakeTestPin(TestNode, PinTypes, DoublePinA, UEdGraphSchema_K2::PC_Double);
	MakeTestPin(TestNode, PinTypes, DoublePinB, UEdGraphSchema_K2::PC_Double);

	MakeTestPin(TestNode, PinTypes, FloatPinA, UEdGraphSchema_K2::PC_Float);
	MakeTestPin(TestNode, PinTypes, FloatPinB, UEdGraphSchema_K2::PC_Float);
	
	MakeTestPin(TestNode, PinTypes, IntPinA, UEdGraphSchema_K2::PC_Int);

	// Add_DoubleDouble
	{
		TArray<UEdGraphPin*> TestPins =
		{
			DoublePinA, DoublePinB
		};

		const UFunction* AddDoubleFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Add"), TestPins);
		static const FName ExpectedName = TEXT("Add_DoubleDouble");
		
		if (TestNotNull(TEXT("Add_DoubleDouble Null check"), AddDoubleFunc))
		{
			TestEqual(TEXT("Add_DoubleDouble Name Check"), AddDoubleFunc->GetFName(), ExpectedName);
		}
	}

	// Subtract_FloatFloat
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA, FloatPinB
		};

		const UFunction* SubtractFloatFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Subtract"), TestPins);
		static const FName ExpectedName = TEXT("Subtract_FloatFloat");

		if (TestNotNull(TEXT("Subtract_FloatFloat null check"), SubtractFloatFunc))
		{
			TestEqual(TEXT("Subtract_FloatFloat Name Check"), SubtractFloatFunc->GetFName(), ExpectedName);
		}
	}

	// Multiply_IntFloat
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA, FloatPinB, IntPinA
		};

		const UFunction* MulFloatFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Multiply"), TestPins);
		static const FName ExpectedName = TEXT("Multiply_IntFloat");

		if (TestNotNull(TEXT("Multiply_IntFloat null check"), MulFloatFunc))
		{
			TestEqual(TEXT("Multiply_IntFloat Name Check"), MulFloatFunc->GetFName(), ExpectedName);
		}
	}

	// Clear our test pins
	for (UEdGraphPin* TestPin : PinTypes)
	{
		if (TestPin)
		{
			TestPin->MarkPendingKill();
		}
	}
	PinTypes.Empty();
	TestNode->MarkPendingKill();

	return true;
}

#undef MakeTestPin

#endif //WITH_DEV_AUTOMATION_TESTS