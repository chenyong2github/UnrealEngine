// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LODUtilities.h"
#include "Misc/MessageDialog.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "GenericQuadTree.h"
#include "Engine/SkeletalMesh.h"
#include "EditorFramework/AssetImportData.h"
#include "MeshUtilities.h"
#include "Assets/ClothingAsset.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxTextureImportData.h"
#include "Factories/FbxImportUI.h"
#include "AssetRegistryModule.h"
#include "ObjectTools.h"
#include "AssetImportTask.h"
#include "FbxImporter.h"
#include "ScopedTransaction.h"

#if WITH_APEX_CLOTHING
	#include "ApexClothingUtils.h"
#endif // #if WITH_APEX_CLOTHING

#include "ComponentReregisterContext.h"
#include "IMeshReductionManagerModule.h"
#include "Animation/SkinWeightProfile.h"

#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Framework/Application/SlateApplication.h"


#define LOCTEXT_NAMESPACE "LODUtilities"

DEFINE_LOG_CATEGORY_STATIC(LogLODUtilities, Log, All);

bool FLODUtilities::RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount /*= 0*/, bool bRegenerateEvenIfImported /*= false*/, bool bGenerateBaseLOD /*= false*/)
{
	if (SkeletalMesh)
	{
		// Unbind any existing clothing assets before we regenerate all LODs
		TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
		ClothingAssetUtils::GetMeshClothingAssetBindings(SkeletalMesh, ClothingBindings);

		for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
		{
			Binding.Asset->UnbindFromSkeletalMesh(SkeletalMesh, Binding.LODIndex);
		}

		int32 LODCount = SkeletalMesh->GetLODNum();

		if (NewLODCount > 0)
		{
			LODCount = NewLODCount;
		}

		SkeletalMesh->Modify();

		FSkeletalMeshUpdateContext UpdateContext;
		UpdateContext.SkeletalMesh = SkeletalMesh;

		// remove LODs
		int32 CurrentNumLODs = SkeletalMesh->GetLODNum();
		if (LODCount < CurrentNumLODs)
		{
			for (int32 LODIdx = CurrentNumLODs - 1; LODIdx >= LODCount; LODIdx--)
			{
				FLODUtilities::RemoveLOD(UpdateContext, LODIdx);
			}
		}
		// we need to add more
		else if (LODCount > CurrentNumLODs)
		{
			// Only create new skeletal mesh LOD level entries
			for (int32 LODIdx = CurrentNumLODs; LODIdx < LODCount; LODIdx++)
			{
				// if no previous setting found, it will use default setting. 
				FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LODIdx);
			}
		}
		else
		{
			for (int32 LODIdx = 0; LODIdx < LODCount; LODIdx++)
			{
				FSkeletalMeshLODInfo& CurrentLODInfo = *(SkeletalMesh->GetLODInfo(LODIdx));
				if ((bRegenerateEvenIfImported && LODIdx > 0) || (bGenerateBaseLOD && LODIdx == 0) || CurrentLODInfo.bHasBeenSimplified )
				{
					FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LODIdx);
				}
			}
		}

		//Restore all clothing we can
		for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
		{
			if (SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(Binding.LODIndex) &&
				SkeletalMesh->GetImportedModel()->LODModels[Binding.LODIndex].Sections.IsValidIndex(Binding.SectionIndex))
			{
				Binding.Asset->BindToSkeletalMesh(SkeletalMesh, Binding.LODIndex, Binding.SectionIndex, Binding.AssetInternalLodIndex);
			}
		}

		SkeletalMesh->PostEditChange();

		return true;
	}

	return false;
}

void FLODUtilities::RemoveLOD(FSkeletalMeshUpdateContext& UpdateContext, int32 DesiredLOD )
{
	USkeletalMesh* SkeletalMesh = UpdateContext.SkeletalMesh;
	FSkeletalMeshModel* SkelMeshModel = SkeletalMesh->GetImportedModel();

	if(SkelMeshModel->LODModels.Num() == 1 )
	{
		FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "NoLODToRemove", "No LODs to remove!") );
		return;
	}

	// Now display combo to choose which LOD to remove.
	TArray<FString> LODStrings;
	LODStrings.AddZeroed(SkelMeshModel->LODModels.Num()-1 );
	for(int32 i=0; i<SkelMeshModel->LODModels.Num()-1; i++)
	{
		LODStrings[i] = FString::Printf( TEXT("%d"), i+1 );
	}

	check( SkeletalMesh->GetLODNum() == SkelMeshModel->LODModels.Num() );

	// If its a valid LOD, kill it.
	if( DesiredLOD > 0 && DesiredLOD < SkelMeshModel->LODModels.Num() )
	{
		//We'll be modifying the skel mesh data so reregister

		//TODO - do we need to reregister something else instead?
		FMultiComponentReregisterContext ReregisterContext(UpdateContext.AssociatedComponents);

		// Release rendering resources before deleting LOD
		SkeletalMesh->ReleaseResources();

		// Block until this is done
		FlushRenderingCommands();

		SkelMeshModel->LODModels.RemoveAt(DesiredLOD);
		SkeletalMesh->RemoveLODInfo(DesiredLOD);
		SkeletalMesh->InitResources();

		RefreshLODChange(SkeletalMesh);

		// Set the forced LOD to Auto.
		for(auto Iter = UpdateContext.AssociatedComponents.CreateIterator(); Iter; ++Iter)
		{
			USkinnedMeshComponent* SkinnedComponent = Cast<USkinnedMeshComponent>(*Iter);
			if(SkinnedComponent)
			{
				SkinnedComponent->SetForcedLOD(0);
			}
		}

		//remove all Morph target data for this LOD
		for (UMorphTarget* MorphTarget : SkeletalMesh->MorphTargets)
		{
			if (MorphTarget->HasDataForLOD(DesiredLOD))
			{
				MorphTarget->MorphLODModels.RemoveAt(DesiredLOD);
			}
		}

		// This will recache derived render data, and re-init resources
		SkeletalMesh->PostEditChange();

		//Notify calling system of change
		UpdateContext.OnLODChanged.ExecuteIfBound();

		// Mark things for saving.
		SkeletalMesh->MarkPackageDirty();
	}
}

/** Given three direction vectors, indicates if A and B are on the same 'side' of Vec. */
bool VectorsOnSameSide(const FVector2D& Vec, const FVector2D& A, const FVector2D& B)
{
	return !FMath::IsNegativeFloat(((B.Y - A.Y)*(Vec.X - A.X)) + ((A.X - B.X)*(Vec.Y - A.Y)));
}

float PointToSegmentDistanceSquare(const FVector2D& A, const FVector2D& B, const FVector2D& P)
{
	return FVector2D::DistSquared(P, FMath::ClosestPointOnSegment2D(P, A, B));
}

/** Return true if P is within triangle created by A, B and C. */
bool PointInTriangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& P)
{
	//If the point is on a triangle point we consider the point inside the triangle
	
	if (P.Equals(A) || P.Equals(B) || P.Equals(C))
	{
		return true;
	}
	// If its on the same side as the remaining vert for all edges, then its inside.	
	if (VectorsOnSameSide(A, B, P) &&
		VectorsOnSameSide(B, C, P) &&
		VectorsOnSameSide(C, A, P))
	{
		return true;
	}

	//Make sure point on the edge are count inside the triangle
	if (PointToSegmentDistanceSquare(A, B, P) <= KINDA_SMALL_NUMBER)
	{
		return true;
	}
	if (PointToSegmentDistanceSquare(B, C, P) <= KINDA_SMALL_NUMBER)
	{
		return true;
	}
	if (PointToSegmentDistanceSquare(C, A, P) <= KINDA_SMALL_NUMBER)
	{
		return true;
	}
	return false;
}

/** Given three direction vectors, indicates if A and B are on the same 'side' of Vec. */
bool VectorsOnSameSide(const FVector& Vec, const FVector& A, const FVector& B, const float SameSideDotProductEpsilon)
{
	const FVector CrossA = Vec ^ A;
	const FVector CrossB = Vec ^ B;
	float DotWithEpsilon = SameSideDotProductEpsilon + (CrossA | CrossB);
	return !FMath::IsNegativeFloat(DotWithEpsilon);
}

/** Util to see if P lies within triangle created by A, B and C. */
bool PointInTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector& P)
{
	// Cross product indicates which 'side' of the vector the point is on
	// If its on the same side as the remaining vert for all edges, then its inside.	
	if (VectorsOnSameSide(B - A, P - A, C - A, KINDA_SMALL_NUMBER) &&
		VectorsOnSameSide(C - B, P - B, A - B, KINDA_SMALL_NUMBER) &&
		VectorsOnSameSide(A - C, P - C, B - C, KINDA_SMALL_NUMBER))
	{
		return true;
	}
	return false;
}

