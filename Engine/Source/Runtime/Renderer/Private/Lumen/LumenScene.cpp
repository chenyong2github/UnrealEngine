// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScene.cpp
=============================================================================*/

#include "LumenMeshCards.h"
#include "RendererPrivate.h"
#include "Lumen.h"

extern void BuildMeshCardsDataForMergedInstances(const FPrimitiveSceneInfo* PrimitiveSceneInfo, FMeshCardsBuildData& MeshCardsBuildData);

FLumenSceneData::FLumenSceneData(EShaderPlatform ShaderPlatform, EWorldType::Type WorldType) :
	Generation(0),
	bFinalLightingAtlasContentsValid(false),
	MaxAtlasSize(0, 0),
	AtlasAllocator(FIntPoint(1, 1), 1)
{
	LLM_SCOPE_BYTAG(Lumen);

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MeshCardRepresentation"));

	bTrackAllPrimitives = (DoesPlatformSupportLumenGI(ShaderPlatform)) && CVar->GetValueOnGameThread() != 0 && WorldType != EWorldType::EditorPreview;
}

FLumenSceneData::~FLumenSceneData()
{
	LLM_SCOPE_BYTAG(Lumen);

	for (FLumenCard& Card : Cards)
	{
		Card.IndexInVisibleCardIndexBuffer = -1;
		Card.RemoveFromAtlas(*this);
	}

	Cards.Reset();
	MeshCards.Reset();
}

bool TrackPrimitiveForLumenScene(const FPrimitiveSceneProxy* Proxy)
{
	bool bTrack = Proxy->AffectsDynamicIndirectLighting()
		&& Proxy->SupportsMeshCardRepresentation()
		// For now Lumen depends on the distance field representation. 
		// This also makes sure that non opaque things won't get included in Lumen Scene
		&& Proxy->SupportsDistanceFieldRepresentation()
		&& (Proxy->IsDrawnInGame() || Proxy->CastsHiddenShadow());

	return bTrack;
}

bool TrackPrimitiveInstanceForLumenScene(const FMatrix& LocalToWorld, const FBox& LocalBoundingBox)
{
	const FVector LocalToWorldScale = LocalToWorld.GetScaleVector();
	const FVector ScaledBoundSize = LocalBoundingBox.GetSize() * LocalToWorldScale;
	const FVector FaceSurfaceArea(ScaledBoundSize.Y * ScaledBoundSize.Z, ScaledBoundSize.X * ScaledBoundSize.Z, ScaledBoundSize.Y * ScaledBoundSize.X);
	const float LargestFaceArea = FaceSurfaceArea.GetMax();

	extern float GLumenMeshCardsMinSize;
	const float MinFaceSurfaceArea = GLumenMeshCardsMinSize * GLumenMeshCardsMinSize;

	return LargestFaceArea > MinFaceSurfaceArea;
}

void FLumenSceneData::AddPrimitiveToUpdate(int32 PrimitiveIndex)
{
	if (bTrackAllPrimitives)
	{
		if (PrimitiveIndex + 1 > PrimitivesMarkedToUpdate.Num())
		{
			const int32 NewSize = Align(PrimitiveIndex + 1, 64);
			PrimitivesMarkedToUpdate.Add(0, NewSize - PrimitivesMarkedToUpdate.Num());
		}

		// Make sure we aren't updating same primitive multiple times.
		if (!PrimitivesMarkedToUpdate[PrimitiveIndex])
		{
			PrimitivesToUpdate.Add(PrimitiveIndex);
			PrimitivesMarkedToUpdate[PrimitiveIndex] = true;
		}
	}
}

void FLumenSceneData::AddPrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if (bTrackAllPrimitives && TrackPrimitiveForLumenScene(Proxy))
	{
		checkSlow(!PendingAddOperations.Contains(InPrimitive));
		checkSlow(!PendingUpdateOperations.Contains(InPrimitive));
		PendingAddOperations.Add(InPrimitive);
	}
}

void FLumenSceneData::UpdatePrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if (bTrackAllPrimitives
		&& TrackPrimitiveForLumenScene(Proxy)
		&& !PendingUpdateOperations.Contains(InPrimitive))
	{
		bool bPendingAdd = false;
		for (FPrimitiveSceneInfo* AddPrimitive : PendingAddOperations)
		{
			if (AddPrimitive == InPrimitive)
			{
				bPendingAdd = true;
				break;
			}
		}

		if (!bPendingAdd)
		{
			PendingUpdateOperations.Add(InPrimitive);
		}
	}
}

