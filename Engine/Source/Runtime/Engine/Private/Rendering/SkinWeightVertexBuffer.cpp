// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rendering/SkinWeightVertexBuffer.h"
#include "EngineUtils.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "RenderUtils.h"
#include "SkeletalMeshTypes.h"

///

FSkinWeightVertexBuffer::FSkinWeightVertexBuffer()
:	bNeedsCPUAccess(false)
,	bExtraBoneInfluences(false)
,	WeightData(nullptr)
,	Data(nullptr)
,	Stride(0)
,	NumVertices(0)
{
}

FSkinWeightVertexBuffer::FSkinWeightVertexBuffer( const FSkinWeightVertexBuffer &Other )
	: bNeedsCPUAccess(Other.bNeedsCPUAccess)
	, bExtraBoneInfluences(Other.bExtraBoneInfluences)
	, WeightData(nullptr)
	, Data(nullptr)
	, Stride(0)
	, NumVertices(0)
{
	
}

FSkinWeightVertexBuffer::~FSkinWeightVertexBuffer()
{
	CleanUp();
}

FSkinWeightVertexBuffer& FSkinWeightVertexBuffer::operator=(const FSkinWeightVertexBuffer& Other)
{
	CleanUp();
	bNeedsCPUAccess = Other.bNeedsCPUAccess;
	bExtraBoneInfluences = Other.bExtraBoneInfluences;
	return *this;
}

void FSkinWeightVertexBuffer::CleanUp()
{
	if (WeightData)
	{
		delete WeightData;
		WeightData = NULL;
	}
}

bool FSkinWeightVertexBuffer::IsWeightDataValid() const
{
	return WeightData != NULL;
}

#if WITH_EDITOR

void FSkinWeightVertexBuffer::Init(const TArray<FSoftSkinVertex>& InVertices)
{
	// Make sure if this is console, use compressed otherwise, use not compressed
	AllocateData();

	WeightData->ResizeBuffer(InVertices.Num());

	if (InVertices.Num() > 0)
	{
		Data = WeightData->GetDataPointer();
		Stride = WeightData->GetStride();
		NumVertices = InVertices.Num();
	}

	if (bExtraBoneInfluences)
	{
		for (int32 VertIdx = 0; VertIdx < InVertices.Num(); VertIdx++)
		{
			const FSoftSkinVertex& SrcVertex = InVertices[VertIdx];
			SetWeightsForVertex<true>(VertIdx, SrcVertex);
		}
	}
	else
	{
		for (int32 VertIdx = 0; VertIdx < InVertices.Num(); VertIdx++)
		{
			const FSoftSkinVertex& SrcVertex = InVertices[VertIdx];
			SetWeightsForVertex<false>(VertIdx, SrcVertex);
		}
	}
}

#endif // WITH_EDITOR


FArchive& operator<<(FArchive& Ar, FSkinWeightVertexBuffer& VertexBuffer)
{
	FStripDataFlags StripFlags(Ar);

	VertexBuffer.SerializeMetaData(Ar);

	if (Ar.IsLoading() || VertexBuffer.WeightData == NULL)
	{
		// If we're loading, or we have no valid buffer, allocate container.
		VertexBuffer.AllocateData();
	}

	// if Ar is counting, it still should serialize. Need to count VertexData
	if (!StripFlags.IsDataStrippedForServer() || Ar.IsCountingMemory())
	{
		if (VertexBuffer.WeightData != NULL)
		{
			VertexBuffer.WeightData->Serialize(Ar);

			if (!Ar.IsCountingMemory())
			{
				// update cached buffer info
				VertexBuffer.Data = (VertexBuffer.NumVertices > 0 && VertexBuffer.WeightData->GetResourceArray()->GetResourceDataSize()) ? VertexBuffer.WeightData->GetDataPointer() : nullptr;
				VertexBuffer.Stride = VertexBuffer.WeightData->GetStride();
			}
		}
	}

	return Ar;
}

void FSkinWeightVertexBuffer::SerializeMetaData(FArchive& Ar)
{
	Ar.UsingCustomVersion(FSkeletalMeshCustomVersion::GUID);
	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::SplitModelAndRenderData)
	{
		check(Ar.IsLoading());
		Ar << bExtraBoneInfluences << NumVertices;
	}
	else
	{
		Ar << bExtraBoneInfluences << Stride << NumVertices;
	}
}

