// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

void FMorphTargetVertexInfoBuffers::InitRHI()
{
	SCOPED_LOADTIMER(FFMorphTargetVertexInfoBuffers_InitRHI);

	check(NumTotalWorkItems > 0);

	{
		FRHIResourceCreateInfo CreateInfo;
		void* VertexIndicesVBData = nullptr;
		VertexIndicesVB = RHICreateAndLockVertexBuffer(VertexIndices.GetAllocatedSize(), BUF_Static | BUF_ShaderResource, CreateInfo, VertexIndicesVBData);
		FMemory::ParallelMemcpy(VertexIndicesVBData, VertexIndices.GetData(), VertexIndices.GetAllocatedSize(), EMemcpyCachePolicy::StoreUncached);
		RHIUnlockVertexBuffer(VertexIndicesVB);
		VertexIndicesSRV = RHICreateShaderResourceView(VertexIndicesVB, 4, PF_R32_UINT);
	}
	{
		FRHIResourceCreateInfo CreateInfo;
		void* MorphDeltasVBData = nullptr;
		MorphDeltasVB = RHICreateAndLockVertexBuffer(MorphDeltas.GetAllocatedSize(), BUF_Static | BUF_ShaderResource, CreateInfo, MorphDeltasVBData);
		FMemory::ParallelMemcpy(MorphDeltasVBData, MorphDeltas.GetData(), MorphDeltas.GetAllocatedSize(), EMemcpyCachePolicy::StoreUncached);
		RHIUnlockVertexBuffer(MorphDeltasVB);
		MorphDeltasSRV = RHICreateShaderResourceView(MorphDeltasVB, 2, PF_R16F);
	}
	{
		FRHIResourceCreateInfo CreateInfo;
		void* MorphPermutationsVBData = nullptr;
		MorphPermutationsVB = RHICreateAndLockVertexBuffer(MorphPermutations.GetAllocatedSize(), BUF_Static | BUF_ShaderResource, CreateInfo, MorphPermutationsVBData);
		FMemory::ParallelMemcpy(MorphPermutationsVBData, MorphPermutations.GetData(), MorphPermutations.GetAllocatedSize(), EMemcpyCachePolicy::StoreUncached);
		RHIUnlockVertexBuffer(MorphPermutationsVB);
		MorphPermutationsSRV = RHICreateShaderResourceView(MorphPermutationsVB, 4, PF_R32_UINT);
	}
	MorphPermutations.Empty();
	VertexIndices.Empty();
	MorphDeltas.Empty();
}

void FMorphTargetVertexInfoBuffers::ReleaseRHI()
{
	VertexIndicesVB.SafeRelease();
	VertexIndicesSRV.SafeRelease();
	MorphDeltasVB.SafeRelease();
	MorphDeltasSRV.SafeRelease();
	MorphPermutationsVB.SafeRelease();
	MorphPermutationsSRV.SafeRelease();
}