// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmptyUAVPool.h"
#include "NiagaraStats.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("# EmptyUAVs"), STAT_NiagaraEmptyUAVPool, STATGROUP_Niagara);

FNiagaraEmptyUAVPoolScopedAccess::FNiagaraEmptyUAVPoolScopedAccess(class FNiagaraEmptyUAVPool* InEmptyUAVPool)
	: EmptyUAVPool(InEmptyUAVPool)
{
	check(EmptyUAVPool);
	++EmptyUAVPool->UAVAccessCounter;
}

FNiagaraEmptyUAVPoolScopedAccess::~FNiagaraEmptyUAVPoolScopedAccess()
{
	--EmptyUAVPool->UAVAccessCounter;
	if (EmptyUAVPool->UAVAccessCounter == 0)
	{
		EmptyUAVPool->ResetEmptyUAVPools();
	}
}

FRHIUnorderedAccessView* FNiagaraEmptyUAVPool::GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, ENiagaraEmptyUAVType Type)
{
	check(IsInRenderingThread());
	checkf(UAVAccessCounter != 0, TEXT("Accessing Niagara's UAV Pool while not within a scope, this could result in a memory leak!"));

	TMap<EPixelFormat, FEmptyUAVPool>& UAVMap = UAVPools[(int)Type];
	FEmptyUAVPool& Pool = UAVMap.FindOrAdd(Format);
	checkSlow(Pool.NextFreeIndex <= Pool.UAVs.Num());
	if (Pool.NextFreeIndex == Pool.UAVs.Num())
	{
		FEmptyUAV& NewUAV = Pool.UAVs.AddDefaulted_GetRef();

		// Initialize the UAV
		FRHIResourceCreateInfo CreateInfo(TEXT("FNiagaraGpuComputeDispatch::EmptyUAV"));
		switch ( Type )
		{
			case ENiagaraEmptyUAVType::Buffer:
			{
				const uint32 BytesPerElement = GPixelFormats[Format].BlockBytes;
				NewUAV.Buffer = RHICreateVertexBuffer(BytesPerElement, BUF_UnorderedAccess | BUF_ShaderResource, CreateInfo);
				NewUAV.UAV = RHICreateUnorderedAccessView(NewUAV.Buffer, Format);
				break;
			}
			
			case ENiagaraEmptyUAVType::Texture2D:
			{
				NewUAV.Texture = RHICreateTexture2D(1, 1, Format, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
				NewUAV.UAV = RHICreateUnorderedAccessView(NewUAV.Texture, 0);
				break;
			}
			
			case ENiagaraEmptyUAVType::Texture2DArray:
			{
				NewUAV.Texture = RHICreateTexture2DArray(1, 1, 1, Format, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
				NewUAV.UAV = RHICreateUnorderedAccessView(NewUAV.Texture, 0);
				break;
			}
			
			case ENiagaraEmptyUAVType::Texture3D:
			{
				NewUAV.Texture = RHICreateTexture3D(1, 1, 1, Format, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
				NewUAV.UAV = RHICreateUnorderedAccessView(NewUAV.Texture, 0);
				break;
			}
			
			default:
			{
				checkNoEntry();
				return nullptr;
			}
		}

		RHICmdList.Transition(FRHITransitionInfo(NewUAV.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

		// Dispatches which use Empty UAVs are allowed to overlap, since we don't care about the contents of these buffers.
		// We never need to calll EndUAVOverlap() on these.
		RHICmdList.BeginUAVOverlap(NewUAV.UAV);

		INC_DWORD_STAT(STAT_NiagaraEmptyUAVPool);
	}

	FRHIUnorderedAccessView* UAV = Pool.UAVs[Pool.NextFreeIndex].UAV;
	++Pool.NextFreeIndex;
	return UAV;
}

void FNiagaraEmptyUAVPool::ResetEmptyUAVPools()
{
	for (int Type = 0; Type < UE_ARRAY_COUNT(UAVPools); ++Type)
	{
		for (TPair<EPixelFormat, FEmptyUAVPool>& Entry : UAVPools[Type])
		{
			Entry.Value.NextFreeIndex = 0;
		}
	}
}

FNiagaraEmptyUAVPool::FEmptyUAV::~FEmptyUAV()
{
	Buffer.SafeRelease();
	Texture.SafeRelease();
	UAV.SafeRelease();
}

FNiagaraEmptyUAVPool::FEmptyUAVPool::~FEmptyUAVPool()
{
	UE_CLOG(NextFreeIndex != 0, LogNiagara, Warning, TEXT("EmptyUAVPool is potentially in use during destruction."));
	DEC_DWORD_STAT_BY(STAT_NiagaraEmptyUAVPool, UAVs.Num());
}

