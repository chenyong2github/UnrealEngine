// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXQAUtils.h"
#include "HAL/UnrealMemory.h"
#include "Kismet/GameplayStatics.h"
#include "NNECoreAttributeMap.h"
#include "NNXCore.h"
#include "NNXInferenceModel.h"
#include "NNXModelOptimizerInterface.h"

#include <functional>

namespace NNX 
{
namespace Test 
{
	static void FillTensors(
		TConstArrayView<FTensor> TensorsFromTestSetup,
		TConstArrayView<FTensorDesc> TensorDescsFromModel,
		TArray<FTensor>& OutTensors)
	{
		OutTensors = TensorsFromTestSetup;
		if (OutTensors.IsEmpty())
		{
			for (const FTensorDesc& SymbolicTensorDesc : TensorDescsFromModel)
			{
				FTensor TensorDesc = FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc);
				OutTensors.Emplace(TensorDesc);
			}
		}
	}
	static void FillTensorBindings(TConstArrayView<FTensor> TensorDescs,
		TArray<TArray<char>>& OutMemBuffers, TArray<FMLTensorBinding>& OutBindings,
        std::function<float(EMLTensorDataType, uint32, uint32)> Initializer)
	{
		OutMemBuffers.Empty();
		OutBindings.Empty();

		for (int Index = 0; Index < TensorDescs.Num(); ++Index)
		{
			const FTensor& TensorDesc = TensorDescs[Index];
			const uint32 NumberOfElements = TensorDesc.GetVolume();
			const int32 ElementByteSize = TensorDesc.GetElemByteSize();
			const uint64 BufferSize = TensorDesc.GetDataSize();
			const EMLTensorDataType DataType = TensorDesc.GetDataType();

			TArray<char> CurInputBuffer;
			CurInputBuffer.SetNum(BufferSize);
			char* DestinationPtr = CurInputBuffer.GetData();
			for (uint32 i = 0; i != NumberOfElements; ++i)
			{
				const float FloatData = Initializer(DataType, i, Index);
				const int32 IntData = (int32)FloatData;
				const uint32 UIntData = (uint32)FloatData;
				const char BoolData = FloatData != 0.0f ? 1 : 0;
				switch (DataType)
				{
					case EMLTensorDataType::Float:
						check(sizeof(FloatData) == ElementByteSize);
						FMemory::Memcpy(DestinationPtr, &FloatData, ElementByteSize); break;
					case EMLTensorDataType::Int32:
						check(sizeof(IntData) == ElementByteSize);
						FMemory::Memcpy(DestinationPtr, &IntData, ElementByteSize); break;
					case EMLTensorDataType::UInt32:
						check(sizeof(UIntData) == ElementByteSize);
						FMemory::Memcpy(DestinationPtr, &UIntData, ElementByteSize); break;
					case EMLTensorDataType::Boolean:
						check(sizeof(BoolData) == ElementByteSize);
						FMemory::Memcpy(DestinationPtr, &BoolData, ElementByteSize); break;
					default:
						//TODO handle more TensorData types
						FMemory::Memzero(DestinationPtr, ElementByteSize);
				}
				DestinationPtr += ElementByteSize;
			}

			OutMemBuffers.Emplace(CurInputBuffer);
			OutBindings.Emplace(
				FMLTensorBinding::FromCPU(OutMemBuffers[Index].GetData(), BufferSize)
			);
		}

		check(TensorDescs.Num() == OutMemBuffers.Num());
		check(TensorDescs.Num() == OutBindings.Num());
	}

	template<typename T> FString ShapeToString(TArrayView<const T> Shape)
	{
		FString TestSuffix(TEXT("["));
		bool bIsFirstDim = true;
		for (T size : Shape)
		{
			if (!bIsFirstDim) TestSuffix += TEXT(",");
			TestSuffix += FString::Printf(TEXT("%d"), size);
			bIsFirstDim = false;
		}
		TestSuffix += TEXT("]");
		return TestSuffix;
	}
	template FString ShapeToString<int32>(TArrayView<const int32> Shape);
	template FString ShapeToString<uint32>(TArrayView<const uint32> Shape);

