// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneData.h: Private scene manager definitions.
=============================================================================*/

#pragma once

// Dependencies.

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "SceneTypes.h"
#include "UniformBuffer.h"
#include "LumenSparseSpanArray.h"

class FLumenMeshCards;
class FLumenMeshCardsBounds;
class FLumenCardBuildData;

class FLumenCardPassUniformParameters;

static constexpr uint32 MaxDistantCards = 8;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardScene, )
	SHADER_PARAMETER(uint32, NumCards)
	SHADER_PARAMETER(uint32, MaxConeSteps)
	SHADER_PARAMETER(FVector2D, AtlasSize)
	SHADER_PARAMETER(float, NumMips)
	SHADER_PARAMETER(uint32, NumDistantCards)
	SHADER_PARAMETER(float, DistantSceneMaxTraceDistance)
	SHADER_PARAMETER(FVector, DistantSceneDirection)
	SHADER_PARAMETER_ARRAY(uint32, DistantCardIndices,[MaxDistantCards])
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CardData)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, MeshCardsData)
	SHADER_PARAMETER_SRV(ByteAddressBuffer, DFObjectToMeshCardsIndexBuffer)
	SHADER_PARAMETER_TEXTURE(Texture2D, AlbedoAtlas)
	SHADER_PARAMETER_TEXTURE(Texture2D, NormalAtlas)
	SHADER_PARAMETER_TEXTURE(Texture2D, EmissiveAtlas)
	SHADER_PARAMETER_TEXTURE(Texture2D, DepthAtlas)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FMeshCardRepresentationLink
{
public:
	int32 CardId = -1;
	FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
};

class FCardSourceData
{
public:
	FCardSourceData();
	~FCardSourceData();

	FBox WorldBounds;
	FVector LocalToWorldRotationX;
	FVector LocalToWorldRotationY;
	FVector LocalToWorldRotationZ;
	FVector Origin;
	FVector LocalExtent;
	bool bMovable;
	bool bVisible = false;
	bool bDistantScene = false;

	bool bAllocated = false;
	FIntPoint DesiredResolution;
	FIntRect AtlasAllocation;

	int32 Orientation = -1;
	int32 IndexInVisibleCardIndexBuffer = -1;
	int32 IndexInMeshCards = -1;
	int32 MeshCardsIndex = -1;
	FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
	int32 InstanceIndexOrMergedFlag = 0;
	float ResolutionScale = 1.0f;

	void Initialize(FPrimitiveSceneInfo* InPrimitiveSceneInfo, int32 InInstanceIndexOrMergedFlag, float InResolutionScale, const FMatrix& LocalToWorld, const FLumenCardBuildData& CardBuildData, int32 InIndexInMeshCards, int32 InMeshCardsIndex);

	void SetTransform(const FMatrix& LocalToWorld, const class FLumenCardBuildData& CardBuildData);

	void SetTransform(
		const FMatrix& LocalToWorld,
		const FVector& LocalOrigin,
		const FVector& CardToLocalRotationX,
		const FVector& CardToLocalRotationY,
		const FVector& CardToLocalRotationZ,
		const FVector& InLocalExtent);

	void RemoveFromAtlas(class FLumenSceneData& LumenSceneData);

	int32 GetNumTexels() const
	{
		return AtlasAllocation.Area();
	}

	inline FVector TransformWorldPositionToCardLocal(FVector WorldPosition) const
	{
		FVector Offset = WorldPosition - Origin;
		return FVector(Offset | LocalToWorldRotationX, Offset | LocalToWorldRotationY, Offset | LocalToWorldRotationZ);
	}

	inline FVector TransformCardLocalPositionToWorld(FVector CardPosition) const
	{
		return Origin + CardPosition.X * LocalToWorldRotationX + CardPosition.Y * LocalToWorldRotationY + CardPosition.Z * LocalToWorldRotationZ;
	}
};

class FLumenPrimitiveRemoveInfo
{
public:
	FLumenPrimitiveRemoveInfo(const FPrimitiveSceneInfo* InPrimitive, int32 InPrimitiveIndex)
		: Primitive(InPrimitive)
		, PrimitiveIndex(InPrimitiveIndex)
		, LumenInstanceOffset(InPrimitive->LumenInstanceOffset)
		, LumenNumInstances(InPrimitive->LumenNumInstances)
		, MeshCardsInstanceIndices(InPrimitive->LumenMeshCardsInstanceIndices)
	{}

	/** 
	 * Must not be dereferenced after creation, the primitive was removed from the scene and deleted
	 * Value of the pointer is still useful for map lookups
	 */
	const FPrimitiveSceneInfo* Primitive;

	// Need to copy by value as this is a deferred remove and Primitive may be already destroyed
	int32 PrimitiveIndex;
	uint32 LumenInstanceOffset;
	int32 LumenNumInstances;
	TArray<int32, TInlineAllocator<1>> MeshCardsInstanceIndices;
};

struct FLumenPrimitiveAddInfo
{
	FLumenPrimitiveAddInfo(FPrimitiveSceneInfo* InPrimitive)
		: Primitive(InPrimitive)
		, NumProcessedInstances(0)
		, NumInstances(0)
		, bPendingUpdate(false)
	{
		TArray<FPrimitiveInstance>* Instances = InPrimitive->Proxy->GetPrimitiveInstances();
		if (Instances)
		{
			NumInstances = Instances->Num();
		}
		else
		{
			NumInstances = 1;
		}
	}