FVector GetBaryCentric(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
{
	// Compute the normal of the triangle
	const FVector TriNorm = (B - A) ^ (C - A);

	//check collinearity of A,B,C
	if (TriNorm.SizeSquared() <= SMALL_NUMBER)
	{
		float DistA = FVector::DistSquared(Point, A);
		float DistB = FVector::DistSquared(Point, B);
		float DistC = FVector::DistSquared(Point, C);
		if(DistA <= DistB && DistA <= DistC)
		{
			return FVector(1.0f, 0.0f, 0.0f);
		}
		if (DistB <= DistC)
		{
			return FVector(0.0f, 1.0f, 0.0f);
		}
		return FVector(0.0f, 0.0f, 1.0f);
	}
	return FMath::ComputeBaryCentric2D(Point, A, B, C);
}

struct FTriangleElement
{
	FBox2D UVsBound;
	FBox PositionBound;
	TArray<FSoftSkinVertex> Vertices;
	TArray<uint32> Indexes;
	uint32 TriangleIndex;
};

bool FindTriangleUVMatch(const FVector2D& TargetUV, const TArray<FTriangleElement>& Triangles, const TArray<uint32>& QuadTreeTriangleResults, TArray<uint32>& MatchTriangleIndexes)
{
	for (uint32 TriangleIndex : QuadTreeTriangleResults)
	{
		const FTriangleElement& TriangleElement = Triangles[TriangleIndex];
		if (PointInTriangle(TriangleElement.Vertices[0].UVs[0], TriangleElement.Vertices[1].UVs[0], TriangleElement.Vertices[2].UVs[0], TargetUV))
		{
			MatchTriangleIndexes.Add(TriangleIndex);
		}
		TriangleIndex++;
	}
	return MatchTriangleIndexes.Num() == 0 ? false : true;
}

bool FindTrianglePositionMatch(const FVector& Position, const TArray<FTriangleElement>& Triangles, const TArray<FTriangleElement>& OcTreeTriangleResults, TArray<uint32>& MatchTriangleIndexes)
{
	for (const FTriangleElement& Triangle : OcTreeTriangleResults)
	{
		uint32 TriangleIndex = Triangle.TriangleIndex;
		const FTriangleElement& TriangleElement = Triangles[TriangleIndex];
		if (PointInTriangle(TriangleElement.Vertices[0].Position, TriangleElement.Vertices[1].Position, TriangleElement.Vertices[2].Position, Position))
		{
			MatchTriangleIndexes.Add(TriangleIndex);
		}
		TriangleIndex++;
	}
	return MatchTriangleIndexes.Num() == 0 ? false : true;
}

struct FTargetMatch
{
	float BarycentricWeight[3]; //The weight we use to interpolate the TARGET data
	uint32 Indices[3]; //BASE Index of the triangle vertice
};

void ProjectTargetOnBase(const TArray<FSoftSkinVertex>& BaseVertices, const TArray<TArray<uint32>>& PerSectionBaseTriangleIndices,
						 TArray<FTargetMatch>& TargetMatchData, const TArray<FSkelMeshSection>& TargetSections, const TArray<int32>& TargetSectionMatchBaseIndex, const TCHAR* DebugContext)
{
	bool bNoMatchMsgDone = false;
	TArray<FTriangleElement> Triangles;
	//Project section target vertices on match base section using the UVs coordinates
	for (int32 SectionIndex = 0; SectionIndex < TargetSections.Num(); ++SectionIndex)
	{
		//Use the remap base index in case some sections disappear during the reduce phase
		int32 BaseSectionIndex = TargetSectionMatchBaseIndex[SectionIndex];
		if (BaseSectionIndex == INDEX_NONE || !PerSectionBaseTriangleIndices.IsValidIndex(BaseSectionIndex))
		{
			continue;
		}
		//Target vertices for the Section
		const TArray<FSoftSkinVertex>& TargetVertices = TargetSections[SectionIndex].SoftVertices;
		//Base Triangle indices for the matched base section
		const TArray<uint32>& BaseTriangleIndices = PerSectionBaseTriangleIndices[BaseSectionIndex];
		FBox2D BaseMeshUVBound(EForceInit::ForceInit);
		FBox BaseMeshPositionBound(EForceInit::ForceInit);
		//Fill the triangle element to speed up the triangle research
		Triangles.Reset(BaseTriangleIndices.Num() / 3);
		for (uint32 TriangleIndex = 0; TriangleIndex < (uint32)BaseTriangleIndices.Num(); TriangleIndex += 3)
		{
			FTriangleElement TriangleElement;
			TriangleElement.UVsBound.Init();
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				uint32 CornerIndice = BaseTriangleIndices[TriangleIndex + Corner];
				check(BaseVertices.IsValidIndex(CornerIndice));
				const FSoftSkinVertex& BaseVertex = BaseVertices[CornerIndice];
				TriangleElement.Indexes.Add(CornerIndice);
				TriangleElement.Vertices.Add(BaseVertex);
				TriangleElement.UVsBound += BaseVertex.UVs[0];
				BaseMeshPositionBound += BaseVertex.Position;
			}
			BaseMeshUVBound += TriangleElement.UVsBound;
			TriangleElement.TriangleIndex = Triangles.Num();
			Triangles.Add(TriangleElement);
		}
		//Setup the Quad tree
		float UVsQuadTreeMinSize = 0.001f;
		TQuadTree<uint32, 100> QuadTree(BaseMeshUVBound, UVsQuadTreeMinSize);
		for (FTriangleElement& TriangleElement : Triangles)
		{
			QuadTree.Insert(TriangleElement.TriangleIndex, TriangleElement.UVsBound, DebugContext);
		}
		//Retrieve all triangle that are close to our point, let get 5% of UV extend
		float DistanceThreshold = BaseMeshUVBound.GetExtent().Size()*0.05f;
		//Find a match triangle for every target vertices
		TArray<uint32> QuadTreeTriangleResults;
		QuadTreeTriangleResults.Reserve(Triangles.Num() / 10); //Reserve 10% to speed up the query
		for (uint32 TargetVertexIndex = 0; TargetVertexIndex < (uint32)TargetVertices.Num(); ++TargetVertexIndex)
		{
			FVector2D TargetUV = TargetVertices[TargetVertexIndex].UVs[0];
			//Reset the last data without flushing the memmery allocation
			QuadTreeTriangleResults.Reset();
			const uint32 FullTargetIndex = TargetSections[SectionIndex].BaseVertexIndex + TargetVertexIndex;
			//Make sure the array is allocate properly
			if (!TargetMatchData.IsValidIndex(FullTargetIndex))
			{
				continue;
			}
			//Set default data for the target match, in case we cannot found a match
			FTargetMatch& TargetMatch = TargetMatchData[FullTargetIndex];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				TargetMatch.Indices[Corner] = INDEX_NONE;
				TargetMatch.BarycentricWeight[Corner] = 0.3333f; //The weight will be use to found the proper delta
			}

			FVector2D Extent(DistanceThreshold, DistanceThreshold);
			FBox2D CurBox(TargetUV - Extent, TargetUV + Extent);
			while (QuadTreeTriangleResults.Num() <= 0)
			{
				QuadTree.GetElements(CurBox, QuadTreeTriangleResults);
				Extent *= 2;
				CurBox = FBox2D(TargetUV - Extent, TargetUV + Extent);
			}

			auto GetDistancePointToBaseTriangle = [&Triangles, &TargetVertices, &TargetVertexIndex](const uint32 BaseTriangleIndex)->float
			{
				FTriangleElement& CandidateTriangle = Triangles[BaseTriangleIndex];
				return FVector::DistSquared(FMath::ClosestPointOnTriangleToPoint(TargetVertices[TargetVertexIndex].Position, CandidateTriangle.Vertices[0].Position, CandidateTriangle.Vertices[1].Position, CandidateTriangle.Vertices[2].Position), TargetVertices[TargetVertexIndex].Position);
			};

			auto FailSafeUnmatchVertex = [&GetDistancePointToBaseTriangle, &QuadTreeTriangleResults](uint32 &OutIndexMatch)->bool
			{
				bool bFoundMatch = false;
				float ClosestTriangleDistSquared = MAX_flt;
				for (uint32 MatchTriangleIndex : QuadTreeTriangleResults)
				{
					float TriangleDistSquared = GetDistancePointToBaseTriangle(MatchTriangleIndex);
					if (TriangleDistSquared < ClosestTriangleDistSquared)
					{
						ClosestTriangleDistSquared = TriangleDistSquared;
						OutIndexMatch = MatchTriangleIndex;
						bFoundMatch = true;
					}
				}
				return bFoundMatch;
			};

			//Find all Triangles that contain the Target UV
			if (QuadTreeTriangleResults.Num() > 0)
			{
				TArray<uint32> MatchTriangleIndexes;
				uint32 FoundIndexMatch = INDEX_NONE;
				if(!FindTriangleUVMatch(TargetUV, Triangles, QuadTreeTriangleResults, MatchTriangleIndexes))
				{
					if (!FailSafeUnmatchVertex(FoundIndexMatch))
					{
						//We should always have a match
						if (!bNoMatchMsgDone)
						{
							UE_LOG(LogLODUtilities, Warning, TEXT("Reduce LOD, remap morph target: Cannot find a triangle from the base LOD that contain a vertex UV in the target LOD. Remap morph target quality will be lower."));
							bNoMatchMsgDone = true;
						}
						continue;
					}
				}
				float ClosestTriangleDistSquared = MAX_flt;
				if (MatchTriangleIndexes.Num() == 1)
				{
					//One match, this mean no mirror UVs simply take the single match
					FoundIndexMatch = MatchTriangleIndexes[0];
					ClosestTriangleDistSquared = GetDistancePointToBaseTriangle(FoundIndexMatch);
				}
				else
				{
					//Geometry can use mirror so the UVs are not unique. Use the closest match triangle to the point to find the best match
					for (uint32 MatchTriangleIndex : MatchTriangleIndexes)
					{
						float TriangleDistSquared = GetDistancePointToBaseTriangle(MatchTriangleIndex);
						if (TriangleDistSquared < ClosestTriangleDistSquared)
						{
							ClosestTriangleDistSquared = TriangleDistSquared;
							FoundIndexMatch = MatchTriangleIndex;
						}
					}
				}

				//FAIL SAFE, make sure we have a match that make sense
				//Use the mesh section geometry bound extent (10% of it) to validate we are close enough.
				if (ClosestTriangleDistSquared > BaseMeshPositionBound.GetExtent().SizeSquared()*0.1f)
				{
					//Executing fail safe, if the UVs are too much off because of the reduction, use the closest distance to polygons to find the match
					//This path is not optimize and should not happen often.
					FailSafeUnmatchVertex(FoundIndexMatch);
				}

				//We should always have a valid match at this point
				check(FoundIndexMatch != INDEX_NONE);
				FTriangleElement& BestTriangle = Triangles[FoundIndexMatch];
				//Found the surface area of the 3 barycentric triangles from the UVs
				FVector BarycentricWeight;
				BarycentricWeight = GetBaryCentric(FVector(TargetUV, 0.0f), FVector(BestTriangle.Vertices[0].UVs[0], 0.0f), FVector(BestTriangle.Vertices[1].UVs[0], 0.0f), FVector(BestTriangle.Vertices[2].UVs[0], 0.0f));
				//Fill the target match
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					TargetMatch.Indices[Corner] = BestTriangle.Indexes[Corner];
					TargetMatch.BarycentricWeight[Corner] = BarycentricWeight[Corner]; //The weight will be use to found the proper delta
				}
			}
			else
			{
				if (!bNoMatchMsgDone)
				{
					UE_LOG(LogLODUtilities, Warning, TEXT("Reduce LOD, remap morph target: Cannot find a triangle from the base LOD that contain a vertex UV in the target LOD. Remap morph target quality will be lower."));
					bNoMatchMsgDone = true;
				}
				continue;
			}
		}
	}
}

void CreateLODMorphTarget(USkeletalMesh* SkeletalMesh, FReductionBaseSkeletalMeshBulkData* ReductionBaseSkeletalMeshBulkData, int32 SourceLOD, int32 DestinationLOD, const TMap<UMorphTarget *, TMap<uint32, uint32>>& PerMorphTargetBaseIndexToMorphTargetDelta, const TMap<uint32, TArray<uint32>>& BaseMorphIndexToTargetIndexList, const TArray<FSoftSkinVertex>& TargetVertices, const TArray<FTargetMatch>& TargetMatchData)
{
	TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
	if (ReductionBaseSkeletalMeshBulkData != nullptr)
	{
		FSkeletalMeshLODModel TempBaseLODModel;
		ReductionBaseSkeletalMeshBulkData->LoadReductionData(TempBaseLODModel, BaseLODMorphTargetData);
	}

	FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();
	const FSkeletalMeshLODModel& TargetLODModel = SkeletalMeshModel->LODModels[DestinationLOD];

	bool bInitializeMorphData = false;

	for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
	{
		if (!MorphTarget->HasDataForLOD(SourceLOD))
		{
			continue;
		}
		bool bUseBaseMorphDelta = SourceLOD == DestinationLOD && BaseLODMorphTargetData.Contains(MorphTarget->GetFullName());

		const TArray<FMorphTargetDelta> *BaseMorphDeltas = bUseBaseMorphDelta ? BaseLODMorphTargetData.Find(MorphTarget->GetFullName()) : nullptr;
		if (BaseMorphDeltas == nullptr || BaseMorphDeltas->Num() <= 0)
		{
			bUseBaseMorphDelta = false;
		}

		const TMap<uint32, uint32>& BaseIndexToMorphTargetDelta = PerMorphTargetBaseIndexToMorphTargetDelta[MorphTarget];
		TArray<FMorphTargetDelta> NewMorphTargetDeltas;
		TSet<uint32> CreatedTargetIndex;
		TMap<FVector, TArray<uint32>> MorphTargetPerPosition;
		const FMorphTargetLODModel& BaseMorphModel = MorphTarget->MorphLODModels[SourceLOD];
		//Iterate each original morph target source index to fill the NewMorphTargetDeltas array with the TargetMatchData.
		const TArray<FMorphTargetDelta>& Vertices = bUseBaseMorphDelta ? *BaseMorphDeltas : BaseMorphModel.Vertices;
		for (uint32 MorphDeltaIndex = 0; MorphDeltaIndex < (uint32)(Vertices.Num()); ++MorphDeltaIndex)
		{
			const FMorphTargetDelta& MorphDelta = Vertices[MorphDeltaIndex];
			const TArray<uint32>* TargetIndexesPtr = BaseMorphIndexToTargetIndexList.Find(MorphDelta.SourceIdx);
			if (TargetIndexesPtr == nullptr)
			{
				continue;
			}
			const TArray<uint32>& TargetIndexes = *TargetIndexesPtr;
			for (int32 MorphTargetIndex = 0; MorphTargetIndex < TargetIndexes.Num(); ++MorphTargetIndex)
			{
				uint32 TargetIndex = TargetIndexes[MorphTargetIndex];
				if (CreatedTargetIndex.Contains(TargetIndex))
				{
					continue;
				}
				CreatedTargetIndex.Add(TargetIndex);
				const FVector& SearchPosition = TargetVertices[TargetIndex].Position;
				FMorphTargetDelta MatchMorphDelta;
				MatchMorphDelta.SourceIdx = TargetIndex;

				const FTargetMatch& TargetMatch = TargetMatchData[TargetIndex];

				//Find the Position/tangent delta for the MatchMorphDelta using the barycentric weight
				MatchMorphDelta.PositionDelta = FVector(0.0f);
				MatchMorphDelta.TangentZDelta = FVector(0.0f);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const uint32* BaseMorphTargetIndexPtr = BaseIndexToMorphTargetDelta.Find(TargetMatch.Indices[Corner]);
					if (BaseMorphTargetIndexPtr != nullptr && Vertices.IsValidIndex(*BaseMorphTargetIndexPtr))
					{
						const FMorphTargetDelta& BaseMorphTargetDelta = Vertices[*BaseMorphTargetIndexPtr];
						FVector BasePositionDelta = !BaseMorphTargetDelta.PositionDelta.ContainsNaN() ? BaseMorphTargetDelta.PositionDelta : FVector(0.0f);
						FVector BaseTangentZDelta = !BaseMorphTargetDelta.TangentZDelta.ContainsNaN() ? BaseMorphTargetDelta.TangentZDelta : FVector(0.0f);
						MatchMorphDelta.PositionDelta += BasePositionDelta * TargetMatch.BarycentricWeight[Corner];
						MatchMorphDelta.TangentZDelta += BaseTangentZDelta * TargetMatch.BarycentricWeight[Corner];
					}
					ensure(!MatchMorphDelta.PositionDelta.ContainsNaN());
					ensure(!MatchMorphDelta.TangentZDelta.ContainsNaN());
				}

				//Make sure all morph delta that are at the same position use the same delta to avoid hole in the geometry
				TArray<uint32> *MorphTargetsIndexUsingPosition = nullptr;
				MorphTargetsIndexUsingPosition = MorphTargetPerPosition.Find(SearchPosition);
				if (MorphTargetsIndexUsingPosition != nullptr)
				{
					//Get the maximum position/tangent delta for the existing matched morph delta
					FVector PositionDelta = MatchMorphDelta.PositionDelta;
					FVector TangentZDelta = MatchMorphDelta.TangentZDelta;
					for (uint32 ExistingMorphTargetIndex : *MorphTargetsIndexUsingPosition)
					{
						const FMorphTargetDelta& ExistingMorphDelta = NewMorphTargetDeltas[ExistingMorphTargetIndex];
						PositionDelta = PositionDelta.SizeSquared() > ExistingMorphDelta.PositionDelta.SizeSquared() ? PositionDelta : ExistingMorphDelta.PositionDelta;
						TangentZDelta = TangentZDelta.SizeSquared() > ExistingMorphDelta.TangentZDelta.SizeSquared() ? TangentZDelta : ExistingMorphDelta.TangentZDelta;
					}
					//Update all MorphTarget that share the same position.
					for (uint32 ExistingMorphTargetIndex : *MorphTargetsIndexUsingPosition)
					{
						FMorphTargetDelta& ExistingMorphDelta = NewMorphTargetDeltas[ExistingMorphTargetIndex];
						ExistingMorphDelta.PositionDelta = PositionDelta;
						ExistingMorphDelta.TangentZDelta = TangentZDelta;
					}
					MatchMorphDelta.PositionDelta = PositionDelta;
					MatchMorphDelta.TangentZDelta = TangentZDelta;
					MorphTargetsIndexUsingPosition->Add(NewMorphTargetDeltas.Num());
				}
				else
				{
					MorphTargetPerPosition.Add(TargetVertices[TargetIndex].Position).Add(NewMorphTargetDeltas.Num());
				}
				NewMorphTargetDeltas.Add(MatchMorphDelta);
			}
		}
		
		//Register the new morph target on the target LOD
		MorphTarget->PopulateDeltas(NewMorphTargetDeltas, DestinationLOD, TargetLODModel.Sections, false, true);
		if (MorphTarget->HasValidData())
		{
			bInitializeMorphData |= SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
		}
	}

	if (bInitializeMorphData)
	{
		SkeletalMesh->InitMorphTargetsAndRebuildRenderData();
	}
}

