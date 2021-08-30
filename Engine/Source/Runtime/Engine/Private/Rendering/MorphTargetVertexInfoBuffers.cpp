// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

void FMorphTargetVertexInfoBuffers::InitRHI()
{
	SCOPED_LOADTIMER(FFMorphTargetVertexInfoBuffers_InitRHI);

	check(NumTotalBatches > 0);

	const uint32 BufferSize = MorphData.Num() * sizeof(uint32);
	FRHIResourceCreateInfo CreateInfo(TEXT("MorphData"));
	MorphDataBuffer = RHICreateStructuredBuffer(sizeof(uint32), BufferSize, BUF_Static | BUF_ByteAddressBuffer | BUF_ShaderResource, ERHIAccess::SRVMask, CreateInfo);
	
	void* BufferPtr = RHILockBuffer(MorphDataBuffer, 0, BufferSize, RLM_WriteOnly);
	FMemory::ParallelMemcpy(BufferPtr, MorphData.GetData(), BufferSize, EMemcpyCachePolicy::StoreUncached);
	RHIUnlockBuffer(MorphDataBuffer);
	MorphDataSRV = RHICreateShaderResourceView(MorphDataBuffer);

	MorphData.Empty();
}

void FMorphTargetVertexInfoBuffers::ReleaseRHI()
{
	MorphDataBuffer.SafeRelease();
	MorphDataSRV.SafeRelease();
}