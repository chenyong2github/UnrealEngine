// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

void FMorphTargetVertexInfoBuffers::InitRHI()
{
	SCOPED_LOADTIMER(FFMorphTargetVertexInfoBuffers_InitRHI);

	check(NumTotalWorkItems > 0);

	{
		FRHIResourceCreateInfo CreateInfo(TEXT("VertexIndicesVB"));
		VertexIndicesVB = RHICreateBuffer(VertexIndices.GetAllocatedSize(), BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
		void* VertexIndicesVBData = RHILockBuffer(VertexIndicesVB, 0, VertexIndices.GetAllocatedSize(), RLM_WriteOnly);
		FMemory::ParallelMemcpy(VertexIndicesVBData, VertexIndices.GetData(), VertexIndices.GetAllocatedSize(), EMemcpyCachePolicy::StoreUncached);
		RHIUnlockBuffer(VertexIndicesVB);
		VertexIndicesSRV = RHICreateShaderResourceView(VertexIndicesVB, 4, PF_R32_UINT);
	}
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("MorphDeltasVB"));
		MorphDeltasVB = RHICreateBuffer(MorphDeltas.GetAllocatedSize(), BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
		void* MorphDeltasVBData = RHILockBuffer(MorphDeltasVB, 0, MorphDeltas.GetAllocatedSize(), RLM_WriteOnly);
		FMemory::ParallelMemcpy(MorphDeltasVBData, MorphDeltas.GetData(), MorphDeltas.GetAllocatedSize(), EMemcpyCachePolicy::StoreUncached);
		RHIUnlockBuffer(MorphDeltasVB);
		MorphDeltasSRV = RHICreateShaderResourceView(MorphDeltasVB, 2, PF_R16F);
	}
	VertexIndices.Empty();
	MorphDeltas.Empty();
}

void FMorphTargetVertexInfoBuffers::ReleaseRHI()
{
	VertexIndicesVB.SafeRelease();
	VertexIndicesSRV.SafeRelease();
	MorphDeltasVB.SafeRelease();
	MorphDeltasSRV.SafeRelease();
}