void FLODUtilities::ClearGeneratedMorphTarget(USkeletalMesh* SkeletalMesh, int32 TargetLOD)
{
	check(SkeletalMesh);
	FSkeletalMeshModel* SkeletalMeshResource = SkeletalMesh->GetImportedModel();
	if (!SkeletalMeshResource ||
		!SkeletalMeshResource->LODModels.IsValidIndex(TargetLOD))
	{
		//Abort clearing 
		return;
	}

	const FSkeletalMeshLODModel& TargetLODModel = SkeletalMeshResource->LODModels[TargetLOD];
	//Make sure we have some morph for this LOD
	for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
	{
		if (!MorphTarget->HasDataForLOD(TargetLOD))
		{
			continue;
		}

		//if (MorphTarget->MorphLODModels[TargetLOD].bGeneratedByEngine)
		{
			MorphTarget->MorphLODModels[TargetLOD].Reset();

			// if this is the last one, we can remove empty ones
			if (TargetLOD == MorphTarget->MorphLODModels.Num() - 1)
			{
				MorphTarget->RemoveEmptyMorphTargets();
			}
		}
	}
}

void FLODUtilities::ApplyMorphTargetsToLOD(USkeletalMesh* SkeletalMesh, int32 SourceLOD, int32 DestinationLOD)
{
	check(SkeletalMesh);
	FSkeletalMeshModel* SkeletalMeshResource = SkeletalMesh->GetImportedModel();
	if (!SkeletalMeshResource ||
		!SkeletalMeshResource->LODModels.IsValidIndex(SourceLOD) ||
		!SkeletalMeshResource->LODModels.IsValidIndex(DestinationLOD) ||
		SourceLOD > DestinationLOD)
	{
		//Cannot reduce if the source model is missing or we reduce from a higher index LOD
		return;
	}

	FSkeletalMeshLODModel& SourceLODModel = SkeletalMeshResource->LODModels[SourceLOD];
	FReductionBaseSkeletalMeshBulkData* ReductionBaseSkeletalMeshBulkData = nullptr;
	bool bReduceBaseLOD = DestinationLOD == SourceLOD && SkeletalMeshResource->OriginalReductionSourceMeshData.IsValidIndex(SourceLOD) && !SkeletalMeshResource->OriginalReductionSourceMeshData[SourceLOD]->IsEmpty();
	if (!bReduceBaseLOD && SourceLOD == DestinationLOD)
	{
		//Abort remapping of morph target since the data is missing
		return;
	}
	if (bReduceBaseLOD)
	{
		ReductionBaseSkeletalMeshBulkData = SkeletalMeshResource->OriginalReductionSourceMeshData[SourceLOD];
	}

	FSkeletalMeshLODModel TempBaseLODModel;
	TMap<FString, TArray<FMorphTargetDelta>> TempBaseLODMorphTargetData;
	if (bReduceBaseLOD)
	{
		ReductionBaseSkeletalMeshBulkData->LoadReductionData(TempBaseLODModel, TempBaseLODMorphTargetData);
	}

	const FSkeletalMeshLODModel& BaseLODModel = bReduceBaseLOD ? TempBaseLODModel : SkeletalMeshResource->LODModels[SourceLOD];
	const FSkeletalMeshLODModel& TargetLODModel = SkeletalMeshResource->LODModels[DestinationLOD];
	//Make sure we have some morph for this LOD
	bool bContainsMorphTargets = false;
	for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
	{
		if (MorphTarget->HasDataForLOD(SourceLOD))
		{
			bContainsMorphTargets = true;
		}
	}
	if (!bContainsMorphTargets)
	{
		//No morph target to remap
		return;
	}

	//We have to match target sections index with the correct base section index. Reduced LODs can contain a different number of sections than the base LOD
	TArray<int32> TargetSectionMatchBaseIndex;
	//Initialize the array to INDEX_NONE
	TargetSectionMatchBaseIndex.AddUninitialized(TargetLODModel.Sections.Num());
	for (int32 TargetSectionIndex = 0; TargetSectionIndex < TargetLODModel.Sections.Num(); ++TargetSectionIndex)
	{
		TargetSectionMatchBaseIndex[TargetSectionIndex] = INDEX_NONE;
	}
	//Find corresponding section indices from Source LOD for Target LOD
	for (int32 BaseSectionIndex = 0; BaseSectionIndex < BaseLODModel.Sections.Num(); ++BaseSectionIndex)
	{
		int32 TargetSectionIndexMatch = INDEX_NONE;
		for (int32 TargetSectionIndex = 0; TargetSectionIndex < TargetLODModel.Sections.Num(); ++TargetSectionIndex)
		{
			if (TargetLODModel.Sections[TargetSectionIndex].MaterialIndex == BaseLODModel.Sections[BaseSectionIndex].MaterialIndex && TargetSectionMatchBaseIndex[TargetSectionIndex] == INDEX_NONE)
			{
				TargetSectionIndexMatch = TargetSectionIndex;
				break;
			}
		}
		//We can set the data only once. There should be no clash
		if (TargetSectionMatchBaseIndex.IsValidIndex(TargetSectionIndexMatch) && TargetSectionMatchBaseIndex[TargetSectionIndexMatch] == INDEX_NONE)
		{
			TargetSectionMatchBaseIndex[TargetSectionIndexMatch] = BaseSectionIndex;
		}
	}
	//We should have match all the target sections
	check(!TargetSectionMatchBaseIndex.Contains(INDEX_NONE));
	TArray<FSoftSkinVertex> BaseVertices;
	TArray<FSoftSkinVertex> TargetVertices;
	BaseLODModel.GetNonClothVertices(BaseVertices);
	TargetLODModel.GetNonClothVertices(TargetVertices);
	//Create the base triangle indices per section
	TArray<TArray<uint32>> BaseTriangleIndices;
	int32 SectionCount = BaseLODModel.NumNonClothingSections();
	BaseTriangleIndices.AddDefaulted(SectionCount);
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		const FSkelMeshSection& Section = BaseLODModel.Sections[SectionIndex];
		uint32 TriangleCount = Section.NumTriangles;
		for (uint32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			for (uint32 PointIndex = 0; PointIndex < 3; PointIndex++)
			{
				BaseTriangleIndices[SectionIndex].Add(BaseLODModel.IndexBuffer[Section.BaseIndex + ((TriangleIndex * 3) + PointIndex)]);
			}
		}
	}
	//Every target vertices match a Base LOD triangle, we also want the barycentric weight of the triangle match. All this done using the UVs
	TArray<FTargetMatch> TargetMatchData;
	TargetMatchData.AddUninitialized(TargetVertices.Num());
	//Match all target vertices to a Base triangle Using UVs.
	ProjectTargetOnBase(BaseVertices, BaseTriangleIndices, TargetMatchData, TargetLODModel.Sections, TargetSectionMatchBaseIndex, *SkeletalMesh->GetName());
	//Helper to retrieve the FMorphTargetDelta from the BaseIndex
	TMap<UMorphTarget *, TMap<uint32, uint32>> PerMorphTargetBaseIndexToMorphTargetDelta;
	//Create a map from BaseIndex to a list of match target index for all base morph target point
	TMap<uint32, TArray<uint32>> BaseMorphIndexToTargetIndexList;
	for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
	{
		if (!MorphTarget->HasDataForLOD(SourceLOD))
		{
			continue;
		}

		bool bUseTempMorphDelta = SourceLOD == DestinationLOD && bReduceBaseLOD && TempBaseLODMorphTargetData.Contains(MorphTarget->GetFullName());
		const TArray<FMorphTargetDelta> *TempMorphDeltas = bUseTempMorphDelta ? TempBaseLODMorphTargetData.Find(MorphTarget->GetFullName()) : nullptr;
		if (TempMorphDeltas == nullptr || TempMorphDeltas->Num() <= 0)
		{
			bUseTempMorphDelta = false;
		}

		TMap<uint32, uint32>& BaseIndexToMorphTargetDelta = PerMorphTargetBaseIndexToMorphTargetDelta.FindOrAdd(MorphTarget);
		const FMorphTargetLODModel& BaseMorphModel = MorphTarget->MorphLODModels[SourceLOD];
		const TArray<FMorphTargetDelta>& Vertices = bUseTempMorphDelta ? *TempMorphDeltas : BaseMorphModel.Vertices;
		for (uint32 MorphDeltaIndex = 0; MorphDeltaIndex < (uint32)(Vertices.Num()); ++MorphDeltaIndex)
		{
			const FMorphTargetDelta& MorphDelta = Vertices[MorphDeltaIndex];
			BaseIndexToMorphTargetDelta.Add(MorphDelta.SourceIdx, MorphDeltaIndex);
			//Iterate the targetmatch data so we can store which target indexes is impacted by this morph delta.
			for (int32 TargetIndex = 0; TargetIndex < TargetMatchData.Num(); ++TargetIndex)
			{
				const FTargetMatch& TargetMatch = TargetMatchData[TargetIndex];
				if (TargetMatch.Indices[0] == INDEX_NONE)
				{
					//In case this vertex did not found a triangle match
					continue;
				}
				if (TargetMatch.Indices[0] == MorphDelta.SourceIdx || TargetMatch.Indices[1] == MorphDelta.SourceIdx || TargetMatch.Indices[2] == MorphDelta.SourceIdx)
				{
					TArray<uint32>& TargetIndexes = BaseMorphIndexToTargetIndexList.FindOrAdd(MorphDelta.SourceIdx);
					TargetIndexes.AddUnique(TargetIndex);
				}
			}
		}
	}
	//Create the target morph target
	CreateLODMorphTarget(SkeletalMesh, ReductionBaseSkeletalMeshBulkData, SourceLOD, DestinationLOD, PerMorphTargetBaseIndexToMorphTargetDelta, BaseMorphIndexToTargetIndexList, TargetVertices, TargetMatchData);
}

