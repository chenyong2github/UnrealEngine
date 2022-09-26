// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQAParametricTest.h"

#include "UObject/Class.h"
#include "NNXCore.h"
#include "NNXTypes.h"
#include "NNXQAUtils.h"
#include "NNXQAJsonUtils.h"
#include "NNXModelBuilder.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ReflectedTypeAccessors.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace NNX 
{
namespace Test
{
	class FParametricTests : public FTests
	{
	public:
		FParametricTests()
		{
			ReloadTestDescriptionsFromJson();
		}

		bool ReloadTestDescriptionsFromJson()
		{
			TestSetups.Empty();

			TSharedPtr<IPlugin> NNXPlugin = IPluginManager::Get().FindPlugin(TEXT("NNX"));
			
			if (!NNXPlugin.IsValid())
			{
				return false;
			}

			//TODO verify the path can be accessed on standalone build and on consoles
			//TODO allow to define the tests in more than one json file
			FString NNXPluginBaseDir = NNXPlugin->GetBaseDir();
			FString FullPath(NNXPluginBaseDir + TEXT("\\Source\\NNXQA\\Resources\\NNXQATestDesc.json"));

			TArray<Json::FTestCategory> ModelTestCategories;
			TArray<Json::FTestCategory> OperatorTestCategories;
			TArray<Json::FTestConfigInputOutputSet> InputOutputSets;
			
			if (!Json::LoadTestDescriptionFromJson(FullPath, ModelTestCategories, OperatorTestCategories, InputOutputSets))
			{
				return false;
			}

			const FString NNXBaseTestPath(TEXT("System.Engine.MachineLearning.NNX.Parametric"));
			AddTestFromCategory(NNXBaseTestPath + TEXT(".Model."), ModelTestCategories, InputOutputSets);
			AddTestFromCategory(NNXBaseTestPath + TEXT(".Operator."), OperatorTestCategories, InputOutputSets);

			return true;
		}
	private:
		
		static void ApplyEpsilons(FTests::FTestSetup& TestSetup, const Json::FTestConfigTarget& TestTarget)
		{
			if (TestTarget.AbsoluteError != 0.0f)
			{
				TestSetup.AbsoluteErrorEpsilon = TestTarget.AbsoluteError;
			}
			
			if (TestTarget.RelativeError != 0.0f)
			{
				TestSetup.RelativeErrorPercent = TestTarget.RelativeError;
			}
		}

		static void ApplyRuntimesConfig(FTests::FTestSetup& TestSetup, const TArray<Json::FTestConfigRuntime>& TestRuntimes)
		{
			for (const Json::FTestConfigRuntime& Runtime : TestRuntimes)
			{
				if (Runtime.Skip && !TestSetup.AutomationExcludedRuntime.Contains(Runtime.Name))
				{
					TestSetup.AutomationExcludedRuntime.Emplace(Runtime.Name);
				}
				else
				{
					if (Runtime.AbsoluteError != 0.0f)
					{
						TestSetup.AbsoluteErrorEpsilonForRuntime.Emplace(Runtime.Name, Runtime.AbsoluteError);
					}
					if (Runtime.RelativeError != 0.0f)
					{
						TestSetup.RelativeErrorPercentForRuntime.Emplace(Runtime.Name, Runtime.RelativeError);
					}
				}
			}
		}

		static void ApplyTargetConfig(FTests::FTestSetup& TestSetup, const Json::FTestConfigTarget& TestTarget)
		{
			//Epsilons
			if (TestTarget.AbsoluteError != 0.0f)
			{
				TestSetup.AbsoluteErrorEpsilon = TestTarget.AbsoluteError;
			}
			if (TestTarget.RelativeError != 0.0f)
			{
				TestSetup.RelativeErrorPercent = TestTarget.RelativeError;
			}

			ApplyRuntimesConfig(TestSetup, TestTarget.Runtimes);;

			//Tags
			TestSetup.Tags = TestTarget.Tags;
		}

		static TArray<uint32> GetShapeFromJsonArray(const TArray<int32>& JsonShape)
		{
			TArray<uint32> Shape;
			Shape.Reserve(JsonShape.Num());
			for (int32 dim : JsonShape)
			{
				Shape.Emplace(dim >= 0 ? dim : 1);
			}

			return Shape;
		}

		static EMLTensorDataType GetTensorTypeFromJson(const FString& TypeName, EMLTensorDataType DefaultValue)
		{
			int64 Value = StaticEnum<EMLTensorDataType>()->GetValueByNameString(TypeName);
			
			return (Value == INDEX_NONE) ? DefaultValue : (EMLTensorDataType)Value;
		}

		static void ApplyDatasetConfig(FTests::FTestSetup& TestSetup, const Json::FTestConfigDataset& TestDataset, EMLTensorDataType DefaultInputType, EMLTensorDataType DefaultOutputType)
		{
			ApplyRuntimesConfig(TestSetup, TestDataset.Runtimes);

			if (TestDataset.Inputs.Num() == 0)
			{
				return;
			}

			uint32 i = 0;
			for (auto&& Tensor : TestDataset.Inputs)
			{
				TArray<uint32> shape = GetShapeFromJsonArray(Tensor.Shape);
				EMLTensorDataType TensorType = GetTensorTypeFromJson(Tensor.Type, DefaultInputType);
				TestSetup.Inputs.Emplace(FMLTensorDesc::Make(FString::Printf(TEXT("in%d"), i++), shape, TensorType));
			}

			i = 0;
			for (auto&& Tensor : TestDataset.Outputs)
			{
				TArray<uint32> shape = GetShapeFromJsonArray(Tensor.Shape);
				EMLTensorDataType TensorType = GetTensorTypeFromJson(Tensor.Type, DefaultOutputType);
				TestSetup.Outputs.Emplace(FMLTensorDesc::Make(FString::Printf(TEXT("output%d"), i++), shape, TensorType));
			}
			//If output is not defined it is the first input shape.
			if (TestDataset.Outputs.Num() == 0 && TestDataset.Inputs.Num() > 0)
			{
				TArray<uint32> shape = GetShapeFromJsonArray(TestDataset.Inputs[0].Shape);
				EMLTensorDataType TensorType = GetTensorTypeFromJson(TestDataset.Inputs[0].Type, DefaultOutputType);
				TestSetup.Outputs.Emplace(FMLTensorDesc::Make(TEXT("output"), shape, TensorType));
			}
		}

		static FString GetTestSuffix(const Json::FTestConfigDataset& Dataset)
		{
			//Build TestSuffix "<inputshape0>_...=><outputshape0>_..."
			bool bIsFirstShape = true;
			FString TestSuffix;
			for (const Json::FTestConfigTensor& Input : Dataset.Inputs)
			{
				if (!bIsFirstShape) TestSuffix += TEXT("_");
				TestSuffix += ShapeToString(Input.Shape);
				bIsFirstShape = false;
			}
			TestSuffix += TEXT("=>");
			//If output is not defined it is the first input shape.
			if (Dataset.Outputs.Num() == 0)
			{
				TestSuffix += ShapeToString(Dataset.Inputs[0].Shape);
			}
			else
			{
				for (const Json::FTestConfigTensor& Output : Dataset.Outputs)
				{
					TestSuffix += TEXT("_");
					TestSuffix += ShapeToString(Output.Shape);
				}
			}
			return TestSuffix;
		}
		
		void AddTestFromCategory(const FString& BaseTestPath, const TArray<Json::FTestCategory>& TestCategories, const TArray<Json::FTestConfigInputOutputSet> InputOutputSets)
		{
			for (const Json::FTestCategory& TestCategory : TestCategories)
			{
				if (TestCategory.Skip)
				{
					continue;
				}

				const bool bIsModelCategory = TestCategory.IsModelTest;
				const FString TestCategoryPath(BaseTestPath + TestCategory.Category + TEXT("."));
				
				for (const Json::FTestConfigTarget& TestTarget : TestCategory.Targets)
				{
					if (TestTarget.Skip)
					{
						continue;
					}

					const FString& TestBaseName = TestTarget.Target;
					EMLTensorDataType InputTypeFromTarget = GetTensorTypeFromJson(TestTarget.InputType, EMLTensorDataType::Float);
					EMLTensorDataType OutputTypeFromTarget = GetTensorTypeFromJson(TestTarget.InputType, EMLTensorDataType::Float);
					bool bAtLeastATestWasAdded = false;
					
					for (const Json::FTestConfigInputOutputSet& InputOutputSet : InputOutputSets)
					{
						//If category is a substring of InputOutputSet name or if target explicitly requested the InputOutputSet name
						if (InputOutputSet.Name.Contains(TestCategory.Category) || TestTarget.AdditionalDatasets.Contains(InputOutputSet.Name))
						{
							for (const Json::FTestConfigDataset& Dataset : InputOutputSet.Datasets)
							{
								if (Dataset.Inputs.Num() == 0)
								{
									continue;
								}

								FTests::FTestSetup& Test = AddTest(TestCategoryPath, TestBaseName, TEXT(".") + GetTestSuffix(Dataset));
								
								ApplyTargetConfig(Test, TestTarget);
								ApplyDatasetConfig(Test, Dataset, InputTypeFromTarget, OutputTypeFromTarget);
								Test.IsModelTest = bIsModelCategory;
							}
							bAtLeastATestWasAdded = true;
						}
					}

					//No dataset were matched with this target, define a test without enforcing input/output.
					if (!bAtLeastATestWasAdded)
					{
						FTests::FTestSetup& Test = AddTest(TestCategoryPath, TestBaseName, TEXT(""));
						
						ApplyTargetConfig(Test, TestTarget);
						Test.IsModelTest = bIsModelCategory;
					}
				}
			}
		}
	};
	static FParametricTests ParametricTests;

	static FString GetFullModelPath(const FString& ModelName)
	{
		//Note: This mean model tests can only run in the context of the current projects (example: NNXIncubator)
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("OnnxModels") / ModelName);
	}
	
	static bool RunParametricTest(FTests::FTestSetup& TestSetup, const FString& RuntimeFilter)
	{
		TArray<uint8> ModelData;

		if (TestSetup.IsModelTest)
		{
			// Model test, load model from disk
			FString ModelPath = GetFullModelPath(TestSetup.TargetName + TEXT(".onnx"));
			const bool bIsModelInMem = FFileHelper::LoadFileToArray(ModelData, *ModelPath);
			
			if (!bIsModelInMem)
			{
				UE_LOG(LogNNX, Error, TEXT("Can't load model from disk at path '%s'. Tests ABORTED!"), *ModelPath);
				return false;
			}
		}
		else
		{
			// Operator test, create model in memory
			if (!CreateONNXModelForOperator(TestSetup.TargetName, TestSetup.Inputs, TestSetup.Outputs, ModelData))
			{
				UE_LOG(LogNNX, Error, TEXT("Failed to create model for test '%s'. Test ABORTED!"), *TestSetup.TargetName);
				return false;
			}
		}

		return CompareONNXModelInferenceAcrossRuntimes(TestSetup.TestName, ModelData, &TestSetup, RuntimeFilter);
	}

	static FString AutomationRuntimeFilter;
	void SetAutomationRuntimeFilter(const FString& InRuntimeFilter)
	{
		AutomationRuntimeFilter = InRuntimeFilter;
	}

	static FAutoConsoleCommand SetAutomationRuntimeFilterCommand(
		TEXT("nnx.test.parametric.setautomationfilter"), TEXT("Set the RuntimeFilter witch automation will use, no parameter to run on all runtime (this is the default)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				if (Args.Num() > 0)
				{
					SetAutomationRuntimeFilter(Args[0]);
				}
				else
				{
					SetAutomationRuntimeFilter(TEXT(""));
				}
			}
		)
	);
	
	bool RunParametricTests(const FString& NameSubstring, const FString& Tag, const FString& InRuntimeFilter)
	{
		bool bAllTestSucceed = true;
		uint32 NumTest = 0;
		uint32 NumTestFailed = 0;
		
		for (FTests::FTestSetup& Test : ParametricTests.TestSetups)
		{
			if (!Tag.IsEmpty() && !Test.Tags.Contains(Tag))
			{
				continue;
			}
			if (!NameSubstring.IsEmpty() && !Test.TestName.Contains(NameSubstring))
			{
				continue;
			}

			++NumTest;
			if (!RunParametricTest(Test, InRuntimeFilter))
			{
				++NumTestFailed;
			}
		}

		if (NumTest == 0)
		{
			UE_LOG(LogNNX, Display, TEXT("No test selected to run (on %d parametric tests registered)."), ParametricTests.TestSetups.Num());
			return true;
		}
		else if (NumTestFailed == 0)
		{
			UE_LOG(LogNNX, Display, TEXT("SUCCEED! All %d tests selected passed (%d tests are registered)."), NumTest, ParametricTests.TestSetups.Num());
			return true;
		}
		else
		{
			UE_LOG(LogNNX, Error, TEXT("FAILED! %d test(s) failed, on the %d test selected to run (%d tests are registered)."), NumTestFailed, NumTest, ParametricTests.TestSetups.Num());
			return false;
		}
	}
	
	static FString FindArg(const FString& ArgName, const TArray< FString >& Args)
	{
		FString Arg(TEXT(""));
		int32 ArgNameIndex = Args.Find(ArgName);
		
		if (ArgNameIndex == INDEX_NONE || ArgNameIndex + 1 >= Args.Num())
		{
			return Arg;
		}
		else
		{
			return Args[ArgNameIndex + 1];
		}
	}

	static FAutoConsoleCommand RunTestCommand(
		TEXT("nnx.test.parametric.run"), TEXT("Run all parametric tests. Use -name to filter by name (substring). Use -tag to filter by tag. Use -runtime to only run for the provided runtime."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				FString Name = FindArg(TEXT("-name"), Args);
				FString Tag = FindArg(TEXT("-tag"), Args);
				FString Runtime = FindArg(TEXT("-runtime"), Args);
				RunParametricTests(Name, Tag, Runtime);
			}
		)
	);

