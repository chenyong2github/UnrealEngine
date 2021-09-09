// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelUnitTester.h"
#include "NeuralNetworkInferenceQATimer.h"
#include "NeuralNetworkInferenceQAUtils.h"
#include "Misc/Paths.h"



/* FModelUnitTester static public functions
 *****************************************************************************/

bool FModelUnitTester::GlobalTest(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory)
{
	// Model names, input values, and number of repetitions for profiling
	const TArray<FString> ModelNames({ TEXT("MLRigDeformer"), TEXT("cloth_network"), TEXT("HS"), TEXT("RL") });
	const TArray<float> InputArrayValues({ 1.f, 0.f, -1.f, 100.f, -100.f, 0.5f, -0.5f }); // This one can be shorter than CPU/GPUGroundTruths
	// Ground truths
	const TArray<TArray<double>> CPUGroundTruths({ {3.728547, 0.008774, 4.595651, 212.193216, 742.434561, 4.250668, 4.717748}, {0.042571, 0.023693, 0.015783, 13.100505, 8.050994, 0.028807, 0.016387},
		{138.372906, 126.753839, 127.287254, 130.316062, 127.303424, 124.800896, 126.546051}, {0.488662, 0.472437, 0.478862, 0.522685, 0.038322, 0.480848, 0.483821} });
	const TArray<TArray<double>> GPUGroundTruths({ {3.728547, 0.008774, 4.595651, 212.193208, 742.434578, 4.250668, 4.717748}, {0.042571, 0.023693, 0.015783, 13.100504, 8.050994, 0.028807, 0.016387},
		{138.373184, 126.754100, 127.287398, 130.316194, 127.303495, 124.801134, 126.5462530}, {0.488662, 0.472437, 0.478862, 0.522685, 0.038322, 0.480848, 0.483821} });
	// Speed profiling test - 0 repetitions means that test will not be run
#ifdef WITH_UE_AND_ORT_SUPPORT
	const TArray<int32> CPURepetitionsForUEAndORTBackEnd({ 1000, 1000,  50, 1000 });
	const TArray<int32> GPURepetitionsForUEAndORTBackEnd({ 1000, 1000, 100, 1000 });
#else //WITH_UE_AND_ORT_SUPPORT
	const TArray<int32> CPURepetitionsForUEAndORTBackEnd({ 0, 0, 0, 0 });
	const TArray<int32> GPURepetitionsForUEAndORTBackEnd({ 0, 0, 0, 0 });
#endif //WITH_UE_AND_ORT_SUPPORT
#if WITH_EDITOR
	const TArray<int32> CPURepetitionsForUEOnlyBackEnd({ 10, 0, 0, 0 });
	const TArray<int32> GPURepetitionsForUEOnlyBackEnd({ 10, 10, 0, 0 });
#else //WITH_EDITOR
	const TArray<int32> CPURepetitionsForUEOnlyBackEnd({ 0, 0, 0, 0 });
	const TArray<int32> GPURepetitionsForUEOnlyBackEnd({ 0, 0, 0, 0 });
#endif //WITH_EDITOR
	// Run tests
	return ModelLoadAccuracyAndSpeedTests(InProjectContentDir, InModelZooRelativeDirectory, ModelNames, InputArrayValues, CPUGroundTruths, GPUGroundTruths, CPURepetitionsForUEAndORTBackEnd, GPURepetitionsForUEAndORTBackEnd, CPURepetitionsForUEOnlyBackEnd, GPURepetitionsForUEOnlyBackEnd);
}



/* FModelUnitTester static private functions
 *****************************************************************************/

