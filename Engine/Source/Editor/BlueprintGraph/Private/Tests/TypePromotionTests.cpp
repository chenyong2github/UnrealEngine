// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"
#include "BlueprintTypePromotion.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_PromotableOperator.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "GameFramework/Actor.h"			// For making a dummy BP
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/WildcardNodeUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/////////////////////////////////////////////////////
// Helpers to make dummy blueprints/pins/nodes

namespace TypePromoTestUtils
{
	/**
	* Spawn a test promotable operator node that we can use to ensure type propegation works
	* correctly.
	*
	* @param Graph		The graph to spawn this node in
	* @param OpName		Name of the promotable op to spawn (Multiply, Add, Subtract, etc)
	*
	* @return UK2Node_PromotableOperator* if the op exists,
	*/
	static UK2Node_PromotableOperator* SpawnPromotableNode(UEdGraph* Graph, const FName OpName)
	{
		if (!Graph)
		{
			return nullptr;
		}

		// The spawner will be null if type promo isn't enabled
		if (UBlueprintFunctionNodeSpawner* Spawner = FTypePromotion::GetOperatorSpawner(OpName))
		{
			// Spawn a new node!
			IBlueprintNodeBinder::FBindingSet Bindings;
			FVector2D SpawnLoc{};
			UK2Node_PromotableOperator* NewOpNode = Cast<UK2Node_PromotableOperator>(Spawner->Invoke(Graph, Bindings, SpawnLoc));

			return NewOpNode;
		}

		return nullptr;
	}

	/**
	* Mark this array of spawned test pins as pending kill to ensure that
	* they get cleaned up properly by GC
	*
	* @param InPins		The array of spawned test pins to mark pending kill
	*/
	static void CleanupTestPins(TArray<UEdGraphPin*>& InPins)
	{
		// Clear our test pins
		for (UEdGraphPin* TestPin : InPins)
		{
			if (TestPin)
			{
				TestPin->MarkPendingKill();
			}
		}
		InPins.Empty();
	}


	/**
	* Attempts to create a connection between the two given pins and tests that the connection was valid
	*
	* @param OpNodePin	The pin on a promotable operator node
	* @param OtherPin	The other pin
	*/
	static bool TestPromotedConnection(UEdGraphPin* OpNodePin, UEdGraphPin* OtherPin)
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		const bool bConnected = K2Schema->TryCreateConnection(OpNodePin, OtherPin);
		UK2Node_PromotableOperator* OwningNode = Cast<UK2Node_PromotableOperator>(OpNodePin->GetOwningNode());

		if (bConnected && OwningNode)
		{
			OwningNode->NotifyPinConnectionListChanged(OpNodePin);
		}

		return bConnected;
	}
}

