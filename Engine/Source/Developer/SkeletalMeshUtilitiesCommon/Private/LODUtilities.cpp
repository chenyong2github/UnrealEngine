// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "ClothingAsset.h"
#include "OverlappingCorners.h"
#include "Framework/Commands/UIAction.h"

#include "ObjectTools.h"
#include "ScopedTransaction.h"

#if WITH_APEX_CLOTHING
	#include "ApexClothingUtils.h"
#endif // #if WITH_APEX_CLOTHING

#include "ComponentReregisterContext.h"
#include "IMeshReductionManagerModule.h"
#include "Animation/SkinWeightProfile.h"

#include "Async/ParallelFor.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, SkeletalMeshUtilitiesCommon)

#define LOCTEXT_NAMESPACE "LODUtilities"

DEFINE_LOG_CATEGORY_STATIC(LogLODUtilities, Log, All);


/**
* Process and update the vertex Influences using the predefined wedges
*
* @param WedgeCount - The number of wedges in the corresponding mesh.
* @param Influences - BoneWeights and Ids for the corresponding vertices.
*/
void FLODUtilities::ProcessImportMeshInfluences(const int32 WedgeCount, TArray<SkeletalMeshImportData::FRawBoneInfluence>& Influences)
{

	// Sort influences by vertex index.
	struct FCompareVertexIndex
	{
		bool operator()(const SkeletalMeshImportData::FRawBoneInfluence& A, const SkeletalMeshImportData::FRawBoneInfluence& B) const
		{
			if (A.VertexIndex > B.VertexIndex) return false;
			else if (A.VertexIndex < B.VertexIndex) return true;
			else if (A.Weight < B.Weight) return false;
			else if (A.Weight > B.Weight) return true;
			else if (A.BoneIndex > B.BoneIndex) return false;
			else if (A.BoneIndex < B.BoneIndex) return true;
			else									  return  false;
		}
	};
	Influences.Sort(FCompareVertexIndex());

	TArray <SkeletalMeshImportData::FRawBoneInfluence> NewInfluences;
	int32	LastNewInfluenceIndex = 0;
	int32	LastVertexIndex = INDEX_NONE;
	int32	InfluenceCount = 0;

	float TotalWeight = 0.f;
	const float MINWEIGHT = 0.01f;

	int MaxVertexInfluence = 0;
	float MaxIgnoredWeight = 0.0f;

	//We have to normalize the data before filtering influences
	//Because influence filtering is base on the normalize value.
	//Some DCC like Daz studio don't have normalized weight
	for (int32 i = 0; i < Influences.Num(); i++)
	{
		// if less than min weight, or it's more than 8, then we clear it to use weight
		InfluenceCount++;
		TotalWeight += Influences[i].Weight;
		// we have all influence for the same vertex, normalize it now
		if (i + 1 >= Influences.Num() || Influences[i].VertexIndex != Influences[i + 1].VertexIndex)
		{
			// Normalize the last set of influences.
			if (InfluenceCount && (TotalWeight != 1.0f))
			{
				float OneOverTotalWeight = 1.f / TotalWeight;
				for (int r = 0; r < InfluenceCount; r++)
				{
					Influences[i - r].Weight *= OneOverTotalWeight;
				}
			}

			if (MaxVertexInfluence < InfluenceCount)
			{
				MaxVertexInfluence = InfluenceCount;
			}

			// clear to count next one
			InfluenceCount = 0;
			TotalWeight = 0.f;
		}

		if (InfluenceCount > MAX_TOTAL_INFLUENCES &&  Influences[i].Weight > MaxIgnoredWeight)
		{
			MaxIgnoredWeight = Influences[i].Weight;
		}
	}

	// warn about too many influences
	if (MaxVertexInfluence > MAX_TOTAL_INFLUENCES)
	{
		UE_LOG(LogLODUtilities, Warning, TEXT("Warning skeletal mesh influence count of %d exceeds max count of %d. Influence truncation will occur. Maximum Ignored Weight %f"), MaxVertexInfluence, MAX_TOTAL_INFLUENCES, MaxIgnoredWeight);
	}

	for (int32 i = 0; i < Influences.Num(); i++)
	{
		// we found next verts, normalize it now
		if (LastVertexIndex != Influences[i].VertexIndex)
		{
			// Normalize the last set of influences.
			if (InfluenceCount && (TotalWeight != 1.0f))
			{
				float OneOverTotalWeight = 1.f / TotalWeight;
				for (int r = 0; r < InfluenceCount; r++)
				{
					NewInfluences[LastNewInfluenceIndex - r].Weight *= OneOverTotalWeight;
				}
			}

			// now we insert missing verts
			if (LastVertexIndex != INDEX_NONE)
			{
				int32 CurrentVertexIndex = Influences[i].VertexIndex;
				for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
				{
					// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
					LastNewInfluenceIndex = NewInfluences.AddUninitialized();
					NewInfluences[LastNewInfluenceIndex].VertexIndex = j;
					NewInfluences[LastNewInfluenceIndex].BoneIndex = 0;
					NewInfluences[LastNewInfluenceIndex].Weight = 1.f;
				}
			}

			// clear to count next one
			InfluenceCount = 0;
			TotalWeight = 0.f;
			LastVertexIndex = Influences[i].VertexIndex;
		}

		// if less than min weight, or it's more than 8, then we clear it to use weight
		if (Influences[i].Weight > MINWEIGHT && InfluenceCount < MAX_TOTAL_INFLUENCES)
		{
			LastNewInfluenceIndex = NewInfluences.Add(Influences[i]);
			InfluenceCount++;
			TotalWeight += Influences[i].Weight;
		}
	}

	Influences = NewInfluences;

	// Ensure that each vertex has at least one influence as e.g. CreateSkinningStream relies on it.
	// The below code relies on influences being sorted by vertex index.
	if (Influences.Num() == 0)
	{
		// warn about no influences
		UE_LOG(LogLODUtilities, Warning, TEXT("Warning skeletal mesh has no vertex influences"));
		// add one for each wedge entry
		Influences.AddUninitialized(WedgeCount);
		for (int32 WedgeIdx = 0; WedgeIdx < WedgeCount; WedgeIdx++)
		{
			Influences[WedgeIdx].VertexIndex = WedgeIdx;
			Influences[WedgeIdx].BoneIndex = 0;
			Influences[WedgeIdx].Weight = 1.0f;
		}
		for (int32 i = 0; i < Influences.Num(); i++)
		{
			int32 CurrentVertexIndex = Influences[i].VertexIndex;

			if (LastVertexIndex != CurrentVertexIndex)
			{
				for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
				{
					// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
					Influences.InsertUninitialized(i, 1);
					Influences[i].VertexIndex = j;
					Influences[i].BoneIndex = 0;
					Influences[i].Weight = 1.f;
				}
				LastVertexIndex = CurrentVertexIndex;
			}
		}
	}
}


