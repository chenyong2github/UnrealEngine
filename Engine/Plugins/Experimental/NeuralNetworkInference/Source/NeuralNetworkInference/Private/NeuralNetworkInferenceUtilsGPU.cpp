// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceUtilsGPU.h"
#include "NeuralNetworkInferenceUtils.h"
#include "NeuralTensorResourceArray.h"

#ifdef PLATFORM_WIN64

#include "RHIResources.h"
#include "ShaderCore.h"

// Disable NOMINMAX & WIN32_LEAN_AND_MEAN defines to avoid compiler warnings
#pragma push_macro("NOMINMAX")
#pragma push_macro("WIN32_LEAN_AND_MEAN")
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN

#include "D3D12RHI.h"
#include "D3D12RHICommon.h"

// @todo: We need D3D12RHIPrivate.h in order to compile correctly, i.e. to be able to use FD3D12Resource and ID3D12Resource
#include "D3D12RHIPrivate.h"

#include "D3D12Resources.h"

#pragma pop_macro("WIN32_LEAN_AND_MEAN")
#pragma pop_macro("NOMINMAX")

#endif

/* FNeuralNetworkInferenceUtilsGPU public functions
 *****************************************************************************/

void FNeuralNetworkInferenceUtilsGPU::CreateAndLoadSRVBuffer(TSharedPtr<FReadBuffer>& OutReadBuffer, const TArray<uint32>& InArrayData, const TCHAR* InDebugName)
{
	if (OutReadBuffer.IsValid())
	{
		OutReadBuffer->Release();
	}
	OutReadBuffer = MakeShared<FReadBuffer>();
	TSharedPtr<FNeuralTensorResourceArray> TensorResourceArray = MakeShared<FNeuralTensorResourceArray>((void*)InArrayData.GetData(), sizeof(uint32) * InArrayData.Num());
	OutReadBuffer->Initialize(InDebugName, sizeof(uint32), InArrayData.Num(), PF_R32_UINT, BUF_ShaderResource | BUF_Static, TensorResourceArray.Get());
}

bool FNeuralNetworkInferenceUtilsGPU::GPUSanityChecks(const FRDGBuilder* const InGraphBuilder)
{
	// Sanity checks
	if (!IsInRenderingThread())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("GPUSanityChecks(): IsInRenderingThread() should be true."));
		return false;
	}
	else if (!InGraphBuilder)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("GPUSanityChecks(): InOutGraphBuilder cannot be nullptr."));
		return false;
	}
	return true;
}

bool FNeuralNetworkInferenceUtilsGPU::GPUSanityChecks(const FRDGBuilder* const InGraphBuilder, const bool bInIsLoaded)
{
	// Sanity checks
	if (!bInIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("GPUSanityChecks(): bIsLoaded should be true."));
		return false;
	}
	return GPUSanityChecks(InGraphBuilder);
}


#ifdef PLATFORM_WIN64

bool FNeuralNetworkInferenceUtilsGPU::IsD3D12RHI()
{
	const FString RHIName = GDynamicRHI->GetName();

	return RHIName == TEXT("D3D12");
}

ID3D12Resource* FNeuralNetworkInferenceUtilsGPU::GetD3D12Resource(FRHIBuffer* Buffer)
{
	if (!IsD3D12RHI())
	{
		return nullptr;
	}

	FD3D12Buffer*	D3DBuffer = static_cast<FD3D12Buffer*>(Buffer);

	return D3DBuffer->GetResource()->GetResource();
}

#endif