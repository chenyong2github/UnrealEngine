// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFBufferUtility.h"
#include "Rendering/SkeletalMeshRenderData.h"

bool FGLTFBufferUtility::GetAllowCPUAccess(const FRawStaticIndexBuffer* IndexBuffer)
{
	struct FRawStaticIndexBufferHack : FIndexBuffer
	{
		TResourceArray<uint8, INDEXBUFFER_ALIGNMENT> IndexStorage;
		int32 CachedNumIndices;
		bool b32Bit;
		bool bShouldExpandTo32Bit;
	};

	static_assert(sizeof(FRawStaticIndexBufferHack) == sizeof(FRawStaticIndexBuffer), "FRawStaticIndexBufferHack memory layout doesn't match FRawStaticIndexBuffer");
	return reinterpret_cast<const FRawStaticIndexBufferHack*>(IndexBuffer)->IndexStorage.GetAllowCPUAccess();
}

bool FGLTFBufferUtility::GetAllowCPUAccess(const FPositionVertexBuffer* VertexBuffer)
{
	struct FPositionVertexBufferHack : FVertexBuffer
	{
		FShaderResourceViewRHIRef PositionComponentSRV;
		TMemoryImagePtr<FStaticMeshVertexDataInterface> VertexData;
		uint8* Data;
		uint32 Stride;
		uint32 NumVertices;
		bool bNeedsCPUAccess;
	};

	static_assert(sizeof(FPositionVertexBufferHack) == sizeof(FPositionVertexBuffer), "FPositionVertexBufferHack memory layout doesn't match FPositionVertexBuffer");
	const FStaticMeshVertexDataInterface* VertexData = reinterpret_cast<const FPositionVertexBufferHack*>(VertexBuffer)->VertexData;
	return VertexData != nullptr && VertexData->GetAllowCPUAccess();
}

bool FGLTFBufferUtility::GetAllowCPUAccess(const FColorVertexBuffer* VertexBuffer)
{
	struct FColorVertexBufferHack : FVertexBuffer
	{
		FStaticMeshVertexDataInterface* VertexData;
		FShaderResourceViewRHIRef ColorComponentsSRV;
		uint8* Data;
		uint32 Stride;
		uint32 NumVertices;
		bool NeedsCPUAccess;
	};

	static_assert(sizeof(FColorVertexBufferHack) == sizeof(FColorVertexBuffer), "FColorVertexBufferHack memory layout doesn't match FColorVertexBuffer");
	const FStaticMeshVertexDataInterface* VertexData = reinterpret_cast<const FColorVertexBufferHack*>(VertexBuffer)->VertexData;
	return VertexData != nullptr && VertexData->GetAllowCPUAccess();
}

const void* FGLTFBufferUtility::GetBufferData(const FSkinWeightDataVertexBuffer* VertexBuffer)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26)
	return VertexBuffer->GetWeightData();

#else
	struct FSkinWeightDataVertexBufferHack : FVertexBuffer
	{
		FShaderResourceViewRHIRef SRVValue;
		bool bNeedsCPUAccess;
		bool bVariableBonesPerVertex;
		uint32 MaxBoneInfluences;
		bool bUse16BitBoneIndex;
		FStaticMeshVertexDataInterface* WeightData;
		uint8* Data;
		uint32 NumVertices;
		uint32 NumBones;
	};

	static_assert(sizeof(FSkinWeightDataVertexBufferHack) == sizeof(FSkinWeightDataVertexBuffer), "FSkinWeightDataVertexBufferHack memory layout doesn't match FSkinWeightDataVertexBuffer");
	return reinterpret_cast<const FSkinWeightDataVertexBufferHack*>(VertexBuffer)->Data;
#endif
}

const void* FGLTFBufferUtility::GetBufferData(const FSkinWeightLookupVertexBuffer* VertexBuffer)
{
	struct FSkinWeightLookupVertexBufferHack : FVertexBuffer
	{
		FShaderResourceViewRHIRef SRVValue;
		bool bNeedsCPUAccess;
		FStaticMeshVertexDataInterface* LookupData;
		uint8* Data;
		uint32 NumVertices;
	};

	static_assert(sizeof(FSkinWeightLookupVertexBufferHack) == sizeof(FSkinWeightLookupVertexBuffer), "FSkinWeightLookupVertexBufferHack memory layout doesn't match FSkinWeightLookupVertexBuffer");
	return reinterpret_cast<const FSkinWeightLookupVertexBufferHack*>(VertexBuffer)->Data;
}

void FGLTFBufferUtility::ReadRHIBuffer(FRHIVertexBuffer* SourceBuffer, TArray<uint8>& OutData)
{
	OutData.Empty();

	if (SourceBuffer == nullptr)
	{
		return;
	}

	const uint32 NumBytes = SourceBuffer->GetSize();
	if (NumBytes == 0)
	{
		return;
	}

	const uint32 Usage = SourceBuffer->GetUsage();
	if ((Usage & BUF_Static) == 0)
	{
		return; // Some RHI implementations only support reading static buffers
	}

	OutData.AddUninitialized(NumBytes);
	void *DstData = OutData.GetData();

	ENQUEUE_RENDER_COMMAND(ReadRHIBuffer)(
		[SourceBuffer, NumBytes, DstData](FRHICommandListImmediate& RHICmdList)
		{
			const void* SrcData = RHICmdList.LockVertexBuffer(SourceBuffer, 0, NumBytes, RLM_ReadOnly);
			FMemory::Memcpy(DstData, SrcData, NumBytes);
			RHICmdList.UnlockVertexBuffer(SourceBuffer);
		}
	);

	FlushRenderingCommands();
}

void FGLTFBufferUtility::ReadRHIBuffer(FRHIIndexBuffer* SourceBuffer, TArray<uint8>& OutData)
{
	OutData.Empty();

	if (SourceBuffer == nullptr)
	{
		return;
	}

	const uint32 NumBytes = SourceBuffer->GetSize();
	if (NumBytes == 0)
	{
		return;
	}

	const uint32 Usage = SourceBuffer->GetUsage();
	if ((Usage & BUF_Static) == 0)
	{
		return; // Some RHI implementations only support reading static buffers
	}

	OutData.AddUninitialized(NumBytes);
	void *DstData = OutData.GetData();

	ENQUEUE_RENDER_COMMAND(ReadRHIBuffer)(
		[SourceBuffer, NumBytes, DstData](FRHICommandListImmediate& RHICmdList)
		{
			const void* SrcData = RHICmdList.LockIndexBuffer(SourceBuffer, 0, NumBytes, RLM_ReadOnly);
			FMemory::Memcpy(DstData, SrcData, NumBytes);
			RHICmdList.UnlockIndexBuffer(SourceBuffer);
		}
	);

	FlushRenderingCommands();
}