bool FLODUtilities::RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount /*= 0*/, bool bRegenerateEvenIfImported /*= false*/, bool bGenerateBaseLOD /*= false*/)
{
	if (SkeletalMesh)
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

		// Unbind any existing clothing assets before we regenerate all LODs
		TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
		FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings);

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
			// Only create new skeletal mesh LOD level entries, we cannot multi thread since the LOD will be create here
			//TArray are not thread safe.
			for (int32 LODIdx = CurrentNumLODs; LODIdx < LODCount; LODIdx++)
			{
				// if no previous setting found, it will use default setting. 
				FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LODIdx, false);
			}
		}
		else
		{
			for (int32 LODIdx = 0; LODIdx < LODCount; LODIdx++)
			{
				FSkeletalMeshLODInfo& CurrentLODInfo = *(SkeletalMesh->GetLODInfo(LODIdx));
				if ((bRegenerateEvenIfImported && LODIdx > 0) || (bGenerateBaseLOD && LODIdx == 0) || CurrentLODInfo.bHasBeenSimplified )
				{
					FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LODIdx, false);
				}
			}
		}

		//Restore all clothing we can
		FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings);

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
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

		// Block until this is done

		SkelMeshModel->LODModels.RemoveAt(DesiredLOD);
		SkeletalMesh->RemoveLODInfo(DesiredLOD);
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
		if (BaseSectionIndex == INDEX_NONE || !PerSectionBaseTriangleIndices.IsValidIndex(BaseSectionIndex) || PerSectionBaseTriangleIndices[BaseSectionIndex].Num() < 1)
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
		check(!BaseMeshUVBound.GetExtent().IsNearlyZero());
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
		ReductionBaseSkeletalMeshBulkData->LoadReductionData(TempBaseLODModel, BaseLODMorphTargetData, SkeletalMesh);
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
		check(ReductionBaseSkeletalMeshBulkData != nullptr);
		ReductionBaseSkeletalMeshBulkData->LoadReductionData(TempBaseLODModel, TempBaseLODMorphTargetData, SkeletalMesh);
	}

	const FSkeletalMeshLODModel& BaseLODModel = bReduceBaseLOD ? TempBaseLODModel : SkeletalMeshResource->LODModels[SourceLOD];
	const FSkeletalMeshLODInfo* BaseLODInfo = SkeletalMesh->GetLODInfo(SourceLOD);
	const FSkeletalMeshLODModel& TargetLODModel = SkeletalMeshResource->LODModels[DestinationLOD];
	const FSkeletalMeshLODInfo* TargetLODInfo = SkeletalMesh->GetLODInfo(DestinationLOD);

	TArray<int32> BaseLODMaterialMap = BaseLODInfo ? BaseLODInfo->LODMaterialMap : TArray<int32>();
	TArray<int32> TargetLODMaterialMap = TargetLODInfo ? TargetLODInfo->LODMaterialMap : TArray<int32>();

	auto InternalGetSectionMaterialIndex = [](const FSkeletalMeshLODModel& LODModel, int32 SectionIndex)->int32
	{
		if (!LODModel.Sections.IsValidIndex(SectionIndex))
		{
			return 0;
		}
		return LODModel.Sections[SectionIndex].MaterialIndex;
	};

	auto GetBaseSectionMaterialIndex = [&BaseLODModel, &InternalGetSectionMaterialIndex](int32 SectionIndex)->int32
	{
		return InternalGetSectionMaterialIndex(BaseLODModel, SectionIndex);
	};

	auto GetTargetSectionMaterialIndex = [&TargetLODModel, &InternalGetSectionMaterialIndex](int32 SectionIndex)->int32
	{
		return InternalGetSectionMaterialIndex(TargetLODModel, SectionIndex);
	};

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
	TBitArray<> BaseSectionMatch;
	BaseSectionMatch.Init(false, BaseLODModel.Sections.Num());
	//Find corresponding section indices from Source LOD for Target LOD
	for (int32 TargetSectionIndex = 0; TargetSectionIndex < TargetLODModel.Sections.Num(); ++TargetSectionIndex)
	{
		int32 TargetSectionMaterialIndex = GetTargetSectionMaterialIndex(TargetSectionIndex);
		for (int32 BaseSectionIndex = 0; BaseSectionIndex < BaseLODModel.Sections.Num(); ++BaseSectionIndex)
		{
			if (BaseSectionMatch[BaseSectionIndex])
			{
				continue;
			}
			int32 BaseSectionMaterialIndex = GetBaseSectionMaterialIndex(BaseSectionIndex);
			if (TargetSectionMaterialIndex == BaseSectionMaterialIndex)
			{
				TargetSectionMatchBaseIndex[TargetSectionIndex] = BaseSectionIndex;
				BaseSectionMatch[BaseSectionIndex] = true;
				break;
			}
		}
	}
	//We should have match all the target sections
	check(!TargetSectionMatchBaseIndex.Contains(INDEX_NONE));
	TArray<FSoftSkinVertex> BaseVertices;
	TArray<FSoftSkinVertex> TargetVertices;
	BaseLODModel.GetVertices(BaseVertices);
	TargetLODModel.GetVertices(TargetVertices);
	//Create the base triangle indices per section
	TArray<TArray<uint32>> BaseTriangleIndices;
	int32 SectionCount = BaseLODModel.Sections.Num();
	BaseTriangleIndices.AddDefaulted(SectionCount);
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		const FSkelMeshSection& Section = BaseLODModel.Sections[SectionIndex];
		uint32 TriangleCount = Section.NumTriangles;
		for (uint32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			for (uint32 PointIndex = 0; PointIndex < 3; PointIndex++)
			{
				uint32 IndexBufferValue = BaseLODModel.IndexBuffer[Section.BaseIndex + ((TriangleIndex * 3) + PointIndex)];
				BaseTriangleIndices[SectionIndex].Add(IndexBufferValue);
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

void FLODUtilities::SimplifySkeletalMeshLOD( USkeletalMesh* SkeletalMesh, int32 DesiredLOD, bool bRestoreClothing /*= false*/)
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

	if (IsInGameThread())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("DesiredLOD"), DesiredLOD);
		Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMesh->GetName()));
		const FText StatusUpdate = FText::Format(NSLOCTEXT("UnrealEd", "MeshSimp_GeneratingLOD_F", "Generating LOD{DesiredLOD} for {SkeletalMeshName}..."), Args);
		GWarn->BeginSlowTask(StatusUpdate, true);
	}

	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

	// Unbind DesiredLOD existing clothing assets before we simplify this LOD
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	if (bRestoreClothing && SkeletalMesh->GetImportedModel() && SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(DesiredLOD))
	{
		FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings, DesiredLOD);
	}

	if (SkeletalMesh->GetLODInfo(DesiredLOD) != nullptr)
	{
		FSkeletalMeshModel* SkeletalMeshResource = SkeletalMesh->GetImportedModel();
		FSkeletalMeshOptimizationSettings& Settings = SkeletalMesh->GetLODInfo(DesiredLOD)->ReductionSettings;

		//We must save the original reduction data, special case when we reduce inline we save even if its already simplified
		if (SkeletalMeshResource->LODModels.IsValidIndex(DesiredLOD) && (!SkeletalMesh->GetLODInfo(DesiredLOD)->bHasBeenSimplified || DesiredLOD == Settings.BaseLOD))
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
				// Unbind clothing before saving the original data, we must not restore clothing to do inline reduction
				{
					TArray<ClothingAssetUtils::FClothingAssetMeshBinding> TemporaryRemoveClothingBindings;
					FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, TemporaryRemoveClothingBindings, DesiredLOD);

					SkeletalMeshResource->OriginalReductionSourceMeshData[DesiredLOD]->SaveReductionData(SrcModel, BaseLODMorphTargetData, SkeletalMesh);

					if (TemporaryRemoveClothingBindings.Num() > 0)
					{
						FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, TemporaryRemoveClothingBindings, DesiredLOD);
					}
				}

				if (DesiredLOD == 0)
				{
					SkeletalMesh->GetLODInfo(DesiredLOD)->SourceImportFilename = SkeletalMesh->AssetImportData->GetFirstFilename();
				}
			}
		}
	}
	

	if (MeshReduction->ReduceSkeletalMesh(SkeletalMesh, DesiredLOD))
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

		ApplyMorphTargetOption();
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
	if (bRestoreClothing && ClothingBindings.Num() > 0 && SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(DesiredLOD))
	{
		FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings, DesiredLOD);
	}

	if (IsInGameThread())
	{
		GWarn->EndSlowTask();
	}
}