	FString FMLTensorDescToString(const FTensor& desc)
	{
		TArrayView<const uint32> Shape(desc.GetShape().Data);

		FString TensorDesc;
		TensorDesc.Reserve(50);
		TensorDesc += FString::Printf(TEXT("Name: %s, Shape: "), *desc.GetName());
		TensorDesc += ShapeToString(Shape);
		const FString& DataTypeName = StaticEnum<EMLTensorDataType>()->GetNameStringByValue(static_cast<int64>(desc.GetDataType()));
		TensorDesc += FString::Printf(TEXT(" DataType: %s"), *DataTypeName);

		return TensorDesc;
	}

	FString TensorToString(const FTensor& TensorDesc, const TArray<char>& TensorData)
	{
		FString TensorLog;
		TensorLog.Reserve(50);

		TensorLog += FMLTensorDescToString(TensorDesc);
		TensorLog += TEXT(", Data: ");

		const constexpr uint32 MAX_DATA_TO_LOG = 10;
		const uint32 MaxIndex = FMath::Min(MAX_DATA_TO_LOG, TensorDesc.GetVolume());
        const uint32 ElementByteSize = TensorDesc.GetElemByteSize();
		for (uint32 i = 0; i < MaxIndex; ++i)
		{
			const uint32 ByteOffset = i * ElementByteSize;
			const char* Data = TensorData.GetData() + ByteOffset;
			check((int32)ByteOffset <= TensorData.Num());

			switch (TensorDesc.GetDataType())
			{
				case EMLTensorDataType::Float:
					TensorLog += FString::Printf(TEXT("%0.2f"), *(float*)Data); break;
				case EMLTensorDataType::Int32:
					TensorLog += FString::Printf(TEXT("%d"), *(int32*)Data); break;
				case EMLTensorDataType::UInt32:
					TensorLog += FString::Printf(TEXT("%u"), *(uint32*)Data); break;
				case EMLTensorDataType::Boolean:
					check(ElementByteSize == 1);
					TensorLog += FString::Printf(TEXT("%s"), *(bool*)Data ? "true" : "false"); break;
				default:
					TensorLog += TEXT("?");
			}
			if (i < MaxIndex)
				TensorLog += TEXT(", ");
		}
		if (MaxIndex < TensorDesc.GetVolume())
			TensorLog += TEXT(",...");

		return TensorLog;
	}