void FLumenSceneData::RemovePrimitive(FPrimitiveSceneInfo* InPrimitive, int32 PrimitiveIndex)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if (bTrackAllPrimitives
		&& TrackPrimitiveForLumenScene(Proxy))
	{
		PendingAddOperations.Remove(InPrimitive);
		PendingUpdateOperations.Remove(InPrimitive);
		PendingRemoveOperations.Add(FLumenPrimitiveRemoveInfo(InPrimitive, PrimitiveIndex));

		InPrimitive->LumenPrimitiveIndex = -1;
	}
}

void FLumenSceneData::AddCardToVisibleCardList(int32 CardIndex)
{
	if (Cards[CardIndex].IndexInVisibleCardIndexBuffer == -1)
	{
		Cards[CardIndex].IndexInVisibleCardIndexBuffer = VisibleCardsIndices.Num();
		VisibleCardsIndices.Add(CardIndex);
	}
}

void FLumenSceneData::RemoveCardFromVisibleCardList(int32 CardIndex)
{
	const int32 IndexInVisibleCardIndexBuffer = Cards[CardIndex].IndexInVisibleCardIndexBuffer;

	if (IndexInVisibleCardIndexBuffer >= 0)
	{
		// Fixup indices of the card that is being swapped
		Cards[VisibleCardsIndices.Last()].IndexInVisibleCardIndexBuffer = IndexInVisibleCardIndexBuffer;

		VisibleCardsIndices.RemoveAtSwap(IndexInVisibleCardIndexBuffer);

		Cards[CardIndex].IndexInVisibleCardIndexBuffer = -1;
	}
}

bool Lumen::IsPrimitiveToDFObjectMappingRequired()
{
	return IsRayTracingEnabled();
}