	bool IsComplete() const
	{
		return NumProcessedInstances == NumInstances;
	}

	bool IsProcessing() const
	{
		return NumProcessedInstances != 0;
	}

	bool IsPartiallyProcessed() const
	{
		return IsProcessing() && !IsComplete();
	}

	void MarkComplete()
	{
		NumProcessedInstances = NumInstances;
	}

	bool operator == (const FLumenPrimitiveAddInfo& Other) const
	{
		return Primitive == Other.Primitive;
	}

	FPrimitiveSceneInfo* Primitive;
	uint32 NumProcessedInstances;
	uint32 NumInstances;
	bool bPendingUpdate;

};

class FLumenSceneData
{
public:

	int32 Generation;

	FScatterUploadBuffer CardUploadBuffer;
	FScatterUploadBuffer UploadMeshCardsBuffer;
	FScatterUploadBuffer ByteBufferUploadBuffer;
	FScatterUploadBuffer UploadPrimitiveBuffer;

	TArray<int32> CardIndicesToUpdateInBuffer;
	FRWBufferStructured CardBuffer;
	TUniformBufferRef<FLumenCardScene> UniformBuffer;

	TArray<FBox> PrimitiveModifiedBounds;

	// Mesh Cards
	TArray<int32> DFObjectIndicesToUpdateInBuffer;
	TArray<int32> MeshCardsIndicesToUpdateInBuffer;
	TSparseSpanArray<FLumenMeshCards> MeshCards;
	TSparseSpanArray<FLumenMeshCardsBounds> MeshCardsBounds; // Parallel array of MeshCards culling data for better cache line utilization
	TSparseSpanArray<FCardSourceData> Cards;
	TArray<int32, TInlineAllocator<8>> DistantCardIndices;
	FRWBufferStructured MeshCardsBuffer;
	FRWByteAddressBuffer DFObjectToMeshCardsIndexBuffer;

	// Mapping from Primitive to LumenInstance
	TArray<int32> PrimitivesToUpdate;
	TBitArray<>	PrimitivesMarkedToUpdate;
	FRWByteAddressBuffer PrimitiveToLumenInstanceOffsetBuffer;
	uint32 PrimitiveToLumenInstanceOffsetBufferSize = 0;

	// Mapping from LumenInstance to DFObjectIndex
	TArray<int32> LumenInstancesToUpdate;
	TSparseSpanArray<int32> LumenInstanceToDFObjectIndex;
	FRWByteAddressBuffer LumenInstanceToDFObjectIndexBuffer;
	uint32 LumenInstanceToDFObjectIndexBufferSize = 0;

	TArray<int32> VisibleCardsIndices;
	TRefCountPtr<FRDGPooledBuffer> VisibleCardsIndexBuffer;

	// --- Captured from the triangle scene ---
	TRefCountPtr<IPooledRenderTarget> AlbedoAtlas;
	TRefCountPtr<IPooledRenderTarget> NormalAtlas;
	TRefCountPtr<IPooledRenderTarget> EmissiveAtlas;

	// --- Generated ---
	TRefCountPtr<IPooledRenderTarget> DepthAtlas;
	TRefCountPtr<IPooledRenderTarget> FinalLightingAtlas;
	TRefCountPtr<IPooledRenderTarget> IrradianceAtlas;
	TRefCountPtr<IPooledRenderTarget> IndirectIrradianceAtlas;
	TRefCountPtr<IPooledRenderTarget> RadiosityAtlas;
	TRefCountPtr<IPooledRenderTarget> OpacityAtlas;

	bool bFinalLightingAtlasContentsValid;
	FIntPoint MaxAtlasSize;
	FBinnedTextureLayout AtlasAllocator;
	int32 NumCardTexels = 0;
	int32 NumCardsLeftToCapture = 0;
	int32 NumCardsLeftToReallocate = 0;
	int32 NumTexelsLeftToCapture = 0;

	bool bTrackAllPrimitives;
	TArray<FLumenPrimitiveAddInfo> PendingAddOperations;
	TSet<FPrimitiveSceneInfo*> PendingUpdateOperations;
	TArray<FLumenPrimitiveRemoveInfo> PendingRemoveOperations;

	FLumenSceneData(EShaderPlatform ShaderPlatform, EWorldType::Type WorldType);
	~FLumenSceneData();

	void AddPrimitiveToUpdate(int32 PrimitiveIndex);

	void AddPrimitive(FPrimitiveSceneInfo* InPrimitive);
	void UpdatePrimitive(FPrimitiveSceneInfo* InPrimitive);
	void RemovePrimitive(FPrimitiveSceneInfo* InPrimitive, int32 PrimitiveIndex);

	void AddCardToVisibleCardList(int32 CardIndex);
	void RemoveCardFromVisibleCardList(int32 CardIndex);

	bool HasPendingOperations() const
	{
		return PendingAddOperations.Num() > 0 || PendingUpdateOperations.Num() > 0 || PendingRemoveOperations.Num() > 0;
	}

	void UpdatePrimitiveToDistanceFieldInstanceMapping(FScene& Scene, FRHICommandListImmediate& RHICmdList);
};