void FLODUtilities::SimplifySkeletalMeshLOD( USkeletalMesh* SkeletalMesh, int32 DesiredLOD, bool bReregisterComponent /*= true*/, bool bRestoreClothing /*= false*/)
{
	IMeshReductionModule& ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionModule>("MeshReductionInterface");
	IMeshReduction* MeshReduction = ReductionModule.GetSkeletalMeshReductionInterface();

	check (MeshReduction && MeshReduction->IsSupported());

	if (DesiredLOD == 0
		&& SkeletalMesh->GetLODInfo(DesiredLOD) != nullptr
		&& SkeletalMesh->GetLODInfo(DesiredLOD)->bHasBeenSimplified
		&& (!SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData.IsValidIndex(0) || SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[0]->IsEmpty()))
	{
		//The base LOD was reduce and there is no valid data, we cannot regenerate this lod it must be re-import before
		FFormatNamedArguments Args;
		Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMesh->GetName()));
		Args.Add(TEXT("LODIndex"), FText::AsNumber(DesiredLOD));
		FText Message = FText::Format(NSLOCTEXT("UnrealEd", "MeshSimp_GenerateLODCannotGenerateMissingData", "Cannot generate LOD {LODIndex} for skeletal mesh '{SkeletalMeshName}'. This LOD must be re-import to create the necessary data"), Args);
		if (FApp::IsUnattended())
		{
			UE_LOG(LogLODUtilities, Warning, TEXT("%s"), *(Message.ToString()));
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, Message);
		}
		return;
	}

	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("DesiredLOD"), DesiredLOD);
		Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMesh->GetName()));
		const FText StatusUpdate = FText::Format(NSLOCTEXT("UnrealEd", "MeshSimp_GeneratingLOD_F", "Generating LOD{DesiredLOD} for {SkeletalMeshName}..."), Args);
		GWarn->BeginSlowTask(StatusUpdate, true);
	}

	// Unbind DesiredLOD existing clothing assets before we simplify this LOD
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	if (bRestoreClothing)
	{
		ClothingAssetUtils::GetMeshClothingAssetBindings(SkeletalMesh, ClothingBindings);
		for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
		{
			if (DesiredLOD == Binding.LODIndex)
			{
				Binding.Asset->UnbindFromSkeletalMesh(SkeletalMesh, Binding.LODIndex);
			}
		}
	}

	if (SkeletalMesh->GetLODInfo(DesiredLOD) != nullptr)
	{
		FSkeletalMeshModel* SkeletalMeshResource = SkeletalMesh->GetImportedModel();
		FSkeletalMeshOptimizationSettings& Settings = SkeletalMesh->GetLODInfo(DesiredLOD)->ReductionSettings;

		if (SkeletalMeshResource->LODModels.IsValidIndex(DesiredLOD) && !SkeletalMesh->GetLODInfo(DesiredLOD)->bHasBeenSimplified)
		{
			FSkeletalMeshLODModel& SrcModel = SkeletalMeshResource->LODModels[DesiredLOD];
			while (DesiredLOD >= SkeletalMeshResource->OriginalReductionSourceMeshData.Num())
			{
				FReductionBaseSkeletalMeshBulkData *EmptyReductionData = new FReductionBaseSkeletalMeshBulkData();
				SkeletalMeshResource->OriginalReductionSourceMeshData.Add(EmptyReductionData);
			}
			check(SkeletalMeshResource->OriginalReductionSourceMeshData.IsValidIndex(DesiredLOD));
			//Make the copy of the data only once until the ImportedModel change (re-imported)
			if (SkeletalMeshResource->OriginalReductionSourceMeshData[DesiredLOD]->IsEmpty())
			{
				TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
				BaseLODMorphTargetData.Empty(SkeletalMesh->MorphTargets.Num());
				for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
				{
					if (!MorphTarget->HasDataForLOD(DesiredLOD))
					{
						continue;
					}
					TArray<FMorphTargetDelta>& MorphDeltasArray = BaseLODMorphTargetData.FindOrAdd(MorphTarget->GetFullName());
					const FMorphTargetLODModel& BaseMorphModel = MorphTarget->MorphLODModels[DesiredLOD];
					//Iterate each original morph target source index to fill the NewMorphTargetDeltas array with the TargetMatchData.
					for (const FMorphTargetDelta& MorphDelta : BaseMorphModel.Vertices)
					{
						MorphDeltasArray.Add(MorphDelta);
					}
				}
				//Copy the original SkeletalMesh LODModel
				SkeletalMeshResource->OriginalReductionSourceMeshData[DesiredLOD]->SaveReductionData(SrcModel, BaseLODMorphTargetData);

				if (DesiredLOD == 0)
				{
					SkeletalMesh->GetLODInfo(DesiredLOD)->SourceImportFilename = SkeletalMesh->AssetImportData->GetFirstFilename();
				}
			}
		}
	}
	

	if (MeshReduction->ReduceSkeletalMesh(SkeletalMesh, DesiredLOD, bReregisterComponent))
	{
		check(SkeletalMesh->GetLODNum() >= 1);

		auto ApplyMorphTargetOption = [&SkeletalMesh, &DesiredLOD]()
		{
			FSkeletalMeshOptimizationSettings& ReductionSettings = SkeletalMesh->GetLODInfo(DesiredLOD)->ReductionSettings;
			//Apply morph to the new LOD. Force it if we reduce the base LOD, base LOD must apply the morph target
			if (ReductionSettings.bRemapMorphTargets)
			{
				ApplyMorphTargetsToLOD(SkeletalMesh, ReductionSettings.BaseLOD, DesiredLOD);
			}
			else
			{
				ClearGeneratedMorphTarget(SkeletalMesh, DesiredLOD);
			}
		};

		if (bReregisterComponent)
		{
			TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;
			SkeletalMesh->ReleaseResources();
			SkeletalMesh->ReleaseResourcesFence.Wait();

			ApplyMorphTargetOption();
			
			SkeletalMesh->PostEditChange();
			SkeletalMesh->InitResources();
		}
		else
		{
			ApplyMorphTargetOption();
		}
		SkeletalMesh->MarkPackageDirty();
	}
	else
	{
		// Simplification failed! Warn the user.
		FFormatNamedArguments Args;
		Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMesh->GetName()));
		const FText Message = FText::Format(NSLOCTEXT("UnrealEd", "MeshSimp_GenerateLODFailed_F", "An error occurred while simplifying the geometry for mesh '{SkeletalMeshName}'.  Consider adjusting simplification parameters and re-simplifying the mesh."), Args);
		FMessageDialog::Open(EAppMsgType::Ok, Message);
	}

	//Put back the clothing for the DesiredLOD
	if (bRestoreClothing)
	{
		for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
		{
			if (SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(Binding.LODIndex) &&
				SkeletalMesh->GetImportedModel()->LODModels[Binding.LODIndex].Sections.IsValidIndex(Binding.SectionIndex))
			{
				if (DesiredLOD == Binding.LODIndex)
				{
					Binding.Asset->BindToSkeletalMesh(SkeletalMesh, Binding.LODIndex, Binding.SectionIndex, Binding.AssetInternalLodIndex);
				}
			}
		}
	}

	GWarn->EndSlowTask();
}

void FLODUtilities::SimplifySkeletalMeshLOD(FSkeletalMeshUpdateContext& UpdateContext, int32 DesiredLOD, bool bReregisterComponent /*= true*/, bool bRestoreClothing /*= false*/)
{
	USkeletalMesh* SkeletalMesh = UpdateContext.SkeletalMesh;
	IMeshReductionModule& ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionModule>("MeshReductionInterface");
	IMeshReduction* MeshReduction = ReductionModule.GetSkeletalMeshReductionInterface();

	if (MeshReduction && MeshReduction->IsSupported() && SkeletalMesh)
	{
		SimplifySkeletalMeshLOD(SkeletalMesh, DesiredLOD, bReregisterComponent, bRestoreClothing);
		
		if (UpdateContext.OnLODChanged.IsBound())
		{
			//Notify calling system of change
			UpdateContext.OnLODChanged.ExecuteIfBound();
		}
	}
}

void FLODUtilities::RestoreSkeletalMeshLODImportedData(USkeletalMesh* SkeletalMesh, int32 LodIndex, bool bReregisterComponent /*= true*/)
{

	if (!SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData.IsValidIndex(LodIndex) || SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LodIndex]->IsEmpty())
	{
		//There is nothing to restore
		return;
	}

	// Unbind LodIndex existing clothing assets before restoring the LOD
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	ClothingAssetUtils::GetMeshClothingAssetBindings(SkeletalMesh, ClothingBindings);
	for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
	{
		//Unbind only the LOD we restore
		if (Binding.LODIndex == LodIndex)
		{
			Binding.Asset->UnbindFromSkeletalMesh(SkeletalMesh, Binding.LODIndex);
		}
	}

	FSkeletalMeshLODModel ImportedBaseLODModel;
	TMap<FString, TArray<FMorphTargetDelta>> ImportedBaseLODMorphTargetData;
	SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LodIndex]->LoadReductionData(ImportedBaseLODModel, ImportedBaseLODMorphTargetData);
	{
		FSkeletalMeshUpdateContext UpdateContext;
		UpdateContext.SkeletalMesh = SkeletalMesh;

		TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;
		if (bReregisterComponent)
		{
			SkeletalMesh->ReleaseResources();
			SkeletalMesh->ReleaseResourcesFence.Wait();
		}
		//Copy the SkeletalMeshLODModel
		SkeletalMesh->GetImportedModel()->LODModels[LodIndex] = ImportedBaseLODModel;
		//Copy the morph target deltas
		bool bInitMorphTargetData = false;
		for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
		{
			if (!ImportedBaseLODMorphTargetData.Contains(MorphTarget->GetFullName()))
			{
				continue;
			}
			TArray<FMorphTargetDelta>& ImportedDeltas = ImportedBaseLODMorphTargetData[MorphTarget->GetFullName()];

			MorphTarget->PopulateDeltas(ImportedDeltas, LodIndex, SkeletalMesh->GetImportedModel()->LODModels[LodIndex].Sections, false, false);
			bInitMorphTargetData |= SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
		}
		SkeletalMesh->InitMorphTargetsAndRebuildRenderData();
		
		//Empty the bulkdata since we restore it
		SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LodIndex]->EmptyBulkData();

		//Put back the clothing for the restore LOD
		for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
		{
			if (LodIndex == Binding.LODIndex && SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(Binding.LODIndex) &&
				SkeletalMesh->GetImportedModel()->LODModels[Binding.LODIndex].Sections.IsValidIndex(Binding.SectionIndex))
			{
				Binding.Asset->BindToSkeletalMesh(SkeletalMesh, Binding.LODIndex, Binding.SectionIndex, Binding.AssetInternalLodIndex);
			}
		}

		if (bReregisterComponent)
		{
			SkeletalMesh->PostEditChange();
			SkeletalMesh->InitResources();
		}

		if (UpdateContext.OnLODChanged.IsBound())
		{
			//Notify calling system of change
			UpdateContext.OnLODChanged.ExecuteIfBound();
		}
	}
}

void FLODUtilities::RefreshLODChange(const USkeletalMesh* SkeletalMesh)
{
	for (FObjectIterator Iter(USkeletalMeshComponent::StaticClass()); Iter; ++Iter)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(*Iter);
		if  (SkeletalMeshComponent->SkeletalMesh == SkeletalMesh)
		{
			// it needs to recreate IF it already has been created
			if (SkeletalMeshComponent->IsRegistered())
			{
				SkeletalMeshComponent->UpdateLODStatus();
				SkeletalMeshComponent->MarkRenderStateDirty();
			}
		}
	}
}

/*
 * The remap use the name to find the corresponding bone index between the source and destination skeleton
 */
void FillRemapBoneIndexSrcToDest(const FSkeletalMeshImportData& ImportDataSrc, const FSkeletalMeshImportData& ImportDataDest, const FString& SkeletalMeshDestName, const int32 LODIndexDest, TMap<int32, int32>& RemapBoneIndexSrcToDest)
{
	bool bIsunattended = GIsRunningUnattendedScript || FApp::IsUnattended();

	RemapBoneIndexSrcToDest.Empty(ImportDataSrc.RefBonesBinary.Num());
	int32 BoneNumberDest = ImportDataDest.RefBonesBinary.Num();
	int32 BoneNumberSrc = ImportDataSrc.RefBonesBinary.Num();
	//We also want to report any missing bone, because skinning quality will be impacted if bones are missing
	TArray<FString> DestBonesNotUsedBySrc;
	TArray<FString> SrcBonesNotUsedByDest;
	for (int32 BoneIndexSrc = 0; BoneIndexSrc < BoneNumberSrc; ++BoneIndexSrc)
	{
		FString BoneNameSrc = ImportDataSrc.RefBonesBinary[BoneIndexSrc].Name;
		for (int32 BoneIndexDest = 0; BoneIndexDest < BoneNumberDest; ++BoneIndexDest)
		{
			if (ImportDataDest.RefBonesBinary[BoneIndexDest].Name.Equals(BoneNameSrc))
			{
				RemapBoneIndexSrcToDest.Add(BoneIndexSrc, BoneIndexDest);
				break;
			}
		}
		if (!RemapBoneIndexSrcToDest.Contains(BoneIndexSrc))
		{
			SrcBonesNotUsedByDest.Add(BoneNameSrc);
			RemapBoneIndexSrcToDest.Add(BoneIndexSrc, INDEX_NONE);
		}
	}

	for (int32 BoneIndexDest = 0; BoneIndexDest < BoneNumberDest; ++BoneIndexDest)
	{
		FString BoneNameDest = ImportDataDest.RefBonesBinary[BoneIndexDest].Name;
		bool bFound = false;
		for (int32 BoneIndexSrc = 0; BoneIndexSrc < BoneNumberSrc; ++BoneIndexSrc)
		{
			FString BoneNameSrc = ImportDataSrc.RefBonesBinary[BoneIndexSrc].Name;
			if (BoneNameDest.Equals(BoneNameSrc))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			DestBonesNotUsedBySrc.Add(BoneNameDest);
		}
	}

	if (SrcBonesNotUsedByDest.Num() > 0)
	{
		//Let the user know
		UE_LOG(LogLODUtilities, Display, TEXT("Alternate skinning import: Not all the alternate mesh bones are used by the mesh."));
		if (!bIsunattended)
		{
			FString BoneList;
			for (FString& BoneName : SrcBonesNotUsedByDest)
			{
				BoneList += BoneName;
				BoneList += TEXT("\n");
			}

			FFormatNamedArguments Args;
			Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMeshDestName));
			Args.Add(TEXT("LODIndex"), FText::AsNumber(LODIndexDest));
			Args.Add(TEXT("BoneList"), FText::FromString(BoneList));
			FText Message = FText::Format(NSLOCTEXT("UnrealEd", "AlternateSkinningImport_SourceBoneNotUseByDestination", "Not all the alternate mesh bones are used by the LOD {LODIndex} when importing alternate weights for skeletal mesh '{SkeletalMeshName}'.\nBones List:\n{BoneList}"), Args);
			FMessageDialog::Open(EAppMsgType::Ok, Message);
		}
	}

	if (DestBonesNotUsedBySrc.Num() > 0)
	{
		//Let the user know
		UE_LOG(LogLODUtilities, Display, TEXT("Alternate skinning import: Not all the mesh bones are used by the alternate mesh."));
		if (!bIsunattended)
		{
			FString BoneList;
			for (FString& BoneName : DestBonesNotUsedBySrc)
			{
				BoneList += BoneName;
				BoneList += TEXT("\n");
			}

			FFormatNamedArguments Args;
			Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMeshDestName));
			Args.Add(TEXT("LODIndex"), FText::AsNumber(LODIndexDest));
			Args.Add(TEXT("BoneList"), FText::FromString(BoneList));
			FText Message = FText::Format(NSLOCTEXT("UnrealEd", "AlternateSkinningImport_DestinationBoneNotUseBySource", "Not all the LOD {LODIndex} bones are used by the alternate mesh when importing alternate weights for skeletal mesh '{SkeletalMeshName}'.\nBones List:\n{BoneList}"), Args);
			FMessageDialog::Open(EAppMsgType::Ok, Message);
		}
	}
}

