// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQATestsOperatorElementWiseVariadic.h"

namespace NNX 
{
namespace Test 
{
	FTestsOperatorElementWiseVariadic::FTestsOperatorElementWiseVariadic()
	{
		//Variadic operator with multi-directional broadcast
		AddTests(TEXT("Max"));
		AddTests(TEXT("Mean"));
		AddTests(TEXT("Min"));
		AddTests(TEXT("Sum"));
	}

	FTests::FTestSetup& FTestsOperatorElementWiseVariadic::AddTest(const FString& OpName,
		std::initializer_list<std::initializer_list<const uint32>> ShapeInputs, TArrayView<const uint32> ShapeOut)
	{
		TMap<FString, float> AbsoluteErrorEpsilonForRuntime;
		TMap<FString, float> RelativeErrorPercentForRuntime;
		TArray<FString> AutomationExcludedRuntime;
		EMLTensorDataType TensorType = EMLTensorDataType::Float;

		//Variadic op not yet implemented on Dml
		AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));

		FString TestSuffix;
		for (auto&& ShapeInput: ShapeInputs)
		{
			TestSuffix += TEXT("_");
			TestSuffix += ShapeToString(ShapeInput);
		}
		TestSuffix += TEXT("=>");
		TestSuffix += ShapeToString(ShapeOut);

		FTestSetup& Test = FTests::AddTest(OpName, TestSuffix);
		
		uint32 i = 0;
		for (auto&& ShapeInput : ShapeInputs)
		{
			Test.Inputs.Emplace(FMLTensorDesc::Make(FString::Printf(TEXT("in%d"), i++), ShapeInput, TensorType));
		}
		
		Test.Outputs.Emplace(FMLTensorDesc::Make(TEXT("out"),ShapeOut, TensorType));
		Test.AbsoluteErrorEpsilonForRuntime.Append(AbsoluteErrorEpsilonForRuntime);
		Test.RelativeErrorPercentForRuntime.Append(RelativeErrorPercentForRuntime);
		Test.AutomationExcludedRuntime.Append(AutomationExcludedRuntime);
		
		return Test;
	}

	void FTestsOperatorElementWiseVariadic::AddTests(const FString& OpName)
	{
		//0 sized tensors
		{
			FTestSetup& Test = AddTest(OpName, { { 1,0 }, { 1,1 } }, { 1,0 });

			//not yet implemented on RDG
			Test.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
			Test.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
		}

		//No broadcast: 1, 2, 3 & 8 inputs
		{
			AddTest(OpName, { { 1 } }, { 1 });
			AddTest(OpName, { { 1 }, { 1 } }, { 1 });
			AddTest(OpName, { { 1 }, { 1 }, { 1 } }, { 1 });
			AddTest(OpName, { { 4 }, { 4 } }, { 4 });
			AddTest(OpName, { { 2,3,4,5,6 }, { 2,3,4,5,6 } }, { 2,3,4,5,6 });
			AddTest(OpName, { { 5 }, { 5 }, { 5 }, { 5 }, { 5 }, { 5 }, { 5 }, { 5 } }, { 5 });
		}

		//Large dispatch behavior
		//This test is slow. So we don't run it for every op
		if (OpName == "Max")
		{
			AddTest(OpName, { { 65536,513 }, { 1 } }, { 65536,513 });
		}

		//Broadcast 2nd input to 1st
		{
			AddTest(OpName, { { 2,3,4 }, { 1,1,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 2,1,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 1,1,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 1,3,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 1,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 2,1,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 2,3,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 1 }     }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 4 }     }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 1, 1  } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 3, 1  } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 1, 4  } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,4 }, { 3, 4  } }, { 2,3,4 });
		}

		//Bidirectional broadcast 2 input
		{
			AddTest(OpName, { { 2,3,1 }, { 1,1,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,1,4 }, { 2,3,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,1 }, { 4 }     }, { 2,3,4 });
			AddTest(OpName, { { 2,3,1 }, { 1,4 }   }, { 2,3,4 });
		}

		//Broadcast 1st input to 2nd
		{
			AddTest(OpName, { { 1,1,1 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,1,1 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 1,1,4 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 1,3,1 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 1,3,4 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,1,4 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,1 }, { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 1 }    , { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 4 }    , { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 1, 1 } , { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 3, 1 } , { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 1, 4 } , { 2,3,4 } }, { 2,3,4 });
			AddTest(OpName, { { 3, 4 } , { 2,3,4 } }, { 2,3,4 });
		}

		//Multi-directional broadcast 3 input
		{
			AddTest(OpName, { { 2,3,1 }, { 1,1,4 }, { 1,1,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,1,4 }, { 1,3,4 }, { 2,3,1 } }, { 2,3,4 });
			AddTest(OpName, { { 2,3,1 }, { 4 }    , { 3,1 }   }, { 2,3,4 });
			AddTest(OpName, { { 2,1,1 }, { 3,1 }  , { 4 }     }, { 2,3,4 });
		}
	}

} // namespace Test
} // namespace NNX