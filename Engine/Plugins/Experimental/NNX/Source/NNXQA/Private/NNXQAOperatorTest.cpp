// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQAOperatorTest.h"
#include "NNXQATestsOperatorElementWiseUnary.h"
#include "NNXQATestsOperatorElementWiseBinary.h"
#include "NNXQATestsOperatorElementWiseVariadic.h"
#include "NNXCore.h"
#include "NNXQAUtils.h"
#include "NNXModelBuilder.h"

#include "HAL/ConsoleManager.h"

namespace NNX 
{
namespace Test 
{
	static FTestsOperatorElementWiseUnary TestsOperatorElementWiseUnary;
	static FTestsOperatorElementWiseBinary TestsOperatorElementWiseBinary;
	static FTestsOperatorElementWiseVariadic TestsOperatorElementWiseVariadic;

	static FTests::FTestSetup* FindTestSetup(const FString& TestName)
	{
		FTests::FTestSetup* UnaryTestSetup = TestsOperatorElementWiseUnary.TestSetups.FindByPredicate([TestName](const FTests::FTestSetup& Other) { return Other.TestName == TestName; });
		FTests::FTestSetup* BinaryTestSetup = TestsOperatorElementWiseBinary.TestSetups.FindByPredicate([TestName](const FTests::FTestSetup& Other) { return Other.TestName == TestName; });
		FTests::FTestSetup* VariadicTestSetup = TestsOperatorElementWiseVariadic.TestSetups.FindByPredicate([TestName](const FTests::FTestSetup& Other) { return Other.TestName == TestName; });

		FTests::FTestSetup* TestSetup = nullptr;
		if (UnaryTestSetup != nullptr)
		{
			TestSetup = UnaryTestSetup;
		}

		if (BinaryTestSetup != nullptr)
		{
			//the test name should only be registered in one test lib.
			check(TestSetup == nullptr);
			TestSetup = BinaryTestSetup;
		}

		if (VariadicTestSetup != nullptr)
		{
			//the test name should only be registered in one test lib.
			check(TestSetup == nullptr);
			TestSetup = VariadicTestSetup;
		}

		return TestSetup;
	}

	static bool RunOperatorTest(const FString& TestName, bool UseAutomationRules)
	{
		FTests::FTestSetup* TestSetup = FindTestSetup(TestName);
		if (TestSetup == nullptr)
		{
			UE_LOG(LogNNX, Error, TEXT("Can't find test setup for test '%s'"), *TestName);
			return false;
		}

		TArray<uint8>	ModelData;
		if (!CreateONNXModelForOperator(TestSetup->TargetName, TestSetup->Inputs, TestSetup->Outputs, ModelData))
		{
			UE_LOG(LogNNX, Error, TEXT("Failed to create model for test '%s'"), *TestName);
			return false;
		}

		return CompareONNXModelInferenceAcrossRuntimes(TestName, ModelData, TestSetup);
	}

	bool RunOperatorTest(const FString& OperatorName)
	{
		return RunOperatorTest(OperatorName, false);
	}

	bool RunAllOperatorTests()
	{
		bool bAllTestsSucceeded = true;
		for (auto TestSetup : TestsOperatorElementWiseUnary.TestSetups)
		{
			bAllTestsSucceeded &= RunOperatorTest(TestSetup.TestName);
		}
		for (auto TestSetup : TestsOperatorElementWiseBinary.TestSetups)
		{
			bAllTestsSucceeded &= RunOperatorTest(TestSetup.TestName);
		}
		for (auto TestSetup : TestsOperatorElementWiseVariadic.TestSetups)
		{
			bAllTestsSucceeded &= RunOperatorTest(TestSetup.TestName);
		}
		return bAllTestsSucceeded;
	}

	static FAutoConsoleCommand TestOperatorCommand(
		TEXT("nnx.test.Operator"), TEXT("Run a unit test for an ML operator by test name."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				if (Args.Num() > 0)
				{
					RunOperatorTest(Args[0]);
				}
			}
		)
	);

	static FAutoConsoleCommand TestAllOperatorsCommand(
		TEXT("nnx.test.AllOperators"), TEXT("Run all operator unit tests."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				RunAllOperatorTests();
			}
		)
	);

#if WITH_DEV_AUTOMATION_TESTS

	#include "Misc/AutomationTest.h"

	// Unary element wise operator category
	IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNNXOperatorTestElementWiseUnary, "System.Engine.MachineLearning.NNX.OperatorTest.UnaryElementWise",
		EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::FeatureMask | EAutomationTestFlags::EngineFilter)

	void FNNXOperatorTestElementWiseUnary::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
	{
		for (FTests::FTestSetup TestSetup : TestsOperatorElementWiseUnary.TestSetups)
		{
			OutBeautifiedNames.Add(TestSetup.TestName);
			OutTestCommands.Add(TestSetup.TestName);
		}
	}

	bool FNNXOperatorTestElementWiseUnary::RunTest(const FString& Parameter)
	{
		return RunOperatorTest(Parameter, true);
	}

	// Binary element wise operator category
	IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNNXOperatorTestElementWiseBinary, "System.Engine.MachineLearning.NNX.OperatorTest.BinaryElementWise",
		EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::FeatureMask | EAutomationTestFlags::EngineFilter)

		void FNNXOperatorTestElementWiseBinary::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
	{
		for (FTests::FTestSetup TestSetup : TestsOperatorElementWiseBinary.TestSetups)
		{
			OutBeautifiedNames.Add(TestSetup.TestName);
			OutTestCommands.Add(TestSetup.TestName);
		}
	}

	bool FNNXOperatorTestElementWiseBinary::RunTest(const FString& Parameter)
	{
		return RunOperatorTest(Parameter, true);
	}

	// Variadic element wise operator category
	IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNNXOperatorTestElementWiseVariadic, "System.Engine.MachineLearning.NNX.OperatorTest.VariadicElementWise",
		EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::FeatureMask | EAutomationTestFlags::EngineFilter)

		void FNNXOperatorTestElementWiseVariadic::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
	{
		for (FTests::FTestSetup TestSetup : TestsOperatorElementWiseVariadic.TestSetups)
		{
			OutBeautifiedNames.Add(TestSetup.TestName);
			OutTestCommands.Add(TestSetup.TestName);
		}
	}

	bool FNNXOperatorTestElementWiseVariadic::RunTest(const FString& Parameter)
	{
		return RunOperatorTest(Parameter, true);
	}

#endif //WITH_DEV_AUTOMATION_TESTS

} // namespace Test
} // namespace NNX