namespace VertexMatchNameSpace
{
	struct FVertexMatchResult
	{
		TArray<uint32> VertexIndexes;
		TArray<float> Ratios;
	};
}

struct FTriangleOctreeSemantics
{
	// When a leaf gets more than this number of elements, it will split itself into a node with multiple child leaves
	enum { MaxElementsPerLeaf = 6 };

	// This is used for incremental updates.  When removing a polygon, larger values will cause leaves to be removed and collapsed into a parent node.
	enum { MinInclusiveElementsPerNode = 7 };

	// How deep the tree can go.
	enum { MaxNodeDepth = 20 };


	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FTriangleElement& Element)
	{
		return Element.PositionBound;
	}

	FORCEINLINE static bool AreElementsEqual(const FTriangleElement& A, const FTriangleElement& B)
	{
		return (A.TriangleIndex == B.TriangleIndex);
	}

	FORCEINLINE static void SetElementId(const FTriangleElement& Element, FOctreeElementId OctreeElementID)
	{
	}
};

typedef TOctree<FTriangleElement, FTriangleOctreeSemantics> TTriangleElementOctree;

void MatchVertexIndexUsingPosition(
	const FSkeletalMeshImportData& ImportDataDest
	, const FSkeletalMeshImportData& ImportDataSrc
	, TSortedMap<uint32, VertexMatchNameSpace::FVertexMatchResult>& VertexIndexSrcToVertexIndexDestMatches
	, const TArray<uint32>& VertexIndexToMatchWithUVs
	, bool& bNoMatchMsgDone)
{
	if (VertexIndexToMatchWithUVs.Num() <= 0)
	{
		return;
	}
	int32 FaceNumberDest = ImportDataDest.Faces.Num();

	//Setup the Position Octree with the destination faces so we can match the source vertex index
	TArray<FTriangleElement> TrianglesDest;
	FBox2D BaseMeshUVBound(EForceInit::ForceInit);
	FBox BaseMeshPositionBound(EForceInit::ForceInit);

	for (int32 FaceIndexDest = 0; FaceIndexDest < FaceNumberDest; ++FaceIndexDest)
	{
		const SkeletalMeshImportData::FTriangle& Triangle = ImportDataDest.Faces[FaceIndexDest];
		FTriangleElement TriangleElement;
		TriangleElement.UVsBound.Init();
		TriangleElement.PositionBound.Init();

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const uint32 WedgeIndexDest = Triangle.WedgeIndex[Corner];
			const uint32 VertexIndexDest = ImportDataDest.Wedges[WedgeIndexDest].VertexIndex;
			const FVector2D UVsDest = ImportDataDest.Wedges[WedgeIndexDest].UVs[0];
			TriangleElement.Indexes.Add(WedgeIndexDest);
			FSoftSkinVertex SoftSkinVertex;
			SoftSkinVertex.Position = ImportDataDest.Points[VertexIndexDest];
			SoftSkinVertex.UVs[0] = ImportDataDest.Wedges[WedgeIndexDest].UVs[0];
			TriangleElement.Vertices.Add(SoftSkinVertex);
			TriangleElement.UVsBound += SoftSkinVertex.UVs[0];
			TriangleElement.PositionBound += SoftSkinVertex.Position;
			BaseMeshPositionBound += SoftSkinVertex.Position;
		}
		BaseMeshUVBound += TriangleElement.UVsBound;
		BaseMeshPositionBound += TriangleElement.PositionBound;
		TriangleElement.TriangleIndex = FaceIndexDest;
		TrianglesDest.Add(TriangleElement);
	}

	TTriangleElementOctree OcTree(BaseMeshPositionBound.GetCenter(), BaseMeshPositionBound.GetExtent().Size());
	for (FTriangleElement& TriangleElement : TrianglesDest)
	{
		OcTree.AddElement(TriangleElement);
	}

	//Retrieve all triangles that are close to our point, start at 0.25% of OcTree extend
	float DistanceThreshold = BaseMeshPositionBound.GetExtent().Size()*0.0025f;

	//Find a match triangle for every target vertices
	TArray<FTriangleElement> OcTreeTriangleResults;
	OcTreeTriangleResults.Reserve(TrianglesDest.Num() / 50); //Reserve 2% to speed up the query

	//This lambda store a source vertex index -> source wedge index destination triangle.
	//It use a barycentric function to determine the impact on the 3 corner of the triangle.
	auto AddMatchTriangle = [&ImportDataDest, &TrianglesDest, &VertexIndexSrcToVertexIndexDestMatches](const FTriangleElement& BestTriangle, const FVector& Position, const uint32 VertexIndexSrc)
	{
		//Found the surface area of the 3 barycentric triangles from the UVs
		FVector BarycentricWeight;
		BarycentricWeight = GetBaryCentric(Position, BestTriangle.Vertices[0].Position, BestTriangle.Vertices[1].Position, BestTriangle.Vertices[2].Position);
		//Fill the match
		VertexMatchNameSpace::FVertexMatchResult& VertexMatchDest = VertexIndexSrcToVertexIndexDestMatches.FindOrAdd(VertexIndexSrc);
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			int32 VertexIndexDest = ImportDataDest.Wedges[BestTriangle.Indexes[CornerIndex]].VertexIndex;
			float Ratio = BarycentricWeight[CornerIndex];
			int32 FindIndex = INDEX_NONE;
			if (!VertexMatchDest.VertexIndexes.Find(VertexIndexDest, FindIndex))
			{
				VertexMatchDest.VertexIndexes.Add(VertexIndexDest);
				VertexMatchDest.Ratios.Add(Ratio);
			}
			else
			{
				check(VertexMatchDest.Ratios.IsValidIndex(FindIndex));
				VertexMatchDest.Ratios[FindIndex] = FMath::Max(VertexMatchDest.Ratios[FindIndex], Ratio);
			}
		}
	};

	for (int32 VertexIndexSrc : VertexIndexToMatchWithUVs)
	{
		FVector PositionSrc = ImportDataSrc.Points[VertexIndexSrc];
		OcTreeTriangleResults.Reset();

		//Use the OcTree to find closest triangle
		FVector Extent(DistanceThreshold, DistanceThreshold, DistanceThreshold);
		FBox CurBox(PositionSrc - Extent, PositionSrc + Extent);
		while (OcTreeTriangleResults.Num() <= 0)
		{
			TTriangleElementOctree::TConstIterator<> OctreeIter(OcTree);
			while (OctreeIter.HasPendingNodes())
			{
				const TTriangleElementOctree::FNode& CurNode = OctreeIter.GetCurrentNode();
				const FOctreeNodeContext& CurContext = OctreeIter.GetCurrentContext();

				// Find the child of the current node, if any, that contains the current new point
				FOctreeChildNodeRef ChildRef = CurContext.GetContainingChild(CurBox);

				if (!ChildRef.IsNULL())
				{
					const TTriangleElementOctree::FNode* ChildNode = CurNode.GetChild(ChildRef);

					// If the specified child node exists and contains any of the old vertices, push it to the iterator for future consideration
					if (ChildNode && ChildNode->GetInclusiveElementCount() > 0)
					{
						OctreeIter.PushChild(ChildRef);
					}
					// If the child node doesn't have any of the old vertices in it, it's not worth pursuing any further. In an attempt to find
					// anything to match vs. the new point, add all of the children of the current octree node that have old points in them to the
					// iterator for future consideration.
					else
					{
						FOREACH_OCTREE_CHILD_NODE(OctreeChildRef)
						{
							if (CurNode.HasChild(OctreeChildRef))
							{
								OctreeIter.PushChild(OctreeChildRef);
							}
						}
					}
				}

				// Add all of the elements in the current node to the list of points to consider for closest point calculations
				OcTreeTriangleResults.Append(CurNode.GetElements());
				OctreeIter.Advance();
			}
			//Increase the extend so we try to found in a larger area
			Extent *= 2;
			CurBox = FBox(PositionSrc - Extent, PositionSrc + Extent);
		}

		//Get the 3D distance between a point and a destination triangle
		auto GetDistanceSrcPointToDestTriangle = [&TrianglesDest, &PositionSrc](const uint32 DestTriangleIndex)->float
		{
			FTriangleElement& CandidateTriangle = TrianglesDest[DestTriangleIndex];
			return FVector::DistSquared(FMath::ClosestPointOnTriangleToPoint(PositionSrc, CandidateTriangle.Vertices[0].Position, CandidateTriangle.Vertices[1].Position, CandidateTriangle.Vertices[2].Position), PositionSrc);
		};

		//Brute force finding of closest triangle using 3D position
		auto FailSafeUnmatchVertex = [&GetDistanceSrcPointToDestTriangle, &OcTreeTriangleResults](uint32 &OutIndexMatch)->bool
		{
			bool bFoundMatch = false;
			float ClosestTriangleDistSquared = MAX_flt;
			for (const FTriangleElement& MatchTriangle : OcTreeTriangleResults)
			{
				int32 MatchTriangleIndex = MatchTriangle.TriangleIndex;
				float TriangleDistSquared = GetDistanceSrcPointToDestTriangle(MatchTriangleIndex);
				if (TriangleDistSquared < ClosestTriangleDistSquared)
				{
					ClosestTriangleDistSquared = TriangleDistSquared;
					OutIndexMatch = MatchTriangleIndex;
					bFoundMatch = true;
				}
			}
			return bFoundMatch;
		};

		//Find all Triangles that contain the Target UV
		if (OcTreeTriangleResults.Num() > 0)
		{
			TArray<uint32> MatchTriangleIndexes;
			uint32 FoundIndexMatch = INDEX_NONE;
			if (!FindTrianglePositionMatch(PositionSrc, TrianglesDest, OcTreeTriangleResults, MatchTriangleIndexes))
			{
				//There is no UV match possible, use brute force fail safe
				if (!FailSafeUnmatchVertex(FoundIndexMatch))
				{
					//We should always have a match
					if (!bNoMatchMsgDone)
					{
						UE_LOG(LogLODUtilities, Warning, TEXT("Alternate skinning import: Cannot find a triangle from the destination LOD that contain a vertex UV in the imported alternate skinning LOD mesh. Alternate skinning quality will be lower."));
						bNoMatchMsgDone = true;
					}
					continue;
				}
			}
			float ClosestTriangleDistSquared = MAX_flt;
			if (MatchTriangleIndexes.Num() == 1)
			{
				//One match, this mean no mirror UVs simply take the single match
				FoundIndexMatch = MatchTriangleIndexes[0];
				ClosestTriangleDistSquared = GetDistanceSrcPointToDestTriangle(FoundIndexMatch);
			}
			else
			{
				//Geometry can use mirror so the UVs are not unique. Use the closest match triangle to the point to find the best match
				for (uint32 MatchTriangleIndex : MatchTriangleIndexes)
				{
					float TriangleDistSquared = GetDistanceSrcPointToDestTriangle(MatchTriangleIndex);
					if (TriangleDistSquared < ClosestTriangleDistSquared)
					{
						ClosestTriangleDistSquared = TriangleDistSquared;
						FoundIndexMatch = MatchTriangleIndex;
					}
				}
			}

			//FAIL SAFE, make sure we have a match that make sense
			//Use the mesh geometry bound extent (1% of it) to validate we are close enough.
			if (ClosestTriangleDistSquared > BaseMeshPositionBound.GetExtent().SizeSquared()*0.01f)
			{
				//Executing fail safe, if the UVs are too much off because of the reduction, use the closest distance to polygons to find the match
				//This path is not optimize and should not happen often.
				FailSafeUnmatchVertex(FoundIndexMatch);
			}

			//We should always have a valid match at this point
			check(TrianglesDest.IsValidIndex(FoundIndexMatch));
			AddMatchTriangle(TrianglesDest[FoundIndexMatch], PositionSrc, VertexIndexSrc);
		}
		else
		{
			if (!bNoMatchMsgDone)
			{
				UE_LOG(LogLODUtilities, Warning, TEXT("Alternate skinning import: Cannot find a triangle from the destination LOD that contain a vertex UV in the imported alternate skinning LOD mesh. Alternate skinning quality will be lower."));
				bNoMatchMsgDone = true;
			}
		}
	}
}

