// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"
#include "BlueprintTypePromotion.h"
#include "EdGraph/EdGraphPin.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypePromotionTest, "Blueprints.Compiler.TypePromotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FTypePromotionTest::RunTest(const FString& Parameters)
{
	FEdGraphPinType DoublePin = {};		DoublePin.PinCategory	= UEdGraphSchema_K2::PC_Double;
	FEdGraphPinType FloatPin = {};		FloatPin.PinCategory	= UEdGraphSchema_K2::PC_Float;
	FEdGraphPinType IntPin = {};		IntPin.PinCategory		= UEdGraphSchema_K2::PC_Int;
	FEdGraphPinType Int64Pin = {};		Int64Pin.PinCategory	= UEdGraphSchema_K2::PC_Int64;
	FEdGraphPinType BytePin = {};		BytePin.PinCategory		= UEdGraphSchema_K2::PC_Byte;
	FEdGraphPinType VecPin = {};		VecPin.PinCategory		= UEdGraphSchema_K2::PC_Struct; VecPin.PinSubCategoryObject = TBaseStructure<FVector>::Get();

	// Test promotions that should happen
	TestEqual(TEXT("Testing float to double"), FTypePromotion::GetHigherType(FloatPin, DoublePin), FTypePromotion::ETypeComparisonResult::TypeBHigher);
	TestEqual(TEXT("Testing float to vector"), FTypePromotion::GetHigherType(FloatPin, VecPin), FTypePromotion::ETypeComparisonResult::TypeBHigher);

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


#define MakeTestPin(OwningNode, PinArray, InPinName, InPinType)		UEdGraphPin* InPinName = UEdGraphPin::CreatePin(OwningNode);		\
																	InPinName->PinType.PinCategory = InPinType;							\
																	PinArray.Add(InPinName);		

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFindBestMatchingFunc, "Blueprints.Compiler.FindBestMatchingFunc", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFindBestMatchingFunc::RunTest(const FString& Parameters)
{
	TArray<UEdGraphPin*> PinTypes = {};

	UEdGraphNode* TestNode = NewObject<UEdGraphNode>();

	////////////////////////////////////////////////////
	// Create test pins!
	MakeTestPin(TestNode, PinTypes, DoublePinA, UEdGraphSchema_K2::PC_Double);
	MakeTestPin(TestNode, PinTypes, DoublePinB, UEdGraphSchema_K2::PC_Double);
	MakeTestPin(TestNode, PinTypes, DoubleOutputPin, UEdGraphSchema_K2::PC_Double);
	DoubleOutputPin->Direction = EGPD_Output;
	
	MakeTestPin(TestNode, PinTypes, FloatPinA, UEdGraphSchema_K2::PC_Float);
	FloatPinA->Direction = EGPD_Input;
	MakeTestPin(TestNode, PinTypes, FloatPinB, UEdGraphSchema_K2::PC_Float);
	FloatPinB->Direction = EGPD_Input;

	MakeTestPin(TestNode, PinTypes, FloatOutputPin, UEdGraphSchema_K2::PC_Float);
	FloatPinB->Direction = EGPD_Output;

	MakeTestPin(TestNode, PinTypes, BoolOutputPin, UEdGraphSchema_K2::PC_Boolean);
	BoolOutputPin->Direction = EGPD_Output;

	MakeTestPin(TestNode, PinTypes, IntPinA, UEdGraphSchema_K2::PC_Int);

	// Structs!
	MakeTestPin(TestNode, PinTypes, VecInputPinA, UEdGraphSchema_K2::PC_Struct);
	VecInputPinA->Direction = EGPD_Input;
	VecInputPinA->PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();

	MakeTestPin(TestNode, PinTypes, VecInputPinB, UEdGraphSchema_K2::PC_Struct);
	VecInputPinB->Direction = EGPD_Input;
	VecInputPinB->PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();

	MakeTestPin(TestNode, PinTypes, VecOutputPinA, UEdGraphSchema_K2::PC_Struct);
	VecOutputPinA->Direction = EGPD_Output;
	VecOutputPinA->PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();

	// Multiply_VectorVector given A float input, vector input, and a vector output
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA, VecInputPinB, VecOutputPinA,
		};

		const UFunction* MulVecFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Multiply"), TestPins);
		static const FName ExpectedName = TEXT("Multiply_VectorVector");

		if (TestNotNull(TEXT("Multiply_VectorVector Null check"), MulVecFunc))
		{
			TestEqual(TEXT("Multiply_VectorVector Name Check"), MulVecFunc->GetFName(), ExpectedName);
		}
	}

	// Multiply_VectorVector given a float, vector, float
	// Order shouldn't matter when passing these pins in, which is what we are testing here
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA, VecOutputPinA, VecInputPinA,
		};

		const UFunction* MulVecFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Multiply"), TestPins);
		static const FName ExpectedName = TEXT("Multiply_VectorVector");

		if (TestNotNull(TEXT("Multiply_VectorVector Null check"), MulVecFunc))
		{
			TestEqual(TEXT("Multiply_VectorVector Name Check"), MulVecFunc->GetFName(), ExpectedName);
		}
	}

	// Multiply_VectorVector given two vector inputs and a vector output
	{
		TArray<UEdGraphPin*> TestPins =
		{
			VecInputPinA, VecInputPinB, VecOutputPinA,
		};

		const UFunction* MulVecFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Multiply"), TestPins);
		static const FName ExpectedName = TEXT("Multiply_VectorVector");

		if (TestNotNull(TEXT("Multiply_VectorVector Null check"), MulVecFunc))
		{
			TestEqual(TEXT("Multiply_VectorVector Name Check"), MulVecFunc->GetFName(), ExpectedName);
		}
	}

	// Add_DoubleDouble
	{
		TArray<UEdGraphPin*> TestPins =
		{
			DoublePinA, DoublePinB, DoubleOutputPin
		};

		const UFunction* AddDoubleFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Add"), TestPins);
		static const FName ExpectedName = TEXT("Add_DoubleDouble");
		
		if (TestNotNull(TEXT("Add_DoubleDouble Null check"), AddDoubleFunc))
		{
			TestEqual(TEXT("Add_DoubleDouble Name Check"), AddDoubleFunc->GetFName(), ExpectedName);
		}
	}

	// Add_DoubleDouble given a double and float
	{
		TArray<UEdGraphPin*> TestPins =
		{
			DoublePinA, FloatPinA, DoubleOutputPin
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
			FloatPinA, FloatPinB, FloatOutputPin
		};

		const UFunction* SubtractFloatFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Subtract"), TestPins);
		static const FName ExpectedName = TEXT("Subtract_FloatFloat");

		if (TestNotNull(TEXT("Subtract_FloatFloat null check"), SubtractFloatFunc))
		{
			TestEqual(TEXT("Subtract_FloatFloat Name Check"), SubtractFloatFunc->GetFName(), ExpectedName);
		}
	}

	// Add_FloatFloat given only one float pin. This simulates the first connection being made to a 
	// promotable operator, in which case we should default to a regular old Float + Float
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA,
		};

		const UFunction* AddFloatFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Add"), TestPins);
		static const FName ExpectedName = TEXT("Add_FloatFloat");

		if (TestNotNull(TEXT("Add_FloatFloat null check"), AddFloatFunc))
		{
			TestEqual(TEXT("Add_FloatFloat Name Check"), AddFloatFunc->GetFName(), ExpectedName);
		}
	}

	// Less_FloatFloat Given a Float and Double
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA, BoolOutputPin
		};

		const UFunction* LessFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Less"), TestPins);
		static const FName ExpectedName = TEXT("Less_FloatFloat");

		if (TestNotNull(TEXT("Less_FloatFloat null check"), LessFunc))
		{
			TestEqual(TEXT("Less_FloatFloat Name Check"), LessFunc->GetFName(), ExpectedName);
		}
	}

	// Less_FloatFloat Given just a single float
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA,
		};

		const UFunction* LessFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Less"), TestPins);
		static const FName ExpectedName = TEXT("Less_FloatFloat");

		if (TestNotNull(TEXT("Less_FloatFloat null check"), LessFunc))
		{
			TestEqual(TEXT("Less_FloatFloat Name Check"), LessFunc->GetFName(), ExpectedName);
		}
	}

	// Greater_DoubleDouble Given a Float and Double
	{
		TArray<UEdGraphPin*> TestPins =
		{
			FloatPinA, DoublePinA
		};

		const UFunction* GreaterFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Greater"), TestPins);
		static const FName ExpectedName = TEXT("Greater_DoubleDouble");

		if (TestNotNull(TEXT("Greater_DoubleDouble null check"), GreaterFunc))
		{
			TestEqual(TEXT("Greater_DoubleDouble Name Check"), GreaterFunc->GetFName(), ExpectedName);
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