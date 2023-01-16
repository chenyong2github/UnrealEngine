// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQAParametricTest.h"

#include "NNXCore.h"
#include "NNECoreTypes.h"
#include "NNXRuntimeFormat.h"
#include "NNECoreAttributeMap.h"
#include "NNXQAUtils.h"
#include "NNXQAJsonUtils.h"
#include "NNXModelBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/ReflectedTypeAccessors.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace NNX 
{
using FSymbolicTensorShape = UE::NNECore::FSymbolicTensorShape;
	
namespace Test
{
	class FParametricTests : public FTests
	{
	public:
		FParametricTests()
		{
			
		}

		bool ReloadTestDescriptionsFromJson()
		{
			TestSetups.Empty();

			TSharedPtr<IPlugin> NNEPlugin = IPluginManager::Get().FindPlugin(TEXT("NNE"));
			
			if (!NNEPlugin.IsValid())
			{
				UE_LOG(LogNNX, Error, TEXT("Unable to find NNE plugin!"));
				return false;
			}

			//TODO verify the path can be accessed on standalone build and on consoles
			//TODO allow to define the tests in more than one json file
			FString NNEPluginBaseDir = NNEPlugin->GetBaseDir();
			FString FullPath(NNEPluginBaseDir + TEXT("\\Source\\NNXQA\\Resources\\NNXQATestDesc.json"));

			TArray<Json::FTestCategory> ModelTestCategories;
			TArray<Json::FTestCategory> OperatorTestCategories;
			TArray<Json::FTestConfigInputOutputSet> InputOutputSets;
			TArray<Json::FTestAttributeSet> AttributeSets;
			
			if (!Json::LoadTestDescriptionFromJson(FullPath, ModelTestCategories, OperatorTestCategories, InputOutputSets, AttributeSets))
			{
				return false;
			}

			const FString NNEBaseTestPath(TEXT("System.Engine.MachineLearning.NNE"));
			AddTestFromCategory(NNEBaseTestPath + TEXT(".Model."), ModelTestCategories, InputOutputSets, AttributeSets);
			AddTestFromCategory(NNEBaseTestPath + TEXT(".Operator."), OperatorTestCategories, InputOutputSets, AttributeSets);

			return true;
		}
	private:
		
		static void ApplyEpsilons(FTests::FTestSetup& TestSetup, const Json::FTestConfigTarget& TestTarget)
		{
			if (TestTarget.AbsoluteTolerance != Json::JSON_TOLERANCE_NOTSET)
			{
				TestSetup.AbsoluteTolerance = TestTarget.AbsoluteTolerance;
			}
			
			if (TestTarget.RelativeTolerance != Json::JSON_TOLERANCE_NOTSET)
			{
				TestSetup.RelativeTolerance = TestTarget.RelativeTolerance;
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
					if (Runtime.AbsoluteTolerance != Json::JSON_TOLERANCE_NOTSET)
					{
						TestSetup.AbsoluteToleranceForRuntime.Emplace(Runtime.Name, Runtime.AbsoluteTolerance);
					}
					if (Runtime.RelativeTolerance != Json::JSON_TOLERANCE_NOTSET)
					{
						TestSetup.RelativeToleranceForRuntime.Emplace(Runtime.Name, Runtime.RelativeTolerance);
					}
				}
			}
		}

		static void ApplyTargetConfig(FTests::FTestSetup& TestSetup, const Json::FTestConfigTarget& TestTarget)
		{
			//Epsilons
			ApplyEpsilons(TestSetup, TestTarget);

			ApplyRuntimesConfig(TestSetup, TestTarget.Runtimes);

			//Tags
			TestSetup.Tags = TestTarget.Tags;
		}

		static FTensorShape GetConcreteShapeFromJsonArray(TConstArrayView<int32> JsonShape)
		{
			FSymbolicTensorShape SymbolicShape = FSymbolicTensorShape::Make(JsonShape);
			check(SymbolicShape.IsConcrete());
			FTensorShape ConcreteShape = FTensorShape::MakeFromSymbolic(SymbolicShape);
			return ConcreteShape;
		}

		static ENNETensorDataType GetTensorTypeFromJson(const FString& TypeName, ENNETensorDataType DefaultValue)
		{
			int64 Value = StaticEnum<ENNETensorDataType>()->GetValueByNameString(TypeName);
			
			return (Value == INDEX_NONE) ? DefaultValue : (ENNETensorDataType)Value;
		}

		class ElementWiseFromJsonStringTensorInitializer
		{
			TConstArrayView<FString> JsonValues;
				
		public:
			ElementWiseFromJsonStringTensorInitializer(TConstArrayView<FString> InJsonValues)
				:JsonValues(InJsonValues) 
			{}

			float operator () (uint32 TensorIndex) const 
			{
				const FString& JsonValue = JsonValues[TensorIndex];
				if (!FCString::IsNumeric(*JsonValue))
				{
					UE_LOG(LogNNX, Error, TEXT("Cannot convert %s to float will default to 0.0f, check test config."), *JsonValue);
				}
				return FCString::Atof(*JsonValues[TensorIndex]);
			}
		};

		static TArray<char> GetInputTensorDataFromJson(const FTensor& Tensor, int TensorIndex, TConstArrayView<FString> JsonValues)
		{
			if (JsonValues.Num() == Tensor.GetVolume())
			{
				ElementWiseFromJsonStringTensorInitializer Initializer(JsonValues);
				return GenerateTensorDataForTest(Tensor, Initializer);
			}
			else if (JsonValues.Num() > 0)
			{
				UE_LOG(LogNNX, Error, TEXT("Incorrect number of element for tensor initializer %s, should have %d but got %d. Fallbacking to default initializer."), *Tensor.GetName(), Tensor.GetVolume(), JsonValues.Num());
				
			}
			ElementWiseCosTensorInitializer Initializer(Tensor.GetDataType(), TensorIndex);
			return GenerateTensorDataForTest(Tensor, Initializer);
		}

		static TArray<char> GetOutputTensorDataFromJson(const FTensor& Tensor, int TensorIndex, TConstArrayView<FString> JsonValues)
		{
			if (JsonValues.Num() == Tensor.GetVolume())
			{
				ElementWiseFromJsonStringTensorInitializer Initializer(JsonValues);
				return GenerateTensorDataForTest(Tensor, Initializer);
			}
			else if (JsonValues.Num() > 0)
			{
				UE_LOG(LogNNX, Error, TEXT("Incorrect number of element for tensor initializer %s, should have %d but got %d."), *Tensor.GetName());

			}
			return TArray<char>();
		}

		static void ApplyDatasetConfig(FTests::FTestSetup& TestSetup, const Json::FTestConfigDataset& TestDataset, ENNETensorDataType DefaultInputType, ENNETensorDataType DefaultOutputType)
		{
			ApplyRuntimesConfig(TestSetup, TestDataset.Runtimes);

			if (TestDataset.Inputs.Num() == 0)
			{
				return;
			}

			uint32 i = 0;
			for (auto&& JsonTensor : TestDataset.Inputs)
			{
				FTensorShape Shape = GetConcreteShapeFromJsonArray(JsonTensor.Shape);
				ENNETensorDataType TensorType = GetTensorTypeFromJson(JsonTensor.Type, DefaultInputType);
				FTensor Tensor = FTensor::Make(FString::Printf(TEXT("input%d"), i), Shape, TensorType);
				TArray<char> TensorData = GetInputTensorDataFromJson(Tensor, i, JsonTensor.Source);

				TestSetup.Inputs.Emplace(Tensor);
				TestSetup.InputsData.Emplace(TensorData);
				++i;
			}

			for (auto&& JsonTensor : TestDataset.Weights)
			{
				FTensorShape Shape = GetConcreteShapeFromJsonArray(JsonTensor.Shape);
				ENNETensorDataType TensorType = GetTensorTypeFromJson(JsonTensor.Type, ENNETensorDataType::Float);
				FTensor Tensor = FTensor::Make(FString::Printf(TEXT("weights%d"), i), Shape, TensorType);
				TArray<char> TensorData = GetInputTensorDataFromJson(Tensor, i, JsonTensor.Source);

				TestSetup.Weights.Emplace(Tensor);
				TestSetup.WeightsData.Emplace(TensorData);
				++i;
			}

			for (auto&& JsonTensor : TestDataset.Outputs)
			{
				FTensorShape Shape = GetConcreteShapeFromJsonArray(JsonTensor.Shape);
				ENNETensorDataType TensorType = GetTensorTypeFromJson(JsonTensor.Type, DefaultOutputType);
				FTensor Tensor = FTensor::Make(FString::Printf(TEXT("output%d"), i), Shape, TensorType);
				TArray<char> TensorData = GetOutputTensorDataFromJson(Tensor, i, JsonTensor.Source);

				TestSetup.Outputs.Emplace(Tensor);
				TestSetup.OutputsData.Emplace(TensorData);
				++i;
			}
			//If output is not defined it is the first input shape.
			if (TestDataset.Outputs.Num() == 0 && TestDataset.Inputs.Num() > 0)
			{
				FTensorShape Shape = GetConcreteShapeFromJsonArray(TestDataset.Inputs[0].Shape);
				ENNETensorDataType TensorType = GetTensorTypeFromJson(TestDataset.Inputs[0].Type, DefaultOutputType);
				FTensor Tensor = FTensor::Make(FString::Printf(TEXT("output"), i), Shape, TensorType);
				TArray<char> TensorData = GetOutputTensorDataFromJson(Tensor, i, TestDataset.Inputs[0].Source);
				
				TestSetup.Outputs.Emplace(Tensor);
				TestSetup.OutputsData.Emplace(TensorData);
			}
		}

		static void ApplyAttributeSetConfig(FTests::FTestSetup& TestSetup, const Json::FTestAttributeMap& AttributeMap)
		{
			for (const Json::FTestAttribute &Attribute : AttributeMap.Attributes)
			{
				TestSetup.AttributeMap.SetAttribute(Attribute.Name, Attribute.Value);
			}
		}

		static FString GetTestSuffix(const Json::FTestConfigDataset& Dataset)
		{
			//Build TestSuffix "<inputshape0>_..._w<weightshape0>_w...=><outputshape0>_..."
			bool bIsFirstShape = true;
			FString TestSuffix;
			for (const Json::FTestConfigTensor& Input : Dataset.Inputs)
			{
				if (!bIsFirstShape) TestSuffix += TEXT("_");
				TestSuffix += ShapeToString<int32>(Input.Shape);
				bIsFirstShape = false;
			}
			for (const Json::FTestConfigTensor& Weight : Dataset.Weights)
			{
				TestSuffix += TEXT("_w");
				TestSuffix += ShapeToString<int32>(Weight.Shape);
			}
			TestSuffix += TEXT("=>");
			//If output is not defined it is the first input shape.
			if (Dataset.Outputs.Num() == 0)
			{
				TestSuffix += ShapeToString<int32>(Dataset.Inputs[0].Shape);
			}
			else
			{
				bIsFirstShape = true;
				for (const Json::FTestConfigTensor& Output : Dataset.Outputs)
				{
					if (!bIsFirstShape) TestSuffix += TEXT("_");
					TestSuffix += ShapeToString<int32>(Output.Shape);
					bIsFirstShape = false;
				}
			}
			return TestSuffix;
		}

		static FString GetAttributeMapSuffix(const Json::FTestAttributeMap &AttributeMap)
		{
			if (AttributeMap.Attributes.IsEmpty()) return TEXT("");

			auto AttributeToString = [] (const FNNEAttributeValue &Value) -> FString {
				ENNEAttributeDataType Type = Value.GetType();
				
				switch(Type)
				{
					case ENNEAttributeDataType::Float: return FString::SanitizeFloat(Value.GetValue<float>());
					case ENNEAttributeDataType::Int32: return FString::FromInt(Value.GetValue<int32>());
					case ENNEAttributeDataType::Int32Array:
					{
						TArray<FString> ArrayStrings;
						for (int32 Val : Value.GetValue<TArray<int32>>())
						{
							ArrayStrings.Add(FString::FromInt(Val));
						}

						return TEXT("[") + FString::Join(ArrayStrings, TEXT(",")) + TEXT("]");
					}
					case ENNEAttributeDataType::String: return Value.GetValue<FString>();
				}
				return TEXT("-");
			};
			
			FStringBuilderBase Str;
			Str  << TEXT(".");

			TArray<FString> AttributeStrings;
			for (const Json::FTestAttribute &Attribute : AttributeMap.Attributes)
			{
				AttributeStrings.Push(Attribute.Name + "=" + AttributeToString(Attribute.Value));
			}

			Str  << FString::Join(AttributeStrings, TEXT(","));

			return Str.ToString();
		}

		static bool IsSubstringFoundInArray(const TArray<FString>& Names, const FString& SubString)
		{
			for (auto&& Name : Names)
			{
				if (Name.Contains(SubString))
				{
					return true;
				}
			}
			return false;
		}
		
		void AddTestFromCategory(const FString& BaseTestPath, const TArray<Json::FTestCategory>& TestCategories, const TArray<Json::FTestConfigInputOutputSet>& InputOutputSets,
				const TArray<Json::FTestAttributeSet>& AttributeSets)
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
					ENNETensorDataType InputTypeFromTarget = GetTensorTypeFromJson(TestTarget.InputType, ENNETensorDataType::Float);
					ENNETensorDataType OutputTypeFromTarget = GetTensorTypeFromJson(TestTarget.OutputType, ENNETensorDataType::Float);
					bool bAtLeastATestWasAdded = false;
					
					for (const Json::FTestConfigInputOutputSet& InputOutputSet : InputOutputSets)
					{
						//Accept test if category is a substring of the dataset name or one of the explicitly requested dataset names
						if (InputOutputSet.Name.Contains(TestCategory.Category) || 
							IsSubstringFoundInArray(TestTarget.AdditionalDatasets, InputOutputSet.Name) ||
							IsSubstringFoundInArray(TestCategory.AdditionalDatasets, InputOutputSet.Name))
						{
							//Reject test if dataset is explicitly rejected
							if (IsSubstringFoundInArray(TestTarget.RemovedDatasets, InputOutputSet.Name) || 
								IsSubstringFoundInArray(TestCategory.RemovedDatasets, InputOutputSet.Name))
							{
								continue;
							}
							
							for (const Json::FTestConfigDataset& Dataset : InputOutputSet.Datasets)
							{
								if (Dataset.Inputs.Num() == 0)
								{
									continue;
								}

								bool bAtLeastAnAttributeTestWasWadded = false;
								for (const Json::FTestAttributeSet &AttributeSet : AttributeSets)
								{
									// TODO may be split by '.' and (partially) match parts
									if (!AttributeSet.Name.Contains(InputOutputSet.Name))
									{
										continue;
									}

									for (const Json::FTestAttributeMap &AttributeMap : AttributeSet.AttributeMaps)
									{
										bool bAtLeastAnotherAttributeSetAdded = false;
										for (const FString &OtherAttributeSetName : AttributeSet.MultiplyWithAttributeSets)
										{
											for (const Json::FTestAttributeSet &OtherAttributeSet : AttributeSets)
											{
												if (OtherAttributeSet.Name != OtherAttributeSetName)
												{
													continue;
												}

												for (const Json::FTestAttributeMap &OtherAttributeMap : OtherAttributeSet.AttributeMaps)
												{
													FTests::FTestSetup& Test = AddTest(TestCategoryPath, TestBaseName, TEXT(".") + GetTestSuffix(Dataset) + GetAttributeMapSuffix(AttributeMap) + GetAttributeMapSuffix(OtherAttributeMap));
										
													ApplyRuntimesConfig(Test, TestCategory.Runtimes);
													ApplyRuntimesConfig(Test, InputOutputSet.Runtimes);
													ApplyTargetConfig(Test, TestTarget);
													ApplyDatasetConfig(Test, Dataset, InputTypeFromTarget, OutputTypeFromTarget);

													ApplyAttributeSetConfig(Test, AttributeMap);
													ApplyAttributeSetConfig(Test, OtherAttributeMap);

													Test.IsModelTest = bIsModelCategory;

													bAtLeastAnAttributeTestWasWadded = true;

													bAtLeastAnotherAttributeSetAdded = true;
												}
											}
										}

										if (!bAtLeastAnotherAttributeSetAdded)
										{
											FTests::FTestSetup& Test = AddTest(TestCategoryPath, TestBaseName, TEXT(".") + GetTestSuffix(Dataset) + GetAttributeMapSuffix(AttributeMap));
											
											ApplyRuntimesConfig(Test, TestCategory.Runtimes);
											ApplyRuntimesConfig(Test, InputOutputSet.Runtimes);
											ApplyTargetConfig(Test, TestTarget);
											ApplyDatasetConfig(Test, Dataset, InputTypeFromTarget, OutputTypeFromTarget);

											ApplyAttributeSetConfig(Test, AttributeMap);

											Test.IsModelTest = bIsModelCategory;

											bAtLeastAnAttributeTestWasWadded = true;
										}
									}
								}

								if (!bAtLeastAnAttributeTestWasWadded)
								{
								FTests::FTestSetup& Test = AddTest(TestCategoryPath, TestBaseName, TEXT(".") + GetTestSuffix(Dataset));
								
								ApplyRuntimesConfig(Test, TestCategory.Runtimes);
								ApplyRuntimesConfig(Test, InputOutputSet.Runtimes);
								ApplyTargetConfig(Test, TestTarget);
								ApplyDatasetConfig(Test, Dataset, InputTypeFromTarget, OutputTypeFromTarget);
								Test.IsModelTest = bIsModelCategory;
							}
							}
							bAtLeastATestWasAdded = true;
						}
					}

					//No dataset were matched with this target, define a test without enforcing input/output.
					if (!bAtLeastATestWasAdded)
					{
						FTests::FTestSetup& Test = AddTest(TestCategoryPath, TestBaseName, TEXT(""));
						
						ApplyRuntimesConfig(Test, TestCategory.Runtimes);
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
		FNNIModelRaw ONNXModel;
		FNNIModelRaw ONNXModelVariadic;
		ONNXModelVariadic.Format = ENNXInferenceFormat::Invalid;

		if (TestSetup.IsModelTest)
		{
			// Model test, load model from disk
			FString ModelPath = GetFullModelPath(TestSetup.TargetName + TEXT(".onnx"));
			const bool bIsModelInMem = FFileHelper::LoadFileToArray(ONNXModel.Data, *ModelPath);
			if (!bIsModelInMem)
			{
				UE_LOG(LogNNX, Error, TEXT("Can't load model from disk at path '%s'. Tests ABORTED!"), *ModelPath);
				return false;
			}
			ONNXModel.Format = ENNXInferenceFormat::ONNX;
		}
		else
		{
			// Operator test, create model in memory
			if (!CreateONNXModelForOperator(false, TestSetup.TargetName, TestSetup.Inputs, TestSetup.Outputs, TestSetup.Weights, TestSetup.WeightsData,TestSetup.AttributeMap, ONNXModel))
			{
				UE_LOG(LogNNX, Error, TEXT("Failed to create static model for test '%s'. Test ABORTED!"), *TestSetup.TargetName);
				return false;
			}
			if (!CreateONNXModelForOperator(true, TestSetup.TargetName, TestSetup.Inputs, TestSetup.Outputs, TestSetup.Weights, TestSetup.WeightsData, TestSetup.AttributeMap, ONNXModelVariadic))
			{
				UE_LOG(LogNNX, Error, TEXT("Failed to create variadic model for test '%s'. Test ABORTED!"), *TestSetup.TargetName);
				return false;
			}

            //FFileHelper::SaveArrayToFile(ONNXModel.Data, TEXT("D:\\Opmodel.onnx"));
            //FFileHelper::SaveArrayToFile(ONNXModelVariadic.Data, TEXT("D:\\OpmodelVar.onnx"));
		}

		return CompareONNXModelInferenceAcrossRuntimes(ONNXModel, ONNXModelVariadic, TestSetup, RuntimeFilter);
	}

	static FString AutomationRuntimeFilter;
	void SetAutomationRuntimeFilter(const FString& InRuntimeFilter)
	{
		AutomationRuntimeFilter = InRuntimeFilter;
	}

	static FAutoConsoleCommand SetAutomationRuntimeFilterCommand(
		TEXT("nnx.test.setautomationfilter"), TEXT("Set the RuntimeFilter witch automation will use, no parameter to run on all runtime (this is the default)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				AutomationRuntimeFilter.Empty();
				for (auto&& Arg : Args)
				{
					AutomationRuntimeFilter += Arg;
					AutomationRuntimeFilter += TEXT(" ");
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
			UE_LOG(LogNNX, Display, TEXT("No test selected to run (on %d tests registered)."), ParametricTests.TestSetups.Num());
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
		TEXT("nnx.test.run"), TEXT("Run all tests. Use -name to filter by name (substring). Use -tag to filter by tag. Use -runtime to only run for the provided runtime, default is to use filter set from setruntimefilter command."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				FString Name = FindArg(TEXT("-name"), Args);
				FString Tag = FindArg(TEXT("-tag"), Args);
				FString Runtime = FindArg(TEXT("-runtime"), Args);
				double StartTime = FPlatformTime::Seconds();
				bool bTestSucceeded = RunParametricTests(Name, Tag, Runtime);
				double EndTime = FPlatformTime::Seconds();
				double TimeForTest = static_cast<float>(EndTime - StartTime);
				if (bTestSucceeded)
				{ 
					UE_LOG(LogNNX, Display, TEXT("Tests succeeded in %0.1f seconds."), TimeForTest);
				}
				else
				{
					UE_LOG(LogNNX, Warning, TEXT("Tests FAILED in %0.1f seconds."), TimeForTest);
				}
			}
		)
	);

	static FAutoConsoleCommand RunSmokeTestCommand(
		TEXT("nnx.test.smokerun"), TEXT("Run all smoke tests. Use -name to additionaly filter by name. Use -runtime to only run for the provided runtime, default is to run on all runtime but NNXRuntimeCPU (too slow at the moment, see comment in code)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(
			[](const TArray< FString >& Args)
			{
				FString Name = FindArg(TEXT("-name"), Args);
				FString Runtime = FindArg(TEXT("-runtime"), Args);
				if (Runtime.IsEmpty())
				{
					//NNXRuntimeCPU tests are currently slow due to a synchronization delay on thread creation/destruction of the ORT session.
					//We want very fast smoke test, thus by default we do NOT run for NNXRuntimeCPU for smoke tests.
					Runtime = TEXT("NNXRuntimeORTDml NNXRuntimeORTCuda NNXRuntimeHlsl NNXRuntimeDml");
				}
				double StartTime = FPlatformTime::Seconds();
				bool bTestSucceeded = RunParametricTests(Name, TEXT("smoketest"), Runtime);
				double EndTime = FPlatformTime::Seconds();
				double TimeForTest = static_cast<float>(EndTime - StartTime);
				if (bTestSucceeded)
				{
					UE_LOG(LogNNX, Display, TEXT("Smoke tests succeeded in %0.1f seconds."), TimeForTest);
				}
				else
				{
					UE_LOG(LogNNX, Warning, TEXT("Smoke tests FAILED in %0.1f seconds."), TimeForTest);
				}
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
		TEXT("nnx.test.reload"), TEXT("Reload NNX tests definition from Json."),
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

	bool InitializeParametricTests()
	{
		bool bResult = ParametricTests.ReloadTestDescriptionsFromJson();
#if WITH_DEV_AUTOMATION_TESTS
		ParametricTestAutomationRegistry.Refresh();
#endif
		return bResult;
	}

} // namespace Test
} // namespace NNX