bool FLODUtilities::UpdateAlternateSkinWeights(USkeletalMesh* SkeletalMeshDest, const FName& ProfileNameDest, USkeletalMesh* SkeletalMeshSrc, const UnFbx::FBXImportOptions& ImportOptions, int32 LODIndexDest, int32 LODIndexSrc)
{
	//Ensure log message only once
	bool bNoMatchMsgDone = false;

	//Grab all the destination structure
	check(SkeletalMeshDest);
	check(SkeletalMeshDest->GetImportedModel());
	check(SkeletalMeshDest->GetImportedModel()->LODModels.IsValidIndex(LODIndexDest));
	FSkeletalMeshLODModel& LODModelDest = SkeletalMeshDest->GetImportedModel()->LODModels[LODIndexDest];
	if (LODModelDest.RawSkeletalMeshBulkData.IsEmpty())
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Failed to import Skin Weight Profile as the target skeletal mesh (%s) requires reimporting first."), SkeletalMeshDest ? *SkeletalMeshDest->GetName() : TEXT("NULL"));
		//Very old asset will not have this data, we cannot add alternate until the asset is reimported
		return false;
	}
	FSkeletalMeshImportData ImportDataDest;
	LODModelDest.RawSkeletalMeshBulkData.LoadRawMesh(ImportDataDest);
	int32 PointNumberDest = ImportDataDest.Points.Num();
	int32 VertexNumberDest = ImportDataDest.Points.Num();

	//Grab all the source structure
	check(SkeletalMeshSrc);
	check(SkeletalMeshSrc->GetImportedModel());
	check(SkeletalMeshSrc->GetImportedModel()->LODModels.IsValidIndex(LODIndexSrc));
	FSkeletalMeshLODModel& LODModelSrc = SkeletalMeshSrc->GetImportedModel()->LODModels[LODIndexSrc];
	//The source model is a fresh import and the data need to be there
	check(!LODModelSrc.RawSkeletalMeshBulkData.IsEmpty());
	FSkeletalMeshImportData ImportDataSrc;
	LODModelSrc.RawSkeletalMeshBulkData.LoadRawMesh(ImportDataSrc);
	int32 PointNumberSrc = ImportDataSrc.Points.Num();
	int32 VertexNumberSrc = ImportDataSrc.Points.Num();
	int32 InfluenceNumberSrc = ImportDataSrc.Influences.Num();

	if (ImportDataDest.NumTexCoords <= 0 || ImportDataSrc.NumTexCoords <= 0)
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Failed to import Skin Weight Profile as the target skeletal mesh (%s) or imported file does not contain UV coordinates."), SkeletalMeshDest ? *SkeletalMeshDest->GetName() : TEXT("NULL"));
		return false;
	}

	//Create a map linking all similar Position of destination vertex index
	TMap<FVector, TArray<uint32>> PositionToVertexIndexDest;
	PositionToVertexIndexDest.Reserve(VertexNumberSrc);
	for (int32 VertexIndex = 0; VertexIndex < VertexNumberDest; ++VertexIndex)
	{
		const FVector& Position = ImportDataDest.Points[VertexIndex];
		TArray<uint32>& VertexIndexArray = PositionToVertexIndexDest.FindOrAdd(Position);
		VertexIndexArray.Add(VertexIndex);
	}

	//Create a map to remap source bone index to destination bone index
	TMap<int32, int32> RemapBoneIndexSrcToDest;
	FillRemapBoneIndexSrcToDest(ImportDataSrc, ImportDataDest, SkeletalMeshDest->GetName(), LODIndexDest, RemapBoneIndexSrcToDest);

	//Map to get the vertex index source to a destination vertex match
	TSortedMap<uint32, VertexMatchNameSpace::FVertexMatchResult> VertexIndexSrcToVertexIndexDestMatches;
	VertexIndexSrcToVertexIndexDestMatches.Reserve(VertexNumberSrc);
	TArray<uint32> VertexIndexToMatchWithUVs;
	// Match all source vertex with destination vertex
	for (int32 VertexIndexSrc = 0; VertexIndexSrc < PointNumberSrc; ++VertexIndexSrc)
	{
		const FVector& PositionSrc = ImportDataSrc.Points[VertexIndexSrc];
		
		TArray<uint32>* SimilarDestinationVertex = PositionToVertexIndexDest.Find(PositionSrc);
		if (!SimilarDestinationVertex)
		{
			//Match with UV projection
			VertexIndexToMatchWithUVs.Add(VertexIndexSrc);
		}
		else
		{
			//We have a direct match
			VertexMatchNameSpace::FVertexMatchResult& VertexMatchDest = VertexIndexSrcToVertexIndexDestMatches.Add(VertexIndexSrc);
			for (int32 MatchDestinationIndex = 0; MatchDestinationIndex < SimilarDestinationVertex->Num(); ++MatchDestinationIndex)
			{
				VertexMatchDest.VertexIndexes.Add((*SimilarDestinationVertex)[MatchDestinationIndex]);
				VertexMatchDest.Ratios.Add(1.0f);
			}
		}
	}
	
	//Find a match for all unmatched source vertex, unmatched vertex happen when the geometry is different between source and destination mesh
	bool bAllSourceVertexAreMatch = VertexIndexToMatchWithUVs.Num() <= 0 && VertexIndexSrcToVertexIndexDestMatches.Num() == PointNumberSrc;
	if (!bAllSourceVertexAreMatch)
	{
		MatchVertexIndexUsingPosition(ImportDataDest, ImportDataSrc, VertexIndexSrcToVertexIndexDestMatches, VertexIndexToMatchWithUVs, bNoMatchMsgDone);
		//Make sure each vertex index source has a match, warn the user in case there is no match
		for (int32 VertexIndexSource = 0; VertexIndexSource < VertexNumberSrc; ++VertexIndexSource)
		{
			if (!VertexIndexSrcToVertexIndexDestMatches.Contains(VertexIndexSource))
			{
				//Skip this vertex, its possible the skinning quality can be affected here
				if (!bNoMatchMsgDone)
				{
					UE_LOG(LogLODUtilities, Warning, TEXT("Alternate skinning import: Cannot find a destination vertex index match for source vertex index. Alternate skinning quality will be lower."));
					bNoMatchMsgDone = true;
				}
				continue;
			}
		}
	}
	
	
	//Find the Destination to source match, to make sure all extra destination vertex get weighted properly in the alternate influences
	TSortedMap<uint32, VertexMatchNameSpace::FVertexMatchResult> VertexIndexDestToVertexIndexSrcMatches;
	if(!bAllSourceVertexAreMatch || PointNumberDest != PointNumberSrc)
	{
		VertexIndexDestToVertexIndexSrcMatches.Reserve(VertexNumberDest);
		TArray<uint32> VertexIndexToMatch;
		VertexIndexToMatch.Reserve(PointNumberDest);
		for (int32 VertexIndexDest = 0; VertexIndexDest < PointNumberDest; ++VertexIndexDest)
		{
			VertexIndexToMatch.Add(VertexIndexDest);
		}
		MatchVertexIndexUsingPosition(ImportDataSrc, ImportDataDest, VertexIndexDestToVertexIndexSrcMatches, VertexIndexToMatch, bNoMatchMsgDone);
	}

	//We now iterate the source influence and create the alternate influence by using the matches between source and destination vertex
	TArray<SkeletalMeshImportData::FRawBoneInfluence> AlternateInfluences;
	AlternateInfluences.Empty(ImportDataSrc.Influences.Num());

	TMap<uint32, TArray<int32>> SourceVertexIndexToAlternateInfluenceIndexMap;
	SourceVertexIndexToAlternateInfluenceIndexMap.Reserve(InfluenceNumberSrc);
	
	for (int32 InfluenceIndexSrc = 0; InfluenceIndexSrc < InfluenceNumberSrc; ++InfluenceIndexSrc)
	{
		SkeletalMeshImportData::FRawBoneInfluence& InfluenceSrc = ImportDataSrc.Influences[InfluenceIndexSrc];
		uint32 VertexIndexSource = InfluenceSrc.VertexIndex;
		uint32 BoneIndexSource = InfluenceSrc.BoneIndex;
		float Weight = InfluenceSrc.Weight;
		//We need to remap the source bone index to have the matching target bone index
		uint32 BoneIndexDest = RemapBoneIndexSrcToDest[BoneIndexSource];
		if (BoneIndexDest != INDEX_NONE)
		{
			//Find the match destination vertex index
			VertexMatchNameSpace::FVertexMatchResult* SourceVertexMatch = VertexIndexSrcToVertexIndexDestMatches.Find(VertexIndexSource);
			if (SourceVertexMatch == nullptr || SourceVertexMatch->VertexIndexes.Num() <= 0)
			{
				//No match skip this influence
				continue;
			}
			TArray<int32>& AlternateInfluencesMap = SourceVertexIndexToAlternateInfluenceIndexMap.FindOrAdd(VertexIndexSource);
			//No need to merge all vertexindex per bone, ProcessImportMeshInfluences will do this for us later
			//So just add all of the entry we have.
			for (int32 ImpactedIndex = 0; ImpactedIndex < SourceVertexMatch->VertexIndexes.Num(); ++ImpactedIndex)
			{
				uint32 VertexIndexDest = SourceVertexMatch->VertexIndexes[ImpactedIndex];
				float Ratio = SourceVertexMatch->Ratios[ImpactedIndex];
				if (FMath::IsNearlyZero(Ratio, KINDA_SMALL_NUMBER))
				{
					continue;
				}
				SkeletalMeshImportData::FRawBoneInfluence AlternateInfluence;
				AlternateInfluence.BoneIndex = BoneIndexDest;
				AlternateInfluence.VertexIndex = VertexIndexDest;
				AlternateInfluence.Weight = InfluenceSrc.Weight;
				int32 AlternateInfluencesIndex = AlternateInfluences.Add(AlternateInfluence);
				AlternateInfluencesMap.Add(AlternateInfluencesIndex);
			}
		}
	}
	
	//In case the source geometry was not matching the destination we have to add influence for each extra destination vertex index
	if (VertexIndexDestToVertexIndexSrcMatches.Num() > 0)
	{
		TArray<bool> DestinationVertexIndexMatched;
		DestinationVertexIndexMatched.AddZeroed(PointNumberDest);

		int32 InfluenceNumberDest = ImportDataDest.Influences.Num();
		int32 AlternateInfluenceNumber = AlternateInfluences.Num();
		
		//We want to avoid making duplicate so we use a map where the key is the boneindex mix with the destination vertex index
		TMap<uint64, int32> InfluenceKeyToInfluenceIndex;
		InfluenceKeyToInfluenceIndex.Reserve(AlternateInfluenceNumber);
		for (int32 AlternateInfluenceIndex = 0; AlternateInfluenceIndex < AlternateInfluenceNumber; ++AlternateInfluenceIndex)
		{
			SkeletalMeshImportData::FRawBoneInfluence& Influence = AlternateInfluences[AlternateInfluenceIndex];
			DestinationVertexIndexMatched[Influence.VertexIndex] = true;
			uint64 Key = ((uint64)(Influence.BoneIndex) << 32 & 0xFFFFFFFF00000000) | ((uint64)(Influence.VertexIndex) & 0x00000000FFFFFFFF);
			InfluenceKeyToInfluenceIndex.Add(Key, AlternateInfluenceIndex);
		}

		for (int32 VertexIndexDestination = 0; VertexIndexDestination < VertexNumberDest; ++VertexIndexDestination)
		{
			//Skip if the vertex is already matched
			if (DestinationVertexIndexMatched[VertexIndexDestination])
			{
				continue;
			}
			VertexMatchNameSpace::FVertexMatchResult* DestinationVertexMatch = VertexIndexDestToVertexIndexSrcMatches.Find(VertexIndexDestination);
			if (DestinationVertexMatch == nullptr || DestinationVertexMatch->VertexIndexes.Num() <= 0)
			{
				//No match skip this influence
				continue;
			}
			for (int32 ImpactedIndex = 0; ImpactedIndex < DestinationVertexMatch->VertexIndexes.Num(); ++ImpactedIndex)
			{
				uint32 VertexIndexSrc = DestinationVertexMatch->VertexIndexes[ImpactedIndex];
				float Ratio = DestinationVertexMatch->Ratios[ImpactedIndex];
				if (!FMath::IsNearlyZero(Ratio, KINDA_SMALL_NUMBER))
				{
					//Find src influence for this source vertex index
					TArray<int32>* AlternateInfluencesMap = SourceVertexIndexToAlternateInfluenceIndexMap.Find(VertexIndexSrc);
					if (AlternateInfluencesMap == nullptr)
					{
						continue;
					}
					for (int32 AlternateInfluencesMapIndex = 0; AlternateInfluencesMapIndex < (*AlternateInfluencesMap).Num(); ++AlternateInfluencesMapIndex)
					{
						int32 AlternateInfluenceIndex = (*AlternateInfluencesMap)[AlternateInfluencesMapIndex];
						if (!AlternateInfluences.IsValidIndex(AlternateInfluenceIndex))
						{
							continue;
						}
						DestinationVertexIndexMatched[VertexIndexDestination] = true;
						SkeletalMeshImportData::FRawBoneInfluence AlternateInfluence = AlternateInfluences[AlternateInfluenceIndex];
						uint64 Key = ((uint64)(AlternateInfluence.BoneIndex) << 32 & 0xFFFFFFFF00000000) | ((uint64)(VertexIndexDestination) & 0x00000000FFFFFFFF);
						if (!InfluenceKeyToInfluenceIndex.Contains(Key))
						{
							AlternateInfluence.VertexIndex = VertexIndexDestination;
							InfluenceKeyToInfluenceIndex.Add(Key, AlternateInfluences.Add(AlternateInfluence));
						}
						else
						{
							int32& InfluenceIndex = InfluenceKeyToInfluenceIndex.FindOrAdd(Key);
							SkeletalMeshImportData::FRawBoneInfluence& ExistAlternateInfluence = AlternateInfluences[InfluenceIndex];
							if (ExistAlternateInfluence.Weight < AlternateInfluence.Weight)
							{
								ExistAlternateInfluence.Weight = AlternateInfluence.Weight;
							}
						}
					}
				}
			}
		}
	}

	//Sort and normalize weights for alternate influences
	ProcessImportMeshInfluences(ImportDataDest.Wedges.Num(), AlternateInfluences);

	//Store the remapped influence into the profile, the function SkeletalMeshTools::ChunkSkinnedVertices will use all profiles including this one to chunk the sections
	FImportedSkinWeightProfileData& ImportedProfileData = LODModelDest.SkinWeightProfiles.Add(ProfileNameDest);
	ImportedProfileData.SourceModelInfluences.Empty(AlternateInfluences.Num());
	for (int32 InfluenceIndex = 0; InfluenceIndex < AlternateInfluences.Num(); ++InfluenceIndex)
	{
		const SkeletalMeshImportData::FRawBoneInfluence& RawInfluence = AlternateInfluences[InfluenceIndex];
		SkeletalMeshImportData::FVertInfluence LODAlternateInfluence;
		LODAlternateInfluence.BoneIndex = RawInfluence.BoneIndex;
		LODAlternateInfluence.VertIndex = RawInfluence.VertexIndex;
		LODAlternateInfluence.Weight = RawInfluence.Weight;
		ImportedProfileData.SourceModelInfluences.Add(LODAlternateInfluence);
	}

	//
	//////////////////////////////////////////////////////////////////////////

	//Prepare the build data to rebuild the asset with the alternate influences
	//The chunking can be different when we have alternate influences

	//Grab the build data from ImportDataDest
	TArray<FVector> LODPointsDest;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedgesDest;
	TArray<SkeletalMeshImportData::FMeshFace> LODFacesDest;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluencesDest;
	TArray<int32> LODPointToRawMapDest;
	ImportDataDest.CopyLODImportData(LODPointsDest, LODWedgesDest, LODFacesDest, LODInfluencesDest, LODPointToRawMapDest);

	//Set the options with the current asset build options
	IMeshUtilities::MeshBuildOptions BuildOptions;
	BuildOptions.OverlappingThresholds = ImportOptions.OverlappingThresholds;
	BuildOptions.bComputeNormals = !ImportOptions.ShouldImportNormals() || !ImportDataDest.bHasNormals;
	BuildOptions.bComputeTangents = !ImportOptions.ShouldImportTangents() || !ImportDataDest.bHasTangents;
	BuildOptions.bUseMikkTSpace = (ImportOptions.NormalGenerationMethod == EFBXNormalGenerationMethod::MikkTSpace) && (!ImportOptions.ShouldImportNormals() || !ImportOptions.ShouldImportTangents());
	BuildOptions.bRemoveDegenerateTriangles = false;

	//Build the skeletal mesh asset
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	TArray<FText> WarningMessages;
	TArray<FName> WarningNames;
	//Build the destination mesh with the Alternate influences, so the chunking is done properly.
	bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(LODModelDest, SkeletalMeshDest->RefSkeleton, LODInfluencesDest, LODWedgesDest, LODFacesDest, LODPointsDest, LODPointToRawMapDest, BuildOptions, &WarningMessages, &WarningNames);
	RegenerateAllImportSkinWeightProfileData(LODModelDest);
	
	return bBuildSuccess;
}