bool FModelUnitTester::ModelLoadAccuracyAndSpeedTests(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory, const TArray<FString>& InModelNames,
	const TArray<float>& InInputArrayValues, const TArray<TArray<double>>& InCPUGroundTruths, const TArray<TArray<double>>& InGPUGroundTruths, const TArray<int32>& InCPURepetitionsForUEAndORTBackEnd,
	const TArray<int32>& InGPURepetitionsForUEAndORTBackEnd, const TArray<int32>& InCPURepetitionsForUEOnlyBackEnd, const TArray<int32>& InGPURepetitionsForUEOnlyBackEnd)
{
	bool bDidGlobalTestPassed = true;

	const FString ModelZooDirectory = InProjectContentDir / InModelZooRelativeDirectory;

	// Test OTXT/UAsset accuracy
	for (int32 ModelIndex = 0; ModelIndex < InModelNames.Num(); ++ModelIndex)
	{
		const FString& ModelName = InModelNames[ModelIndex];
		const TArray<double>& CPUGroundTruths = InCPUGroundTruths[ModelIndex];
		const TArray<double>& GPUGroundTruths = InGPUGroundTruths[ModelIndex];

		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("--------------- %s - Network Uasset Load and Run"), *ModelName);
		const FString UAssetModelFilePath = GetUAssetModelFilePath(ModelName, InModelZooRelativeDirectory);
		UNeuralNetwork* Network = NetworkUassetLoadTest(UAssetModelFilePath);
		if (!Network)
		{
			UE_LOG(LogNeuralNetworkInferenceQA, Warning, TEXT("UNeuralNetwork could not be loaded from UAssetModelFilePath %s."), *UAssetModelFilePath);
			return false;
		}
		// Input debugging
		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("--------------- %s - Input/Output"), *ModelName);
		for (int32 TensorIndex = 0; TensorIndex < Network->GetInputTensorNumber(); ++TensorIndex)
		{
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("InputTensor[%d] = %s."), TensorIndex, *Network->GetInputTensor(TensorIndex).GetName());
		}
		// Output debugging
		for (int32 TensorIndex = 0; TensorIndex < Network->GetOutputTensorNumber(); ++TensorIndex)
		{
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("OutputTensor[%d] = %s."), TensorIndex, *Network->GetOutputTensor(TensorIndex).GetName());
		}
		const bool bShouldRunUEAndORTBackEnd = (InCPURepetitionsForUEAndORTBackEnd[ModelIndex] * InCPURepetitionsForUEAndORTBackEnd[ModelIndex] > 0);
		if (bShouldRunUEAndORTBackEnd)
		{
			bDidGlobalTestPassed &= ModelAccuracyTest(Network, ENeuralBackEnd::UEAndORT, InInputArrayValues, CPUGroundTruths, GPUGroundTruths);
		}
		const bool bShouldRunUEOnlyBackEnd = (InCPURepetitionsForUEOnlyBackEnd[ModelIndex] * InCPURepetitionsForUEOnlyBackEnd[ModelIndex] > 0);
		if (bShouldRunUEOnlyBackEnd)
		{
			bDidGlobalTestPassed &= ModelAccuracyTest(Network, ENeuralBackEnd::UEOnly, InInputArrayValues, CPUGroundTruths, GPUGroundTruths);
		}

		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));

		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("--------------- %s - Network ONNX/ORT Load and Run"), *ModelName);
#if WITH_EDITOR
		const int32 ONNXOrORTIndex = 0; //for (int32 ONNXOrORTIndex = 0; ONNXOrORTIndex < 2; ++ONNXOrORTIndex)
		{
			const FString ModelFilePath = (ONNXOrORTIndex % 2 == 0 ? GetONNXModelFilePath(ModelZooDirectory, ModelName) : GetORTModelFilePath(ModelZooDirectory, ModelName));
			const FString ModelType = (ONNXOrORTIndex % 2 == 0 ? TEXT("ONNX") : TEXT("ORT"));
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("-------------------- %s - Network %s Load and Run - %s"), *ModelName, *ModelType, *ModelFilePath);
			if (bShouldRunUEAndORTBackEnd)
			{
				bDidGlobalTestPassed &= ModelAccuracyTest(NetworkONNXOrORTLoadTest(ModelFilePath), ENeuralBackEnd::UEAndORT, InInputArrayValues, CPUGroundTruths, GPUGroundTruths);
			}
			if (bShouldRunUEOnlyBackEnd)
			{
				bDidGlobalTestPassed &= ModelAccuracyTest(NetworkONNXOrORTLoadTest(ModelFilePath), ENeuralBackEnd::UEOnly, InInputArrayValues, CPUGroundTruths, GPUGroundTruths);
			}
		}
