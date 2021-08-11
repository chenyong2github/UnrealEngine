// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceBackEndUtilsGPU.h"
#include "NeuralNetworkInferenceBackEndUtils.h"
#include "NeuralTensorResourceArray.h"



/* FNeuralNetworkInferenceBackEndUtilsGPU public functions
 *****************************************************************************/

void FNeuralNetworkInferenceBackEndUtilsGPU::CreateAndLoadSRVBuffer(TSharedPtr<FReadBuffer>& OutReadBuffer, const TArray<uint32>& InArrayData, const TCHAR* InDebugName)
{
	if (OutReadBuffer.IsValid())
	{
		OutReadBuffer->Release();
	}
	OutReadBuffer = MakeShared<FReadBuffer>();
	TSharedPtr<FNeuralTensorResourceArray> TensorResourceArray = MakeShared<FNeuralTensorResourceArray>((void*)InArrayData.GetData(), sizeof(uint32) * InArrayData.Num());
	OutReadBuffer->Initialize(InDebugName, sizeof(uint32), InArrayData.Num(), PF_R32_UINT, BUF_ShaderResource | BUF_Static, TensorResourceArray.Get());
}

bool FNeuralNetworkInferenceBackEndUtilsGPU::GPUSanityChecks(const FRDGBuilder* const InGraphBuilder)
{
	// Sanity checks
	if (!IsInRenderingThread())
	{
		UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning, TEXT("GPUSanityChecks(): IsInRenderingThread() should be true."));
		return false;
	}
	else if (!InGraphBuilder)
	{
		UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning, TEXT("GPUSanityChecks(): InOutGraphBuilder cannot be nullptr."));
		return false;
	}
	return true;
}

bool FNeuralNetworkInferenceBackEndUtilsGPU::GPUSanityChecks(const FRDGBuilder* const InGraphBuilder, const bool bInIsLoaded)
{
	// Sanity checks
	if (!bInIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning, TEXT("GPUSanityChecks(): bIsLoaded should be true."));
		return false;
	}
	return GPUSanityChecks(InGraphBuilder);
}