void FLODUtilities::GenerateImportedSkinWeightProfileData(const FSkeletalMeshLODModel& LODModelDest, FImportedSkinWeightProfileData &ImportedProfileData)
{
	//Add the override buffer with the alternate influence data
	TArray<FSoftSkinVertex> DestinationSoftVertices;
	LODModelDest.GetVertices(DestinationSoftVertices);
	//Get the SkinWeights buffer allocated before filling it
	TArray<FRawSkinWeight>& SkinWeights = ImportedProfileData.SkinWeights;
	SkinWeights.Empty(DestinationSoftVertices.Num());

	for (int32 VertexInstanceIndex = 0; VertexInstanceIndex < DestinationSoftVertices.Num(); ++VertexInstanceIndex)
	{
		int32 SectionIndex = INDEX_NONE;
		int32 OutVertexIndexGarb = INDEX_NONE;
		LODModelDest.GetSectionFromVertexIndex(VertexInstanceIndex, SectionIndex, OutVertexIndexGarb);
		if (!LODModelDest.Sections.IsValidIndex(SectionIndex))
		{
			continue;
		}
		const TArray<FBoneIndexType> SectionBoneMap = LODModelDest.Sections[SectionIndex].BoneMap;
		const FSoftSkinVertex& Vertex = DestinationSoftVertices[VertexInstanceIndex];
		const int32 VertexIndex = LODModelDest.MeshToImportVertexMap[VertexInstanceIndex];
		check(VertexIndex >= 0 && VertexIndex <= LODModelDest.MaxImportVertex);
		FRawSkinWeight& SkinWeight = SkinWeights.AddDefaulted_GetRef();
		//Zero out all value
		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
		{
			SkinWeight.InfluenceBones[InfluenceIndex] = 0;
			SkinWeight.InfluenceWeights[InfluenceIndex] = 0;
		}
		TMap<FBoneIndexType, float> WeightForBone;
		for (const SkeletalMeshImportData::FVertInfluence& VertInfluence : ImportedProfileData.SourceModelInfluences)
		{
			if(VertexIndex == VertInfluence.VertIndex)
			{
				//Use the section bone map to remap the bone index
				int32 BoneMapIndex = INDEX_NONE;
				SectionBoneMap.Find(VertInfluence.BoneIndex, BoneMapIndex);
				if (BoneMapIndex == INDEX_NONE)
				{
					//Map to root of the section
					BoneMapIndex = 0;
				}
				WeightForBone.Add(BoneMapIndex, VertInfluence.Weight);
			}
		}
		//Add the prepared alternate influences for this skin vertex
		uint32	TotalInfluenceWeight = 0;
		int32 InfluenceBoneIndex = 0;
		for (auto Kvp : WeightForBone)
		{
			SkinWeight.InfluenceBones[InfluenceBoneIndex] = (uint8)(Kvp.Key);
			SkinWeight.InfluenceWeights[InfluenceBoneIndex] = FMath::Clamp((uint8)(Kvp.Value*((float)0xFF)), (uint8)0x00, (uint8)0xFF);
			TotalInfluenceWeight += SkinWeight.InfluenceWeights[InfluenceBoneIndex];
			InfluenceBoneIndex++;
		}
		//Use the same code has the build where we modify the index 0 to have a sum of 255 for all influence per skin vertex
		SkinWeight.InfluenceWeights[0] += 255 - TotalInfluenceWeight;
	}
}

void FLODUtilities::RegenerateAllImportSkinWeightProfileData(FSkeletalMeshLODModel& LODModelDest)
{
	for (TPair<FName, FImportedSkinWeightProfileData>& ProfilePair : LODModelDest.SkinWeightProfiles)
	{
		GenerateImportedSkinWeightProfileData(LODModelDest, ProfilePair.Value);
	}
}