#else //WITH_EDITOR
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("-------------------- Skipped (only if WITH_EDITOR enabled)."));
#endif //WITH_EDITOR
	}

	// Profile speed
	for (int32 ModelIndex = 0; ModelIndex < InModelNames.Num(); ++ModelIndex)
	{
		const FString& ModelName = InModelNames[ModelIndex];
		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("--------------- %s - Network UAsset Speed Profiling"), *ModelName);
		const FString UAssetModelFilePath = GetUAssetModelFilePath(ModelName, InModelZooRelativeDirectory);

		// UEAndORT (if WITH_UE_AND_ORT_SUPPORT)
#ifdef WITH_UE_AND_ORT_SUPPORT
		bDidGlobalTestPassed &= ModelSpeedTest(UAssetModelFilePath, ENeuralDeviceType::CPU, ENeuralBackEnd::UEAndORT, InCPURepetitionsForUEAndORTBackEnd[ModelIndex]);
#ifdef PLATFORM_WIN64
		bDidGlobalTestPassed &= ModelSpeedTest(UAssetModelFilePath, ENeuralDeviceType::GPU, ENeuralBackEnd::UEAndORT, InGPURepetitionsForUEAndORTBackEnd[ModelIndex]);
#else //PLATFORM_WIN64
		bDidGlobalTestPassed &= ModelSpeedTest(UAssetModelFilePath, ENeuralDeviceType::GPU, ENeuralBackEnd::UEAndORT, 0);
#endif //PLATFORM_WIN64
#endif //WITH_UE_AND_ORT_SUPPORT

		// UEOnly
		bDidGlobalTestPassed &= ModelSpeedTest(UAssetModelFilePath, ENeuralDeviceType::CPU, ENeuralBackEnd::UEOnly, InCPURepetitionsForUEOnlyBackEnd[ModelIndex]);
		bDidGlobalTestPassed &= ModelSpeedTest(UAssetModelFilePath, ENeuralDeviceType::GPU, ENeuralBackEnd::UEOnly, InGPURepetitionsForUEOnlyBackEnd[ModelIndex]);
	}

	return bDidGlobalTestPassed;
}

FString FModelUnitTester::GetONNXModelFilePath(const FString& ModelZooDirectory, const FString& InModelName)
{
	return /*FPaths::ConvertRelativePathToFull*/(ModelZooDirectory / InModelName + TEXT("/") + InModelName + TEXT(".onnx")); // E.g., ModelZooDirectory / TEXT("ExampleNetworkReadable/ExampleNetworkReadable.onnx")
}

FString FModelUnitTester::GetORTModelFilePath(const FString& ModelZooDirectory, const FString& InModelName)
{
	return /*FPaths::ConvertRelativePathToFull*/(ModelZooDirectory / InModelName + TEXT("/") + InModelName + TEXT(".ort")); // E.g., ModelZooDirectory / TEXT("ExampleNetworkReadable/ExampleNetworkReadable.ort")
}

FString FModelUnitTester::GetUAssetModelFilePath(const FString& InModelName, const FString& InModelZooRelativeDirectory)
{
	return InModelName + TEXT("'/Game/") + InModelZooRelativeDirectory / InModelName + TEXT("/") + InModelName + TEXT(".") + InModelName + TEXT("'"); // E.g., '/Game/[MODEL_ZOO_DIR]/ExampleNetworkReadable/ExampleNetworkReadable.ExampleNetworkReadable'
}

UNeuralNetwork* FModelUnitTester::NetworkUassetLoadTest(const FString& InUAssetPath)
{
	UNeuralNetwork* Network = LoadObject<UNeuralNetwork>((UObject*)GetTransientPackage(), *InUAssetPath);
	if (!Network)
	{
		ensureMsgf(false, TEXT("UNeuralNetwork is a nullptr. Path: \"%s\"."), *InUAssetPath);
		return nullptr;
	}
	if (!Network->IsLoaded())
	{
		ensureMsgf(false, TEXT("UNeuralNetwork could not be loaded from uasset disk location. Path: \"%s\"."), *InUAssetPath);
		return nullptr;
	}
	return Network;
}

