// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenMeshCards.cpp
=============================================================================*/

#include "LumenMeshCards.h"
#include "RendererPrivate.h"
#include "MeshCardRepresentation.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LumenSceneUtils.h"

constexpr uint32 INVALID_LUMEN_DF_INSTANCE_OFFSET = UINT32_MAX;
constexpr uint32 LUMEN_SINGLE_DF_INSTANCE_BIT = 0x80000000;

int32 GLumenSceneMaxInstanceAddsPerFrame = 5000;
FAutoConsoleVariableRef CVarLumenSceneMaxInstanceAddsPerFrame(
	TEXT("r.LumenScene.MaxInstanceAddsPerFrame"),
	GLumenSceneMaxInstanceAddsPerFrame,
	TEXT("Max number of instanced allowed to be added per frame, remainder deferred to subsequent frames. (default 5000)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenMeshCardsMinSize = 30.0f;
FAutoConsoleVariableRef CVarLumenMeshCardsMinSize(
	TEXT("r.LumenScene.MeshCardsMinSize"),
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
	if (!Lumen::IsPrimitiveToDFObjectMappingRequired())
	{
		ResizeResourceIfNeeded(RHICmdList, PrimitiveToDFLumenInstanceOffsetBuffer, 16, TEXT("PrimitiveToLumenDFInstanceOffset"));
		ResizeResourceIfNeeded(RHICmdList, LumenDFInstanceToDFObjectIndexBuffer, 16, TEXT("LumenDFInstanceToDFObjectIndexBuffer"));
		PrimitiveToLumenDFInstanceOffsetBufferSize = 0;
		LumenDFInstanceToDFObjectIndexBufferSize = 0;
		return;
	}

	if (GLumenSceneUploadPrimitiveToDistanceFieldInstanceMappingEveryFrame != 0)
	{
		PrimitivesToUpdate.Reset();
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene.Primitives.Num(); ++PrimitiveIndex)
		{
			PrimitivesToUpdate.Add(PrimitiveIndex);

			FPrimitiveSceneInfo* Primitive = Scene.Primitives[PrimitiveIndex];
			if (Primitive && Primitive->LumenPrimitiveIndex >= 0)
			{
				const FLumenPrimitive& LumenPrimitive = LumenPrimitives[Primitive->LumenPrimitiveIndex];

				for (int32 InstanceIndex = 0; InstanceIndex < LumenPrimitive.LumenNumDFInstances; ++InstanceIndex)
				{
					int32 DistanceFieldObjectIndex = -1;
					if (InstanceIndex < Primitive->DistanceFieldInstanceIndices.Num())
					{
						DistanceFieldObjectIndex = Primitive->DistanceFieldInstanceIndices[InstanceIndex];
					}

					const int32 LumenDFInstanceIndex = LumenPrimitive.LumenDFInstanceOffset + InstanceIndex;
					LumenDFInstanceToDFObjectIndex[LumenDFInstanceIndex] = DistanceFieldObjectIndex;
					LumenDFInstancesToUpdate.Add(LumenDFInstanceIndex);
				}
			}
		}
	}

	// Upload PrimitiveToLumenInstance
	{
		const int32 NumIndices = FMath::RoundUpToPowerOfTwo(Scene.Primitives.Num());
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = FMath::DivideAndRoundUp<int32>(NumIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		const uint32 LastBufferSizeInBytes = PrimitiveToDFLumenInstanceOffsetBuffer.NumBytes;
		const bool bBufferResized = ResizeResourceIfNeeded(RHICmdList, PrimitiveToDFLumenInstanceOffsetBuffer, IndicesSizeInBytes, TEXT("PrimitiveToLumenInstanceOffset"));

		// Memset resized part of array to invalid offsets
		const int32 MemsetSizeInBytes = PrimitiveToDFLumenInstanceOffsetBuffer.NumBytes - LastBufferSizeInBytes;
		if (MemsetSizeInBytes > 0)
		{
			RHICmdList.Transition(FRHITransitionInfo(PrimitiveToDFLumenInstanceOffsetBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

			const int32 MemsetOffsetInBytes = LastBufferSizeInBytes;
			MemsetResource(RHICmdList, PrimitiveToDFLumenInstanceOffsetBuffer, INVALID_LUMEN_DF_INSTANCE_OFFSET, MemsetSizeInBytes, MemsetOffsetInBytes);
		}

		const int32 NumIndexUploads = PrimitivesToUpdate.Num();
		if (NumIndexUploads > 0)
		{
			ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenUploadBuffer"));

			for (int32 PrimitiveIndex : PrimitivesToUpdate)
			{
				uint32 LumenInstanceOffset = INVALID_LUMEN_DF_INSTANCE_OFFSET;
				if (PrimitiveIndex < Scene.Primitives.Num())
				{
					const FPrimitiveSceneInfo* Primitive = Scene.Primitives[PrimitiveIndex];
					if (Primitive && Primitive->LumenPrimitiveIndex >= 0)
					{
						const FLumenPrimitive& LumenPrimitive = LumenPrimitives[Primitive->LumenPrimitiveIndex];
						LumenInstanceOffset = LumenPrimitive.LumenDFInstanceOffset;

						// Handle ray tracing auto instancing where PrimitiveInstanceIndex > 0 but real PrimitiveInstanceIndex is = 0
						if (LumenPrimitive.LumenNumDFInstances <= 1)
						{
							LumenInstanceOffset |= LUMEN_SINGLE_DF_INSTANCE_BIT;
						}
					}
				}
				ByteBufferUploadBuffer.Add(PrimitiveIndex, &LumenInstanceOffset);
			}

			RHICmdList.Transition(FRHITransitionInfo(PrimitiveToDFLumenInstanceOffsetBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, PrimitiveToDFLumenInstanceOffsetBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(PrimitiveToDFLumenInstanceOffsetBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}

		PrimitiveToLumenDFInstanceOffsetBufferSize = Scene.Primitives.Num();
	}

	// Push distance field scene updates to LumenInstanceToDFObject
	{
		const FDistanceFieldSceneData& DistanceFieldSceneData = Scene.DistanceFieldSceneData;
		for (int32 DistanceFieldObjectIndex : DFObjectIndicesToUpdateInBuffer)
		{
			if (DistanceFieldObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
			{
				const FPrimitiveAndInstance& Mapping = DistanceFieldSceneData.PrimitiveInstanceMapping[DistanceFieldObjectIndex];
				if (Mapping.Primitive->LumenPrimitiveIndex >= 0)
				{
					const FLumenPrimitive& LumenPrimitive = LumenPrimitives[Mapping.Primitive->LumenPrimitiveIndex];
					if (LumenPrimitive.LumenNumDFInstances > 0)
					{
						const int32 PrimitiveIndex = Mapping.Primitive->GetIndex();
						const uint32 LumenDFInstanceIndex = LumenPrimitive.LumenDFInstanceOffset + Mapping.InstanceIndex;
						LumenDFInstanceToDFObjectIndex[LumenDFInstanceIndex] = DistanceFieldObjectIndex;
						LumenDFInstancesToUpdate.Add(LumenDFInstanceIndex);
					}
				}
			}
		}
	}

	// Upload LumenInstanceToDFObject
	{
		const int32 NumIndices = FMath::RoundUpToPowerOfTwo(LumenDFInstanceToDFObjectIndex.Num());
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = FMath::DivideAndRoundUp<int32>(NumIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		ResizeResourceIfNeeded(RHICmdList, LumenDFInstanceToDFObjectIndexBuffer, IndicesSizeInBytes, TEXT("LumenDFInstanceToDFObjectIndexBuffer"));

		const int32 NumIndexUploads = LumenDFInstancesToUpdate.Num();
		if (NumIndexUploads > 0)
		{
			ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenUploadBuffer"));

			for (int32 LumenDFInstanceIndex : LumenDFInstancesToUpdate)
			{
				int32 DistanceFieldInstanceIndex = -1;
				if (LumenDFInstanceToDFObjectIndex.IsAllocated(LumenDFInstanceIndex))
				{
					DistanceFieldInstanceIndex = LumenDFInstanceToDFObjectIndex[LumenDFInstanceIndex];
				}
				ByteBufferUploadBuffer.Add(LumenDFInstanceIndex, &DistanceFieldInstanceIndex);
			}

			RHICmdList.Transition(FRHITransitionInfo(LumenDFInstanceToDFObjectIndexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, LumenDFInstanceToDFObjectIndexBuffer, false);
			RHICmdList.Transition(FRHITransitionInfo(LumenDFInstanceToDFObjectIndexBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}

		LumenDFInstanceToDFObjectIndexBufferSize = LumenDFInstanceToDFObjectIndex.Num();
	}
}

void UpdateLumenMeshCards(FScene& Scene, const FDistanceFieldSceneData& DistanceFieldSceneData, FLumenSceneData& LumenSceneData, FRHICommandListImmediate& RHICmdList)
{
	LLM_SCOPE_BYTAG(Lumen);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenMeshCards);

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
		const bool bResourceResized = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.MeshCardsBuffer, MeshCardsNumBytes, TEXT("LumenMeshCards"));

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
		else if (bResourceResized)
		{
			RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.MeshCardsBuffer.UAV, ERHIAccess::UAVCompute | ERHIAccess::UAVGraphics, ERHIAccess::SRVMask));
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
					const int32 LumenPrimitiveIndex = Mapping.Primitive->LumenPrimitiveIndex;

					int32 MeshCardsIndex = -1;
					if (LumenPrimitiveIndex >= 0)
					{
						const FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[LumenPrimitiveIndex];
						MeshCardsIndex = LumenPrimitive.GetMeshCardsIndex(Mapping.InstanceIndex);
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
	LumenSceneData.LumenDFInstancesToUpdate.Empty(1024);
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

	if (LumenPrimitiveInstance.MeshCardsIndex < 0 && CardRepresentationData && PrimitiveSceneInfo->HasLumenCaptureMeshPass())
	{
		FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

		if (LumenPrimitive.bMergedInstances)
		{
			FMeshCardsBuildData MeshCardsBuildData;
			BuildMeshCardsDataForMergedInstances(PrimitiveSceneInfo, MeshCardsBuildData);

			LumenPrimitiveInstance.MeshCardsIndex = AddMeshCardsFromBuildData(LocalToWorld, MeshCardsBuildData, LumenPrimitive.CardResolutionScale);

			for (int32 DFInstanceIndex = 0; DFInstanceIndex < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num(); ++DFInstanceIndex)
			{
				DFObjectIndicesToUpdateInBuffer.Add(PrimitiveSceneInfo->DistanceFieldInstanceIndices[DFInstanceIndex]);
			}
		}
		else
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
			if (PrimitiveInstances && LumenInstanceIndex < PrimitiveInstances->Num())
			{
				LocalToWorld = (*PrimitiveInstances)[LumenInstanceIndex].InstanceToLocal * LocalToWorld;
			}

			const FMeshCardsBuildData& MeshCardsBuildData = CardRepresentationData->MeshCardsBuildData;

			LumenPrimitiveInstance.MeshCardsIndex = AddMeshCardsFromBuildData(LocalToWorld, MeshCardsBuildData, LumenPrimitive.CardResolutionScale);

			if (LumenInstanceIndex < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
			{
				DFObjectIndicesToUpdateInBuffer.Add(PrimitiveSceneInfo->DistanceFieldInstanceIndices[LumenInstanceIndex]);
			}
		}

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

int32 FLumenSceneData::AddMeshCardsFromBuildData(const FMatrix& LocalToWorld, const FMeshCardsBuildData& MeshCardsBuildData, float ResolutionScale)
{
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
			const int32 FirstCardIndex = Cards.AddSpan(NumCards);

			const int32 MeshCardsIndex = MeshCards.AddSpan(1);
			MeshCards[MeshCardsIndex].Initialize(
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
				const int32 AxisIndex = CardBuildData.Orientation / 2;
				const float AxisSurfaceArea = FaceSurfaceArea[AxisIndex];
				const bool bCardPassedCulling = (!GLumenMeshCardsCullFaces || AxisSurfaceArea > MinFaceSurfaceArea);
				const bool bCardPassedLODTest = CardBuildData.LODLevel == LODLevel;

				if (bCardPassedCulling && bCardPassedLODTest)
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

void FLumenSceneData::RemoveMeshCards(FLumenPrimitive& LumenPrimitive, FLumenPrimitiveInstance& LumenPrimitiveInstance)
{
	if (LumenPrimitiveInstance.MeshCardsIndex >= 0)
	{
		const FLumenMeshCards& MeshCardsInstance = MeshCards[LumenPrimitiveInstance.MeshCardsIndex];

		for (uint32 CardIndex = MeshCardsInstance.FirstCardIndex; CardIndex < MeshCardsInstance.FirstCardIndex + MeshCardsInstance.NumCards; ++CardIndex)
		{
			RemoveCardFromVisibleCardList(CardIndex);
			Cards[CardIndex].RemoveFromAtlas(*this);
			CardIndicesToUpdateInBuffer.Add(CardIndex);
		}

		Cards.RemoveSpan(MeshCardsInstance.FirstCardIndex, MeshCardsInstance.NumCards);
		MeshCards.RemoveSpan(LumenPrimitiveInstance.MeshCardsIndex, 1);

		MeshCardsIndicesToUpdateInBuffer.Add(LumenPrimitiveInstance.MeshCardsIndex);

		LumenPrimitiveInstance.MeshCardsIndex = -1;

		--LumenPrimitive.NumMeshCards;
		checkSlow(LumenPrimitive.NumMeshCards >= 0);
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