	template<typename T> bool CompareTensorData(
		const FTensor& RefTensorDesc,   const TArray<char>& RefRawBuffer,
		const FTensor& OtherTensorDesc, const TArray<char>& OtherRawBuffer,
		float AbsoluteErrorEpsilon, float RelativeErrorPercent)
	{
		const T* RefBuffer = (T*)RefRawBuffer.GetData();
		const T* OtherBuffer = (T*)OtherRawBuffer.GetData();
		
		const uint32 Volume = RefTensorDesc.GetVolume();
		const uint32 ElementByteSize = RefTensorDesc.GetElemByteSize();

		check(Volume == OtherTensorDesc.GetVolume());
		check(Volume * ElementByteSize == RefRawBuffer.Num());
		check(Volume * ElementByteSize == OtherRawBuffer.Num());

		bool bTensorMemMatch = true;

		float WorstAbsoluteError = 0.0f;
		int32 WorstAbsoluteErrorIndex = -1;
		float WorstAbsoluteErrorRef = 0.0f;
		float WorstAbsoluteErrorOther = 0.0f;

		float WorstRelativeError = 0.0f;
		int32 WorstRelativeErrorIndex = -1;
		float WorstRelativeErrorRef = 0.0f;
		float WorstRelativeErrorOther = 0.0f;

		int32 NumExtraNaNsInResults = 0;
		int32 FirstExtraNaNIndex = -1;
		int32 NumMissingNaNsInResults = 0;
		int32 FirstMissingNaNIndex = -1;

		for (uint32 i = 0; i < Volume; ++i)
		{
			//All type are converted to float for comparison purpose
			const float Result = (float)OtherBuffer[i];
			const float Reference = (float)RefBuffer[i];

			
			if (FMath::IsNaN(Result) && !FMath::IsNaN(Reference))
			{
				bTensorMemMatch = false;
				++NumExtraNaNsInResults;
				if (FirstExtraNaNIndex == -1)
				{
					FirstExtraNaNIndex = i;
				}
			}
			if (!FMath::IsNaN(Result) && FMath::IsNaN(Reference))
			{
				bTensorMemMatch = false;
				++NumMissingNaNsInResults;
				if (FirstMissingNaNIndex == -1)
				{
					FirstMissingNaNIndex = i;
				}
			}
			
			if (FMath::IsNaN(Result) || FMath::IsNaN(Reference))
			{
				continue;
			}

			const float AbsoluteError = FMath::Abs<float>(Result - Reference);
			const float RelativeError = 100.0f * (AbsoluteError / FMath::Abs<float>(Reference));
			if (AbsoluteError > AbsoluteErrorEpsilon || RelativeError > RelativeErrorPercent)
			{
				bTensorMemMatch = false;
				if (AbsoluteError > WorstAbsoluteError)
				{
					WorstAbsoluteError = AbsoluteError;
					WorstAbsoluteErrorIndex = i;
					WorstAbsoluteErrorRef = Reference;
					WorstAbsoluteErrorOther = Result;
				}
				if (RelativeError > WorstRelativeError)
				{
					WorstRelativeError = RelativeError;
					WorstRelativeErrorIndex = i;
					WorstRelativeErrorRef = Reference;
					WorstRelativeErrorOther = Result;
				}
			}
		}

		if (!bTensorMemMatch)
		{
			UE_LOG(LogNNX, Error, TEXT("Tensor data do not match.\n"
				"LogNNX: Worst absolute error %f (epsilon %f) at position %d, got %f expected %f\n"
				"LogNNX: Worst relative error % f % %(epsilon % f%%) at position % d, got % f expected % f\n"
				"LogNNX: Num unexpected NaNs %d (First at index %d), Num missing NaNs %d (first at index %d)"),
				WorstAbsoluteError, AbsoluteErrorEpsilon, WorstAbsoluteErrorIndex, WorstAbsoluteErrorOther, WorstAbsoluteErrorRef,
				WorstRelativeError, RelativeErrorPercent, WorstRelativeErrorIndex, WorstRelativeErrorOther, WorstRelativeErrorRef,
				NumExtraNaNsInResults, FirstExtraNaNIndex, NumMissingNaNsInResults, FirstMissingNaNIndex);
			UE_LOG(LogNNX, Error, TEXT("   Expected : %s"), *TensorToString(RefTensorDesc, RefRawBuffer));
			UE_LOG(LogNNX, Error, TEXT("   But got  : %s"), *TensorToString(OtherTensorDesc, OtherRawBuffer));
			return false;
		}

		return true;
	}