void FLODUtilities::SimplifySkeletalMeshLOD(FSkeletalMeshUpdateContext& UpdateContext, int32 DesiredLOD, bool bRestoreClothing /*= false*/)
{
	USkeletalMesh* SkeletalMesh = UpdateContext.SkeletalMesh;
	IMeshReductionModule& ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionModule>("MeshReductionInterface");
	IMeshReduction* MeshReduction = ReductionModule.GetSkeletalMeshReductionInterface();

	if (MeshReduction && MeshReduction->IsSupported() && SkeletalMesh)
	{
		SimplifySkeletalMeshLOD(SkeletalMesh, DesiredLOD, bRestoreClothing);
		
		if (UpdateContext.OnLODChanged.IsBound())
		{
			//Notify calling system of change
			UpdateContext.OnLODChanged.ExecuteIfBound();
		}
	}
}

void FLODUtilities::RestoreSkeletalMeshLODImportedData(USkeletalMesh* SkeletalMesh, int32 LodIndex)
{
	if (!SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData.IsValidIndex(LodIndex) || SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LodIndex]->IsEmpty())
	{
		//There is nothing to restore
		return;
	}

	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

	// Unbind LodIndex existing clothing assets before restoring the LOD
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings);

	FSkeletalMeshLODModel ImportedBaseLODModel;
	TMap<FString, TArray<FMorphTargetDelta>> ImportedBaseLODMorphTargetData;
	SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LodIndex]->LoadReductionData(ImportedBaseLODModel, ImportedBaseLODMorphTargetData, SkeletalMesh);
	{
		TArray<int32> EmptyLodInfoMaterialMap;
		ImportedBaseLODModel.UpdateChunkedSectionInfo(SkeletalMesh->GetName(), EmptyLodInfoMaterialMap);
		//When we restore a LOD we destroy the LODMaterialMap (user manual section material slot assignation)
		FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LodIndex);
		LODInfo->LODMaterialMap.Empty();
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
		FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings);
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

bool FLODUtilities::UpdateAlternateSkinWeights(USkeletalMesh* SkeletalMeshDest, const FName& ProfileNameDest, int32 LODIndexDest, FOverlappingThresholds OverlappingThresholds, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, bool bComputeWeightedNormals)
{
	//Grab all the destination structure
	check(SkeletalMeshDest);
	check(SkeletalMeshDest->GetImportedModel());
	check(SkeletalMeshDest->GetImportedModel()->LODModels.IsValidIndex(LODIndexDest));
	FSkeletalMeshLODModel& LODModelDest = SkeletalMeshDest->GetImportedModel()->LODModels[LODIndexDest];
	return UpdateAlternateSkinWeights(LODModelDest, SkeletalMeshDest->GetName(), SkeletalMeshDest->RefSkeleton, ProfileNameDest, LODIndexDest, OverlappingThresholds, ShouldImportNormals, ShouldImportTangents, bUseMikkTSpace, bComputeWeightedNormals);
}

