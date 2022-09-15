// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQATestsOperatorElementWiseBinary.h"

namespace NNX 
{
namespace Test 
{

	FTestsOperatorElementWiseBinary::FTestsOperatorElementWiseBinary()
	{
		//Binary operator with bidirectional broadcast
		AddTests(TEXT("Add"));
		AddTests(TEXT("And"));
		AddTests(TEXT("Div"));
		AddTests(TEXT("Equal"));
		AddTests(TEXT("Greater"));
		AddTests(TEXT("GreaterOrEqual"));
		AddTests(TEXT("Less"));
		AddTests(TEXT("LessOrEqual"));
		//AddTests(TEXT("Mod"));             //TODO need to set fmod attribute to 1 for floating point tensors
		AddTests(TEXT("Mul"));
		AddTests(TEXT("Or"));
		AddTests(TEXT("PRelu"));             //Note: Prelu only unidirectional broadcast see AddEWBinaryOpTest()
		AddTests(TEXT("Pow"));
		AddTests(TEXT("Sub"));
		AddTests(TEXT("Xor"));
	}

	FTests::FTestSetup& FTestsOperatorElementWiseBinary::AddTest(const FString& OpName, TArrayView<const uint32> ShapeLHS, TArrayView<const uint32> ShapeRHS, TArrayView<const uint32> ShapeOut)
	{
		TMap<FString, float> AbsoluteErrorEpsilonForRuntime;
		TMap<FString, float> RelativeErrorPercentForRuntime;
		TArray<FString> ExcludedRuntimes;
		EMLTensorDataType InputTensorType = EMLTensorDataType::Float;
		EMLTensorDataType OutputTensorType = EMLTensorDataType::Float;

		// Those operators need bool tensors
		if ((OpName == "And") ||
			(OpName == "Or") ||
			(OpName == "Xor"))
		{
			InputTensorType = EMLTensorDataType::Boolean;
			OutputTensorType = EMLTensorDataType::Boolean;
		}

		// Those operators need bool outputs tensors
		if ((OpName == "Equal") ||
			(OpName == "Greater") ||
			(OpName == "GreaterOrEqual") ||
			(OpName == "Less") ||
			(OpName == "LessOrEqual"))
		{
			OutputTensorType = EMLTensorDataType::Boolean;
		}

		//Bool tensor not yet supported on RDG
		if (InputTensorType != EMLTensorDataType::Float || OutputTensorType != EMLTensorDataType::Float)
		{
			ExcludedRuntimes.Emplace(TEXT("NNXRuntimeDml"));
			ExcludedRuntimes.Emplace(TEXT("NNXRuntimeHlsl"));
		}

		// Tweak test required precision for NNXRuntimeHlsl runtime
		if ((OpName == "Pow") ||
			(OpName == "Div"))
		{
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeHlsl", 1e-4f);
		}

		FString TestSuffix;
		TestSuffix += TEXT("_");
		TestSuffix += ShapeToString(ShapeLHS);
		TestSuffix += TEXT("_");
		TestSuffix += ShapeToString(ShapeRHS);
		TestSuffix += TEXT("=>");
		TestSuffix += ShapeToString(ShapeOut);

		FTestSetup& Test = FTests::AddTest(OpName, TestSuffix);

		Test.Inputs.Emplace(FMLTensorDesc::Make(TEXT("in0"), ShapeLHS, InputTensorType));
		Test.Inputs.Emplace(FMLTensorDesc::Make(TEXT("in1"), ShapeRHS, InputTensorType));
		Test.Outputs.Emplace(FMLTensorDesc::Make(TEXT("out"), ShapeOut, OutputTensorType));
		Test.AbsoluteErrorEpsilonForRuntime.Append(AbsoluteErrorEpsilonForRuntime);
		Test.RelativeErrorPercentForRuntime.Append(RelativeErrorPercentForRuntime);
		Test.AutomationExcludedRuntime.Append(ExcludedRuntimes);

		return Test;
	}

	void FTestsOperatorElementWiseBinary::AddTests(const FString& OpName)
	{
		//0 sized tensors
		{
			FTestSetup& Test = AddTest(OpName, { 1,0 }, { 1,1 }, { 1,0 });

			// TODO: not yet implemented on RDG
			Test.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
			Test.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
		}

		//Large dispatch behavior
		//This test is slow. So we don't run it for every op
		if (OpName == "Add")
		{
			AddTest(OpName, { 65536,513 }, { 1 }, { 65536,513 });
		}

		//No broadcast
		{
			AddTest(OpName, { 1 }, { 1 }, { 1 });
			AddTest(OpName, { 4 }, { 4 }, { 4 });
			AddTest(OpName, { 2,3,4,5,6 }, { 2,3,4,5,6 }, { 2,3,4,5,6 });
		}

		//Broadcast RHS -> LHS
		{
			AddTest(OpName, { 2,3,4 }, { 1,1,1 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 2,1,1 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 1,1,4 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 1,3,1 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 1,3,4 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 2,1,4 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 2,3,1 }, { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 1 }    , { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 4 }    , { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 1, 1 } , { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 3, 1 } , { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 1, 4 } , { 2,3,4 });
			AddTest(OpName, { 2,3,4 }, { 3, 4 } , { 2,3,4 });
		}

		//Prelu does not support bidirectional broadcast. It only support slope(RHS) to input0(LHS)
		if (OpName == TEXT("PRelu"))
		{
			return;
		}

		//Bidirectional broadcast
		{
			AddTest(OpName, { 2,3,1 }, { 1,1,4 }, { 2,3,4 });
			AddTest(OpName, { 2,1,4 }, { 2,3,1 }, { 2,3,4 });
			AddTest(OpName, { 2,3,1 }, { 4 }    , { 2,3,4 });
			AddTest(OpName, { 2,3,1 }, { 1,4 }  , { 2,3,4 });
		}

		//Broadcast LHS <- RHS
		{
			AddTest(OpName, { 1,1,1 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 2,1,1 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 1,1,4 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 1,3,1 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 1,3,4 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 2,1,4 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 2,3,1 }, { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 1 }    , { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 4 }    , { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 1, 1 } , { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 3, 1 } , { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 1, 4 } , { 2,3,4 }, { 2,3,4 });
			AddTest(OpName, { 3, 4 } , { 2,3,4 }, { 2,3,4 });
		}
	}

} // namespace Test
} // namespace NNX