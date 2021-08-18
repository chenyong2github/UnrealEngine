// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

void FMorphTargetVertexInfoBuffers::InitRHI()
{
	SCOPED_LOADTIMER(FFMorphTargetVertexInfoBuffers_InitRHI);

	check(NumTotalWorkItems > 0);

	{
		FRHIResourceCreateInfo CreateInfo(TEXT("VertexIndicesVB"));
		if (bMaxVertexIndex32Bits)
		{
			VertexIndicesVB = RHICreateBuffer(VertexIndices.Num() * sizeof(uint32), BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
			void* VertexIndicesVBData = RHILockBuffer(VertexIndicesVB, 0, VertexIndicesVB->GetSize(), RLM_WriteOnly);
			FMemory::ParallelMemcpy(VertexIndicesVBData, VertexIndices.GetData(), VertexIndices.Num() * sizeof(uint32), EMemcpyCachePolicy::StoreUncached);
			RHIUnlockBuffer(VertexIndicesVB);
			VertexIndicesSRV = RHICreateShaderResourceView(VertexIndicesVB, sizeof(uint32), PF_R32_UINT);
		}
		else
		{
			VertexIndicesVB = RHICreateBuffer(VertexIndices.Num() * sizeof(uint16), BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
			uint16* VertexIndicesVBData = (uint16*)RHILockBuffer(VertexIndicesVB, 0, VertexIndicesVB->GetSize(), RLM_WriteOnly);
			for (int32 i = 0; i < VertexIndices.Num(); ++i)
			{
				VertexIndicesVBData[i] = (uint16)VertexIndices[i];
			}
			RHIUnlockBuffer(VertexIndicesVB);
			VertexIndicesSRV = RHICreateShaderResourceView(VertexIndicesVB, sizeof(uint16), PF_R16_UINT);
		}
	}
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("MorphDeltasVB"));
		MorphDeltasVB = RHICreateBuffer(MorphDeltas.Num() * MorphDeltas.GetTypeSize(), BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
		void* MorphDeltasVBData = RHILockBuffer(MorphDeltasVB, 0, MorphDeltasVB->GetSize(), RLM_WriteOnly);
		FMemory::ParallelMemcpy(MorphDeltasVBData, MorphDeltas.GetData(), MorphDeltas.Num() * MorphDeltas.GetTypeSize(), EMemcpyCachePolicy::StoreUncached);
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