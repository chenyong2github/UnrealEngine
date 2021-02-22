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

class FLumenSceneData;
class FLumenMeshCards;
class FMeshCardsBuildData;
class FLumenCardBuildData;
class FLumenCardPassUniformParameters;
class FPrimitiveSceneInfo;

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
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NormalAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthAtlas)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FMeshCardRepresentationLink
{
public:
	int32 CardId = -1;
	FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
};

class FLumenCard
{
public:
	FLumenCard();
	~FLumenCard();

	FBox WorldBounds;
	FVector LocalToWorldRotationX;
	FVector LocalToWorldRotationY;
	FVector LocalToWorldRotationZ;
	FVector Origin;
	FVector LocalExtent;
	bool bVisible = false;
	bool bDistantScene = false;

	bool bAllocated = false;
	FIntPoint DesiredResolution;
	FIntRect AtlasAllocation;

	int32 Orientation = -1;
	int32 IndexInVisibleCardIndexBuffer = -1;
	int32 IndexInMeshCards = -1;
	int32 MeshCardsIndex = -1;
	float ResolutionScale = 1.0f;

	void Initialize(float InResolutionScale, const FMatrix& LocalToWorld, const FLumenCardBuildData& CardBuildData, int32 InIndexInMeshCards, int32 InMeshCardsIndex);

	void SetTransform(
		const FMatrix& LocalToWorld,
		FVector CardLocalCenter,
		FVector CardLocalExtent,
		int32 InOrientation);

	void SetTransform(
		const FMatrix& LocalToWorld,
		const FVector& LocalOrigin,
		const FVector& CardToLocalRotationX,
		const FVector& CardToLocalRotationY,
		const FVector& CardToLocalRotationZ,
		const FVector& InLocalExtent);

	void RemoveFromAtlas(FLumenSceneData& LumenSceneData);

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
		, LumenPrimitiveIndex(InPrimitive->LumenPrimitiveIndex)
	{}

	/** 
	 * Must not be dereferenced after creation, the primitive was removed from the scene and deleted
	 * Value of the pointer is still useful for map lookups
	 */
	const FPrimitiveSceneInfo* Primitive;

	// Need to copy by value as this is a deferred remove and Primitive may be already destroyed
	int32 PrimitiveIndex;
	int32 LumenPrimitiveIndex;
};

class FLumenPrimitiveInstance
{
public:
	FBox BoundingBox;
	int32 MeshCardsIndex;
	bool bValidMeshCards;
};

class FLumenPrimitive
{
public:
	FBox BoundingBox;
	TArray<FLumenPrimitiveInstance, TInlineAllocator<1>> Instances;

	FPrimitiveSceneInfo* Primitive = nullptr;

	bool bMergedInstances = false;
	float CardResolutionScale = 1.0f;
	int32 NumMeshCards = 0;

	// Mapping into LumenDFInstanceToDFObjectIndex
	uint32 LumenDFInstanceOffset = UINT32_MAX;
	int32 LumenNumDFInstances = 0;

	int32 GetMeshCardsIndex(int32 InstanceIndex) const
	{
		if (bMergedInstances)
		{
			return Instances[0].MeshCardsIndex;
		}

		if (InstanceIndex < Instances.Num())
		{
			return Instances[InstanceIndex].MeshCardsIndex;
		}

		return -1;
	}
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

	TArray<FBox> PrimitiveModifiedBounds;

	// Lumen Primitives
	TArray<FLumenPrimitive> LumenPrimitives;

	// Mesh Cards
	TArray<int32> DFObjectIndicesToUpdateInBuffer;
	TArray<int32> MeshCardsIndicesToUpdateInBuffer;
	TSparseSpanArray<FLumenMeshCards> MeshCards;
	TSparseSpanArray<FLumenCard> Cards;
	TArray<int32, TInlineAllocator<8>> DistantCardIndices;
	FRWBufferStructured MeshCardsBuffer;
	FRWByteAddressBuffer DFObjectToMeshCardsIndexBuffer;

	// Mapping from Primitive to LumenDFInstance
	TArray<int32> PrimitivesToUpdate;
	TBitArray<>	PrimitivesMarkedToUpdate;
	FRWByteAddressBuffer PrimitiveToDFLumenInstanceOffsetBuffer;
	uint32 PrimitiveToLumenDFInstanceOffsetBufferSize = 0;

	// Mapping from LumenDFInstance to DFObjectIndex
	TArray<int32> LumenDFInstancesToUpdate;
	TSparseSpanArray<int32> LumenDFInstanceToDFObjectIndex;
	FRWByteAddressBuffer LumenDFInstanceToDFObjectIndexBuffer;
	uint32 LumenDFInstanceToDFObjectIndexBufferSize = 0;

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
	int32 NumMeshCardsToAddToSurfaceCache = 0;

	bool bTrackAllPrimitives;
	TArray<FPrimitiveSceneInfo*> PendingAddOperations;
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

	void AddMeshCards(int32 LumenPrimitiveIndex, int32 LumenInstanceIndex);
	void UpdateMeshCards(const FMatrix& LocalToWorld, int32 MeshCardsIndex, const FMeshCardsBuildData& MeshCardsBuildData);
	void RemoveMeshCards(FLumenPrimitive& LumenPrimitive, FLumenPrimitiveInstance& LumenPrimitiveInstance);

	bool HasPendingOperations() const
	{
		return PendingAddOperations.Num() > 0 || PendingUpdateOperations.Num() > 0 || PendingRemoveOperations.Num() > 0;
	}

	void UpdatePrimitiveToDistanceFieldInstanceMapping(FScene& Scene, FRHICommandListImmediate& RHICmdList);

private:

	int32 AddMeshCardsFromBuildData(const FMatrix& LocalToWorld, const FMeshCardsBuildData& MeshCardsBuildData, float ResolutionScale);
};
