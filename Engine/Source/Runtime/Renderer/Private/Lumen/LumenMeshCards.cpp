// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenMeshCards.cpp
=============================================================================*/

#include "LumenMeshCards.h"
#include "RendererPrivate.h"
#include "MeshCardRepresentation.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LumenSceneUtils.h"

float GLumenMeshCardsMinSize = 30.0f;
FAutoConsoleVariableRef CVarLumenMeshCardsMinSize(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsMinSize"),
	GLumenMeshCardsMinSize,
	TEXT("Minimum mesh card size to be captured by Lumen Scene."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenMeshCardsMergeInstances = 1;
FAutoConsoleVariableRef CVarLumenMeshCardsMergeInstances(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsMergeInstances"),
	GLumenMeshCardsMergeInstances,
	TEXT("Whether to merge all instances of a Instanced Static Mesh Component into a single MeshCards."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenMeshCardsMaxLOD = 1;
FAutoConsoleVariableRef CVarLumenMeshCardsMaxLOD(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsMaxLOD"),
	GLumenMeshCardsMaxLOD,
	TEXT("Max LOD level for the card representation. 0 - lowest quality."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
		{
			FGlobalComponentRecreateRenderStateContext Context;
		}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio = 1.7f;
FAutoConsoleVariableRef CVarLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsMergeInstancesMaxSurfaceAreaRatio"),
	GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio,
	TEXT("Only merge if the (combined box surface area) / (summed instance box surface area) < MaxSurfaceAreaRatio"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenMeshCardsMergedResolutionScale = .3f;
FAutoConsoleVariableRef CVarLumenMeshCardsMergedResolutionScale(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsMergedResolutionScale"),
	GLumenMeshCardsMergedResolutionScale,
	TEXT("Scale on the resolution calculation for a merged MeshCards.  This compensates for the merged box getting a higher resolution assigned due to being closer to the viewer."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenMeshCardsMergedMaxWorldSize = 10000.0f;
FAutoConsoleVariableRef CVarLumenMeshCardsMergedMaxWorldSize(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsMergedMaxWorldSize"),
	GLumenMeshCardsMergedMaxWorldSize,
	TEXT("Only merged bounds less than this size on any axis are considered, since Lumen Scene streaming relies on object granularity."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenMeshCardsCullFaces = 1;
FAutoConsoleVariableRef CVarLumenMeshCardsCullFaces(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsCullFaces"),
	GLumenMeshCardsCullFaces,
	TEXT(""),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenMeshCardsCullOrientation = -1;
FAutoConsoleVariableRef CVarLumenMeshCardsCullOrientation(
	TEXT("r.LumenScene.SurfaceCache.MeshCardsCullOrientation"),
	GLumenMeshCardsCullOrientation,
	TEXT("Cull all mesh cards to a single orientation for debugging."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_RenderThreadSafe
);

extern int32 GLumenSceneUploadEveryFrame;

class FLumenCardGPUData
{
public:
	// Must match usf
	enum { DataStrideInFloat4s = 5 };
	enum { DataStrideInBytes = DataStrideInFloat4s * sizeof(FVector4) };

	static void PackSurfaceMipMap(const FLumenCard& Card, int32 ResLevel, uint32& PackedSizeInPages, uint32& PackedPageTableOffset)
	{
		PackedSizeInPages = 0;
		PackedPageTableOffset = 0;

		if (Card.IsAllocated())
		{
			const FLumenSurfaceMipMap& MipMap = Card.GetMipMap(ResLevel);

			if (MipMap.IsAllocated())
			{
				PackedSizeInPages = MipMap.SizeInPagesX | (MipMap.SizeInPagesY << 16);
				PackedPageTableOffset = MipMap.PageTableSpanOffset;
			}
		}
	}

	static void FillData(const class FLumenCard& RESTRICT Card, FVector4* RESTRICT OutData)
	{
		// Note: layout must match GetLumenCardData in usf

		OutData[0] = FVector4(Card.LocalToWorldRotationX[0], Card.LocalToWorldRotationY[0], Card.LocalToWorldRotationZ[0], Card.Origin.X);
		OutData[1] = FVector4(Card.LocalToWorldRotationX[1], Card.LocalToWorldRotationY[1], Card.LocalToWorldRotationZ[1], Card.Origin.Y);
		OutData[2] = FVector4(Card.LocalToWorldRotationX[2], Card.LocalToWorldRotationY[2], Card.LocalToWorldRotationZ[2], Card.Origin.Z);

		const FIntPoint ResLevelBias = Card.ResLevelToResLevelXYBias();
		uint32 Packed3W = 0;
		Packed3W = uint8(ResLevelBias.X) & 0xFF;
		Packed3W |= (uint8(ResLevelBias.Y) & 0xFF) << 8;
		Packed3W |= Card.bVisible && Card.IsAllocated() ? (1 << 16) : 0;

		OutData[3] = FVector4(Card.LocalExtent.X, Card.LocalExtent.Y, Card.LocalExtent.Z, 0.0f);
		OutData[3].W = *((float*)&Packed3W);

		// Map low-res level for diffuse
		uint32 PackedSizeInPages = 0;
		uint32 PackedPageTableOffset = 0;
		PackSurfaceMipMap(Card, Card.MinAllocatedResLevel, PackedSizeInPages, PackedPageTableOffset);

		// Map hi-res for specular
		uint32 PackedHiResSizeInPages = 0;
		uint32 PackedHiResPageTableOffset = 0;
		PackSurfaceMipMap(Card, Card.MaxAllocatedResLevel, PackedHiResSizeInPages, PackedHiResPageTableOffset);

		OutData[4].X = *((float*)&PackedSizeInPages);
		OutData[4].Y = *((float*)&PackedPageTableOffset);
		OutData[4].Z = *((float*)&PackedHiResSizeInPages);
		OutData[4].W = *((float*)&PackedHiResPageTableOffset);

		static_assert(DataStrideInFloat4s == 5, "Data stride doesn't match");
	}
};

uint32 PackOffsetAndNum(const FLumenMeshCards& RESTRICT MeshCards, uint32 BaseOffset)
{
	const uint32 Packed = 
		((MeshCards.NumCardsPerOrientation[BaseOffset + 0] & 0xFF) << 0)
		| ((MeshCards.CardOffsetPerOrientation[BaseOffset + 0] & 0xFF) << 8)
		| ((MeshCards.NumCardsPerOrientation[BaseOffset + 1] & 0xFF) << 16)
		| ((MeshCards.CardOffsetPerOrientation[BaseOffset + 1] & 0xFF) << 24);

	return Packed;
}

struct FLumenMeshCardsGPUData
{
	// Must match LUMEN_MESH_CARDS_DATA_STRIDE in usf
	enum { DataStrideInFloat4s = 4 };
	enum { DataStrideInBytes = DataStrideInFloat4s * 16 };

	static void FillData(const class FLumenMeshCards& RESTRICT MeshCards, FVector4* RESTRICT OutData);
};

void FLumenMeshCardsGPUData::FillData(const FLumenMeshCards& RESTRICT MeshCards, FVector4* RESTRICT OutData)
{
	// Note: layout must match GetLumenMeshCardsData in usf

	const FMatrix WorldToLocal = MeshCards.LocalToWorld.Inverse();

	const FMatrix TransposedWorldToLocal = WorldToLocal.GetTransposed();

	OutData[0] = *(FVector4*)&TransposedWorldToLocal.M[0];
	OutData[1] = *(FVector4*)&TransposedWorldToLocal.M[1];
	OutData[2] = *(FVector4*)&TransposedWorldToLocal.M[2];
	
	uint32 PackedData[4];
	PackedData[0] = PackOffsetAndNum(MeshCards, 0);
	PackedData[1] = PackOffsetAndNum(MeshCards, 2);
	PackedData[2] = PackOffsetAndNum(MeshCards, 4);
	PackedData[3] = MeshCards.FirstCardIndex;
	OutData[3] = *(FVector4*)&PackedData;

	static_assert(DataStrideInFloat4s == 4, "Data stride doesn't match");
}

void Lumen::UpdateCardSceneBuffer(FRHICommandListImmediate& RHICmdList, const FSceneViewFamily& ViewFamily, FScene* Scene)
{
	LLM_SCOPE_BYTAG(Lumen);

	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateCardSceneBuffer);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateCardSceneBuffer);
	SCOPED_DRAW_EVENT(RHICmdList, UpdateCardSceneBuffer);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	// CardBuffer
	{
		bool bResourceResized = false;
		{
			const int32 NumCardEntries = LumenSceneData.Cards.Num();
			const uint32 CardSceneNumFloat4s = NumCardEntries * FLumenCardGPUData::DataStrideInFloat4s;
			const uint32 CardSceneNumBytes = FMath::DivideAndRoundUp(CardSceneNumFloat4s, 16384u) * 16384 * sizeof(FVector4);
			bResourceResized = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.CardBuffer, FMath::RoundUpToPowerOfTwo(CardSceneNumFloat4s) * sizeof(FVector4), TEXT("Lumen.Cards"));
		}

		if (GLumenSceneUploadEveryFrame)
		{
			LumenSceneData.CardIndicesToUpdateInBuffer.Reset();

			for (int32 i = 0; i < LumenSceneData.Cards.Num(); i++)
			{
				LumenSceneData.CardIndicesToUpdateInBuffer.Add(i);
			}
		}

		const int32 NumCardDataUploads = LumenSceneData.CardIndicesToUpdateInBuffer.Num();

		if (NumCardDataUploads > 0)
		{
			FLumenCard NullCard;

			LumenSceneData.UploadBuffer.Init(NumCardDataUploads, FLumenCardGPUData::DataStrideInBytes, true, TEXT("Lumen.UploadBuffer"));

			for (int32 Index : LumenSceneData.CardIndicesToUpdateInBuffer)
			{
				if (Index < LumenSceneData.Cards.Num())
				{
					const FLumenCard& Card = LumenSceneData.Cards.IsAllocated(Index) ? LumenSceneData.Cards[Index] : NullCard;

					FVector4* Data = (FVector4*)LumenSceneData.UploadBuffer.Add_GetRef(Index);
					FLumenCardGPUData::FillData(Card, Data);
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.CardBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			LumenSceneData.UploadBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.CardBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.CardBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
		else if (bResourceResized)
		{
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.CardBuffer.UAV, ERHIAccess::UAVCompute | ERHIAccess::UAVGraphics, ERHIAccess::SRVMask));
		}
	}

	UpdateLumenMeshCards(*Scene, Scene->DistanceFieldSceneData, LumenSceneData, RHICmdList);

	const uint32 MaxUploadBufferSize = 64 * 1024;
	if (LumenSceneData.UploadBuffer.GetNumBytes() > MaxUploadBufferSize)
	{
		LumenSceneData.UploadBuffer.Release();
	}
}

void UpdateLumenMeshCards(FScene& Scene, const FDistanceFieldSceneData& DistanceFieldSceneData, FLumenSceneData& LumenSceneData, FRHICommandListImmediate& RHICmdList)
{
	LLM_SCOPE_BYTAG(Lumen);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenMeshCards);

	extern int32 GLumenSceneUploadEveryFrame;
	if (GLumenSceneUploadEveryFrame)
	{
		LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Reset();

		for (int32 i = 0; i < LumenSceneData.MeshCards.Num(); i++)
		{
			LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Add(i);
		}
	}

	// Upload MeshCards
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateMeshCards);

		const uint32 NumMeshCards = LumenSceneData.MeshCards.Num();
		const uint32 MeshCardsNumFloat4s = FMath::RoundUpToPowerOfTwo(NumMeshCards * FLumenMeshCardsGPUData::DataStrideInFloat4s);
		const uint32 MeshCardsNumBytes = MeshCardsNumFloat4s * sizeof(FVector4);
		const bool bResourceResized = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.MeshCardsBuffer, MeshCardsNumBytes, TEXT("Lumen.MeshCards"));

		const int32 NumMeshCardsUploads = LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Num();

		if (NumMeshCardsUploads > 0)
		{
			FLumenMeshCards NullMeshCards;

			LumenSceneData.UploadBuffer.Init(NumMeshCardsUploads, FLumenMeshCardsGPUData::DataStrideInBytes, true, TEXT("Lumen.UploadBuffer"));

			for (int32 Index : LumenSceneData.MeshCardsIndicesToUpdateInBuffer)
			{
				if (Index < LumenSceneData.MeshCards.Num())
				{
					const FLumenMeshCards& MeshCards = LumenSceneData.MeshCards.IsAllocated(Index) ? LumenSceneData.MeshCards[Index] : NullMeshCards;

					FVector4* Data = (FVector4*) LumenSceneData.UploadBuffer.Add_GetRef(Index);
					FLumenMeshCardsGPUData::FillData(MeshCards, Data);
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.MeshCardsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			LumenSceneData.UploadBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.MeshCardsBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.MeshCardsBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
		else if (bResourceResized)
		{
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.MeshCardsBuffer.UAV, ERHIAccess::UAVCompute | ERHIAccess::UAVGraphics, ERHIAccess::SRVMask));
		}
	}

	// Upload MeshCards
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateSceneInstanceIndexToMeshCardsIndexBuffer);

		if (GLumenSceneUploadEveryFrame)
		{
			LumenSceneData.PrimitivesToUpdateMeshCards.Reset();

			for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene.Primitives.Num(); ++PrimitiveIndex)
			{
				LumenSceneData.PrimitivesToUpdateMeshCards.Add(PrimitiveIndex);
			}
		}

		const int32 NumIndices = FMath::Max(FMath::RoundUpToPowerOfTwo(Scene.GPUScene.InstanceDataAllocator.GetMaxSize()), 1024u);
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = NumIndices * IndexSizeInBytes;
		ResizeResourceIfNeeded(RHICmdList, LumenSceneData.SceneInstanceIndexToMeshCardsIndexBuffer, IndicesSizeInBytes, TEXT("SceneInstanceIndexToMeshCardsIndexBuffer"));

		uint32 NumIndexUploads = 0;

		for (int32 PrimitiveIndex : LumenSceneData.PrimitivesToUpdateMeshCards)
		{
			if (PrimitiveIndex < Scene.Primitives.Num())
			{
				const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene.Primitives[PrimitiveIndex];
				NumIndexUploads += PrimitiveSceneInfo->GetNumInstanceDataEntries();
			}
		}

		if (NumIndexUploads > 0)
		{
			LumenSceneData.ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenUploadBuffer"));

			for (int32 PrimitiveIndex : LumenSceneData.PrimitivesToUpdateMeshCards)
			{
				if (PrimitiveIndex < Scene.Primitives.Num())
				{
					const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene.Primitives[PrimitiveIndex];
					const int32 NumInstances = PrimitiveSceneInfo->GetNumInstanceDataEntries();

					for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
					{
						int32 MeshCardsIndex = -1;

						if (PrimitiveSceneInfo->LumenPrimitiveIndex > -1)
						{
							const FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[PrimitiveSceneInfo->LumenPrimitiveIndex];
							MeshCardsIndex = LumenPrimitive.GetMeshCardsIndex(InstanceIndex);
						}

						LumenSceneData.ByteBufferUploadBuffer.Add(PrimitiveSceneInfo->GetInstanceDataOffset() + InstanceIndex, &MeshCardsIndex);
					}
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.SceneInstanceIndexToMeshCardsIndexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			LumenSceneData.ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.SceneInstanceIndexToMeshCardsIndexBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.SceneInstanceIndexToMeshCardsIndexBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
	}

	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.PrimitivesToUpdateMeshCards.Empty(1024);
}

void BuildMeshCardsDataForMergedInstances(const FPrimitiveSceneInfo* PrimitiveSceneInfo, FMeshCardsBuildData& MeshCardsBuildData)
{
	const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
	if (!PrimitiveInstances)
	{
		MeshCardsBuildData.MaxLODLevel = 0;
		MeshCardsBuildData.Bounds.Init();
		return;
	}


	FBox MergedBounds;
	MergedBounds.Init();

	{
		const int32 NumInstances = PrimitiveInstances->Num();

		for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
		{
			const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
			MergedBounds += Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
		}
	}

	// Make sure BBox isn't empty and we can generate card representation for it. This handles e.g. infinitely thin planes.
	const FVector SafeCenter = MergedBounds.GetCenter();
	const FVector SafeExtent = FVector::Max(MergedBounds.GetExtent() + 1.0f, FVector(5.0f));
	MergedBounds = FBox(SafeCenter - SafeExtent, SafeCenter + SafeExtent);

	MeshCardsBuildData.MaxLODLevel = 0;
	MeshCardsBuildData.Bounds = MergedBounds;

	MeshCardsBuildData.CardBuildData.SetNum(6);
	for (uint32 Orientation = 0; Orientation < 6; ++Orientation)
	{
		FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[Orientation];
		CardBuildData.Center = MergedBounds.GetCenter();
		CardBuildData.Extent = FLumenCardBuildData::TransformFaceExtent(MergedBounds.GetExtent() + FVector(1), Orientation);
		CardBuildData.Orientation = Orientation;
		CardBuildData.LODLevel = 0;
	}
}

void FLumenSceneData::AddMeshCards(int32 LumenPrimitiveIndex, int32 LumenInstanceIndex)
{
	FLumenPrimitive& LumenPrimitive = LumenPrimitives[LumenPrimitiveIndex];
	FLumenPrimitiveInstance& LumenPrimitiveInstance = LumenPrimitive.Instances[LumenInstanceIndex];

	const FPrimitiveSceneInfo* PrimitiveSceneInfo = LumenPrimitive.Primitive;
	const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

	if (LumenPrimitiveInstance.MeshCardsIndex < 0 && CardRepresentationData)
	{
		FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

		if (LumenPrimitive.bMergedInstances)
		{
			FMeshCardsBuildData MeshCardsBuildData;
			BuildMeshCardsDataForMergedInstances(PrimitiveSceneInfo, MeshCardsBuildData);

			LumenPrimitiveInstance.MeshCardsIndex = AddMeshCardsFromBuildData(LumenPrimitive, LumenInstanceIndex, LocalToWorld, MeshCardsBuildData, LumenPrimitive.CardResolutionScale);
		}
		else
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
			if (PrimitiveInstances && LumenInstanceIndex < PrimitiveInstances->Num())
			{
				LocalToWorld = (*PrimitiveInstances)[LumenInstanceIndex].InstanceToLocal * LocalToWorld;
			}

			const FMeshCardsBuildData& MeshCardsBuildData = CardRepresentationData->MeshCardsBuildData;

			LumenPrimitiveInstance.MeshCardsIndex = AddMeshCardsFromBuildData(LumenPrimitive, LumenInstanceIndex, LocalToWorld, MeshCardsBuildData, LumenPrimitive.CardResolutionScale);
		}

		PrimitivesToUpdateMeshCards.Add(LumenPrimitive.Primitive->GetIndex());

		if (LumenPrimitiveInstance.MeshCardsIndex >= 0)
		{
			++LumenPrimitive.NumMeshCards;
			checkSlow(LumenPrimitive.NumMeshCards <= LumenPrimitive.Instances.Num());
		}
		else
		{
			LumenPrimitiveInstance.bValidMeshCards = false;
		}
	}
}

bool IsMatrixOrthogonal(const FMatrix& Matrix)
{
	const FVector MatrixScale = Matrix.GetScaleVector();

	if (MatrixScale.GetAbsMin() >= KINDA_SMALL_NUMBER)
	{
		FVector AxisX;
		FVector AxisY;
		FVector AxisZ;
		Matrix.GetUnitAxes(AxisX, AxisY, AxisZ);

		return FMath::Abs(AxisX | AxisY) < KINDA_SMALL_NUMBER
			&& FMath::Abs(AxisX | AxisZ) < KINDA_SMALL_NUMBER
			&& FMath::Abs(AxisY | AxisZ) < KINDA_SMALL_NUMBER;
	}

	return false;
}

bool MeshCardCullTest(const FLumenCardBuildData& CardBuildData, const int32 LODLevel, const FVector FaceSurfaceArea, float MinFaceSurfaceArea)
{
	const int32 AxisIndex = CardBuildData.Orientation / 2;
	const float AxisSurfaceArea = FaceSurfaceArea[AxisIndex];
	const bool bCardPassedCulling = (!GLumenMeshCardsCullFaces || AxisSurfaceArea > MinFaceSurfaceArea);
	const bool bCardPassedLODTest = CardBuildData.LODLevel == LODLevel;

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	if (GLumenMeshCardsCullOrientation >= 0 && CardBuildData.Orientation != GLumenMeshCardsCullOrientation)
	{
		return false;
	}
#endif

	return bCardPassedCulling && bCardPassedLODTest;
}

int32 FLumenSceneData::AddMeshCardsFromBuildData(const FLumenPrimitive& LumenPrimitive, int32 LumenInstanceIndex, const FMatrix& LocalToWorld, const FMeshCardsBuildData& MeshCardsBuildData, float ResolutionScale)
{
	const FVector LocalToWorldScale = LocalToWorld.GetScaleVector();
	const FVector ScaledBoundSize = MeshCardsBuildData.Bounds.GetSize() * LocalToWorldScale;
	const FVector FaceSurfaceArea(ScaledBoundSize.Y * ScaledBoundSize.Z, ScaledBoundSize.X * ScaledBoundSize.Z, ScaledBoundSize.Y * ScaledBoundSize.X);
	const float LargestFaceArea = FaceSurfaceArea.GetMax();
	const float MinFaceSurfaceArea = GLumenMeshCardsMinSize * GLumenMeshCardsMinSize;
	const int32 LODLevel = FMath::Clamp(GLumenMeshCardsMaxLOD, 0, MeshCardsBuildData.MaxLODLevel);

	if (LargestFaceArea > MinFaceSurfaceArea
		&& IsMatrixOrthogonal(LocalToWorld)) // #lumen_todo: implement card capture for non orthogonal local to world transforms
	{
		const int32 NumBuildDataCards = MeshCardsBuildData.CardBuildData.Num();

		uint32 NumCards = 0;
		uint32 NumCardsPerOrientation[6] = { 0 };
		uint32 CardOffsetPerOrientation[6] = { 0 };

		for (int32 CardIndex = 0; CardIndex < NumBuildDataCards; ++CardIndex)
		{
			const FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[CardIndex];

			if (MeshCardCullTest(CardBuildData, LODLevel, FaceSurfaceArea, MinFaceSurfaceArea))
			{
				++NumCardsPerOrientation[CardBuildData.Orientation];
				++NumCards;
			}
		}

		for (uint32 Orientation = 1; Orientation < 6; ++Orientation)
		{
			CardOffsetPerOrientation[Orientation] = CardOffsetPerOrientation[Orientation - 1] + NumCardsPerOrientation[Orientation - 1];
		}

		if (NumCards > 0)
		{
			const int32 FirstCardIndex = Cards.AddSpan(NumCards);

			const int32 MeshCardsIndex = MeshCards.AddSpan(1);
			MeshCards[MeshCardsIndex].Initialize(
				LumenPrimitive.Primitive,
				LumenPrimitive.bMergedInstances ? -1 : LumenInstanceIndex,
				LocalToWorld,
				MeshCardsBuildData.Bounds,
				FirstCardIndex,
				NumCards,
				NumCardsPerOrientation,
				CardOffsetPerOrientation);

			MeshCardsIndicesToUpdateInBuffer.Add(MeshCardsIndex);

			// Add cards
			for (int32 CardIndex = 0; CardIndex < NumBuildDataCards; ++CardIndex)
			{	
				const FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[CardIndex];

				if (MeshCardCullTest(CardBuildData, LODLevel, FaceSurfaceArea, MinFaceSurfaceArea))
				{
					const int32 CardInsertIndex = FirstCardIndex + CardOffsetPerOrientation[CardBuildData.Orientation];
					++CardOffsetPerOrientation[CardBuildData.Orientation];

					Cards[CardInsertIndex].Initialize(ResolutionScale, LocalToWorld, CardBuildData, CardIndex, MeshCardsIndex);
					CardIndicesToUpdateInBuffer.Add(CardInsertIndex);
				}
			}

			return MeshCardsIndex;
		}
	}

	return -1;
}

void FLumenSceneData::RemoveMeshCards(int32 ScenePrimitiveId, FLumenPrimitive& LumenPrimitive, FLumenPrimitiveInstance& LumenPrimitiveInstance)
{
	if (LumenPrimitiveInstance.MeshCardsIndex >= 0)
	{
		const FLumenMeshCards& MeshCardsInstance = MeshCards[LumenPrimitiveInstance.MeshCardsIndex];

		for (uint32 CardIndex = MeshCardsInstance.FirstCardIndex; CardIndex < MeshCardsInstance.FirstCardIndex + MeshCardsInstance.NumCards; ++CardIndex)
		{
			RemoveCardFromAtlas(CardIndex);
		}

		Cards.RemoveSpan(MeshCardsInstance.FirstCardIndex, MeshCardsInstance.NumCards);
		MeshCards.RemoveSpan(LumenPrimitiveInstance.MeshCardsIndex, 1);

		MeshCardsIndicesToUpdateInBuffer.Add(LumenPrimitiveInstance.MeshCardsIndex);

		LumenPrimitiveInstance.MeshCardsIndex = -1;

		--LumenPrimitive.NumMeshCards;
		checkSlow(LumenPrimitive.NumMeshCards >= 0);

		if (ScenePrimitiveId >= 0)
		{
			PrimitivesToUpdateMeshCards.Add(ScenePrimitiveId);
		}

	}
}

void FLumenSceneData::UpdateMeshCards(const FMatrix& LocalToWorld, int32 MeshCardsIndex, const FMeshCardsBuildData& MeshCardsBuildData)
{
	if (MeshCardsIndex >= 0 && IsMatrixOrthogonal(LocalToWorld))
	{
		FLumenMeshCards& MeshCardsInstance = MeshCards[MeshCardsIndex];
		MeshCardsInstance.SetTransform(LocalToWorld);
		MeshCardsIndicesToUpdateInBuffer.Add(MeshCardsIndex);

		for (uint32 RelativeCardIndex = 0; RelativeCardIndex < MeshCardsInstance.NumCards; RelativeCardIndex++)
		{
			const int32 CardIndex = RelativeCardIndex + MeshCardsInstance.FirstCardIndex;
			FLumenCard& Card = Cards[CardIndex];

			const FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[Card.IndexInMeshCards];
			Card.SetTransform(LocalToWorld, CardBuildData.Center, CardBuildData.Extent, CardBuildData.Orientation);
			CardIndicesToUpdateInBuffer.Add(CardIndex);
		}
	}
}

void FLumenSceneData::RemoveCardFromAtlas(int32 CardIndex)
{
	FLumenCard& Card = Cards[CardIndex];
	Card.DesiredLockedResLevel = 0;
	FreeVirtualSurface(Card, Card.MinAllocatedResLevel, Card.MaxAllocatedResLevel);
	CardIndicesToUpdateInBuffer.Add(CardIndex);
}