double BoxSurfaceArea(FVector Extent)
{
	return 2.0 * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

void UpdateLumenScenePrimitives(FScene* Scene)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateLumenScenePrimitives);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenScenePrimitives);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	// Remove LumenPrimitives
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveLumenPrimitives);
		QUICK_SCOPE_CYCLE_COUNTER(RemoveLumenPrimitives);

		struct FGreaterLumenPrimitiveIndex
		{
			FORCEINLINE bool operator()(const FLumenPrimitiveRemoveInfo& A, const FLumenPrimitiveRemoveInfo& B) const
			{
				return A.LumenPrimitiveIndex > B.LumenPrimitiveIndex;
			}
		};

		// Sort from largest to smallest so we can safely RemoveAtSwap without invalidating indices in this array
		LumenSceneData.PendingRemoveOperations.Sort(FGreaterLumenPrimitiveIndex());

		for (const FLumenPrimitiveRemoveInfo& RemoveInfo : LumenSceneData.PendingRemoveOperations)
		{
			if (RemoveInfo.LumenPrimitiveIndex >= 0)
			{
				// Deallocate resources
				{
					FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[RemoveInfo.LumenPrimitiveIndex];

					if (LumenPrimitive.LumenNumDFInstances > 0)
					{
						LumenSceneData.LumenDFInstanceToDFObjectIndex.RemoveSpan(LumenPrimitive.LumenDFInstanceOffset, LumenPrimitive.LumenNumDFInstances);
						LumenSceneData.AddPrimitiveToUpdate(RemoveInfo.PrimitiveIndex);
					}

					for (FLumenPrimitiveInstance& LumenPrimitiveInstance : LumenPrimitive.Instances)
					{
						LumenSceneData.RemoveMeshCards(LumenPrimitive, LumenPrimitiveInstance);
					}

					checkSlow(LumenPrimitive.NumMeshCards == 0);
					LumenPrimitive.Instances.Empty(1);
				}

				// Swap, remove and fixup indices to the swapped element
				{
					LumenSceneData.LumenPrimitives.RemoveAtSwap(RemoveInfo.LumenPrimitiveIndex);

					if (RemoveInfo.LumenPrimitiveIndex < LumenSceneData.LumenPrimitives.Num())
					{
						const FLumenPrimitive& SwappedLumenPrimitive = LumenSceneData.LumenPrimitives[RemoveInfo.LumenPrimitiveIndex];
						SwappedLumenPrimitive.Primitive->LumenPrimitiveIndex = RemoveInfo.LumenPrimitiveIndex;
					}
				}
			}
		}
	}

	// AddLumenPrimitives
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddLumenPrimitives);
		QUICK_SCOPE_CYCLE_COUNTER(AddLumenPrimitives);

		for (FPrimitiveSceneInfo* PrimitiveSceneInfo : LumenSceneData.PendingAddOperations)
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();

			// #lumen_todo: Remove after non-Nanite per instance ISM capture is fixed (now every instance draws entire ISM)
			int32 NumInstances = PrimitiveInstances ? PrimitiveInstances->Num() : 1;
			if (!PrimitiveSceneInfo->Proxy->IsNaniteMesh())
			{
				NumInstances = 1;
			}

			bool bAnyInstanceValid = false;

			if (PrimitiveSceneInfo->HasLumenCaptureMeshPass())
			{
				const FMatrix& LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					FBox LocalBoundingBox;
					if (PrimitiveInstances && InstanceIndex < PrimitiveInstances->Num())
					{
						LocalBoundingBox = (*PrimitiveInstances)[InstanceIndex].RenderBounds.GetBox();
					}
					else
					{
						LocalBoundingBox = PrimitiveSceneInfo->Proxy->GetLocalBounds().GetBox();
					}

					if (TrackPrimitiveInstanceForLumenScene(LocalToWorld, LocalBoundingBox))
					{
						bAnyInstanceValid = true;
						break;
					}
				}
			}

			if (bAnyInstanceValid)
			{
				const int32 LumenPrimitiveIndex = LumenSceneData.LumenPrimitives.AddDefaulted(1);
				FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[LumenPrimitiveIndex];

				LumenPrimitive.Primitive = PrimitiveSceneInfo;
				LumenPrimitive.Primitive->LumenPrimitiveIndex = LumenPrimitiveIndex;
				LumenPrimitive.BoundingBox = PrimitiveSceneInfo->Proxy->GetBounds().GetBox();

				if (PrimitiveInstances && NumInstances > 1)
				{
					// Check if we can merge all instances into one MeshCards
					extern int32 GLumenMeshCardsMergeInstances;
					extern float GLumenMeshCardsMergedMaxWorldSize;
					if (GLumenMeshCardsMergeInstances
						&& NumInstances > 1
						&& LumenPrimitive.BoundingBox.GetSize().GetMax() < GLumenMeshCardsMergedMaxWorldSize)
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

						extern float GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio;
						extern float GLumenMeshCardsMergedResolutionScale;

						if (SurfaceAreaRatio < GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio)
						{
							LumenPrimitive.bMergedInstances = true;
							LumenPrimitive.CardResolutionScale = FMath::Sqrt(1.0f / SurfaceAreaRatio) * GLumenMeshCardsMergedResolutionScale;

							LumenPrimitive.Instances.SetNum(1);
							LumenPrimitive.Instances[0].BoundingBox = LocalBounds;
							LumenPrimitive.Instances[0].MeshCardsIndex = -1;
							LumenPrimitive.Instances[0].bValidMeshCards = true;
						}

#define LOG_LUMEN_PRIMITIVE_ADDS 0
#if LOG_LUMEN_PRIMITIVE_ADDS
						{
							UE_LOG(LogRenderer, Log, TEXT("AddLumenPrimitive %s: Instances: %u, Merged: %u, SurfaceAreaRatio: %.1f"),
								*LumenPrimitive.Primitive->Proxy->GetOwnerName().ToString(),
								NumInstances,
								LumenPrimitive.bMergedInstances ? 1 : 0,
								SurfaceAreaRatio);
						}
#endif
					}

					if (!LumenPrimitive.bMergedInstances)
					{
						const FMatrix& LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();
						LumenPrimitive.Instances.SetNum(NumInstances);

						for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
						{
							FLumenPrimitiveInstance& LumenInstance = LumenPrimitive.Instances[InstanceIndex];
							const FPrimitiveInstance& PrimitiveInstance = (*PrimitiveInstances)[InstanceIndex];

							const FBox& LocalBoundingBox = PrimitiveInstance.RenderBounds.GetBox();

							LumenInstance.BoundingBox = LocalBoundingBox.TransformBy(PrimitiveInstance.InstanceToLocal);
							LumenInstance.MeshCardsIndex = -1;
							LumenInstance.bValidMeshCards = true;
						}
					}
				}
				else
				{
					LumenPrimitive.Instances.SetNum(1);
					LumenPrimitive.Instances[0].BoundingBox = LumenPrimitive.BoundingBox;
					LumenPrimitive.Instances[0].MeshCardsIndex = -1;
					LumenPrimitive.Instances[0].bValidMeshCards = true;
				}

				if (Lumen::IsPrimitiveToDFObjectMappingRequired())
				{
					TArray<FMatrix> ObjectLocalToWorldTransforms;
					PrimitiveSceneInfo->Proxy->GetDistancefieldInstanceData(ObjectLocalToWorldTransforms);

					LumenPrimitive.LumenNumDFInstances = FMath::Max(ObjectLocalToWorldTransforms.Num(), 1);
					LumenPrimitive.LumenDFInstanceOffset = LumenSceneData.LumenDFInstanceToDFObjectIndex.AddSpan(LumenPrimitive.LumenNumDFInstances);

					for (int32 InstanceIndex = 0; InstanceIndex < LumenPrimitive.LumenNumDFInstances; ++InstanceIndex)
					{
						int32 DistanceFieldObjectIndex = -1;
						if (InstanceIndex < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
						{
							DistanceFieldObjectIndex = PrimitiveSceneInfo->DistanceFieldInstanceIndices[InstanceIndex];
						}

						const int32 LumenDFInstanceIndex = LumenPrimitive.LumenDFInstanceOffset + InstanceIndex;
						LumenSceneData.LumenDFInstanceToDFObjectIndex[LumenDFInstanceIndex] = DistanceFieldObjectIndex;
						LumenSceneData.LumenDFInstancesToUpdate.Add(LumenDFInstanceIndex);
					}

					LumenSceneData.AddPrimitiveToUpdate(PrimitiveSceneInfo->GetIndex());
				}
			}
		}
	}

	// UpdateLumenPrimitives
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateLumenPrimitives);
		QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenPrimitives);

		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;

			if (PrimitiveSceneInfo->LumenPrimitiveIndex >= 0)
			{
				FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[PrimitiveSceneInfo->LumenPrimitiveIndex];
				LumenPrimitive.BoundingBox = PrimitiveSceneInfo->Proxy->GetBounds().GetBox();

				const TArray<FPrimitiveInstance>* PrimitiveInstances = LumenPrimitive.Primitive->Proxy->GetPrimitiveInstances();

				if (LumenPrimitive.bMergedInstances)
				{
					FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

					FMeshCardsBuildData MeshCardsBuildData;
					BuildMeshCardsDataForMergedInstances(PrimitiveSceneInfo, MeshCardsBuildData);
					LumenSceneData.UpdateMeshCards(LocalToWorld, LumenPrimitive.Instances[0].MeshCardsIndex, MeshCardsBuildData);
				}
				else
				{
					for (int32 LumenInstanceIndex = 0; LumenInstanceIndex < LumenPrimitive.Instances.Num(); ++LumenInstanceIndex)
					{
						FLumenPrimitiveInstance& LumenInstance = LumenPrimitive.Instances[LumenInstanceIndex];
						FBox BoundingBox = LumenPrimitive.BoundingBox;
						FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

						if (PrimitiveInstances && LumenInstanceIndex < PrimitiveInstances->Num())
						{
							const FPrimitiveInstance& PrimitiveInstance = (*PrimitiveInstances)[LumenInstanceIndex];
							LocalToWorld = PrimitiveInstance.InstanceToLocal * LocalToWorld;
							BoundingBox = PrimitiveInstance.RenderBounds.GetBox().TransformBy(PrimitiveInstance.InstanceToLocal);
						}

						const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

						LumenInstance.BoundingBox = BoundingBox;
						LumenSceneData.UpdateMeshCards(LocalToWorld, LumenInstance.MeshCardsIndex, CardRepresentationData->MeshCardsBuildData);
					}
				}
			}
		}
	}

	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.PendingAddOperations.Empty(1024);
	LumenSceneData.PendingRemoveOperations.Empty(1024);
	LumenSceneData.PendingUpdateOperations.Empty(1024);
}