	bool VerifyTensorResult(
		const FTensor& RefTensorDesc, const TArray<char>& RefRawBuffer,
		const FTensor& OtherTensorDesc, const TArray<char>& OtherRawBuffer,
		float AbsoluteErrorEpsilon, float RelativeErrorPercent)
	{
		bool bTensorDescMatch = true;
		bTensorDescMatch &= (RefTensorDesc.GetName() == OtherTensorDesc.GetName());
		bTensorDescMatch &= (RefTensorDesc.GetShape().Rank() == OtherTensorDesc.GetShape().Rank());
		bTensorDescMatch &= (RefTensorDesc.GetDataType() == OtherTensorDesc.GetDataType());
		check(RefTensorDesc.GetShape().Rank() <= FTensorShape::MaxRank);
		for (int32 i = 0; i < RefTensorDesc.GetShape().Rank(); ++i)
		{
			bTensorDescMatch &= (RefTensorDesc.GetShape().Data[i] == OtherTensorDesc.GetShape().Data[i]);
		}

		if (!bTensorDescMatch)
		{
			UE_LOG(LogNNX, Error, TEXT("Tensor desc do not match.\nExpected: %s\nGot:      %s"), *FMLTensorDescToString(RefTensorDesc), *FMLTensorDescToString(OtherTensorDesc));
			return false;
		}

		if (RefTensorDesc.GetDataType() == EMLTensorDataType::Float)
		{
			return CompareTensorData<float>(RefTensorDesc, RefRawBuffer, OtherTensorDesc, OtherRawBuffer, AbsoluteErrorEpsilon, RelativeErrorPercent);
		}
		else if (RefTensorDesc.GetDataType() == EMLTensorDataType::Boolean)
		{
			check(RefTensorDesc.GetElemByteSize() == 1);
			return CompareTensorData<bool>(RefTensorDesc, RefRawBuffer, OtherTensorDesc, OtherRawBuffer, AbsoluteErrorEpsilon, RelativeErrorPercent);
		}
		else if (RefTensorDesc.GetDataType() == EMLTensorDataType::Int32)
		{
			return CompareTensorData<int32>(RefTensorDesc, RefRawBuffer, OtherTensorDesc, OtherRawBuffer, AbsoluteErrorEpsilon, RelativeErrorPercent);
		}
		else if (RefTensorDesc.GetDataType() == EMLTensorDataType::UInt32)
		{
			return CompareTensorData<uint32>(RefTensorDesc, RefRawBuffer, OtherTensorDesc, OtherRawBuffer, AbsoluteErrorEpsilon, RelativeErrorPercent);
		}
		else
		{
			UE_LOG(LogNNX, Error, TEXT("Tensor comparison for this type of tensor not implemented"));
			return false;
		}

		return true;
	}

	static float InputTensorInitializer(EMLTensorDataType DataType, uint32 ElementIndex, uint32 TensorIndex)
	{
		const constexpr uint32 IndexOffsetBetweenTensor = 9;
		switch(DataType)
		{
			
			case EMLTensorDataType::Boolean:
				return (ElementIndex + IndexOffsetBetweenTensor * TensorIndex) % 2;
			case EMLTensorDataType::Char:
			case EMLTensorDataType::Int8:
			case EMLTensorDataType::Int16:
			case EMLTensorDataType::Int32:
			case EMLTensorDataType::Int64:
				return 10.0f * FMath::Cos((float)(ElementIndex + IndexOffsetBetweenTensor * TensorIndex));
			case EMLTensorDataType::UInt8:
			case EMLTensorDataType::UInt16:
			case EMLTensorDataType::UInt32:
			case EMLTensorDataType::UInt64:
				return 10.0f * FMath::Abs(FMath::Cos((float)(ElementIndex + IndexOffsetBetweenTensor * TensorIndex)));
			default:
			//case EMLTensorDataType::None:
			//case EMLTensorDataType::Half:
			//case EMLTensorDataType::Double:
			//case EMLTensorDataType::Float:
			//case EMLTensorDataType::Complex64:
			//case EMLTensorDataType::Complex128:
			//case EMLTensorDataType::BFloat16:
				return FMath::Cos((float)(ElementIndex + IndexOffsetBetweenTensor * TensorIndex));
		}
	}

	static float OutputTensorInitializer(EMLTensorDataType DataType, uint32 ElementIndex, uint32 TensorIndex)
	{
		const constexpr uint32 IndexOffsetBetweenTensor = 13;
		return FMath::Sin((float)(ElementIndex + IndexOffsetBetweenTensor * TensorIndex));
	}