bool FLODUtilities::UpdateAlternateSkinWeights(FSkeletalMeshLODModel& LODModelDest, const FString SkeletalMeshName, FReferenceSkeleton& RefSkeleton, const FName& ProfileNameDest, int32 LODIndexDest, FOverlappingThresholds OverlappingThresholds, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, bool bComputeWeightedNormals)
{
	//Ensure log message only once
	bool bNoMatchMsgDone = false;
	if (LODModelDest.RawSkeletalMeshBulkData.IsEmpty())
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Failed to import Skin Weight Profile as the target skeletal mesh (%s) requires reimporting first."), *SkeletalMeshName);
		//Very old asset will not have this data, we cannot add alternate until the asset is reimported
		return false;
	}
	FSkeletalMeshImportData ImportDataDest;
	LODModelDest.RawSkeletalMeshBulkData.LoadRawMesh(ImportDataDest);
	int32 PointNumberDest = ImportDataDest.Points.Num();
	int32 VertexNumberDest = ImportDataDest.Points.Num();

	int32 ProfileIndex = 0;
	if (!ImportDataDest.AlternateInfluenceProfileNames.Find(ProfileNameDest.ToString(), ProfileIndex))
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Failed to import Skin Weight Profile the alternate skinning imported source data is not available."), *SkeletalMeshName);
		return false;
	}
	check(ImportDataDest.AlternateInfluences.IsValidIndex(ProfileIndex));
	//The data must be there and must be verified before getting here
	const FSkeletalMeshImportData& ImportDataSrc = ImportDataDest.AlternateInfluences[ProfileIndex];
	int32 PointNumberSrc = ImportDataSrc.Points.Num();
	int32 VertexNumberSrc = ImportDataSrc.Points.Num();
	int32 InfluenceNumberSrc = ImportDataSrc.Influences.Num();

	if (ImportDataDest.NumTexCoords <= 0 || ImportDataSrc.NumTexCoords <= 0)
	{
		UE_LOG(LogLODUtilities, Error, TEXT("Failed to import Skin Weight Profile as the target skeletal mesh (%s) or imported file does not contain UV coordinates."), *SkeletalMeshName);
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
	FillRemapBoneIndexSrcToDest(ImportDataSrc, ImportDataDest, SkeletalMeshName, LODIndexDest, RemapBoneIndexSrcToDest);

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
		const SkeletalMeshImportData::FRawBoneInfluence& InfluenceSrc = ImportDataSrc.Influences[InfluenceIndexSrc];
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
	BuildOptions.OverlappingThresholds = OverlappingThresholds;
	BuildOptions.bComputeNormals = !ShouldImportNormals || !ImportDataDest.bHasNormals;
	BuildOptions.bComputeTangents = !ShouldImportTangents || !ImportDataDest.bHasTangents;
	BuildOptions.bUseMikkTSpace = (bUseMikkTSpace) && (!ShouldImportNormals || !ShouldImportTangents);
	BuildOptions.bComputeWeightedNormals = bComputeWeightedNormals;
	BuildOptions.bRemoveDegenerateTriangles = false;

	//Build the skeletal mesh asset
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	TArray<FText> WarningMessages;
	TArray<FName> WarningNames;
	//Build the destination mesh with the Alternate influences, so the chunking is done properly.
	bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(LODModelDest, RefSkeleton, LODInfluencesDest, LODWedgesDest, LODFacesDest, LODPointsDest, LODPointToRawMapDest, BuildOptions, &WarningMessages, &WarningNames);
	//Re-Apply the user section changes, the UserSectionsData is map to original section and should match the builded LODModel
	LODModelDest.SyncronizeUserSectionsDataArray();

	RegenerateAllImportSkinWeightProfileData(LODModelDest);
	
	return bBuildSuccess;
}

bool FLODUtilities::UpdateAlternateSkinWeights(USkeletalMesh* SkeletalMeshDest, const FName& ProfileNameDest, USkeletalMesh* SkeletalMeshSrc, int32 LODIndexDest, int32 LODIndexSrc, FOverlappingThresholds OverlappingThresholds, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, bool bComputeWeightedNormals)
{
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
	
	//Remove all unnecessary array data from the structure (this will save a lot of memory)
	ImportDataSrc.KeepAlternateSkinningBuildDataOnly();

	//Replace the data into the destination bulk data and save it
	int32 ProfileIndex = 0;
	if (ImportDataDest.AlternateInfluenceProfileNames.Find(ProfileNameDest.ToString(), ProfileIndex))
	{
		ImportDataDest.AlternateInfluenceProfileNames.RemoveAt(ProfileIndex);
		ImportDataDest.AlternateInfluences.RemoveAt(ProfileIndex);
	}
	ImportDataDest.AlternateInfluenceProfileNames.Add(ProfileNameDest.ToString());
	ImportDataDest.AlternateInfluences.Add(ImportDataSrc);
	//Resave the bulk data with the new or refreshed data
	LODModelDest.RawSkeletalMeshBulkData.SaveRawMesh(ImportDataDest);

	//Build the alternate buffer with all the data into the bulk
	return UpdateAlternateSkinWeights(SkeletalMeshDest, ProfileNameDest, LODIndexDest, OverlappingThresholds, ShouldImportNormals, ShouldImportTangents, bUseMikkTSpace, bComputeWeightedNormals);
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

void FLODUtilities::RegenerateDependentLODs(USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	int32 LODNumber = SkeletalMesh->GetLODNum();
	TMap<int32, TArray<int32>> Dependencies;
	TBitArray<> DependentLOD;
	DependentLOD.Init(false, LODNumber);
	DependentLOD[LODIndex] = true;
	for (int32 DependentLODIndex = LODIndex + 1; DependentLODIndex < LODNumber; ++DependentLODIndex)
	{
		const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(DependentLODIndex);
		//Only add active reduction LOD that are not inline reducted (inline mean they do not depend on LODIndex)
		if (LODInfo && (SkeletalMesh->IsReductionActive(DependentLODIndex) || LODInfo->bHasBeenSimplified) && DependentLODIndex > LODInfo->ReductionSettings.BaseLOD && DependentLOD[LODInfo->ReductionSettings.BaseLOD])
		{
			TArray<int32>& LODDependencies = Dependencies.FindOrAdd(LODInfo->ReductionSettings.BaseLOD);
			LODDependencies.Add(DependentLODIndex);
			DependentLOD[DependentLODIndex] = true;
		}
	}
	if (Dependencies.Contains(LODIndex))
	{
		//Load the necessary module before going multithreaded
		IMeshReductionModule& ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionModule>("MeshReductionInterface");
		//This will load all necessary module before kicking the multi threaded reduction
		IMeshReduction* MeshReduction = ReductionModule.GetSkeletalMeshReductionInterface();
		check(MeshReduction && MeshReduction->IsSupported());

		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("DesiredLOD"), LODIndex);
			Args.Add(TEXT("SkeletalMeshName"), FText::FromString(SkeletalMesh->GetName()));
			const FText StatusUpdate = FText::Format(NSLOCTEXT("UnrealEd", "MeshSimp_GeneratingDependentLODs_F", "Generating All Dependent LODs from LOD {DesiredLOD} for {SkeletalMeshName}..."), Args);
			GWarn->BeginSlowTask(StatusUpdate, true);
		}
		for (const auto& Kvp : Dependencies)
		{
			SkeletalMesh->Modify();
			//Use a TQueue which is thread safe, this Queue will be fill by some delegate call from other threads
			TQueue<FSkeletalMeshLODModel*> LODModelReplaceByReduction;

			const TArray<int32>& DependentLODs = Kvp.Value;
			//Clothing do not play well with multithread, backup it here. Also bind the LODModel delete delegates
			TMap<int32, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>> PerLODClothingBindings;
			for (int32 DependentLODIndex : DependentLODs)
			{
				TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings = PerLODClothingBindings.FindOrAdd(DependentLODIndex);
				FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings, DependentLODIndex);

				const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(DependentLODIndex);
				check(LODInfo);
				LODInfo->ReductionSettings.OnDeleteLODModelDelegate.BindLambda([&LODModelReplaceByReduction](FSkeletalMeshLODModel* ReplacedLODModel)
				{
					LODModelReplaceByReduction.Enqueue(ReplacedLODModel);
				});
			}

			// Load the BulkData for all dependent LODs; this has to be done on the main thread since it requires access to the Linker and FLinkerLoad::Serialize is not threadsafe
			FSkeletalMeshModel* SkeletalMeshResource = SkeletalMesh->GetImportedModel();
			if (SkeletalMeshResource)
			{
				FSkeletalMeshLODModel** LODModels = SkeletalMeshResource->LODModels.GetData();
				const int32 NumLODModels = SkeletalMeshResource->LODModels.Num();
				for (int32 DependentLODIndex : DependentLODs)
				{
					if (DependentLODIndex < NumLODModels)
					{
						FSkeletalMeshLODModel* LODModel = LODModels[DependentLODIndex];
						if (LODModel)
						{
							LODModel->RawSkeletalMeshBulkData.GetBulkData().ForceBulkDataResident();
						}
					}
				}
			}

			//Reduce all dependent LOD in same time
			ParallelFor(DependentLODs.Num(), [&DependentLODs, &SkeletalMesh](int32 IterationIndex)
			{
				check(DependentLODs.IsValidIndex(IterationIndex));
				int32 DependentLODIndex = DependentLODs[IterationIndex];
				check(SkeletalMesh->GetLODInfo(DependentLODIndex)); //We cannot add a LOD when reducing with multi thread, so check we already have one
				FLODUtilities::SimplifySkeletalMeshLOD(SkeletalMesh, DependentLODIndex, false);
			});

			//Restore the clothings and unbind the delegates
			for (int32 DependentLODIndex : DependentLODs)
			{
				TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings = PerLODClothingBindings.FindChecked(DependentLODIndex);
				FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings);

				FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(DependentLODIndex);
				check(LODInfo);
				LODInfo->ReductionSettings.OnDeleteLODModelDelegate.Unbind();
			}

			while (!LODModelReplaceByReduction.IsEmpty())
			{
				FSkeletalMeshLODModel* ReplacedLODModel = nullptr;
				LODModelReplaceByReduction.Dequeue(ReplacedLODModel);
				if (ReplacedLODModel)
				{
					delete ReplacedLODModel;
				}
			}
			check(LODModelReplaceByReduction.IsEmpty());
		}

		GWarn->EndSlowTask();
	}
}

