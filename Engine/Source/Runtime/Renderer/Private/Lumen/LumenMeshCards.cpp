// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenMeshCards.cpp
=============================================================================*/

#include "LumenMeshCards.h"
#include "RendererPrivate.h"
#include "MeshCardRepresentation.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LumenSceneUtils.h"

#define LUMEN_LOG_HITCHES 0
constexpr uint32 INVALID_LUMEN_INSTANCE_OFFSET = UINT32_MAX;
constexpr uint32 LUMEN_SINGLE_INSTANCE_BIT = 0x80000000;

int32 GLumenSceneMaxInstanceAddsPerFrame = 5000;
FAutoConsoleVariableRef CVarLumenSceneMaxInstanceAddsPerFrame(
	TEXT("r.LumenScene.MaxInstanceAddsPerFrame"),
	GLumenSceneMaxInstanceAddsPerFrame,
	TEXT("Max number of instanced allowed to be added per frame, remainder deferred to subsequent frames. (default 5000)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenMeshCardsMinSize = 100.0f;
FAutoConsoleVariableRef CVarLumenMeshCardsMinSize(
	TEXT("r.LumenScene.MeshCardsMinSize"),
	GLumenMeshCardsMinSize,
	TEXT("Min mesh size to be included in the Lumen cube map tree."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenMeshCardsMergeInstances = 1;
FAutoConsoleVariableRef CVarLumenMeshCardsMergeInstances(
	TEXT("r.LumenScene.MeshCardsMergeInstances"),
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
	TEXT("r.LumenScene.MeshCardsMaxLOD"),
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
	TEXT("r.LumenScene.MeshCardsMergeInstancesMaxSurfaceAreaRatio"),
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
	TEXT("r.LumenScene.MeshCardsMergedResolutionScale"),
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
	TEXT("r.LumenScene.MeshCardsMergedMaxWorldSize"),
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
	TEXT("r.LumenScene.MeshCardsCullFaces"),
	GLumenMeshCardsCullFaces,
	TEXT(""),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneUploadPrimitiveToDistanceFieldInstanceMappingEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneUploadPrimitiveToDistanceFieldInstanceMappingEveryFrame(
	TEXT("r.LumenScene.UploadPrimitiveToDistanceFieldInstanceMappingEveryFrame"),
	GLumenSceneUploadPrimitiveToDistanceFieldInstanceMappingEveryFrame,
	TEXT(""),
	ECVF_RenderThreadSafe
);

bool IsPrimitiveToDFObjectMappingRequired()
{
	return IsRayTracingEnabled();
}

uint32 PackOffsetAndNum(const FLumenMeshCards& RESTRICT MeshCards, uint32 BaseOffset)
{
	const uint32 Packed = 
		((MeshCards.NumCardsPerOrientation[BaseOffset + 0] & 0xFF) << 0)
		| ((MeshCards.CardOffsetPerOrientation[BaseOffset + 0] & 0xFF) << 8)
		| ((MeshCards.NumCardsPerOrientation[BaseOffset + 1] & 0xFF) << 16)
		| ((MeshCards.CardOffsetPerOrientation[BaseOffset + 1] & 0xFF) << 24);

	return Packed;
}

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

void LumenUpdateDFObjectIndex(FScene* Scene, int32 DFObjectIndex)
{
	Scene->LumenSceneData->DFObjectIndicesToUpdateInBuffer.Add(DFObjectIndex);
}

void FLumenSceneData::UpdatePrimitiveToDistanceFieldInstanceMapping(FScene& Scene, FRHICommandListImmediate& RHICmdList)
{
	if (!IsPrimitiveToDFObjectMappingRequired())
	{
		ResizeResourceIfNeeded(RHICmdList, PrimitiveToLumenInstanceOffsetBuffer, 16, TEXT("PrimitiveToLumenInstanceOffset"));
		ResizeResourceIfNeeded(RHICmdList, LumenInstanceToDFObjectIndexBuffer, 16, TEXT("LumenInstanceToDFObjectIndexBuffer"));
		PrimitiveToLumenInstanceOffsetBufferSize = 0;
		LumenInstanceToDFObjectIndexBufferSize = 0;
		return;
	}

	if (GLumenSceneUploadPrimitiveToDistanceFieldInstanceMappingEveryFrame != 0)
	{
		PrimitivesToUpdate.Reset();
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene.Primitives.Num(); ++PrimitiveIndex)
		{
			PrimitivesToUpdate.Add(PrimitiveIndex);

			FPrimitiveSceneInfo* Primitive = Scene.Primitives[PrimitiveIndex];
			if (Primitive)
			{
				for (int32 InstanceIndex = 0; InstanceIndex < Primitive->LumenNumInstances; ++InstanceIndex)
				{
					int32 DistanceFieldObjectIndex = -1;
					if (InstanceIndex < Primitive->DistanceFieldInstanceIndices.Num())
					{
						DistanceFieldObjectIndex = Primitive->DistanceFieldInstanceIndices[InstanceIndex];
					}

					const int32 LumenInstanceIndex = Primitive->LumenInstanceOffset + InstanceIndex;
					LumenInstanceToDFObjectIndex[LumenInstanceIndex] = DistanceFieldObjectIndex;
					LumenInstancesToUpdate.Add(LumenInstanceIndex);
				}
			}
		}
	}

	// Upload PrimitiveToLumenInstance
	{
		const int32 NumIndices = FMath::RoundUpToPowerOfTwo(Scene.Primitives.Num());
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = FMath::DivideAndRoundUp<int32>(NumIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		const uint32 LastBufferSizeInBytes = PrimitiveToLumenInstanceOffsetBuffer.NumBytes;
		const bool bBufferResized = ResizeResourceIfNeeded(RHICmdList, PrimitiveToLumenInstanceOffsetBuffer, IndicesSizeInBytes, TEXT("PrimitiveToLumenInstanceOffset"));

		// Memset resized part of array to invalid offsets
		const int32 MemsetSizeInBytes = PrimitiveToLumenInstanceOffsetBuffer.NumBytes - LastBufferSizeInBytes;
		if (MemsetSizeInBytes > 0)
		{
			RHICmdList.Transition(FRHITransitionInfo(PrimitiveToLumenInstanceOffsetBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

			const int32 MemsetOffsetInBytes = LastBufferSizeInBytes;
			MemsetResource(RHICmdList, PrimitiveToLumenInstanceOffsetBuffer, INVALID_LUMEN_INSTANCE_OFFSET, MemsetSizeInBytes, MemsetOffsetInBytes);
		}

		const int32 NumIndexUploads = PrimitivesToUpdate.Num();
		if (NumIndexUploads > 0)
		{
			ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenUploadBuffer"));

			for (int32 PrimitiveIndex : PrimitivesToUpdate)
			{
				uint32 LumenInstanceOffset = INVALID_LUMEN_INSTANCE_OFFSET;
				if (PrimitiveIndex < Scene.Primitives.Num())
				{
					const FPrimitiveSceneInfo* Primitive = Scene.Primitives[PrimitiveIndex];
					LumenInstanceOffset = Primitive->LumenInstanceOffset;

					// Handle ray tracing auto instancing where PrimitiveInstanceIndex > 0 but real PrimitiveInstanceIndex is = 0
					if (Primitive->LumenNumInstances <= 1)
					{
						LumenInstanceOffset |= LUMEN_SINGLE_INSTANCE_BIT;
					}
				}
				ByteBufferUploadBuffer.Add(PrimitiveIndex, &LumenInstanceOffset);
			}

			RHICmdList.Transition(FRHITransitionInfo(PrimitiveToLumenInstanceOffsetBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, PrimitiveToLumenInstanceOffsetBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(PrimitiveToLumenInstanceOffsetBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}

		PrimitiveToLumenInstanceOffsetBufferSize = Scene.Primitives.Num();
	}

	// Push distance field scene updates to LumenInstanceToDFObject
	{
		const FDistanceFieldSceneData& DistanceFieldSceneData = Scene.DistanceFieldSceneData;
		for (int32 DistanceFieldObjectIndex : DFObjectIndicesToUpdateInBuffer)
		{
			if (DistanceFieldObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
			{
				const FPrimitiveAndInstance& Mapping = DistanceFieldSceneData.PrimitiveInstanceMapping[DistanceFieldObjectIndex];
				if (Mapping.Primitive->LumenNumInstances > 0)
				{
					const int32 PrimitiveIndex = Mapping.Primitive->GetIndex();
					const uint32 LumenInstanceIndex = Mapping.Primitive->LumenInstanceOffset + Mapping.InstanceIndex;
					LumenInstanceToDFObjectIndex[LumenInstanceIndex] = DistanceFieldObjectIndex;
					LumenInstancesToUpdate.Add(LumenInstanceIndex);
				}
			}
		}
	}

	// Upload LumenInstanceToDFObject
	{
		const int32 NumIndices = FMath::RoundUpToPowerOfTwo(LumenInstanceToDFObjectIndex.Num());
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = FMath::DivideAndRoundUp<int32>(NumIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		ResizeResourceIfNeeded(RHICmdList, LumenInstanceToDFObjectIndexBuffer, IndicesSizeInBytes, TEXT("LumenInstanceToDFObjectIndexBuffer"));

		const int32 NumIndexUploads = LumenInstancesToUpdate.Num();
		if (NumIndexUploads > 0)
		{
			ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenUploadBuffer"));

			for (int32 LumenInstanceIndex : LumenInstancesToUpdate)
			{
				int32 DistanceFieldInstanceIndex = -1;
				if (LumenInstanceToDFObjectIndex.IsAllocated(LumenInstanceIndex))
				{
					DistanceFieldInstanceIndex = LumenInstanceToDFObjectIndex[LumenInstanceIndex];
				}
				ByteBufferUploadBuffer.Add(LumenInstanceIndex, &DistanceFieldInstanceIndex);
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenInstanceToDFObjectIndexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, LumenInstanceToDFObjectIndexBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenInstanceToDFObjectIndexBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}

		LumenInstanceToDFObjectIndexBufferSize = LumenInstanceToDFObjectIndex.Num();
	}
}

void UpdateLumenMeshCards(FScene& Scene, const FDistanceFieldSceneData& DistanceFieldSceneData, FLumenSceneData& LumenSceneData, FRHICommandListImmediate& RHICmdList)
{
	LLM_SCOPE_BYTAG(Lumen);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenMeshCards);

	checkf(LumenSceneData.MeshCardsBounds.Num() == LumenSceneData.MeshCards.Num(),
		TEXT("MeshCards and MeshCardsBounds arrays are expected to be fully in sync, as they are accessed using the same index"));

	extern int32 GLumenSceneUploadMeshCardsBufferEveryFrame;
	if (GLumenSceneUploadMeshCardsBufferEveryFrame)
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
		ResizeResourceIfNeeded(RHICmdList, LumenSceneData.MeshCardsBuffer, MeshCardsNumBytes, TEXT("LumenMeshCards"));

		const int32 NumMeshCardsUploads = LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Num();

		if (NumMeshCardsUploads > 0)
		{
			FLumenMeshCards NullMeshCards;

			LumenSceneData.UploadMeshCardsBuffer.Init(NumMeshCardsUploads, FLumenMeshCardsGPUData::DataStrideInBytes, true, TEXT("LumenSceneUploadMeshCardsBuffer"));

			for (int32 Index : LumenSceneData.MeshCardsIndicesToUpdateInBuffer)
			{
				if (Index < LumenSceneData.MeshCards.Num())
				{
					const FLumenMeshCards& MeshCards = LumenSceneData.MeshCards.IsAllocated(Index) ? LumenSceneData.MeshCards[Index] : NullMeshCards;

					FVector4* Data = (FVector4*) LumenSceneData.UploadMeshCardsBuffer.Add_GetRef(Index);
					FLumenMeshCardsGPUData::FillData(MeshCards, Data);
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.MeshCardsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			LumenSceneData.UploadMeshCardsBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.MeshCardsBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.MeshCardsBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
	}

	// Upload mesh SDF to mesh cards index buffer
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateDFObjectToMeshCardsIndices);

		extern int32 GLumenSceneUploadDFObjectToMeshCardsIndexBufferEveryFrame;
		if (GLumenSceneUploadDFObjectToMeshCardsIndexBufferEveryFrame)
		{
			LumenSceneData.DFObjectIndicesToUpdateInBuffer.Reset();

			for (int32 DFObjectIndex = 0; DFObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num(); ++DFObjectIndex)
			{
				LumenSceneData.DFObjectIndicesToUpdateInBuffer.Add(DFObjectIndex);
			}
		}

		const int32 NumIndices = FMath::RoundUpToPowerOfTwo(DistanceFieldSceneData.NumObjectsInBuffer);
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = FMath::DivideAndRoundUp<int32>(NumIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		ResizeResourceIfNeeded(RHICmdList, LumenSceneData.DFObjectToMeshCardsIndexBuffer, IndicesSizeInBytes, TEXT("DFObjectToMeshCardsIndices"));

		const int32 NumIndexUploads = LumenSceneData.DFObjectIndicesToUpdateInBuffer.Num();

		if (NumIndexUploads > 0)
		{
			LumenSceneData.ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenSceneUploadBuffer"));

			for (int32 DFObjectIndex : LumenSceneData.DFObjectIndicesToUpdateInBuffer)
			{
				if (DFObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
				{
					const FPrimitiveAndInstance& Mapping = DistanceFieldSceneData.PrimitiveInstanceMapping[DFObjectIndex];

					int32 MeshCardsIndex = -1;

					if (Mapping.InstanceIndex < Mapping.Primitive->LumenMeshCardsInstanceIndices.Num())
					{
						MeshCardsIndex = Mapping.Primitive->LumenMeshCardsInstanceIndices[Mapping.InstanceIndex];
					}
					// When instances are merged, only one entry is added to LumenMeshCardsInstanceIndices
					else if (Mapping.Primitive->LumenMeshCardsInstanceIndices.Num() == 1)
					{
						MeshCardsIndex = Mapping.Primitive->LumenMeshCardsInstanceIndices[0];
					}

					LumenSceneData.ByteBufferUploadBuffer.Add(DFObjectIndex, &MeshCardsIndex);
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.DFObjectToMeshCardsIndexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			LumenSceneData.ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.DFObjectToMeshCardsIndexBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.DFObjectToMeshCardsIndexBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
	}

	LumenSceneData.UpdatePrimitiveToDistanceFieldInstanceMapping(Scene, RHICmdList);
	
	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.DFObjectIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.PrimitivesToUpdate.Empty(1024);
	LumenSceneData.PrimitivesMarkedToUpdate.Reset();
	LumenSceneData.LumenInstancesToUpdate.Empty(1024);
}

void AddPrimitiveToLumenScene(FLumenSceneData& LumenSceneData, FPrimitiveSceneInfo* Primitive)
{
	if (IsPrimitiveToDFObjectMappingRequired())
	{
		checkSlow(Primitive->LumenInstanceOffset == INVALID_LUMEN_INSTANCE_OFFSET);
		checkSlow(Primitive->LumenNumInstances == 0);

		TArray<FMatrix> ObjectLocalToWorldTransforms;
		Primitive->Proxy->GetDistancefieldInstanceData(ObjectLocalToWorldTransforms);

		Primitive->LumenNumInstances = FMath::Max(ObjectLocalToWorldTransforms.Num(), 1);
		Primitive->LumenInstanceOffset = LumenSceneData.LumenInstanceToDFObjectIndex.AddSpan(Primitive->LumenNumInstances);

		for (int32 InstanceIndex = 0; InstanceIndex < Primitive->LumenNumInstances; ++InstanceIndex)
		{
			int32 DistanceFieldObjectIndex = -1;
			if (InstanceIndex < Primitive->DistanceFieldInstanceIndices.Num())
			{
				DistanceFieldObjectIndex = Primitive->DistanceFieldInstanceIndices[InstanceIndex];
			}

			const int32 LumenInstanceIndex = Primitive->LumenInstanceOffset + InstanceIndex;
			LumenSceneData.LumenInstanceToDFObjectIndex[LumenInstanceIndex] = DistanceFieldObjectIndex;
			LumenSceneData.LumenInstancesToUpdate.Add(LumenInstanceIndex);
		}

		LumenSceneData.AddPrimitiveToUpdate(Primitive->GetIndex());
	}
}

void RemovePrimitiveFromLumenScene(FLumenSceneData& LumenSceneData, int32 PrimitiveIndex, uint32 LumenInstanceOffset, int32 NumInstances)
{
	if (NumInstances > 0)
	{
		LumenSceneData.LumenInstanceToDFObjectIndex.RemoveSpan(LumenInstanceOffset, NumInstances);
		LumenSceneData.AddPrimitiveToUpdate(PrimitiveIndex);
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

void AddMeshCardsForInstance(
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	int32 InstanceIndexOrMergedFlag,
	float ResolutionScale,
	const FCardRepresentationData* CardRepresentationData,
	const FMatrix& LocalToWorld,
	FLumenSceneData& LumenSceneData)
{
	const FMeshCardsBuildData& MeshCardsBuildData = CardRepresentationData->MeshCardsBuildData;

	const FVector LocalToWorldScale = LocalToWorld.GetScaleVector();
	const FVector ScaledBoundSize = MeshCardsBuildData.Bounds.GetSize() * LocalToWorldScale;
	const FVector FaceSurfaceArea(ScaledBoundSize.Y * ScaledBoundSize.Z, ScaledBoundSize.X * ScaledBoundSize.Z, ScaledBoundSize.Y * ScaledBoundSize.X);
	const float LargestFaceArea = FaceSurfaceArea.GetMax();
	const float MinFaceSurfaceArea = GLumenMeshCardsMinSize * GLumenMeshCardsMinSize;

	if (LargestFaceArea > MinFaceSurfaceArea
		&& IsMatrixOrthogonal(LocalToWorld)) // #lumen_todo: implement card capture for non orthogonal local to world transforms
	{
		const int32 NumBuildDataCards = MeshCardsBuildData.CardBuildData.Num();
		const int32 LODLevel = FMath::Clamp(GLumenMeshCardsMaxLOD, 0, MeshCardsBuildData.MaxLODLevel);

		uint32 NumCards = 0;
		uint32 NumCardsPerOrientation[6] = { 0 };
		uint32 CardOffsetPerOrientation[6] = { 0 };

		for (int32 CardIndex = 0; CardIndex < NumBuildDataCards; ++CardIndex)
		{
			const FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[CardIndex];
			const int32 AxisIndex = CardBuildData.Orientation / 2;
			const float AxisSurfaceArea = FaceSurfaceArea[AxisIndex];
			const bool bCardPassedCulling = (!GLumenMeshCardsCullFaces || AxisSurfaceArea > MinFaceSurfaceArea);
			const bool bCardPassedLODTest = CardBuildData.LODLevel == LODLevel;

			if (bCardPassedCulling && bCardPassedLODTest)
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
			const int32 FirstCardIndex = LumenSceneData.Cards.AddSpan(NumCards);

			checkf(LumenSceneData.MeshCardsBounds.Num() == LumenSceneData.MeshCards.Num(),
				TEXT("MeshCards and MeshCardsBounds arrays are expected to be fully in sync, as they are accessed using the same index"));

			const int32 MeshCardsIndex = LumenSceneData.MeshCards.AddSpan(1);
			LumenSceneData.MeshCards[MeshCardsIndex].Initialize(
				PrimitiveSceneInfo, 
				InstanceIndexOrMergedFlag, 
				LocalToWorld,
				MeshCardsBuildData.Bounds,
				FirstCardIndex,
				NumCards,
				NumCardsPerOrientation,
				CardOffsetPerOrientation);

			LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Add(MeshCardsIndex);

			// Add cards
			for (int32 CardIndex = 0; CardIndex < NumBuildDataCards; ++CardIndex)
			{
				const FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[CardIndex];
				const int32 AxisIndex = CardBuildData.Orientation / 2;
				const float AxisSurfaceArea = FaceSurfaceArea[AxisIndex];
				const bool bCardPassedCulling = (!GLumenMeshCardsCullFaces || AxisSurfaceArea > MinFaceSurfaceArea);
				const bool bCardPassedLODTest = CardBuildData.LODLevel == LODLevel;

				if (bCardPassedCulling && bCardPassedLODTest)
				{
					const int32 CardInsertIndex = FirstCardIndex + CardOffsetPerOrientation[CardBuildData.Orientation];
					++CardOffsetPerOrientation[CardBuildData.Orientation];

					LumenSceneData.Cards[CardInsertIndex].Initialize(PrimitiveSceneInfo, InstanceIndexOrMergedFlag, ResolutionScale, LocalToWorld, CardBuildData, CardIndex, MeshCardsIndex);
					LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardInsertIndex);
				}
			}

			// Mesh card bounds
			LumenSceneData.MeshCardsBounds.AddSpan(1);
			LumenSceneData.MeshCardsBounds[MeshCardsIndex].InitFromMeshCards(LumenSceneData.MeshCards[MeshCardsIndex], LumenSceneData.Cards);

			if (InstanceIndexOrMergedFlag >= 0)
			{
				PrimitiveSceneInfo->LumenMeshCardsInstanceIndices[InstanceIndexOrMergedFlag] = MeshCardsIndex;
				if (InstanceIndexOrMergedFlag < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
				{
					LumenSceneData.DFObjectIndicesToUpdateInBuffer.Add(PrimitiveSceneInfo->DistanceFieldInstanceIndices[InstanceIndexOrMergedFlag]);
				}
			}
			else
			{
				PrimitiveSceneInfo->LumenMeshCardsInstanceIndices[0] = MeshCardsIndex;

				const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
				const int32 NumInstances = PrimitiveInstances->Num();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
				{
					if (InstanceIndex < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
					{
						LumenSceneData.DFObjectIndicesToUpdateInBuffer.Add(PrimitiveSceneInfo->DistanceFieldInstanceIndices[InstanceIndex]);
					}
				}
			}
		}
	}
}

double BoxSurfaceArea(FVector Extent)
{
	return 2.0 * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

struct FAddMeshCardsResult
{
	int32 NumAdded = 0;
};

FAddMeshCardsResult AddMeshCardsForPrimitive(FLumenPrimitiveAddInfo& AddInfo, FLumenSceneData& LumenSceneData, int32 MaxInstancesToAdd = INT32_MAX)
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo = AddInfo.Primitive;
	FAddMeshCardsResult Result;

	const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

	if (CardRepresentationData)
	{
		if (PrimitiveSceneInfo->HasLumenCaptureMeshPass())
		{
			const FBox& WorldBounds = PrimitiveSceneInfo->Proxy->GetBounds().GetBox();
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
			const int32 NumInstances = PrimitiveInstances ? PrimitiveInstances->Num() : 1;

			bool bMergeInstances = false;
			float ResolutionScale = 1.0f;

			if (GLumenMeshCardsMergeInstances 
				&& NumInstances > 1
				&& WorldBounds.GetSize().GetMax() < GLumenMeshCardsMergedMaxWorldSize
				&& !AddInfo.IsProcessing())
			{
				FBox LocalBounds;
				LocalBounds.Init();
				double TotalInstanceSurfaceArea = 0;

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
					const FBox InstanceLocalBounds = Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
					LocalBounds += InstanceLocalBounds;
					const double InstanceSurfaceArea = BoxSurfaceArea(InstanceLocalBounds.GetExtent());
					TotalInstanceSurfaceArea += InstanceSurfaceArea;
				}

				const double BoundsSurfaceArea = BoxSurfaceArea(LocalBounds.GetExtent());
				const float SurfaceAreaRatio = BoundsSurfaceArea / TotalInstanceSurfaceArea;

				if (SurfaceAreaRatio < GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio)
				{
					bMergeInstances = true;
					ResolutionScale = FMath::Sqrt(1.0f / SurfaceAreaRatio) * GLumenMeshCardsMergedResolutionScale;
				}

				/*
				UE_LOG(LogRenderer, Log, TEXT("AddMeshCardsForPrimitive %s: Instances: %u, Merged: %u, SurfaceAreaRatio: %.1f"),
					*PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(),
					NumInstances,
					bMergeInstances ? 1 : 0,
					SurfaceAreaRatio);*/
			}

			if (bMergeInstances)
			{
				PrimitiveSceneInfo->LumenMeshCardsInstanceIndices.SetNum(1);
				PrimitiveSceneInfo->LumenMeshCardsInstanceIndices[0] = -1;

				FBox LocalBounds;
				LocalBounds.Init();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
					LocalBounds += Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
				}

				const FMeshCardsBuildData& MeshCardsBuildData = CardRepresentationData->MeshCardsBuildData;
				FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();
				LocalToWorld = FTranslationMatrix(-MeshCardsBuildData.Bounds.GetCenter()) 
					* FScaleMatrix(FVector(1.0f) / MeshCardsBuildData.Bounds.GetExtent()) 
					* FScaleMatrix(LocalBounds.GetExtent()) 
					* FTranslationMatrix(LocalBounds.GetCenter()) 
					* LocalToWorld;

				const int32 InstanceIndexOrMergedFlag = -1;
				AddMeshCardsForInstance(PrimitiveSceneInfo, InstanceIndexOrMergedFlag, ResolutionScale, CardRepresentationData, LocalToWorld, LumenSceneData);
				Result.NumAdded++;

				AddInfo.MarkComplete();
			}
			else
			{
				check(AddInfo.NumInstances == NumInstances);
				check(MaxInstancesToAdd > 0);

				if (!AddInfo.IsProcessing())
				{
					PrimitiveSceneInfo->LumenMeshCardsInstanceIndices.SetNumUninitialized(NumInstances);
					for (int32& Index : PrimitiveSceneInfo->LumenMeshCardsInstanceIndices)
					{
						Index = -1;
					}
				}

				while (!AddInfo.IsComplete() && MaxInstancesToAdd != 0)
				{
					int32 InstanceIndex = AddInfo.NumProcessedInstances;

					FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

					if (PrimitiveInstances)
					{
						LocalToWorld = (*PrimitiveInstances)[InstanceIndex].InstanceToLocal * LocalToWorld;
					}

					AddMeshCardsForInstance(PrimitiveSceneInfo, InstanceIndex, ResolutionScale, CardRepresentationData, LocalToWorld, LumenSceneData);
					Result.NumAdded++;

					AddInfo.NumProcessedInstances++;
					MaxInstancesToAdd--;
				}
			}
		}
		else
		{
			AddInfo.MarkComplete();
		}
	}
	else
	{
		AddInfo.MarkComplete();
	}

	return Result;
}

void UpdateMeshCardsForInstance(
	int32 MeshCardsIndex,
	const FMeshCardsBuildData& MeshCardsBuildData,
	const FMatrix& LocalToWorld,
	FLumenSceneData& LumenSceneData)
{
	if (MeshCardsIndex >= 0 && IsMatrixOrthogonal(LocalToWorld))
	{
		FLumenMeshCards& MeshCards = LumenSceneData.MeshCards[MeshCardsIndex];
		MeshCards.SetTransform(LocalToWorld);
		LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Add(MeshCardsIndex);

		for (uint32 RelativeCardIndex = 0; RelativeCardIndex < MeshCards.NumCards; RelativeCardIndex++)
		{
			const int32 CardIndex = RelativeCardIndex + MeshCards.FirstCardIndex;
			FCardSourceData& Card = LumenSceneData.Cards[CardIndex];

			const FLumenCardBuildData& CardBuildData = MeshCardsBuildData.CardBuildData[Card.IndexInMeshCards];
			Card.SetTransform(LocalToWorld, CardBuildData);
			LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
		}

		// Intentionally accessed using MeshCardsIndex
		LumenSceneData.MeshCardsBounds[MeshCardsIndex].UpdateBounds(MeshCards, LumenSceneData.Cards);
	}
}

void UpdateMeshCardsForPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, FLumenSceneData& LumenSceneData)
{
	const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

	if (CardRepresentationData)
	{
		if (PrimitiveSceneInfo->HasLumenCaptureMeshPass())
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
			const int32 NumInstances = PrimitiveInstances ? PrimitiveInstances->Num() : 1;

			if (PrimitiveSceneInfo->LumenMeshCardsInstanceIndices.Num() == NumInstances)
			{
				const FMeshCardsBuildData& MeshCardsBuildData = CardRepresentationData->MeshCardsBuildData;

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

					if (PrimitiveInstances)
					{
						LocalToWorld = (*PrimitiveInstances)[InstanceIndex].InstanceToLocal * LocalToWorld;
					}

					const int32 MeshCardsIndex = PrimitiveSceneInfo->LumenMeshCardsInstanceIndices[InstanceIndex];
					UpdateMeshCardsForInstance(MeshCardsIndex, MeshCardsBuildData, LocalToWorld, LumenSceneData);
				}
			}
			else if (PrimitiveSceneInfo->LumenMeshCardsInstanceIndices.Num() == 1 && PrimitiveInstances)
			{
				FBox LocalBounds;
				LocalBounds.Init();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
					LocalBounds += Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
				}

				const FMeshCardsBuildData& MeshCardsBuildData = CardRepresentationData->MeshCardsBuildData;
				FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();
				LocalToWorld = FTranslationMatrix(-MeshCardsBuildData.Bounds.GetCenter()) 
					* FScaleMatrix(FVector(1.0f) / MeshCardsBuildData.Bounds.GetExtent()) 
					* FScaleMatrix(LocalBounds.GetExtent()) 
					* FTranslationMatrix(LocalBounds.GetCenter()) 
					* LocalToWorld;

				const int32 MeshCardsIndex = PrimitiveSceneInfo->LumenMeshCardsInstanceIndices[0];
				UpdateMeshCardsForInstance(MeshCardsIndex, MeshCardsBuildData, LocalToWorld, LumenSceneData);
			}
		}
	}
}

void RemoveMeshCardsForPrimitive(
	FLumenSceneData& LumenSceneData, 
	const FPrimitiveSceneInfo* PrimitiveSceneInfo,
	const TArray<int32, TInlineAllocator<1>>& MeshCardsInstanceIndices)
{
	// Can't dereference the PrimitiveSceneInfo here, it has already been deleted

	for (int32 InstanceIndex = 0; InstanceIndex < MeshCardsInstanceIndices.Num(); ++InstanceIndex)
	{
		const int32 MeshCardsIndex = MeshCardsInstanceIndices[InstanceIndex];

		if (MeshCardsIndex >= 0)
		{
			FLumenMeshCards& MeshCards = LumenSceneData.MeshCards[MeshCardsIndex];

			checkSlow(MeshCards.PrimitiveSceneInfo == PrimitiveSceneInfo);

			for (uint32 CardIndex = MeshCards.FirstCardIndex; CardIndex < MeshCards.FirstCardIndex + MeshCards.NumCards; ++CardIndex)
			{
				LumenSceneData.RemoveCardFromVisibleCardList(CardIndex);
				LumenSceneData.Cards[CardIndex].RemoveFromAtlas(LumenSceneData);
				LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
			}

			checkf(LumenSceneData.MeshCardsBounds.Num() == LumenSceneData.MeshCards.Num(),
				TEXT("MeshCards and MeshCardsBounds arrays are expected to be fully in sync, as they are accessed using the same index"));

			LumenSceneData.Cards.RemoveSpan(MeshCards.FirstCardIndex, MeshCards.NumCards);
			LumenSceneData.MeshCards.RemoveSpan(MeshCardsIndex, 1);
			LumenSceneData.MeshCardsBounds.RemoveSpan(MeshCardsIndex, 1); // Intentionally accessed using MeshCardsIndex

			LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Add(MeshCardsIndex);
		}
	}
}

void UpdateMeshCardRepresentations(FScene* Scene)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateMeshCardRepresentations);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateMeshCardRepresentations);
	const double StartTime = FPlatformTime::Seconds();

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveMeshCards);
		QUICK_SCOPE_CYCLE_COUNTER(RemoveMeshCards);

		for (int32 RemoveIndex = 0; RemoveIndex < LumenSceneData.PendingRemoveOperations.Num(); RemoveIndex++)
		{
			FLumenPrimitiveRemoveInfo& RemoveInfo = LumenSceneData.PendingRemoveOperations[RemoveIndex];

			RemovePrimitiveFromLumenScene(
				LumenSceneData,
				RemoveInfo.PrimitiveIndex,
				RemoveInfo.LumenInstanceOffset,
				RemoveInfo.LumenNumInstances);

			RemoveMeshCardsForPrimitive(
				LumenSceneData, 
				RemoveInfo.Primitive,
				RemoveInfo.MeshCardsInstanceIndices);
		}
	}

	int32 NumInstancesAdded = 0;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddMeshCards);
		QUICK_SCOPE_CYCLE_COUNTER(AddMeshCards);

		int32 MaxInstancesToAdd = GLumenSceneMaxInstanceAddsPerFrame > 0 ? GLumenSceneMaxInstanceAddsPerFrame : INT32_MAX;

		while (LumenSceneData.PendingAddOperations.Num() != 0)
		{
			FLumenPrimitiveAddInfo& AddInfo = LumenSceneData.PendingAddOperations.Last();
			FAddMeshCardsResult Result = AddMeshCardsForPrimitive(
				AddInfo,
				LumenSceneData,
				MaxInstancesToAdd);

			MaxInstancesToAdd -= Result.NumAdded;
			NumInstancesAdded += Result.NumAdded;

			if (AddInfo.IsComplete())
			{
				AddPrimitiveToLumenScene(LumenSceneData, AddInfo.Primitive);

				if (AddInfo.bPendingUpdate)
				{
					UpdateMeshCardsForPrimitive(AddInfo.Primitive, LumenSceneData);
				}
				LumenSceneData.PendingAddOperations.Pop(false);
			}

			if (MaxInstancesToAdd <= 0)
			{
				break;
			}
		}
	}

	static bool bUseUpdatePath = true;

	if (bUseUpdatePath)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateMeshCards);
		QUICK_SCOPE_CYCLE_COUNTER(UpdateMeshCards);

		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			UpdateMeshCardsForPrimitive(PrimitiveSceneInfo, LumenSceneData);
		}
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateMeshCards);
		QUICK_SCOPE_CYCLE_COUNTER(UpdateMeshCards);

		//@todo - implement fast update path which just updates transforms with no capture triggered
		// For now we are just removing / re-adding for update transform
		for (TSet<FPrimitiveSceneInfo*>::TConstIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			RemoveMeshCardsForPrimitive(
				LumenSceneData,
				PrimitiveSceneInfo,
				PrimitiveSceneInfo->LumenMeshCardsInstanceIndices);
		}

		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			FLumenPrimitiveAddInfo AddInfo(PrimitiveSceneInfo);
			AddMeshCardsForPrimitive(AddInfo, LumenSceneData);
		}
	}

#if LUMEN_LOG_HITCHES
	const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

	if (TimeElapsed > 0.01f)
	{
		uint32 NumInstancesToRemove = 0;
		uint32 NumInstancesToUpdate = 0;

		for (int32 RemoveIndex = 0; RemoveIndex < LumenSceneData.PendingRemoveOperations.Num(); RemoveIndex++)
		{
			FLumenPrimitiveRemoveInfo& RemoveInfo = LumenSceneData.PendingRemoveOperations[RemoveIndex];
			NumInstancesToRemove += RemoveInfo.MeshCardsInstanceIndices.Num();
		}


		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			if (PrimitiveSceneInfo->Proxy->GetPrimitiveInstances() && PrimitiveSceneInfo->Proxy->GetPrimitiveInstances()->Num() > 0)
			{
				NumInstancesToUpdate += PrimitiveSceneInfo->Proxy->GetPrimitiveInstances()->Num();
			}
			else
			{
				NumInstancesToUpdate += 1;
			}
		}

		UE_LOG(LogRenderer, Log, TEXT("UpdateMeshCardRepresentations took %.1fms Remove:%u inst:%u, Add:%u inst:%u Update:%u inst:%u"), 
			TimeElapsed * 1000.0f,
			(uint32) LumenSceneData.PendingRemoveOperations.Num(),
			NumInstancesToRemove,
			(uint32)LumenSceneData.PendingAddOperations.Num(),
			NumInstancesAdded,
			(uint32) LumenSceneData.PendingUpdateOperations.Num(),
			NumInstancesToUpdate);
	}
#endif

	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.PendingRemoveOperations.Empty(1024);
	LumenSceneData.PendingUpdateOperations.Empty(1024);
}

void FLumenMeshCardsBounds::InitFromMeshCards(const FLumenMeshCards& MeshCards, const TSparseSpanArray<FCardSourceData>& Cards)
{
	FirstCardIndex = MeshCards.FirstCardIndex;
	NumCards = uint16(MeshCards.NumCards);
	NumVisibleCards = 0;

	UpdateBounds(MeshCards, Cards);
}

void FLumenMeshCardsBounds::UpdateBounds(const FLumenMeshCards& MeshCards, const TSparseSpanArray<FCardSourceData>& Cards)
{
	WorldBoundsMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
	WorldBoundsMax = -WorldBoundsMin;
	ResolutionScale = 0.0f;

	for (uint32 i = 0; i < MeshCards.NumCards; ++i)
	{
		const int32 CardIndex = MeshCards.FirstCardIndex + i;
		const FCardSourceData& Card = Cards[CardIndex];
		WorldBoundsMin = FVector::Min(WorldBoundsMin, Card.WorldBounds.Min);
		WorldBoundsMax = FVector::Max(WorldBoundsMax, Card.WorldBounds.Max);
		ResolutionScale = FMath::Max(ResolutionScale, Card.ResolutionScale);
	}
}