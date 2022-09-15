// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQATestsOperatorElementWiseUnary.h"

namespace NNX 
{
namespace Test 
{
	FTestsOperatorElementWiseUnary::FTestsOperatorElementWiseUnary()
	{
		//Element wise
		AddTests(TEXT("Abs"));
		AddTests(TEXT("Acos"));
		AddTests(TEXT("Acosh"));
		AddTests(TEXT("Asin"));
		AddTests(TEXT("Asinh"));
		AddTests(TEXT("Atan"));
		AddTests(TEXT("Atanh"));
		//AddTests(TEXT("BitShift"));   //TODO need attributes
		//AddTests(TEXT("Cast"));       //TODO need attributes
		AddTests(TEXT("Ceil"));
		AddTests(TEXT("Clip"));
		AddTests(TEXT("Cos"));
		AddTests(TEXT("Cosh"));
		AddTests(TEXT("Elu"));                //TODO test with not-default attribute
		AddTests(TEXT("Erf"));
		AddTests(TEXT("Exp"));
		AddTests(TEXT("Floor"));
		//AddTests(TEXT("IsInf"));      //TODO add flexibility in test data setup + bool tensors
		//AddTests(TEXT("IsNan"));      //TODO add flexibility in test data setup + bool tensors
		AddTests(TEXT("HardSigmoid"));        //TODO test with not-default attribute
		AddTests(TEXT("HardSwish"));
		AddTests(TEXT("LeakyRelu"));          //TODO test with not-default attribute
		AddTests(TEXT("Log"));
		AddTests(TEXT("Neg"));
		AddTests(TEXT("Not"));
		AddTests(TEXT("Reciprocal"));
		AddTests(TEXT("Relu"));
		AddTests(TEXT("Round"));
		AddTests(TEXT("Selu"));               //TODO test with not-default attribute
		AddTests(TEXT("Sigmoid"));
		AddTests(TEXT("Sign"));
		AddTests(TEXT("Sin"));
		AddTests(TEXT("Sinh"));
		AddTests(TEXT("Softplus"));
		AddTests(TEXT("Softsign"));
		AddTests(TEXT("Sqrt"));
		AddTests(TEXT("Tan"));
		AddTests(TEXT("Tanh"));
	}

	FTests::FTestSetup& FTestsOperatorElementWiseUnary::AddTest(const FString& OpName,
		TArrayView<const uint32> Shape, const FString& ExtraSuffix)
	{
		TMap<FString, float> AbsoluteErrorEpsilonForRuntime;
		TMap<FString, float> RelativeErrorPercentForRuntime;
		TArray<FString> AutomationExcludedRuntime;
		EMLTensorDataType TensorType = EMLTensorDataType::Float;

		if (OpName == "Not")
		{
			TensorType = EMLTensorDataType::Boolean;
		}

		// Those operators have feature not yet implemented on RDG
		if ((OpName == "Clip") ||      //RDG TODO need scalar tensor inputs
			(OpName == "Not"))         //RDG TODO need bool tensors
		{
			AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
			AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
		}

		// Tweak test required precision for GPUS based runtime
		if ((OpName == "Acos") ||
			(OpName == "Asinh") ||
			(OpName == "Atan") ||
			(OpName == "Elu") ||
			(OpName == "Selu") ||
			(OpName == "Sin") ||
			(OpName == "Tan") ||
			(OpName == "Tanh"))
		{
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeORTDml", 1e-4f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 0.03f);
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeHlsl", 1e-4f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeHlsl", 0.03f);
		}
		else if (OpName == "Asin")
		{
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeORTDml", 1e-4f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 1.5f);
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeHlsl", 1e-4f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeHlsl", 1.5f);
		}
		else if (OpName == "Log")
		{
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 0.15f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeHlsl", 0.15f);
		}

		//NNXRuntimeHLSL Erf implementation is less precise than the DML one
		if (OpName == "Erf")
		{
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeORTDml", 1e-4f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 0.03f);
			AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeHlsl", 5e-4f);
			RelativeErrorPercentForRuntime.Emplace("NNXRuntimeHlsl", 0.05f);
		}

		FString TestSuffix;
		TestSuffix += TEXT("_");
		TestSuffix += ShapeToString(Shape);
		TestSuffix += ExtraSuffix;

		FTestSetup& Test = FTests::AddTest(OpName, TestSuffix);
		
		Test.Inputs.Emplace(FMLTensorDesc::Make(TEXT("in"), Shape, TensorType));
		Test.Outputs.Emplace(FMLTensorDesc::Make(TEXT("out"), Shape, TensorType));
		Test.AbsoluteErrorEpsilonForRuntime.Append(AbsoluteErrorEpsilonForRuntime);
		Test.RelativeErrorPercentForRuntime.Append(RelativeErrorPercentForRuntime);
		Test.AutomationExcludedRuntime.Append(AutomationExcludedRuntime);
		
		return Test;
	}

	void FTestsOperatorElementWiseUnary::AddTests(const FString& OpName)
	{
		// Various shapes & rank
		{
			AddTest(OpName, { 1 });
			AddTest(OpName, { 1,512 });
			AddTest(OpName, { 1,2,3,4 });
		}

		//Large dispatch behavior
		//This test is slow. So we don't run it for every op
		if (OpName == "Abs")
		{
			AddTest(OpName, {65536, 513});
		}

		//Clip with min arguments
		if (OpName == "Clip")
		{
			FTestSetup& Test = AddTest(OpName, { 20 }, TEXT("_min"));
			
			Test.Inputs.Emplace(FMLTensorDesc::Make(TEXT("min"), { }, Test.Inputs[0].DataType));
		}

		//Clip with min & max arguments
		if (OpName == "Clip")
		{
			FTestSetup& Test = AddTest(OpName, { 20 }, TEXT("_min_max"));
			
			Test.Inputs.Emplace(FMLTensorDesc::Make(TEXT("min"), { }, Test.Inputs[0].DataType));
			Test.Inputs.Emplace(FMLTensorDesc::Make(TEXT("max"), { }, Test.Inputs[0].DataType));
		}

		// 0 sized tensors
		{
			FTestSetup& Test = AddTest(OpName, { 1,0,3 } );
			
			//not yet implemented on RDG
			Test.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
			Test.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
		}
	}

} // namespace Test
} // namespace NNX