UNeuralNetwork* FModelUnitTester::NetworkONNXOrORTLoadTest(const FString& InModelFilePath)
{
	// Load network architecture and weights from file
	UNeuralNetwork* Network = NewObject<UNeuralNetwork>((UObject*)GetTransientPackage(), UNeuralNetwork::StaticClass());
	if (!Network)
	{
		ensureMsgf(false, TEXT("UNeuralNetwork is a nullptr. Path: \"%s\"."), *InModelFilePath);
		return nullptr;
	}
	if (!Network->Load(InModelFilePath))
	{
		ensureMsgf(false, TEXT("UNeuralNetwork could not be loaded from ONNX file disk location. Path: \"%s\"."), *InModelFilePath);
		return nullptr;
	}
	return Network;
}

bool FModelUnitTester::ModelAccuracyTest(UNeuralNetwork* InOutNetwork, const ENeuralBackEnd InBackEnd, const TArray<float>& InInputArrayValues, const TArray<double>& InCPUGroundTruths, const TArray<double>& InGPUGroundTruths)
{
	// Sanity check
	if (!InOutNetwork)
	{
		return false;
	}
	// Find NetworkSize
	const int64 NetworkSize = InOutNetwork->GetInputTensor().Num();
	// Initialize input data
	TArray<TArray<float>> InputArrays;
	{
		InputArrays.Reserve(InInputArrayValues.Num());
		for (const float InputArrayValue : InInputArrayValues)
		{
			InputArrays.Emplace(TArray<float>({}));
			InputArrays.Last().Init(InputArrayValue, NetworkSize);
		}
		if (InputArrays.Num() > InCPUGroundTruths.Num() || InputArrays.Num() > InGPUGroundTruths.Num())
		{
			UE_LOG(LogNeuralNetworkInferenceQA, Warning, TEXT("InputArrays.Num() <= InCPUGroundTruths.Num() && InputArrays.Num() <= InGPUGroundTruths.Num() failed: %d vs. %d vs. %d."),
				InputArrays.Num(), InCPUGroundTruths.Num(), InGPUGroundTruths.Num());
			return false;
		}
	}
	// Save original network state
	const ENeuralDeviceType OriginalDeviceType = InOutNetwork->GetDeviceType();
	const ENeuralDeviceType OriginalInputDeviceType = InOutNetwork->GetInputDeviceType();
	const ENeuralDeviceType OriginalOutputDeviceType = InOutNetwork->GetOutputDeviceType();
	const ENeuralBackEnd OriginalBackEnd = InOutNetwork->GetBackEnd();
	// Set back end
	InOutNetwork->SetBackEnd(InBackEnd);
	const FString BackEndString = GetBackEndString(InBackEnd);
	// Run each input with CPU/GPU and compare with each other and with the ground truth
	bool bDidGlobalTestPassed = true;
	for (int32 Index = 0; Index < InputArrays.Num(); ++Index)
	{
		TArray<float>& InputArray = InputArrays[Index];
		const double CPUGroundTruth = InCPUGroundTruths[Index];
		const double GPUGroundTruth = InGPUGroundTruths[Index];
		InOutNetwork->SetInputFromArrayCopy(InputArray);
		// CPU
		InOutNetwork->SetDeviceType(ENeuralDeviceType::CPU);
		InOutNetwork->Run();
		TArray<float> CPUOutput = InOutNetwork->GetOutputTensor().GetArrayCopy<float>();
		// Input CPU + GPU + Output CPU
		InOutNetwork->SetDeviceType(ENeuralDeviceType::GPU);
		InOutNetwork->Run();
		TArray<float> CPUGPUCPUOutput = InOutNetwork->GetOutputTensor().GetArrayCopy<float>();
		// Input CPU + GPU + Output GPU
		InOutNetwork->SetDeviceType(ENeuralDeviceType::GPU);
		InOutNetwork->SetOutputDeviceType(ENeuralDeviceType::GPU);
		//InOutNetwork->Run(); // @todo: Uncomment line to re-enable test
		TArray<float> CPUGPUGPUOutput = CPUGPUCPUOutput; // InOutNetwork->GetOutputTensor().GetArrayCopy<float>(); // @todo: Modify line to re-enable test
		// Final verbose
		const double CPUAvgL1Norm = GetAveragedL1Norm(CPUOutput);
		const double CPUGPUCPUAvgL1Norm = GetAveragedL1Norm(CPUGPUCPUOutput);
		const double CPUGPUGPUAvgL1Norm = GetAveragedL1Norm(CPUGPUGPUOutput);
		const double RelativeCoefficient = 1. / FMath::Max(1., FMath::Min(CPUAvgL1Norm, CPUGPUCPUAvgL1Norm)); // Max(1, X) to avoid 0s
		const double CPUGPUAvgL1NormDiff = GetAveragedL1NormDiff(CPUOutput, CPUGPUCPUOutput) * RelativeCoefficient * 1e3;
		const double GPUGPUAvgL1NormDiff = GetAveragedL1NormDiff(CPUGPUCPUOutput, CPUGPUGPUOutput) * RelativeCoefficient * 1e3;
		const double FastCPUGPUAvgL1NormDiff = FMath::Abs((CPUAvgL1Norm - CPUGPUCPUAvgL1Norm)) * RelativeCoefficient * 1e6;
		const double FastCPUAvgL1NormDiff = FMath::Abs(CPUAvgL1Norm - CPUGroundTruth) / FMath::Max(1., FMath::Min(CPUAvgL1Norm, CPUGroundTruth)) * 1e7;
		const double FastGPUAvgL1NormDiff = FMath::Abs(CPUGPUCPUAvgL1Norm - GPUGroundTruth) / FMath::Max(1., FMath::Min(CPUGPUCPUAvgL1Norm, GPUGroundTruth)) * 1e7;
		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("%s: InputNorm = %f, OutputNormCPU = %f, OutputNormGPU = %f, OutputNormCPUGPUGPU = %f, OutputNormGT = %f, CPUAvgL1Norm = %f, CPUGPUCPUAvgL1Norm = %f,"),
			*BackEndString, GetAveragedL1Norm(InputArray), CPUAvgL1Norm, CPUGPUCPUAvgL1Norm, CPUGPUGPUAvgL1Norm, CPUGroundTruth, CPUAvgL1Norm, CPUGPUCPUAvgL1Norm);
		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("\tCPUGPUAvgL1NormDiff = %fe-3, GPUGPUAvgL1NormDiff = %fe-3, FastCPUGPUAvgL1NormDiff = %fe-6, FastCPUAvgL1NormDiff = %fe-7, FastGPUAvgL1NormDiff = %fe-7 (1e-7 is roughly the precision for float)."),
			CPUGPUAvgL1NormDiff, GPUGPUAvgL1NormDiff, FastCPUGPUAvgL1NormDiff, FastCPUAvgL1NormDiff, FastGPUAvgL1NormDiff);
		const TArray<int64>& InputSizes = InOutNetwork->GetInputTensor().GetSizes();
		const TArray<int64>& OutputSizes = InOutNetwork->GetOutputTensor().GetSizes();
		const int64 MaxNumberElementsToDisplay = 100;
		// Check if test failed and (if so) display information
		bool bDidSomeTestFailed = (!FMath::IsFinite(FastCPUGPUAvgL1NormDiff) || FastCPUGPUAvgL1NormDiff > 5);
		bDidSomeTestFailed |= (!FMath::IsFinite(CPUGPUAvgL1NormDiff) || CPUGPUAvgL1NormDiff > 1);
		bDidSomeTestFailed |= (!FMath::IsFinite(GPUGPUAvgL1NormDiff) || GPUGPUAvgL1NormDiff > 1);
		bDidSomeTestFailed |= (!FMath::IsFinite(FastCPUAvgL1NormDiff) || FastCPUAvgL1NormDiff > 30);
		bDidSomeTestFailed |= (!FMath::IsFinite(FastGPUAvgL1NormDiff) || FastGPUAvgL1NormDiff > 30);
		if (bDidSomeTestFailed)
		{
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("FastCPUGPUAvgL1NormDiff (%fe-6) < 5e-6 might have failed."), FastCPUGPUAvgL1NormDiff);
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("CPUGPUAvgL1NormDiff (%fe-7) < 1e-7 might have failed."), CPUGPUAvgL1NormDiff);
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("GPUGPUAvgL1NormDiff (%fe-7) < 1e-7 might have failed."), GPUGPUAvgL1NormDiff);
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("FastCPUAvgL1NormDiff (%fe-7) < 30e-7 might have failed (~30 times the float precision).\nCPUOutput = %s."), FastCPUAvgL1NormDiff, *FNeuralTensor(CPUOutput, OutputSizes).ToString(MaxNumberElementsToDisplay));
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("FastGPUAvgL1NormDiff (%fe-7) < 30e-7 might have failed (~30 times the float precision).\nCPUGPUCPUOutput = %s."), FastGPUAvgL1NormDiff, *FNeuralTensor(CPUGPUCPUOutput, OutputSizes).ToString(MaxNumberElementsToDisplay));
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("Input = %s"),
				*FNeuralTensor(InOutNetwork->GetInputTensor().GetArrayCopy<float>(), InputSizes).ToString(MaxNumberElementsToDisplay));
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("CPUOutput = %s"), *FNeuralTensor(CPUOutput, OutputSizes).ToString(MaxNumberElementsToDisplay));
			UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("CPUGPUCPUOutput = %s"), *FNeuralTensor(CPUGPUCPUOutput, OutputSizes).ToString(MaxNumberElementsToDisplay));
			UE_LOG(LogNeuralNetworkInferenceQA, Warning, TEXT("At least 1 of the 5 CPU/GPU tests failed."));
			return false;
		}
		bDidGlobalTestPassed &= !bDidSomeTestFailed;
	}
	// Reset to original network state
	InOutNetwork->SetDeviceType(OriginalDeviceType);
	InOutNetwork->SetInputDeviceType(OriginalInputDeviceType);
	InOutNetwork->SetOutputDeviceType(OriginalOutputDeviceType);
	InOutNetwork->SetBackEnd(OriginalBackEnd);
	// Test successful
	return bDidGlobalTestPassed;
}