	static int RunTestInference(const FNNIModelRaw& ONNXModelData, const FTests::FTestSetup& TestSetup, 
		IRuntime* Runtime, TArray<FTensor>& OutOutputTensors, TArray<TArray<char>>& OutOutputMemBuffers)
	{
		OutOutputMemBuffers.Empty();
		OutOutputTensors.Empty();

		FOptimizerOptionsMap Options;
		FNNIModelRaw RuntimeModelData;
		TUniquePtr<IModelOptimizer> Optimizer = Runtime->CreateModelOptimizer();
		
		if (!Optimizer || !Optimizer->Optimize(ONNXModelData, RuntimeModelData, Options))
		{
			UE_LOG(LogNNX, Error, TEXT("Failed to optimize the model"));
			return -1;
		}
		
		UMLInferenceModel* UInferenceModel = UMLInferenceModel::CreateFromFormatDesc(RuntimeModelData);
		TUniquePtr<FMLInferenceModel> InferenceModel(Runtime->CreateInferenceModel(UInferenceModel));
		
		if (!InferenceModel.IsValid())
		{
			UE_LOG(LogNNX, Error, TEXT("Could not create Inference model."));
			return -1;
		}

		// If test does not ask for specific input/output, fill with model default converting variable dim to 1 if any.
		TArray<FTensor> InputTensors;
		
		FillTensors(TestSetup.Inputs, InferenceModel->GetInputTensorDescs(), InputTensors);
		FillTensors(TestSetup.Outputs, InferenceModel->GetOutputTensorDescs(), OutOutputTensors);
		
		// bind tensors to memory (CPU) and initialize
		TArray<FMLTensorBinding> InputBindings;
		TArray<FMLTensorBinding> OutputBindings;
		TArray<TArray<char>> InputMemBuffers;
		TArray<TArray<char>> OutputMemBuffers;
		
		FillTensorBindings(InputTensors, InputMemBuffers, InputBindings, InputTensorInitializer);
		FillTensorBindings(OutOutputTensors, OutOutputMemBuffers, OutputBindings, OutputTensorInitializer);

		check(InputTensors.Num() == InputMemBuffers.Num());
		check(OutOutputTensors.Num() == OutOutputMemBuffers.Num());

		// To help for debugging sessions.
		#ifdef UE_BUILD_DEBUG
		const constexpr int32 NumTensorPtrForDebug = 3;
		float* InputsAsFloat[NumTensorPtrForDebug];
		float* OutputsAsFloat[NumTensorPtrForDebug];
		FMemory::Memzero(InputsAsFloat, NumTensorPtrForDebug * sizeof(float*));
		FMemory::Memzero(OutputsAsFloat, NumTensorPtrForDebug * sizeof(float*));
		for (int32 i = 0; i < NumTensorPtrForDebug && i < InputMemBuffers.Num(); ++i)
		{
			InputsAsFloat[i] = (float*)InputMemBuffers[i].GetData();
		}
		for (int32 i = 0; i < NumTensorPtrForDebug && i < OutOutputMemBuffers.Num(); ++i)
		{
			OutputsAsFloat[i] = (float*)OutOutputMemBuffers[i].GetData();
		}
		#endif //UE_BUILD_DEBUG

		
		TArray<FTensorShape> InputShapes;
		for (const FTensor& Tensor : InputTensors)
		{
			InputShapes.Emplace(Tensor.GetShape());
		}

		// Setup inputs
		int ReturnValue = InferenceModel->SetInputTensorShapes(InputShapes);
		if (ReturnValue != 0)
		{
			UE_LOG(LogNNX, Error, TEXT("Failed to set input tensor shapes."));
			return ReturnValue;
		}

		// Run inference
		ReturnValue = InferenceModel->Run(InputBindings, OutputBindings);
		if (ReturnValue != 0)
		{
			UE_LOG(LogNNX, Error, TEXT("Failed to run the model."));
			return ReturnValue;
		}
		
		return ReturnValue;
	}