//////////////////////////////////////////////////////////////////////////
// Morph targets build code
//

struct FMeshDataBundle
{
	TArray< FVector > Vertices;
	TArray< uint32 > Indices;
	TArray< FVector2D > UVs;
	TArray< uint32 > SmoothingGroups;
	TArray<SkeletalMeshImportData::FTriangle> Faces;
};

static void ConvertImportDataToMeshData(const FSkeletalMeshImportData& ImportData, FMeshDataBundle& MeshDataBundle)
{
	for (const SkeletalMeshImportData::FTriangle& Face : ImportData.Faces)
	{
		SkeletalMeshImportData::FTriangle FaceTriangle;
		FaceTriangle = Face;
		for (int32 i = 0; i < 3; ++i)
		{
			const SkeletalMeshImportData::FVertex& Wedge = ImportData.Wedges[Face.WedgeIndex[i]];
			int32 FaceWedgeIndex = MeshDataBundle.Indices.Add(Wedge.VertexIndex);
			MeshDataBundle.UVs.Add(Wedge.UVs[0]);
			FaceTriangle.WedgeIndex[i] = FaceWedgeIndex;
		}
		MeshDataBundle.Faces.Add(FaceTriangle);
		MeshDataBundle.SmoothingGroups.Add(Face.SmoothingGroups);
	}

	MeshDataBundle.Vertices = ImportData.Points;
}

/**
* A class encapsulating morph target processing that occurs during import on a separate thread
*/
class FAsyncImportMorphTargetWork : public FNonAbandonableTask
{
public:
	FAsyncImportMorphTargetWork(FSkeletalMeshLODModel* InLODModel, const FReferenceSkeleton& InRefSkeleton, const FSkeletalMeshImportData& InBaseImportData, TArray<FVector>&& InMorphLODPoints,
		TArray< FMorphTargetDelta >& InMorphDeltas, TArray<uint32>& InBaseIndexData, TArray< uint32 >& InBaseWedgePointIndices,
		TMap<uint32, uint32>& InWedgePointToVertexIndexMap, const FOverlappingCorners& InOverlappingCorners,
		const TSet<uint32> InModifiedPoints, const TMultiMap< int32, int32 >& InWedgeToFaces, const FMeshDataBundle& InMeshDataBundle, const TArray<FVector>& InTangentZ,
		bool InShouldImportNormals, bool InShouldImportTangents, bool InbUseMikkTSpace)
		: LODModel(InLODModel)
		, RefSkeleton(InRefSkeleton)
		, BaseImportData(InBaseImportData)
		, MorphLODPoints(InMorphLODPoints)
		, MorphTargetDeltas(InMorphDeltas)
		, BaseIndexData(InBaseIndexData)
		, BaseWedgePointIndices(InBaseWedgePointIndices)
		, WedgePointToVertexIndexMap(InWedgePointToVertexIndexMap)
		, OverlappingCorners(InOverlappingCorners)
		, ModifiedPoints(InModifiedPoints)
		, WedgeToFaces(InWedgeToFaces)
		, MeshDataBundle(InMeshDataBundle)
		, BaseTangentZ(InTangentZ)
		, TangentZ(InTangentZ)
		, ShouldImportNormals(InShouldImportNormals)
		, ShouldImportTangents(InShouldImportTangents)
		, bUseMikkTSpace(InbUseMikkTSpace)
	{
		MeshUtilities = &FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	}