#define MODEL_UNIT_TESTER_SPEED_TEST(OutCopyTimeInMilliSeconds, OutNetworkTimeInMilliSeconds, InRepetitions, bInIsGPU) \
	/* Input/output copy speed */ \
	Timer.Tic(); \
	for (int32 TimerIndex = 0; TimerIndex < InRepetitions; ++TimerIndex) \
	{ \
		InOutNetwork->SetInputFromArrayCopy(InputArray); \
		CPUGPUCPUOutput = InOutNetwork->GetOutputTensor().GetArrayCopy<float>(); \
	} \
	const float OutCopyTimeInMilliSeconds = Timer.Toc() / InRepetitions; \
	/* Forward() speed */ \
	if (InRepetitions > 1) \
	{ \
		for (int32 TimerIndex = 0; TimerIndex < 5; ++TimerIndex) \
		{ \
			InOutNetwork->Run(); \
		} \
	} \
	Timer.Tic(); \
	if (InRepetitions > 0) \
	{ \
		for (int32 TimerIndex = 0; TimerIndex < InRepetitions; ++TimerIndex) \
		{ \
			InOutNetwork->SetInputFromArrayCopy(InputArray); \
			InOutNetwork->Run(); \
			CPUGPUCPUOutput = InOutNetwork->GetOutputTensor().GetArrayCopy<float>(); \
		} \
	} \
	const float OutNetworkTimeInMilliSeconds = Timer.Toc() / InRepetitions - OutCopyTimeInMilliSeconds