#define MakeTestableBP(BPName, GraphName)												\
		UBlueprint* BPName = FKismetEditorUtilities::CreateBlueprint(					\
		AActor::StaticClass(),															\
		GetTransientPackage(),															\
		TEXT(#BPName),																	\
		BPTYPE_Normal,																	\
		UBlueprint::StaticClass(),														\
		UBlueprintGeneratedClass::StaticClass(),										\
		NAME_None																		\
	);																					\
	UEdGraph* GraphName = BPName ? FBlueprintEditorUtils::FindEventGraph( BPName ) : nullptr;	

#define MakeTestableNode(NodeName, OwningGraph)											\
		UK2Node* NodeName = NewObject<UK2Node_CallFunction>(/*outer*/ OwningGraph );	\
		OwningGraph->AddNode(NodeName);

#define MakeTestPin(OwningNode, PinArray, InPinName, InPinType, PinDirection)		UEdGraphPin* InPinName = UEdGraphPin::CreatePin(OwningNode);		\
																				InPinName->PinType.PinCategory = InPinType;							\
																				PinArray.Add(InPinName);											\
																				InPinName->Direction = PinDirection;

#define MakeTestPins(OwningNode, OutArray)																\
	MakeTestPin(OwningNode, OutArray, DoublePinA, UEdGraphSchema_K2::PC_Double, EGPD_Output);			\
	MakeTestPin(OwningNode, OutArray, DoublePinB, UEdGraphSchema_K2::PC_Double, EGPD_Output);			\
	MakeTestPin(OwningNode, OutArray, Int64PinA, UEdGraphSchema_K2::PC_Int64, EGPD_Output);				\
	MakeTestPin(OwningNode, OutArray, Int64PinB, UEdGraphSchema_K2::PC_Int64, EGPD_Input);				\
	MakeTestPin(OwningNode, OutArray, BytePinA, UEdGraphSchema_K2::PC_Byte, EGPD_Output);				\
	MakeTestPin(OwningNode, OutArray, WildPinA, UEdGraphSchema_K2::PC_Wildcard, EGPD_Output);			\
	MakeTestPin(OwningNode, OutArray, WildPinB, UEdGraphSchema_K2::PC_Wildcard, EGPD_Input);			\
	MakeTestPin(OwningNode, OutArray, BytePinB, UEdGraphSchema_K2::PC_Byte, EGPD_Input);				\
	MakeTestPin(OwningNode, OutArray, BoolPinA, UEdGraphSchema_K2::PC_Boolean, EGPD_Output);			\
	MakeTestPin(OwningNode, OutArray, BoolPinB, UEdGraphSchema_K2::PC_Boolean, EGPD_Input);				\
	MakeTestPin(OwningNode, OutArray, DoubleOutputPin, UEdGraphSchema_K2::PC_Double, EGPD_Output);		\
	MakeTestPin(OwningNode, OutArray, FloatPinA, UEdGraphSchema_K2::PC_Float, EGPD_Input);				\
	MakeTestPin(OwningNode, OutArray, FloatPinB, UEdGraphSchema_K2::PC_Float, EGPD_Input);				\
	MakeTestPin(OwningNode, OutArray, FloatOutputPin, UEdGraphSchema_K2::PC_Float, EGPD_Output);		\
	MakeTestPin(OwningNode, OutArray, BoolOutputPin, UEdGraphSchema_K2::PC_Boolean, EGPD_Output);		\
	MakeTestPin(OwningNode, OutArray, IntPinA, UEdGraphSchema_K2::PC_Int, EGPD_Output);					\
	MakeTestPin(OwningNode, OutArray, VecInputPinA, UEdGraphSchema_K2::PC_Struct, EGPD_Input);			\
	VecInputPinA->PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();						\
	MakeTestPin(OwningNode, OutArray, VecInputPinB, UEdGraphSchema_K2::PC_Struct, EGPD_Input);			\
	VecInputPinB->PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();						\
	MakeTestPin(OwningNode, OutArray, VecOutputPinA, UEdGraphSchema_K2::PC_Struct, EGPD_Output);		\
	VecOutputPinA->PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();						\
	MakeTestPin(OwningNode, OutArray, Vec2DOutputPinA, UEdGraphSchema_K2::PC_Struct, EGPD_Output);		\
	Vec2DOutputPinA->PinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypePromotionTest, "Blueprints.Compiler.TypePromotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FTypePromotionTest::RunTest(const FString& Parameters)
{
	FEdGraphPinType DoublePin = {};		DoublePin.PinCategory = UEdGraphSchema_K2::PC_Double;
	FEdGraphPinType FloatPin = {};		FloatPin.PinCategory = UEdGraphSchema_K2::PC_Float;
	FEdGraphPinType IntPin = {};		IntPin.PinCategory = UEdGraphSchema_K2::PC_Int;
	FEdGraphPinType Int64Pin = {};		Int64Pin.PinCategory = UEdGraphSchema_K2::PC_Int64;
	FEdGraphPinType BytePin = {};		BytePin.PinCategory = UEdGraphSchema_K2::PC_Byte;
	FEdGraphPinType VecPin = {};		VecPin.PinCategory = UEdGraphSchema_K2::PC_Struct; VecPin.PinSubCategoryObject = TBaseStructure<FVector>::Get();

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

// Test that when given an array of UEdGraphPins we can find the appropriate UFunction that best
// matches them. This is the core of how the Type Promotion system works at BP compile time
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFindBestMatchingFunc, "Blueprints.Compiler.FindBestMatchingFunc", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFindBestMatchingFunc::RunTest(const FString& Parameters)
{
	if (!TypePromoDebug::IsTypePromoEnabled())
	{
		return true;
	}

	UEdGraphNode* TestNode = NewObject<UEdGraphNode>();

	////////////////////////////////////////////////////
	// Create test pins!
	TArray<UEdGraphPin*> PinTypes = {};
	MakeTestPins(TestNode, PinTypes);

	{
		TArray<UEdGraphPin*> TestPins =
		{
			Vec2DOutputPinA,
		};

		const UFunction* AddVecFunc = FTypePromotion::FindBestMatchingFunc(TEXT("Add"), TestPins);
		static const FName ExpectedName = TEXT("Add_Vector2DVector2D");

		if (TestNotNull(TEXT("Add_Vector2DVector2D Null check"), AddVecFunc))
		{
			TestEqual(TEXT("Add_Vector2DVector2D Name Check"), AddVecFunc->GetFName(), ExpectedName);
		}
	}

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

	
	TypePromoTestUtils::CleanupTestPins(PinTypes);
	TestNode->MarkPendingKill();

	return true;
}

// Test the default state of all operator nodes to ensure they are correct.
// Comparison operators (Greater Than, Less Than, etc) should have two 
// wildcard inputs and one boolean output. All others should be all wildcards.
// The node's set function should also match the operator correctly and 
// it should have the 'OperationName' variable set.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPromotableOpDefaultState, "Blueprints.Nodes.PromotableOp.DefaultState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPromotableOpDefaultState::RunTest(const FString& Parameters)
{
	if (!TypePromoDebug::IsTypePromoEnabled())
	{
		return true;
	}

	// Refresh the actions within this test in case the editor is open but hasn't loaded BlueprintGraph yet
	FTypePromotion::ClearNodeSpawners();
	FBlueprintActionDatabase::Get().RefreshAll();

	MakeTestableBP(WildcardStartTestBP, TestWildcardGraph);

	const TSet<FName> AllOpNames = FTypePromotion::GetAllOpNames();

	for (const FName& OpName : AllOpNames)
	{
		const bool bIsComparisonOp = FTypePromotion::GetComparisonOpNames().Contains(OpName);
		const FString OpNameString = OpName.ToString();

		UK2Node_PromotableOperator* OpNode = TypePromoTestUtils::SpawnPromotableNode(TestWildcardGraph, OpName);
		TestNotNull(FString::Printf(TEXT("Spawning a '%s' operator node"), *OpNameString), OpNode);
		
		// The 'OperationName' variable is correct
		TestTrue(FString::Printf(TEXT("Operation Name '%s' matches after spawning node"), *OpNameString), OpNode->GetOperationName() == OpName);

		// The target function has been set when the node is spawned
		const UFunction* TargetFunc = OpNode->GetTargetFunction();
		TestNotNull(FString::Printf(TEXT("'%s' Operation function is not null"), *OpNameString), TargetFunc);
		
		// The target function is of the correct operation type
		const FName TargetFunctionOpName = FTypePromotion::GetOpNameFromFunction(TargetFunc);
		TestTrue(FString::Printf(TEXT("'%s' Operation function matches requested operation"), *OpNameString), TargetFunctionOpName == OpName);

		// Test pin types
		const UEdGraphPin* TopInputPin = OpNode->FindPin(TEXT("A"), EGPD_Input);
		const UEdGraphPin* BottomInputPin = OpNode->FindPin(TEXT("B"), EGPD_Input);
		const UEdGraphPin* OutputPin = OpNode->GetOutputPin();
		
		// Comparison operators should be spawned with two wildcard inputs and a boolean output
		if (bIsComparisonOp)
		{
			TestTrue(TEXT("Top is pin wildcard"), FWildcardNodeUtils::IsWildcardPin(TopInputPin));
			TestTrue(TEXT("Bottom Pin is wildcard"), FWildcardNodeUtils::IsWildcardPin(BottomInputPin));
			TestTrue(TEXT("Bottom Pin is a bool"), OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
		}
		// All other operators should be spawned with all wildcard inputs and output
		else
		{
			TestTrue(TEXT("Top is pin wildcard"), FWildcardNodeUtils::IsWildcardPin(TopInputPin));
			TestTrue(TEXT("Bottom Pin is wildcard"), FWildcardNodeUtils::IsWildcardPin(BottomInputPin));
			TestTrue(TEXT("Bottom Pin is a wildcard"), FWildcardNodeUtils::IsWildcardPin(OutputPin));
		}
	}

	// Cleanup test BP and graph
	{
		WildcardStartTestBP->MarkPendingKill();
		WildcardStartTestBP->Rename(nullptr, nullptr, REN_DontCreateRedirectors);
		TestWildcardGraph->MarkPendingKill();
	}

	return true;
}

// Test that promotable operator nodes can correctly have pins added to them
// and that comparison operators cannot have pins added to them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPromotableOpNodeAddPinInterface, "Blueprints.Nodes.PromotableOp.AddPinInterface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPromotableOpNodeAddPinInterface::RunTest(const FString& Parameters)
{
	if (!TypePromoDebug::IsTypePromoEnabled())
	{
		return true;
	}

	// Refresh the actions within this test in case the editor is open but hasn't loaded BlueprintGraph yet
	FTypePromotion::ClearNodeSpawners();
	FBlueprintActionDatabase::Get().RefreshAll();

	MakeTestableBP(TestBP, TestGraph);
	MakeTestableNode(TestNode, TestGraph);

	// Create test pins!
	TArray<UEdGraphPin*> PinTypes = {};
	MakeTestPins(TestNode, PinTypes);

	const TSet<FName> ComparisonOpNames = FTypePromotion::GetComparisonOpNames();
	for (const FName& OpName : ComparisonOpNames)
	{
		const FString OpNameString = OpName.ToString();

		UK2Node_PromotableOperator* OpNode = TypePromoTestUtils::SpawnPromotableNode(TestGraph, OpName);
		TestNotNull(FString::Printf(TEXT("'%s' Comparison op spawned"), *OpNameString), OpNode);

		TestFalse(FString::Printf(TEXT("'%s' Comparison op cannot add pin"), *OpNameString), OpNode->CanAddPin());
	}

	// Anything that is not a comparison operator can have a pin added to it
	{
		UK2Node_PromotableOperator* MultiplyNode = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Multiply"));
		TestNotNull(TEXT("Multiply Node spawn"), MultiplyNode);
		TestTrue(TEXT("Multiply can add pin"), MultiplyNode->CanAddPin());
	}

	{
		UK2Node_PromotableOperator* MultiplyNode = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Multiply"));
		TestNotNull(TEXT("Multiply Node spawn"), MultiplyNode);
		TestTrue(TEXT("Multiply can add pin"), MultiplyNode->CanAddPin());

		const int32 StartingPinCount = MultiplyNode->Pins.Num();
		MultiplyNode->AddInputPin();
		const int32 EndingPinCount = MultiplyNode->Pins.Num();

		TestTrue(TEXT("Multiply node had a pin added to it"), EndingPinCount == StartingPinCount + 1);
		const UEdGraphPin* AdditonalPin = MultiplyNode->GetAdditionalPin(EndingPinCount - StartingPinCount);
		TestNotNull(TEXT("Additional Pin is not null"), AdditonalPin);
		TestTrue(TEXT("New Pin is wildcard"), FWildcardNodeUtils::IsWildcardPin(AdditonalPin));
		TestTrue(TEXT("New Pin can be removed"), MultiplyNode->CanRemovePin(AdditonalPin));

		const UEdGraphPin* InputPinA = MultiplyNode->FindPin(FName("A"), EGPD_Input);
		TestNotNull(TEXT("First input pin is not null"), InputPinA);
		TestTrue(TEXT("First Pin can be removed"), MultiplyNode->CanRemovePin(InputPinA));

		const UEdGraphPin* InputPinB = MultiplyNode->FindPin(FName("B"), EGPD_Input);
		TestNotNull(TEXT("Second input pin is not null"), InputPinB);
		TestTrue(TEXT("Second Pin can be removed"), MultiplyNode->CanRemovePin(InputPinB));
	}

	// Anything that is not a comparison operator can have a pin added to it
	{
		UK2Node_PromotableOperator* AddNode = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Add"));
		TestNotNull(TEXT("Add Node spawn"), AddNode);
		TestTrue(TEXT("Add can add pin"), AddNode->CanAddPin());
		
		const UEdGraphPin* TopInputPin = AddNode->FindPin(TEXT("A"), EGPD_Input);
		const UEdGraphPin* BottomInputPin = AddNode->FindPin(TEXT("B"), EGPD_Input);
		const UEdGraphPin* OutputPin = AddNode->GetOutputPin();
		const int32 StartingPinCount = AddNode->Pins.Num();
		AddNode->AddInputPin();
		
		UEdGraphPin* AdditonalPin = AddNode->GetAdditionalPin(2);
		TestNotNull(TEXT("Additional Pin is not null"), AdditonalPin);
		
		// Connect a float pin to the additional input pin
		const bool bConnected = TypePromoTestUtils::TestPromotedConnection(AdditonalPin, FloatOutputPin);
		TestTrue(TEXT("Connection to additional pin success"), bConnected);

		// The other pins have propagated correctly with this new connection 
		TestTrue(TEXT("Top Pin type propegates to new connection"), TopInputPin->PinType.PinCategory == FloatOutputPin->PinType.PinCategory);
		TestTrue(TEXT("Bottom Pin type propegates to new connection"), BottomInputPin->PinType.PinCategory == FloatOutputPin->PinType.PinCategory);
		TestTrue(TEXT("Out Pin type propegates to new connection"), OutputPin->PinType.PinCategory == FloatOutputPin->PinType.PinCategory);

		// Removing the only pin with a connection with reset the node to wildcard
		AddNode->RemoveInputPin(AdditonalPin);
		TestTrue(TEXT("Top Pin type propegates to wildcard on connection break"), FWildcardNodeUtils::IsWildcardPin(TopInputPin));
		TestTrue(TEXT("Bottom Pin type propegates to wildcard on connection break"), FWildcardNodeUtils::IsWildcardPin(BottomInputPin));
		TestTrue(TEXT("Out Pin type propegates to wildcard on connection break"), FWildcardNodeUtils::IsWildcardPin(OutputPin));
		
		TestTrue(TEXT("Additional pin was successfully removed"), StartingPinCount == AddNode->Pins.Num());
	}

	// Cleanup
	{
		TypePromoTestUtils::CleanupTestPins(PinTypes);

		TestBP->MarkPendingKill();
		TestBP->Rename(nullptr, nullptr, REN_DontCreateRedirectors);
		TestGraph->MarkPendingKill();
		TestNode->MarkPendingKill();
	}

	return true;
}

// Test that making connections to a Promotable Operator node results in the correct propagation of types
// throughout the whole node and that the node has the correct UFunction that it will expand to upon compiling.
// This will also test that pin connections are broken if they are connected to an invalid promotion, 
// and that pin connections are preserved if a valid promotion is occuring. 
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPromotableOperatorConnectionChanged, "Blueprints.Nodes.PromotableOp.ConnectionChanged", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPromotableOperatorConnectionChanged::RunTest(const FString& Parameters)
{
	if (!TypePromoDebug::IsTypePromoEnabled())
	{
		return true;
	}

	// Refresh the actions within this test in case the editor is open but hasn't loaded BlueprintGraph yet
	FTypePromotion::ClearNodeSpawners();
	FBlueprintActionDatabase::Get().RefreshAll();

	MakeTestableBP(BP_ConnectionChanged, TestGraph);
	MakeTestableNode(TestNode, TestGraph);

	// Create test pins!
	TArray<UEdGraphPin*> PinTypes = {};
	MakeTestPins(TestNode, PinTypes);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Test that adding a float pin to the top input on an add node makes the whole thing a float
	{
		UK2Node_PromotableOperator* AddNode = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Add"));
		TestNotNull(TEXT("AddNode Node spawn"), AddNode);

		UEdGraphPin* TopInputPin = AddNode->FindPin(TEXT("A"), EGPD_Input);
		UEdGraphPin* BottomInputPin = AddNode->FindPin(TEXT("B"), EGPD_Input);
		UEdGraphPin* OutputPin = AddNode->GetOutputPin();

		check(TopInputPin && BottomInputPin && OutputPin);

		const bool bConnected = K2Schema->TryCreateConnection(TopInputPin, FloatOutputPin);
		AddNode->NotifyPinConnectionListChanged(TopInputPin);

		TestTrue(TEXT("Bottom Pin type propegates to float"), bConnected && BottomInputPin->PinType.PinCategory == FloatPinB->PinType.PinCategory);
	}

	// Connecting a vector output should make the other input be a vector as well
	{
		UK2Node_PromotableOperator* Node = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Multiply"));
		TestNotNull(TEXT("Multiply Node spawn"), Node);

		UEdGraphPin* TopInputPin = Node->FindPin(TEXT("A"), EGPD_Input);
		UEdGraphPin* OutputPin = Node->GetOutputPin();

		check(TopInputPin);

		const bool bConnected = K2Schema->TryCreateConnection(OutputPin, VecInputPinA);
		Node->NotifyPinConnectionListChanged(OutputPin);

		TestTrue(TEXT("Bottom Pin type propegates to float"), bConnected && TopInputPin->PinType.PinCategory == VecOutputPinA->PinType.PinCategory);
	}

	// Connecting a vector output should make the other input be a vector as well
	{
		UK2Node_PromotableOperator* Node = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Multiply"));
		TestNotNull(TEXT("Multiply Node spawn"), Node);

		UEdGraphPin* TopInputPin = Node->FindPin(TEXT("A"), EGPD_Input);
		UEdGraphPin* BottomInputPin = Node->FindPin(TEXT("B"), EGPD_Input);
		UEdGraphPin* OutputPin = Node->GetOutputPin();

		// Connect a float to the top pin
		const bool bConnected = K2Schema->TryCreateConnection(OutputPin, VecInputPinA);
		Node->NotifyPinConnectionListChanged(OutputPin);
		TestTrue(TEXT("Bottom Pin type propegates to float"), bConnected && TopInputPin->PinType.PinCategory == VecOutputPinA->PinType.PinCategory);

		// The output should be a float right now

		// Connect a double to the bottom pin

		// The output should be a double now
	}

	// Cleanup
	{
		TypePromoTestUtils::CleanupTestPins(PinTypes);

		BP_ConnectionChanged->MarkPendingKill();
		BP_ConnectionChanged->Rename(nullptr, nullptr, REN_DontCreateRedirectors);
		TestGraph->MarkPendingKill();
		TestNode->MarkPendingKill();
	}

	return true;
}

// Test the connections between primitive types and ensure that each one gets
// the correct output type pin
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPromotableOperatorPrimitivePromotions, "Blueprints.Nodes.PromotableOp.PrimitivePromotions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPromotableOperatorPrimitivePromotions::RunTest(const FString& Parameters)
{
	if (!TypePromoDebug::IsTypePromoEnabled())
	{
		return true;
	}

	// Refresh the actions within this test in case the editor is open but hasn't loaded BlueprintGraph yet
	FTypePromotion::ClearNodeSpawners();
	FBlueprintActionDatabase::Get().RefreshAll();

	MakeTestableBP(BP_Primative_Connections, TestGraph);
	MakeTestableNode(TestNode, TestGraph);

	// Create test pins!
	TArray<UEdGraphPin*> PinTypes = {};
	MakeTestPins(TestNode, PinTypes);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	const TMap<FName, TArray<FName>>* const PromoTable = FTypePromotion::GetPrimativePromotionTable();
	TestNotNull(TEXT("Primative Promotion table exists"), PromoTable);
	
	if (!PromoTable)
	{
		return false;
	}


	for(TPair<FName, TArray<FName>> Pair : *PromoTable)
	{
		FName TypeName = Pair.Key;
		if(TypeName == UEdGraphSchema_K2::PC_Wildcard)
		{
			continue;
		}
		
		UEdGraphPin* TypePin = *PinTypes.FindByPredicate([&TypeName](const UEdGraphPin* Pin) -> bool { return Pin && Pin->PinType.PinCategory == TypeName && Pin->Direction == EGPD_Output; });
		
		for (const FName AvailablePromoType : Pair.Value)
		{
			UK2Node_PromotableOperator* Node = TypePromoTestUtils::SpawnPromotableNode(TestGraph, TEXT("Add"));
			TestNotNull(TEXT("Multiply Node spawn"), Node);

			UEdGraphPin* TopInputPin = Node->FindPin(TEXT("A"), EGPD_Input);
			UEdGraphPin* BottomInputPin = Node->FindPin(TEXT("B"), EGPD_Input);
			UEdGraphPin* OutputPin = Node->GetOutputPin();
			UEdGraphPin* PinToConnectTo = *PinTypes.FindByPredicate([&AvailablePromoType](const UEdGraphPin* Pin) -> bool { return Pin && Pin->PinType.PinCategory == AvailablePromoType && Pin->Direction == EGPD_Output; });

			// Connect to the top input pin
			const bool bConnectedTop = TypePromoTestUtils::TestPromotedConnection(TopInputPin, TypePin);
			FString StatusMessage = FString::Printf(TEXT("Connecting '%s' to '%s'"), *K2Schema->TypeToText(TopInputPin->PinType).ToString(), *K2Schema->TypeToText(TypePin->PinType).ToString());

			TestTrue(StatusMessage, bConnectedTop);

			// The other pins should now all be set to the first pin type
			TestTrue(TEXT("Bottom Pin type propegates to new connection"), BottomInputPin->PinType.PinCategory == TypePin->PinType.PinCategory);
			TestTrue(TEXT("Output Pin type propegates to new connection"), OutputPin->PinType.PinCategory == TypePin->PinType.PinCategory);

			// Connect the bottom pin to the type that the first one can be promoted to
			const bool bConnectedBottom = TypePromoTestUtils::TestPromotedConnection(BottomInputPin, PinToConnectTo);
			FString BottomStatusMessage = FString::Printf(TEXT("Bottom Pin '%s' Connecting to '%s' "), *K2Schema->TypeToText(BottomInputPin->PinType).ToString(), *K2Schema->TypeToText(PinToConnectTo->PinType).ToString());

			TestTrue(BottomStatusMessage, bConnectedBottom);
			
			// The top should be same type, and the output type should have been updated to the be new higher type
			TestTrue(TEXT("Top Pin type propegates to new connection"), TopInputPin->PinType.PinCategory == TypePin->PinType.PinCategory);
			TestTrue(TEXT("Output Pin type propegates to new connection"), OutputPin->PinType.PinCategory == PinToConnectTo->PinType.PinCategory);
		}
	}

	// Cleanup
	{
		TypePromoTestUtils::CleanupTestPins(PinTypes);

		BP_Primative_Connections->MarkPendingKill();
		BP_Primative_Connections->Rename(nullptr, nullptr, REN_DontCreateRedirectors);
		TestGraph->MarkPendingKill();
		TestNode->MarkPendingKill();
	}

	return true;
}

#undef MakeTestPin

#endif //WITH_DEV_AUTOMATION_TESTS