	void PrepareTangents()
	{
		TArray<bool> WasProcessed;
		WasProcessed.Empty(MeshDataBundle.Indices.Num());
		WasProcessed.AddZeroed(MeshDataBundle.Indices.Num());

		TArray< int32 > WedgeFaces;
		TArray< int32 > OverlappingWedgesDummy;
		TArray< int32 > OtherOverlappingWedgesDummy;

		// For each ModifiedPoints, reset the tangents for the affected wedges
		for (int32 WedgeIdx = 0; WedgeIdx < MeshDataBundle.Indices.Num(); ++WedgeIdx)
		{
			int32 PointIdx = MeshDataBundle.Indices[WedgeIdx];

			if (ModifiedPoints.Find(PointIdx) != nullptr)
			{
				TangentZ[WedgeIdx] = FVector::ZeroVector;

				const TArray<int32>& OverlappingWedges = FindIncludingNoOverlapping(OverlappingCorners, WedgeIdx, OverlappingWedgesDummy);

				for (const int32 OverlappingWedgeIndex : OverlappingWedges)
				{
					if (WasProcessed[OverlappingWedgeIndex])
					{
						continue;
					}

					WasProcessed[OverlappingWedgeIndex] = true;

					WedgeFaces.Reset();
					WedgeToFaces.MultiFind(OverlappingWedgeIndex, WedgeFaces);

					for (const int32 FaceIndex : WedgeFaces)
					{
						for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
						{
							int32 WedgeIndex = MeshDataBundle.Faces[FaceIndex].WedgeIndex[CornerIndex];

							TangentZ[WedgeIndex] = FVector::ZeroVector;

							const TArray<int32>& OtherOverlappingWedges = FindIncludingNoOverlapping(OverlappingCorners, WedgeIndex, OtherOverlappingWedgesDummy);

							for (const int32 OtherDupVert : OtherOverlappingWedges)
							{
								TArray< int32 > OtherWedgeFaces;
								WedgeToFaces.MultiFind(OtherDupVert, OtherWedgeFaces);

								for (const int32 OtherFaceIndex : OtherWedgeFaces)
								{
									for (int32 OtherCornerIndex = 0; OtherCornerIndex < 3; ++OtherCornerIndex)
									{
										int32 OtherWedgeIndex = MeshDataBundle.Faces[OtherFaceIndex].WedgeIndex[OtherCornerIndex];

										TangentZ[OtherWedgeIndex] = FVector::ZeroVector;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	void ComputeTangents()
	{
		bool bComputeNormals = !ShouldImportNormals || !BaseImportData.bHasNormals;
		bool bComputeTangents = !ShouldImportTangents || !BaseImportData.bHasTangents;
		bool bUseMikkTSpaceFinal = bUseMikkTSpace && (!ShouldImportNormals || !ShouldImportTangents);

		check(MorphLODPoints.Num() == MeshDataBundle.Vertices.Num());

		ETangentOptions::Type TangentOptions = ETangentOptions::BlendOverlappingNormals;

		// MikkTSpace should be use only when the user want to recompute the normals or tangents otherwise should always fallback on builtin
		if (bUseMikkTSpaceFinal && (bComputeNormals || bComputeTangents))
		{
			TangentOptions = (ETangentOptions::Type)(TangentOptions | ETangentOptions::UseMikkTSpace);
		}

		MeshUtilities->CalculateNormals(MorphLODPoints, MeshDataBundle.Indices, MeshDataBundle.UVs, MeshDataBundle.SmoothingGroups, TangentOptions, TangentZ);
	}

	void ComputeMorphDeltas()
	{
		TArray<bool> WasProcessed;
		WasProcessed.Empty(LODModel->NumVertices);
		WasProcessed.AddZeroed(LODModel->NumVertices);

		for (int32 Idx = 0; Idx < BaseIndexData.Num(); ++Idx)
		{
			uint32 BaseVertIdx = BaseIndexData[Idx];
			// check for duplicate processing
			if (!WasProcessed[BaseVertIdx])
			{
				// mark this base vertex as already processed
				WasProcessed[BaseVertIdx] = true;

				// clothing can add extra verts, and we won't have source point, so we ignore those
				if (BaseWedgePointIndices.IsValidIndex(BaseVertIdx))
				{
					// get the base mesh's original wedge point index
					uint32 BasePointIdx = BaseWedgePointIndices[BaseVertIdx];
					if (MeshDataBundle.Vertices.IsValidIndex(BasePointIdx) && MorphLODPoints.IsValidIndex(BasePointIdx))
					{
						FVector BasePosition = MeshDataBundle.Vertices[BasePointIdx];
						FVector TargetPosition = MorphLODPoints[BasePointIdx];

						FVector PositionDelta = TargetPosition - BasePosition;

						uint32* VertexIdx = WedgePointToVertexIndexMap.Find(BasePointIdx);

						FVector NormalDeltaZ = FVector::ZeroVector;

						if (VertexIdx != nullptr)
						{
							FVector BaseNormal = BaseTangentZ[*VertexIdx];
							FVector TargetNormal = TangentZ[*VertexIdx];

							NormalDeltaZ = TargetNormal - BaseNormal;
						}

						// check if position actually changed much
						if (PositionDelta.SizeSquared() > FMath::Square(THRESH_POINTS_ARE_NEAR) ||
							// since we can't get imported morphtarget normal from FBX
							// we can't compare normal unless it's calculated
							// this is special flag to ignore normal diff
							((ShouldImportNormals == false) && NormalDeltaZ.SizeSquared() > 0.01f))
						{
							// create a new entry
							FMorphTargetDelta NewVertex;
							// position delta
							NewVertex.PositionDelta = PositionDelta;
							// normal delta
							NewVertex.TangentZDelta = NormalDeltaZ;
							// index of base mesh vert this entry is to modify
							NewVertex.SourceIdx = BaseVertIdx;

							// add it to the list of changed verts
							MorphTargetDeltas.Add(NewVertex);
						}
					}
				}
			}
		}
	}

	void DoWork()
	{
		PrepareTangents();
		ComputeTangents();
		ComputeMorphDeltas();
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncImportMorphTargetWork, STATGROUP_ThreadPoolAsyncTasks);
	}

private:

	const TArray<int32>& FindIncludingNoOverlapping(const FOverlappingCorners& Corners, int32 Key, TArray<int32>& NoOverlapping)
	{
		const TArray<int32>& Found = Corners.FindIfOverlapping(Key);
		if (Found.Num() > 0)
		{
			return Found;
		}
		else
		{
			NoOverlapping.Reset(1);
			NoOverlapping.Add(Key);
			return NoOverlapping;
		}
	}

	FSkeletalMeshLODModel* LODModel;
	// @todo not thread safe
	const FReferenceSkeleton& RefSkeleton;
	const FSkeletalMeshImportData& BaseImportData;
	const TArray<FVector> MorphLODPoints;

	IMeshUtilities* MeshUtilities;

	TArray< FMorphTargetDelta >& MorphTargetDeltas;
	TArray< uint32 >& BaseIndexData;
	TArray< uint32 >& BaseWedgePointIndices;
	TMap<uint32, uint32>& WedgePointToVertexIndexMap;

	const FOverlappingCorners& OverlappingCorners;
	const TSet<uint32> ModifiedPoints;
	const TMultiMap< int32, int32 >& WedgeToFaces;
	const FMeshDataBundle& MeshDataBundle;

	const TArray<FVector>& BaseTangentZ;
	TArray<FVector> TangentZ;
	bool ShouldImportNormals;
	bool ShouldImportTangents;
	bool bUseMikkTSpace;
};

void FLODUtilities::BuildMorphTargets(USkeletalMesh* BaseSkelMesh, FSkeletalMeshImportData &BaseImportData, int32 LODIndex, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace)
{
	bool bComputeNormals = !ShouldImportNormals || !BaseImportData.bHasNormals;
	bool bComputeTangents = !ShouldImportTangents || !BaseImportData.bHasTangents;
	bool bUseMikkTSpaceFinal = bUseMikkTSpace && (!ShouldImportNormals || !ShouldImportTangents);

	// Prepare base data
	FSkeletalMeshLODModel& BaseLODModel = BaseSkelMesh->GetImportedModel()->LODModels[LODIndex];

	FMeshDataBundle MeshDataBundle;
	ConvertImportDataToMeshData(BaseImportData, MeshDataBundle);

	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

	ETangentOptions::Type TangentOptions = ETangentOptions::BlendOverlappingNormals;

	// MikkTSpace should be use only when the user want to recompute the normals or tangents otherwise should always fallback on builtin
	if (bUseMikkTSpaceFinal && (bComputeNormals || bComputeTangents))
	{
		TangentOptions = (ETangentOptions::Type)(TangentOptions | ETangentOptions::UseMikkTSpace);
	}

	FOverlappingCorners OverlappingVertices;
	MeshUtilities.CalculateOverlappingCorners(MeshDataBundle.Vertices, MeshDataBundle.Indices, false, OverlappingVertices);

	TArray<FVector> TangentZ;
	MeshUtilities.CalculateNormals(MeshDataBundle.Vertices, MeshDataBundle.Indices, MeshDataBundle.UVs, MeshDataBundle.SmoothingGroups, TangentOptions, TangentZ);

	TArray< uint32 > BaseWedgePointIndices;
	if (BaseLODModel.RawPointIndices.GetBulkDataSize())
	{
		BaseWedgePointIndices.Empty(BaseLODModel.RawPointIndices.GetElementCount());
		BaseWedgePointIndices.AddUninitialized(BaseLODModel.RawPointIndices.GetElementCount());
		FMemory::Memcpy(BaseWedgePointIndices.GetData(), BaseLODModel.RawPointIndices.Lock(LOCK_READ_ONLY), BaseLODModel.RawPointIndices.GetBulkDataSize());
		BaseLODModel.RawPointIndices.Unlock();
	}

	TArray<uint32> BaseIndexData = BaseLODModel.IndexBuffer;

	TMap<uint32, uint32> WedgePointToVertexIndexMap;
	// Build a mapping of wedge point indices to vertex indices for fast lookup later.
	for (int32 Idx = 0; Idx < MeshDataBundle.Indices.Num(); ++Idx)
	{
		WedgePointToVertexIndexMap.Add(MeshDataBundle.Indices[Idx], Idx);
	}

	// Create a map from wedge indices to faces
	TMultiMap< int32, int32 > WedgeToFaces;
	for (int32 FaceIndex = 0; FaceIndex < MeshDataBundle.Faces.Num(); FaceIndex++)
	{
		const SkeletalMeshImportData::FTriangle& Face = MeshDataBundle.Faces[FaceIndex];
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			WedgeToFaces.AddUnique(Face.WedgeIndex[CornerIndex], FaceIndex);
		}
	}

	// Temp arrays to keep track of data being used by threads
	TArray< TArray< FMorphTargetDelta >* > Results;
	TArray<UMorphTarget*> MorphTargets;

	// Array of pending tasks that are not complete
	TIndirectArray<FAsyncTask<FAsyncImportMorphTargetWork> > PendingWork;

	int32 NumCompleted = 0;
	int32 NumTasks = 0;
	int32 MaxShapeInProcess = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	int32 ShapeIndex = 0;
	int32 TotalShapeCount = BaseImportData.MorphTargetNames.Num();

	// iterate through shapename, and create morphtarget
	for (int32 MorphTargetIndex = 0; MorphTargetIndex < BaseImportData.MorphTargetNames.Num(); ++MorphTargetIndex)
	{
		int32 CurrentNumTasks = PendingWork.Num();
		while (CurrentNumTasks >= MaxShapeInProcess)
		{
			//Wait until the first slot is available
			PendingWork[0].EnsureCompletion();
			for (int32 TaskIndex = PendingWork.Num() - 1; TaskIndex >= 0; --TaskIndex)
			{
				if (PendingWork[TaskIndex].IsDone())
				{
					PendingWork.RemoveAt(TaskIndex);
					++NumCompleted;
					FFormatNamedArguments Args;
					Args.Add(TEXT("NumCompleted"), NumCompleted);
					Args.Add(TEXT("NumTasks"), TotalShapeCount);
					GWarn->StatusUpdate(NumCompleted, TotalShapeCount, FText::Format(LOCTEXT("ImportingMorphTargetStatus", "Importing Morph Target: {NumCompleted} of {NumTasks}"), Args));
				}
			}
			CurrentNumTasks = PendingWork.Num();
		}

		check(BaseImportData.MorphTargetNames.IsValidIndex(MorphTargetIndex));
		check(BaseImportData.MorphTargetModifiedPoints.IsValidIndex(MorphTargetIndex));
		check(BaseImportData.MorphTargets.IsValidIndex(MorphTargetIndex));

		FString& ShapeName = BaseImportData.MorphTargetNames[MorphTargetIndex];
		FSkeletalMeshImportData& ShapeImportData = BaseImportData.MorphTargets[MorphTargetIndex];
		TSet<uint32>& ModifiedPoints = BaseImportData.MorphTargetModifiedPoints[MorphTargetIndex];

		// See if this morph target already exists.
		UMorphTarget * MorphTarget = FindObject<UMorphTarget>(BaseSkelMesh, *ShapeName);
		// we only create new one for LOD0, otherwise don't create new one
		if (!MorphTarget)
		{
			if (LODIndex == 0)
			{
				MorphTarget = NewObject<UMorphTarget>(BaseSkelMesh, FName(*ShapeName));
			}
			else
			{
				/*AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(FText::FromString("Could not find the {0} morphtarget for LOD {1}. \
					Make sure the name for morphtarget matches with LOD 0"), FText::FromString(ShapeName), FText::FromString(FString::FromInt(LODIndex)))),
					FFbxErrors::SkeletalMesh_LOD_MissingMorphTarget);*/
			}
		}

		if (MorphTarget)
		{
			MorphTargets.Add(MorphTarget);
			int32 NewMorphDeltasIdx = Results.Add(new TArray< FMorphTargetDelta >());

			TArray< FMorphTargetDelta >* Deltas = Results[NewMorphDeltasIdx];

			FAsyncTask<FAsyncImportMorphTargetWork>* NewWork = new FAsyncTask<FAsyncImportMorphTargetWork>(&BaseLODModel, BaseSkelMesh->RefSkeleton, BaseImportData,
				MoveTemp(ShapeImportData.Points), *Deltas, BaseIndexData, BaseWedgePointIndices, WedgePointToVertexIndexMap, OverlappingVertices, MoveTemp(ModifiedPoints), WedgeToFaces, MeshDataBundle, TangentZ,
				ShouldImportNormals, ShouldImportTangents, bUseMikkTSpace);
			PendingWork.Add(NewWork);

			NewWork->StartBackgroundTask(GLargeThreadPool);
			CurrentNumTasks++;
			NumTasks++;
		}

		++ShapeIndex;
	}

	// Wait for all importing tasks to complete
	for (int32 TaskIndex = 0; TaskIndex < PendingWork.Num(); ++TaskIndex)
	{
		PendingWork[TaskIndex].EnsureCompletion();

		++NumCompleted;

		FFormatNamedArguments Args;
		Args.Add(TEXT("NumCompleted"), NumCompleted);
		Args.Add(TEXT("NumTasks"), TotalShapeCount);
		GWarn->StatusUpdate(NumCompleted, NumTasks, FText::Format(LOCTEXT("ImportingMorphTargetStatus", "Importing Morph Target: {NumCompleted} of {NumTasks}"), Args));
	}

	bool bNeedToInvalidateRegisteredMorph = false;
	// Create morph streams for each morph target we are importing.
	// This has to happen on a single thread since the skeletal meshes' bulk data is locked and cant be accessed by multiple threads simultaneously
	for (int32 Index = 0; Index < MorphTargets.Num(); Index++)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("NumCompleted"), Index + 1);
		Args.Add(TEXT("NumTasks"), MorphTargets.Num());
		GWarn->StatusUpdate(Index + 1, MorphTargets.Num(), FText::Format(LOCTEXT("BuildingMorphTargetRenderDataStatus", "Building Morph Target Render Data: {NumCompleted} of {NumTasks}"), Args));

		UMorphTarget* MorphTarget = MorphTargets[Index];
		MorphTarget->PopulateDeltas(*Results[Index], LODIndex, BaseLODModel.Sections, ShouldImportNormals == false);

		// register does mark package as dirty
		if (MorphTarget->HasValidData())
		{
			bNeedToInvalidateRegisteredMorph |= BaseSkelMesh->RegisterMorphTarget(MorphTarget, false);
		}

		delete Results[Index];
		Results[Index] = nullptr;
	}

	if (bNeedToInvalidateRegisteredMorph)
	{
		BaseSkelMesh->InitMorphTargetsAndRebuildRenderData();
	}
}

void FLODUtilities::UnbindClothingAndBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings)
{
	for (int32 LODIndex = 0; LODIndex < SkeletalMesh->GetImportedModel()->LODModels.Num(); ++LODIndex)
	{
		TArray<ClothingAssetUtils::FClothingAssetMeshBinding> LODBindings;
		UnbindClothingAndBackup(SkeletalMesh, LODBindings, LODIndex);
		ClothingBindings.Append(LODBindings);
	}
}

void FLODUtilities::UnbindClothingAndBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings, const int32 LODIndex)
{
	if (!SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex))
	{
		return;
	}
	FSkeletalMeshLODModel& LODModel = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	//Store the clothBinding
	ClothingAssetUtils::GetMeshClothingAssetBindings(SkeletalMesh, ClothingBindings, LODIndex);
	//Unbind the Cloth for this LOD before we reduce it, we will put back the cloth after the reduction, if it still match the sections
	for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
	{
		if (Binding.LODIndex == LODIndex)
		{
			check(Binding.Asset);
			Binding.Asset->UnbindFromSkeletalMesh(SkeletalMesh, Binding.LODIndex);
			
			//Use the UserSectionsData original section index, this will ensure we remap correctly the cloth if the reduction has change the number of sections
			int32 OriginalDataSectionIndex = LODModel.Sections[Binding.SectionIndex].OriginalDataSectionIndex;
			Binding.SectionIndex = OriginalDataSectionIndex;
			
			FSkelMeshSourceSectionUserData& SectionUserData = LODModel.UserSectionsData.FindChecked(OriginalDataSectionIndex);
			SectionUserData.ClothingData.AssetGuid = FGuid();
			SectionUserData.ClothingData.AssetLodIndex = INDEX_NONE;
			SectionUserData.CorrespondClothAssetIndex = INDEX_NONE;
		}
	}
}

void FLODUtilities::RestoreClothingFromBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings)
{
	for (int32 LODIndex = 0; LODIndex < SkeletalMesh->GetImportedModel()->LODModels.Num(); ++LODIndex)
	{
		RestoreClothingFromBackup(SkeletalMesh, ClothingBindings, LODIndex);
	}
}

void FLODUtilities::RestoreClothingFromBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings, const int32 LODIndex)
{
	if (!SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex))
	{
		return;
	}
	FSkeletalMeshLODModel& LODModel = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	for (ClothingAssetUtils::FClothingAssetMeshBinding& Binding : ClothingBindings)
	{
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
		{
			if (LODModel.Sections[SectionIndex].OriginalDataSectionIndex != Binding.SectionIndex)
			{
				continue;
			}
			if (Binding.LODIndex == LODIndex)
			{
				check(Binding.Asset);
				if (Binding.Asset->BindToSkeletalMesh(SkeletalMesh, Binding.LODIndex, SectionIndex, Binding.AssetInternalLodIndex))
				{
					//If successfull set back the section user data
					FSkelMeshSourceSectionUserData& SectionUserData = LODModel.UserSectionsData.FindChecked(Binding.SectionIndex);
					SectionUserData.CorrespondClothAssetIndex = LODModel.Sections[SectionIndex].CorrespondClothAssetIndex;
					SectionUserData.ClothingData = LODModel.Sections[SectionIndex].ClothingData;
				}
			}
			break;
		}
	}
}



#undef LOCTEXT_NAMESPACE // "LODUtilities"