bool FModelUnitTester::ModelSpeedTest(const FString& InUAssetPath, const ENeuralDeviceType InDeviceType, const ENeuralBackEnd InBackEnd, const int32 InRepetitions)
{
	// Get debug strings
	const FString DeviceTypeString = GetDeviceTypeString(InDeviceType);
	const FString BackEndString = GetBackEndString(InBackEnd);
	// Skip test if reps = 0
	if (InRepetitions < 1)
	{
		UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("ModelSpeedTest skipped for uasset \"%s\" for InDeviceType = %s (%d) and InBackEnd = %s (%d)."), *InUAssetPath, *DeviceTypeString, (int32)InDeviceType, *BackEndString, (int32)InBackEnd);
		return true;
	}
	// Load Network
	UNeuralNetwork* InOutNetwork = NetworkUassetLoadTest(InUAssetPath);
	// Sanity checks
	if (!InOutNetwork)
	{
		UE_LOG(LogNeuralNetworkInferenceQA, Warning, TEXT("FModelUnitTester::ModelSpeedTest(): InOutNetwork was a nullptr. Path: \"%s\"."), *InUAssetPath);
		return false;
	}
	// Save original network state
	const ENeuralDeviceType OriginalDeviceType = InOutNetwork->GetDeviceType();
	const ENeuralBackEnd OriginalBackEnd = InOutNetwork->GetBackEnd();
	// Set desired back end
	InOutNetwork->SetBackEnd(InBackEnd);
	// Needed variables
	const int64 NetworkSize = InOutNetwork->GetInputTensor().Num();
	TArray<float> InputArray;
	InputArray.Init(1.f, NetworkSize);
	TArray<float> CPUGPUCPUOutput;
	// Speed profiling
	FNeuralNetworkInferenceQATimer Timer;
	// Run profiling 1 time
	InOutNetwork->SetDeviceType(InDeviceType);
	MODEL_UNIT_TESTER_SPEED_TEST(CopyTimer1, NetworkTimer1, 1, /*bIsGPU*/false);
	// Run profiling n times
	InOutNetwork->SetDeviceType(InDeviceType);
	MODEL_UNIT_TESTER_SPEED_TEST(CopyTimer, NetworkTimer, InRepetitions, /*bIsGPU*/false);
	// Display speed times
	UE_LOG(LogNeuralNetworkInferenceQA, Display,
		TEXT("%s-%s:\t1 time = %f+%f msec, avg(%d times) = %f+%f msec."),
		*BackEndString, *DeviceTypeString, CopyTimer1, NetworkTimer1, InRepetitions, CopyTimer, NetworkTimer);
	// Reset to original network state
	InOutNetwork->SetDeviceType(OriginalDeviceType);
	InOutNetwork->SetBackEnd(OriginalBackEnd);
	// Test successful
	return true;
}