void FSkinWeightVertexBuffer::CopyMetaData(const FSkinWeightVertexBuffer& Other)
{
	bExtraBoneInfluences = Other.bExtraBoneInfluences;
	Stride = Other.Stride;
	NumVertices = Other.NumVertices;
}

template <bool bRenderThread>
FVertexBufferRHIRef FSkinWeightVertexBuffer::CreateRHIBuffer_Internal()
{
	if (NumVertices)
	{
		// Create the vertex buffer.
		FResourceArrayInterface* ResourceArray = WeightData ? WeightData->GetResourceArray() : nullptr;
		const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;
		const uint32 BuffFlags = BUF_Static | BUF_ShaderResource;
		FRHIResourceCreateInfo CreateInfo(ResourceArray);
		CreateInfo.bWithoutNativeResource = !WeightData;

		// BUF_ShaderResource is needed for support of the SkinCache (we could make is dependent on GEnableGPUSkinCacheShaders or are there other users?)
		if (bRenderThread)
		{
			return RHICreateVertexBuffer(SizeInBytes, BuffFlags, CreateInfo);
		}
		else
		{
			return RHIAsyncCreateVertexBuffer(SizeInBytes, BuffFlags, CreateInfo);
		}
	}
	return nullptr;
}

FVertexBufferRHIRef FSkinWeightVertexBuffer::CreateRHIBuffer_RenderThread()
{
	return CreateRHIBuffer_Internal<true>();
}

FVertexBufferRHIRef FSkinWeightVertexBuffer::CreateRHIBuffer_Async()
{
	return CreateRHIBuffer_Internal<false>();
}

void FSkinWeightVertexBuffer::InitRHI()
{
	// BUF_ShaderResource is needed for support of the SkinCache (we could make is dependent on GEnableGPUSkinCacheShaders or are there other users?)
	VertexBufferRHI = CreateRHIBuffer_RenderThread();

	bool bSRV = VertexBufferRHI && GSupportsResourceView && GPixelFormats[PF_R32_UINT].Supported;
	// When bAllowCPUAccess is true, the meshes is likely going to be used for Niagara to spawn particles on mesh surface.
	// And it can be the case for CPU *and* GPU access: no differenciation today. That is why we create a SRV in this case.
	// This also avoid setting lots of states on all the members of all the different buffers used by meshes. Follow up: https://jira.it.epicgames.net/browse/UE-69376.
	bSRV |= GetNeedsCPUAccess();

	if (bSRV)
	{
		SRVValue = RHICreateShaderResourceView(WeightData ? VertexBufferRHI : nullptr, 4, PF_R32_UINT);
	}
}

void FSkinWeightVertexBuffer::ReleaseRHI()
{
	SRVValue.SafeRelease();

	FVertexBuffer::ReleaseRHI();
}




void FSkinWeightVertexBuffer::AllocateData()
{
	// Clear any old WeightData before allocating.
	CleanUp();

	if (bExtraBoneInfluences)
	{
		WeightData = new FSkinWeightVertexData< TSkinWeightInfo<true> >(bNeedsCPUAccess);
	}
	else
	{
		WeightData = new FSkinWeightVertexData< TSkinWeightInfo<false> >(bNeedsCPUAccess);
	}
}

#if WITH_EDITOR

template <bool bExtraBoneInfluencesT>
void FSkinWeightVertexBuffer::SetWeightsForVertex(uint32 VertexIndex, const FSoftSkinVertex& SrcVertex)
{
	checkSlow(VertexIndex < GetNumVertices());
	auto* VertBase = (TSkinWeightInfo<bExtraBoneInfluencesT>*)(Data + VertexIndex * Stride);
	FMemory::Memcpy(VertBase->InfluenceBones, SrcVertex.InfluenceBones, TSkinWeightInfo<bExtraBoneInfluencesT>::NumInfluences);
	FMemory::Memcpy(VertBase->InfluenceWeights, SrcVertex.InfluenceWeights, TSkinWeightInfo<bExtraBoneInfluencesT>::NumInfluences);
}

#endif //WITH_EDITOR
