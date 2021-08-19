// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnitTester.h"
#include "LegacyModelUnitTester.h"
#include "ModelUnitTester.h"
#include "NeuralNetworkInferenceQAUtils.h"
#include "OperatorUnitTester.h"

// #if WITH_EDITOR
// #ifdef PLATFORM_WIN64
// #include "ONNXRuntimeDLLTester.h"
// #endif //PLATFORM_WIN64
// #endif //WITH_EDITOR



/* FUnitTester static public functions
 *****************************************************************************/

void FUnitTester::GlobalTest(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory, const FString& InUnitTestRelativeDirectory)
{
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("----- Starting UnitTesting() ----------------------------------------------------------------------------------------------------"));

	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------- 1. Model Unit Testing (Legacy)"));
	FLegacyModelUnitTester::GlobalTest(InProjectContentDir, InModelZooRelativeDirectory);

	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------- 2. Model Unit Testing"));
	FModelUnitTester::GlobalTest(InProjectContentDir, InModelZooRelativeDirectory);

	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------- 3. Operator Unit Testing"));
	FOperatorUnitTester::GlobalTest(InProjectContentDir, InUnitTestRelativeDirectory);

// #if WITH_EDITOR
// #ifdef PLATFORM_WIN64
// 	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
// 	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
// 	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------------------------------------------------------------------------------------------------------------------------------"));
// 	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("---------- 4. ONNX Runtime DLL Unit Testing (Deprecated)"));
// 	FONNXRuntimeDLLTester::GlobalTest(InProjectContentDir, InModelZooRelativeDirectory);
// #endif //PLATFORM_WIN64
// #endif //WITH_EDITOR

	UE_LOG(LogNeuralNetworkInferenceQA, Display, TEXT("----- UnitTesting() completed! --------------------------------------------------------------------------------------------------"));
}