double FModelUnitTester::GetAveragedL1Norm(const TArray<float>& InArray)
{
	double AveragedL1Norm = 0.;
	for (const float Value : InArray)
	{
		AveragedL1Norm += FMath::Abs(Value);
	}
	AveragedL1Norm /= InArray.Num();
	return AveragedL1Norm;
}

double FModelUnitTester::GetAveragedL1NormDiff(const TArray<float>& InArray1, const TArray<float>& InArray2)
{
	// Sanity check
	if (InArray1.Num() != InArray2.Num())
	{
		ensureMsgf(false, TEXT("InArray1.Num() == InArray2.Num() failed: %d != %d."), InArray1.Num(), InArray2.Num());
		return -1.;
	}
	// Averaged L1 norm
	double AveragedL1NormDiff = 0.;
	for (int32 Index = 0; Index < InArray1.Num(); ++Index)
	{
		AveragedL1NormDiff += FMath::Abs(InArray1[Index] - InArray2[Index]);
	}
	AveragedL1NormDiff /= InArray1.Num();
	return AveragedL1NormDiff;
}

FString FModelUnitTester::GetDeviceTypeString(const ENeuralDeviceType InDeviceType)
{
	if (InDeviceType == ENeuralDeviceType::CPU)
	{
		return TEXT("CPU");
	}
	else if (InDeviceType == ENeuralDeviceType::GPU)
	{
		return TEXT("GPU");
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown DeviceType = %d."), (int32)InDeviceType);
		return TEXT("");
	}
}

FString FModelUnitTester::GetBackEndString(const ENeuralBackEnd InBackEnd)
{
	if (InBackEnd == ENeuralBackEnd::UEAndORT)
	{
		return TEXT("UEAndORT");
	}
	else if (InBackEnd == ENeuralBackEnd::UEOnly)
	{
		return TEXT("UEOnly");
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown BackEndForCurrentPlatform = %d."), (int32)InBackEnd);
		return TEXT("");
	}
}