bool FLODUtilities::ImportAlternateSkinWeight(USkeletalMesh* SkeletalMesh, FString Path, int32 TargetLODIndex, const FName& ProfileName, bool bReregisterComponent)
{
	check(SkeletalMesh);
	check(SkeletalMesh->GetLODInfo(TargetLODIndex));
	FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(TargetLODIndex);
	
	if (LODInfo && LODInfo->bHasBeenSimplified && LODInfo->ReductionSettings.BaseLOD != TargetLODIndex)
	{
		//We cannot remove alternate skin weights profile for a generated LOD
		UE_LOG(LogLODUtilities, Error, TEXT("Cannot import Skin Weight Profile for a generated LOD."));
		return false;
	}

	FString AbsoluteFilePath = UAssetImportData::ResolveImportFilename(Path, SkeletalMesh->GetOutermost());
	if (!FPaths::FileExists(AbsoluteFilePath))
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Path containing Skin Weight Profile data does not exist (%s)."), *Path);
		return false;
	}
	UnFbx::FBXImportOptions ImportOptions;
	//Import the alternate fbx into a temporary skeletal mesh using the same import options
	UFbxFactory* FbxFactory = NewObject<UFbxFactory>(UFbxFactory::StaticClass());
	FbxFactory->AddToRoot();

	FbxFactory->ImportUI = NewObject<UFbxImportUI>(FbxFactory);
	UFbxSkeletalMeshImportData* OriginalSkeletalMeshImportData = UFbxSkeletalMeshImportData::GetImportDataForSkeletalMesh(SkeletalMesh, nullptr);
	if (OriginalSkeletalMeshImportData != nullptr)
	{
		//Copy the skeletal mesh import data options
		FbxFactory->ImportUI->SkeletalMeshImportData = DuplicateObject<UFbxSkeletalMeshImportData>(OriginalSkeletalMeshImportData, FbxFactory);
	}
	//Skip the auto detect type on import, the test set a specific value
	FbxFactory->SetDetectImportTypeOnImport(false);
	FbxFactory->ImportUI->bImportAsSkeletal = true;
	FbxFactory->ImportUI->MeshTypeToImport = FBXIT_SkeletalMesh;
	FbxFactory->ImportUI->bIsReimport = false;
	FbxFactory->ImportUI->ReimportMesh = nullptr;
	FbxFactory->ImportUI->bAllowContentTypeImport = true;
	FbxFactory->ImportUI->bImportAnimations = false;
	FbxFactory->ImportUI->bAutomatedImportShouldDetectType = false;
	FbxFactory->ImportUI->bCreatePhysicsAsset = false;
	FbxFactory->ImportUI->bImportMaterials = false;
	FbxFactory->ImportUI->bImportTextures = false;
	FbxFactory->ImportUI->bImportMesh = true;
	FbxFactory->ImportUI->bImportRigidMesh = false;
	FbxFactory->ImportUI->bIsObjImport = false;
	FbxFactory->ImportUI->bOverrideFullName = true;
	FbxFactory->ImportUI->Skeleton = nullptr;
	
	//Force some skeletal mesh import options
	if (FbxFactory->ImportUI->SkeletalMeshImportData)
	{
		FbxFactory->ImportUI->SkeletalMeshImportData->bImportMeshLODs = false;
		FbxFactory->ImportUI->SkeletalMeshImportData->bImportMorphTargets = false;
		FbxFactory->ImportUI->SkeletalMeshImportData->bUpdateSkeletonReferencePose = false;
		FbxFactory->ImportUI->SkeletalMeshImportData->ImportContentType = EFBXImportContentType::FBXICT_All; //We need geo and skinning, so we can match the weights
	}
	//Force some material options
	if (FbxFactory->ImportUI->TextureImportData)
	{
		FbxFactory->ImportUI->TextureImportData->MaterialSearchLocation = EMaterialSearchLocation::Local;
		FbxFactory->ImportUI->TextureImportData->BaseMaterialName.Reset();
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FString ImportAssetPath = TEXT("/Engine/TempEditor/SkeletalMeshTool");
	//Empty the temporary path
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	
	auto DeletePathAssets = [&AssetRegistryModule, &ImportAssetPath]()
	{
		TArray<FAssetData> AssetsToDelete;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*ImportAssetPath), AssetsToDelete, true);
		ObjectTools::DeleteAssets(AssetsToDelete, false);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	};

	DeletePathAssets();

	ApplyImportUIToImportOptions(FbxFactory->ImportUI, ImportOptions);

	TArray<FString> ImportFilePaths;
	ImportFilePaths.Add(AbsoluteFilePath);

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->AddToRoot();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->DestinationPath = ImportAssetPath;
	Task->bSave = false;
	Task->DestinationName = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Task->Options = FbxFactory->ImportUI->SkeletalMeshImportData;
	Task->Filename = AbsoluteFilePath;
	Task->Factory = FbxFactory;
	FbxFactory->SetAssetImportTask(Task);
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	UObject* ImportedObject = nullptr;
	
	for (FString AssetPath : Task->ImportedObjectPaths)
	{
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*AssetPath));
		ImportedObject = AssetData.GetAsset();
		if (ImportedObject != nullptr)
		{
			break;
		}
	}
	
	//Factory and task can now be garbage collected
	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

	USkeletalMesh* TmpSkeletalMesh = Cast<USkeletalMesh>(ImportedObject);
	if (TmpSkeletalMesh == nullptr || TmpSkeletalMesh->Skeleton == nullptr)
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Failed to import Skin Weight Profile from provided FBX file (%s)."), *Path);
		DeletePathAssets();
		return false;
	}

	//The LOD index of the source is always 0, 
	const int32 SrcLodIndex = 0;
	bool bResult = false;

	if (SkeletalMesh && TmpSkeletalMesh)
	{
		if (FSkeletalMeshModel* TargetModel = SkeletalMesh->GetImportedModel())
		{
			if (TargetModel->LODModels.IsValidIndex(TargetLODIndex))
			{
				//Prepare the profile data
				FSkeletalMeshLODModel& TargetLODModel = TargetModel->LODModels[TargetLODIndex];
				
				// Prepare the profile data
				FSkinWeightProfileInfo* Profile = SkeletalMesh->GetSkinWeightProfiles().FindByPredicate([ProfileName](FSkinWeightProfileInfo Profile) { return Profile.Name == ProfileName; });

				const bool bIsReimport = Profile != nullptr;
				FText TransactionName = bIsReimport ? NSLOCTEXT("UnrealEd", "UpdateAlternateSkinningWeight", "Update Alternate Skinning Weight")
					: NSLOCTEXT("UnrealEd", "ImportAlternateSkinningWeight", "Import Alternate Skinning Weight");
				FScopedTransaction ScopedTransaction(TransactionName);
				SkeletalMesh->Modify();

				if (bIsReimport)
				{
					// Update source file path
					FString& StoredPath = Profile->PerLODSourceFiles.FindOrAdd(TargetLODIndex);
					StoredPath = UAssetImportData::SanitizeImportFilename(AbsoluteFilePath, SkeletalMesh->GetOutermost());
					Profile->PerLODSourceFiles.KeySort([](int32 A, int32 B) { return A < B; });
				}
				
				// Clear profile data before import
				FImportedSkinWeightProfileData& ProfileData = TargetLODModel.SkinWeightProfiles.FindOrAdd(ProfileName);
				ProfileData.SkinWeights.Empty();
				ProfileData.SourceModelInfluences.Empty();

				FImportedSkinWeightProfileData PreviousProfileData = ProfileData;
				
				TArray<FRawSkinWeight>& SkinWeights = ProfileData.SkinWeights;
				if (bReregisterComponent)
				{
					TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;
					SkeletalMesh->ReleaseResources();
					SkeletalMesh->ReleaseResourcesFence.Wait();

					bResult = UpdateAlternateSkinWeights(SkeletalMesh, ProfileName, TmpSkeletalMesh, ImportOptions, TargetLODIndex, SrcLodIndex);
					SkeletalMesh->PostEditChange();
					SkeletalMesh->InitResources();
				}
				else
				{
					bResult = UpdateAlternateSkinWeights(SkeletalMesh, ProfileName, TmpSkeletalMesh, ImportOptions, TargetLODIndex, SrcLodIndex);
				}
								
				if (!bResult)
				{
					// Remove invalid profile data due to failed import
					if (!bIsReimport)
					{
						TargetLODModel.SkinWeightProfiles.Remove(ProfileName);
					}
					else
					{
						// Otherwise restore previous data
						ProfileData = PreviousProfileData;
					}
				}

				// Only add if it is an initial import and it was successful 
				if (!bIsReimport && bResult)
				{
					FSkinWeightProfileInfo SkeletalMeshProfile;
					SkeletalMeshProfile.DefaultProfile = (SkeletalMesh->GetNumSkinWeightProfiles() == 0);
					SkeletalMeshProfile.DefaultProfileFromLODIndex = TargetLODIndex;
					SkeletalMeshProfile.Name = ProfileName;
					SkeletalMeshProfile.PerLODSourceFiles.Add(TargetLODIndex, UAssetImportData::SanitizeImportFilename(AbsoluteFilePath, SkeletalMesh->GetOutermost()));
					SkeletalMesh->AddSkinWeightProfile(SkeletalMeshProfile);

					Profile = &SkeletalMeshProfile;
				}
			}
		}
	}
	
	//Make sure all created objects are gone
	DeletePathAssets();

	return bResult;
}

bool FLODUtilities::ReimportAlternateSkinWeight(USkeletalMesh* SkeletalMesh, int32 TargetLODIndex, bool bReregisterComponent)
{
	bool bResult = false;

	//Bulk work of the function, we use a lambda because of the re-register component option.
	auto DoWork = [&SkeletalMesh, &TargetLODIndex, &bResult]()
	{
		const TArray<FSkinWeightProfileInfo>& SkinWeightProfiles = SkeletalMesh->GetSkinWeightProfiles();
		for (int32 ProfileIndex = 0; ProfileIndex < SkinWeightProfiles.Num(); ++ProfileIndex)
		{
			const FSkinWeightProfileInfo& ProfileInfo = SkinWeightProfiles[ProfileIndex];

			const FString* PathNamePtr = ProfileInfo.PerLODSourceFiles.Find(TargetLODIndex);
			//Skip profile that do not have data for TargetLODIndex
			if (!PathNamePtr)
			{
				continue;
			}

			const FString& PathName = *PathNamePtr;

			if (FPaths::FileExists(PathName))
			{
				bResult |= FLODUtilities::ImportAlternateSkinWeight(SkeletalMesh, PathName, TargetLODIndex, ProfileInfo.Name, false);
			}
			else
			{
				FText WarningMessage = FText::Format(LOCTEXT("Warning_SkinWeightsFileMissing", "Previous file {0} containing Skin Weight data for LOD {1} could not be found, do you want to specify a new path?"), FText::FromString(PathName), TargetLODIndex);
				if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
				{
					const FString PickedFileName = FLODUtilities::PickSkinWeightFBXPath(TargetLODIndex);
					if (!PickedFileName.IsEmpty() && FPaths::FileExists(PickedFileName))
					{
						bResult |= FLODUtilities::ImportAlternateSkinWeight(SkeletalMesh, PickedFileName, TargetLODIndex, ProfileInfo.Name, false);
					}
				}
			}
		}
	};

	if (bReregisterComponent)
	{
		TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;
		SkeletalMesh->ReleaseResources();
		SkeletalMesh->ReleaseResourcesFence.Wait();

		DoWork();

		SkeletalMesh->PostEditChange();
		SkeletalMesh->InitResources();
	}
	else
	{
		DoWork();
	}
	
	if (bResult)
	{
		FLODUtilities::RegenerateDependentLODs(SkeletalMesh, TargetLODIndex);
	}
	
	return bResult;
}

bool FLODUtilities::RemoveSkinnedWeightProfileData(USkeletalMesh* SkeletalMesh, const FName& ProfileName, int32 LODIndex)
{
	check(SkeletalMesh);
	check(SkeletalMesh->GetImportedModel());
	check(SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex));
	FSkeletalMeshLODModel& LODModelDest = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	LODModelDest.SkinWeightProfiles.Remove(ProfileName);

	FSkeletalMeshImportData ImportDataDest;
	LODModelDest.RawSkeletalMeshBulkData.LoadRawMesh(ImportDataDest);

	//Rechunk the skeletal mesh since we remove it, we rebuild the skeletal mesh to achieve rechunking
	UFbxSkeletalMeshImportData* OriginalSkeletalMeshImportData = UFbxSkeletalMeshImportData::GetImportDataForSkeletalMesh(SkeletalMesh, nullptr);

	TArray<FVector> LODPointsDest;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedgesDest;
	TArray<SkeletalMeshImportData::FMeshFace> LODFacesDest;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluencesDest;
	TArray<int32> LODPointToRawMapDest;
	ImportDataDest.CopyLODImportData(LODPointsDest, LODWedgesDest, LODFacesDest, LODInfluencesDest, LODPointToRawMapDest);

	const bool bShouldImportNormals = OriginalSkeletalMeshImportData->NormalImportMethod == FBXNIM_ImportNormals || OriginalSkeletalMeshImportData->NormalImportMethod == FBXNIM_ImportNormalsAndTangents;
	const bool bShouldImportTangents = OriginalSkeletalMeshImportData->NormalImportMethod == FBXNIM_ImportNormalsAndTangents;
	//Set the options with the current asset build options
	IMeshUtilities::MeshBuildOptions BuildOptions;
	BuildOptions.OverlappingThresholds.ThresholdPosition = OriginalSkeletalMeshImportData->ThresholdPosition;
	BuildOptions.OverlappingThresholds.ThresholdTangentNormal = OriginalSkeletalMeshImportData->ThresholdTangentNormal;
	BuildOptions.OverlappingThresholds.ThresholdUV = OriginalSkeletalMeshImportData->ThresholdUV;
	BuildOptions.bComputeNormals = !bShouldImportNormals || !ImportDataDest.bHasNormals;
	BuildOptions.bComputeTangents = !bShouldImportTangents || !ImportDataDest.bHasTangents;
	BuildOptions.bUseMikkTSpace = (OriginalSkeletalMeshImportData->NormalGenerationMethod == EFBXNormalGenerationMethod::MikkTSpace) && (!bShouldImportNormals || !bShouldImportTangents);
	BuildOptions.bRemoveDegenerateTriangles = false;

	//Build the skeletal mesh asset
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	TArray<FText> WarningMessages;
	TArray<FName> WarningNames;
	//Build the destination mesh with the Alternate influences, so the chunking is done properly.
	const bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(LODModelDest, SkeletalMesh->RefSkeleton, LODInfluencesDest, LODWedgesDest, LODFacesDest, LODPointsDest, LODPointToRawMapDest, BuildOptions, &WarningMessages, &WarningNames);
	RegenerateAllImportSkinWeightProfileData(LODModelDest);

	return bBuildSuccess;
}

void FLODUtilities::RegenerateDependentLODs(USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	FSkeletalMeshUpdateContext UpdateContext;
	UpdateContext.SkeletalMesh = SkeletalMesh;
	//Check the dependencies and regenerate the LODs acoording to it
	TArray<bool> LODDependencies;
	int32 LODNumber = SkeletalMesh->GetLODNum();
	LODDependencies.AddZeroed(LODNumber);
	bool bRegenLODs = false;
	LODDependencies[LODIndex] = true;
	for (int32 DependentLODIndex = LODIndex + 1; DependentLODIndex < LODNumber; ++DependentLODIndex)
	{
		const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(DependentLODIndex);
		if (LODInfo && LODInfo->bHasBeenSimplified && LODDependencies[LODInfo->ReductionSettings.BaseLOD])
		{
			LODDependencies[DependentLODIndex] = true;
			bRegenLODs = true;

		}
	}
	if (bRegenLODs)
	{
		TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;
		SkeletalMesh->Modify();
		SkeletalMesh->ReleaseResources();
		SkeletalMesh->ReleaseResourcesFence.Wait();
		for (int32 DependentLODIndex = LODIndex + 1; DependentLODIndex < LODNumber; ++DependentLODIndex)
		{
			if (LODDependencies[DependentLODIndex])
			{
				FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, DependentLODIndex, false);
			}
		}
		SkeletalMesh->PostEditChange();
		SkeletalMesh->InitResources();
	}
}

FString FLODUtilities::PickSkinWeightFBXPath(int32 LODIndex)
{
	FString PickedFileName("");

	FString ExtensionStr;
	ExtensionStr += TEXT("FBX files|*.fbx|");

	// First, display the file open dialog for selecting the file.
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpen = false;
	if (DesktopPlatform)
	{
		const FString DialogTitle = TEXT("Pick FBX file containing Skin Weight data for LOD ") + FString::FormatAsNumber(LODIndex);
		bOpen = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			DialogTitle,
			*FEditorDirectories::Get().GetLastDirectory(ELastDirectory::FBX),
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OpenFilenames
		);
	}

	if (bOpen)
	{
		if (OpenFilenames.Num() == 1)
		{
			PickedFileName = OpenFilenames[0];
			// Set last directory path for FBX files
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::FBX, FPaths::GetPath(PickedFileName));
		}
		else
		{
			// Error
		}
	}

	return PickedFileName;
}

#undef LOCTEXT_NAMESPACE // "LODUtilities"