#if WITH_DEV_AUTOMATION_TESTS
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST_PRIVATE(FNNXParametricTestBase, FAutomationTestBase, "NNXParametricTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::FeatureMask | EAutomationTestFlags::EngineFilter, __FILE__, __LINE__)
	bool FNNXParametricTestBase::RunTest(const FString& Parameters) { return false; }

	class FNNXParametricTest : public FNNXParametricTestBase
	{
		FTests::FTestSetup Test;

	public:
		FNNXParametricTest(const FTests::FTestSetup& InTest) : FNNXParametricTestBase(InTest.TestName), Test(InTest) {}
		virtual ~FNNXParametricTest() {}
		virtual FString GetTestSourceFileName() const override { return "From Json"; }//TODO return source json file path
		virtual int32 GetTestSourceFileLine() const override { return 0; }
	protected:
		virtual FString GetBeautifiedTestName() const override { return Test.TestName; }
		bool RunTest(const FString& Parameter)
		{
			return RunParametricTest(Test, AutomationRuntimeFilter);
		}
	};

	class FParametricTestAutomationRegistry
	{
	
		TArray<FNNXParametricTest*> RegisteredTests;

	public:
		FParametricTestAutomationRegistry()
		{
			Refresh();
		}
		
		~FParametricTestAutomationRegistry()
		{
			Clear();
		}

		void Clear()
		{
			for (FNNXParametricTest* Test : RegisteredTests)
			{
				delete Test;
			}
			RegisteredTests.Empty();
		}

		void Refresh()
		{
			Clear();

			for (const FTests::FTestSetup& Test : ParametricTests.TestSetups)
			{
				RegisteredTests.Emplace(new FNNXParametricTest(Test));
			}
		}
	};

	static FParametricTestAutomationRegistry ParametricTestAutomationRegistry;

#endif //WITH_DEV_AUTOMATION_TESTS

	static FAutoConsoleCommand TestReloadCommand(
		TEXT("nnx.test.parametric.reload"), TEXT("Reload NNX parametric tests definition from Json."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				ParametricTests.ReloadTestDescriptionsFromJson();
				
				#if WITH_DEV_AUTOMATION_TESTS
					ParametricTestAutomationRegistry.Refresh();
				#endif
			}
		)
	);

} // namespace Test
} // namespace NNX
