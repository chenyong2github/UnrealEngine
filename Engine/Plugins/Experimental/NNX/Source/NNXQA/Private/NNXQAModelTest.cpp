// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQAModelTest.h"
#include "NNXQAUtils.h"
#include "NNXCore.h"
#include "NNXRuntime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/ConsoleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace NNX 
{
namespace Test 
{
	struct FModelTests : public FTests
	{
		FModelTests()
		{
			// List of all model to test (located in FPaths::ProjectDir()/OnnxModel)

			{
				//https://github.com/onnx/models/tree/main/vision/classification/resnet
				FTestSetup& TestSetup = AddModelTest(TEXT("resnet50-v2-7"));

				// NNXRuntimeORTDml require higher relative error.
				TestSetup.RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 1.0f);

				//Model with multiple layer not yet implemented 
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
			}

			{
				//https://github.com/onnx/models/tree/main/vision/style_transfer/fast_neural_style
				FTestSetup& TestSetup = AddModelTest(TEXT("mosaic-9"));

				//TODO Test fail on ORT backend need to be investigated
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeORTCpu"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeORTCuda"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeORTDml"));

				//Model with multiple layer not yet implemented 
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
			}

			{
				FTestSetup& TestSetup = AddModelTest(TEXT("NeuralMorphModel_global"));

				// NNXRuntimeORTDml require higher relative error.
				TestSetup.RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 0.02f);

				//Model with multiple layer not yet implemented 
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
			}

			{
				FTestSetup& TestSetup = AddModelTest(TEXT("NeuralMorphModel_Local"));

				// NNXRuntimeORTDml require higher relative error.
				TestSetup.RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 0.04f);

				//Model with multiple layer not yet implemented 
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
			}

			{
				FTestSetup& TestSetup = AddModelTest(TEXT("VertexDeltaModel"));

				// NNXRuntimeORTDml require higher absolute and relative error.
				TestSetup.AbsoluteErrorEpsilonForRuntime.Emplace("NNXRuntimeORTDml", 1e-4f);
				TestSetup.RelativeErrorPercentForRuntime.Emplace("NNXRuntimeORTDml", 10.0f);//TODO seems very high

				//Model with multiple layer not yet implemented 
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeDml"));
				TestSetup.AutomationExcludedRuntime.Emplace(TEXT("NNXRuntimeHlsl"));
			}
		}

		FTestSetup& AddModelTest(const FString& ModelName)
		{
			return AddTest(ModelName, TEXT(""));
		}
	};
	static FModelTests ModelTests;

	static FString GetFullModelPathFromProjectContent(const FString& ModelName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("OnnxModels") / ModelName);
	}

	static bool TestModelFromPath(const FString& ModelPath, const FTests::FTestSetup* AutomationTestSetup)
	{
		// Load model from disk
		TArray<uint8> ModelBytes;
		const bool bIsModelInMem = FFileHelper::LoadFileToArray(ModelBytes, *ModelPath);
		if (!bIsModelInMem)
		{
			UE_LOG(LogNNX, Error, TEXT("Can't load model from disk. Tests ABORTED!"));
			return false;
		}

		return CompareONNXModelInferenceAcrossRuntimes(*FPaths::GetCleanFilename(ModelPath), ModelBytes, AutomationTestSetup);
		
	}

	static bool TestModelFromName(const FString& ModelName, const FTests::FTestSetup* AutomationTestSetup)
	{
		return TestModelFromPath(GetFullModelPathFromProjectContent(ModelName) + TEXT(".onnx"), AutomationTestSetup);
	}

	bool TestModelFromName(const FString& ModelName)
	{
		return TestModelFromName(ModelName, nullptr);
	}

	bool TestModelFromPath(const FString& ModelPath)
	{
		return TestModelFromPath(ModelPath, nullptr);
	}

	bool TestAllModels()
	{
		bool bAllTestsSucceeded = true;
		for (auto TestSetup : ModelTests.TestSetups)
		{
			bAllTestsSucceeded &= TestModelFromName(TestSetup.TestName);
		}
		return bAllTestsSucceeded;
	}

	static FAutoConsoleCommand TestModelFromPathCommand(
		TEXT("nnx.test.ModelFromPath"), TEXT("Run a model from path against all runtimes and compare results."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				if (Args.Num() > 0)
				{
					TestModelFromName(Args[0]);
				}
				else
				{
					UE_LOG(LogNNX, Display, TEXT("Please provide a fully qualified model path"));
				}
			}
		)
	);

	static FAutoConsoleCommand TestModelCommand(
		TEXT("nnx.test.Model"), TEXT("Run a model from name (model in FPaths::ProjectDir()/OnnxModel) against all runtimes, use resnet50-v2-7 if no model name is provided."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				FString ModelPath = Args.Num() > 0 ? Args[0] : TEXT("resnet50-v2-7");
				TestModelFromName(ModelPath);
			}
		)
	);

	static FAutoConsoleCommand TestAllModelsCommand(
		TEXT("nnx.test.AllModels"), TEXT("Run all test models (should be located in FPaths::ProjectDir()/OnnxModel) against all runtimes."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				TestAllModels();
			}
		)
	);


#if WITH_DEV_AUTOMATION_TESTS

	IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNNXModelTest, "System.Engine.MachineLearning.NNX.ModelTest",
		EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::FeatureMask | EAutomationTestFlags::EngineFilter)

	void FNNXModelTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
	{
		for (FTests::FTestSetup TestSetup : ModelTests.TestSetups)
		{
			OutBeautifiedNames.Add(TestSetup.TestName);
			OutTestCommands.Add(TestSetup.TestName);
		}
	}

	bool FNNXModelTest::RunTest(const FString& Parameters)
	{
		FString ModelName = Parameters;
		FTests::FTestSetup* AutomationTestSetup = ModelTests.TestSetups.FindByPredicate([ModelName](const FTests::FTestSetup& Other) { return Other.TestName == ModelName; });

		check(AutomationTestSetup != nullptr);
		return TestModelFromName(ModelName, AutomationTestSetup);
	}

#endif //WITH_DEV_AUTOMATION_TESTS

} // namespace Test
} // namespace NNX