	bool CompareONNXModelInferenceAcrossRuntimes(const FNNIModelRaw& ONNXModel, const FTests::FTestSetup& TestSetup, const FString& RuntimeFilter)
	{
		FString CurrentPlatform = UGameplayStatics::GetPlatformName();
		if (TestSetup.AutomationExcludedPlatform.Contains(CurrentPlatform))
		{
			UE_LOG(LogNNX, Display, TEXT("Skipping test of '%s' for platform %s (by config)"), *TestSetup.TargetName, *CurrentPlatform);
			return true;
		}
		UE_LOG(LogNNX, Display, TEXT("Starting tests of '%s'"), *TestSetup.TargetName);

		// Reference runtime
		IRuntime* RefRuntime = NNX::GetRuntime(TEXT("NNXRuntimeCPU"));
		if (!RefRuntime)
		{
			UE_LOG(LogNNX, Error, TEXT("Can't load NNXRuntimeCPU runtime. Tests ABORTED!"));
			return false;
		}
		const FString& RefName = RefRuntime->GetRuntimeName();
		TArray<TArray<char>> RefOutputMemBuffers;
		TArray<FTensor> RefOutputTensorDescs;

		RunTestInference(ONNXModel, TestSetup, RefRuntime, RefOutputTensorDescs, RefOutputMemBuffers);

		// Test against other runtime
		TArray<IRuntime*> Runtimes;
		bool bAllTestsSucceeded = true;

		Runtimes = GetAllRuntimes();
		for (auto Runtime : Runtimes)
		{
			const FString& RuntimeName = Runtime->GetRuntimeName();
			if (RuntimeName == RefName)
			{
				continue;
			}

			if (!RuntimeFilter.IsEmpty() && !RuntimeFilter.Contains(RuntimeName))
			{
				continue;
			}

			if (RuntimeName == "NNXRuntimeORTCuda")
			{
				//TODO Reactivate tests for NNXRuntimeORTCuda runtime. Skipped for now as we wait for legal approval for the dlls.
				continue;
			}

			FString TestResult;

			if (TestSetup.AutomationExcludedRuntime.Contains(RuntimeName) ||
				TestSetup.AutomationExcludedPlatformRuntimeCombination.Contains(TPair<FString, FString>(CurrentPlatform, RuntimeName)))
			{
				TestResult = TEXT("skipped (by config)");
			}
			else
			{
				TArray<TArray<char>> OutputMemBuffers;
				TArray<FTensor> OutputTensorDescs;
				bool bTestSuceeded = true;
				float AbsoluteErrorEpsilon = TestSetup.GetAbsoluteErrorEpsilonForRuntime(RuntimeName);
				float RelativeErrorPercent = TestSetup.GetRelativeErrorPercentForRuntime(RuntimeName);

				RunTestInference(ONNXModel, TestSetup, Runtime, OutputTensorDescs, OutputMemBuffers);
				if (OutputTensorDescs.Num() == RefOutputTensorDescs.Num())
				{
					for (int i = 0; i < OutputTensorDescs.Num(); ++i)
					{
						bTestSuceeded &= VerifyTensorResult(
							RefOutputTensorDescs[i], RefOutputMemBuffers[i],
							OutputTensorDescs[i], OutputMemBuffers[i],
							AbsoluteErrorEpsilon, RelativeErrorPercent);
					}
				}
				else
				{
					UE_LOG(LogNNX, Error, TEXT("Expecting %d output tensor(s), got %d."), RefOutputTensorDescs.Num(), OutputTensorDescs.Num());
					bTestSuceeded = false;
				}
				TestResult = bTestSuceeded ? TEXT("SUCCESS") : TEXT("FAILED");
				
				bAllTestsSucceeded &= bTestSuceeded;
			}

			UE_LOG(LogNNX, Display, TEXT("  %s tests: %s"), *RuntimeName, *TestResult);
		}
		return bAllTestsSucceeded;
	}
	FTests::FTestSetup& FTests::AddTest(const FString & Category, const FString & ModelOrOperatorName, const FString & TestSuffix)
	{
		FString TestName = Category + ModelOrOperatorName + TestSuffix;
		//Test name should be unique
		check(nullptr == TestSetups.FindByPredicate([TestName](const FTestSetup& Other) { return Other.TestName == TestName; }));
		TestSetups.Emplace(Category, ModelOrOperatorName, TestSuffix);
		return TestSetups.Last(0);
	}

} // namespace Test
} // namespace NNX