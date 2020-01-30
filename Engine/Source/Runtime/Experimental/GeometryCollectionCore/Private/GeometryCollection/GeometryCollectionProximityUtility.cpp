// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionProximityUtility.h"

#include "Async/ParallelFor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "Math/GenericOctreePublic.h"
#include "Math/GenericOctree.h"

#include <chrono>

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogChaosProximity, Verbose, All);

#define USE_OLD_METHOD 1

struct FProximityTriangle
{
	int32 ArrayIndex;
	FVector Vertices[3];
	FVector Normal;
	FBoxCenterAndExtent BoxCenterAndExtent;
	FBox Bounds;
};

/** Semantics for the simple mesh paint octree */
struct FMeshProximityTriangleOctreeSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	/**
	* Get the bounding box of the provided octree element. In this case, the box
	* is merely the point specified by the element.
	*
	* @param	Element	Octree element to get the bounding box for
	*
	* @return	Bounding box of the provided octree element
	*/
	FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FProximityTriangle& Element)
	{
		return Element.BoxCenterAndExtent;
	}


	/**
	* Determine if two octree elements are equal
	*
	* @param	A	First octree element to check
	* @param	B	Second octree element to check
	*
	* @return	true if both octree elements are equal, false if they are not
	*/
	FORCEINLINE static bool AreElementsEqual(const FProximityTriangle& A, const FProximityTriangle& B)
	{
		return (A.ArrayIndex == B.ArrayIndex);
	}

	/** Ignored for this implementation */
	FORCEINLINE static void SetElementId(const FProximityTriangle& Element, FOctreeElementId Id)
	{

	}
};
typedef TOctree<FProximityTriangle, FMeshProximityTriangleOctreeSemantics> FProximityTriangleOctree;


bool FGeometryCollectionProximityUtility::IsPointInsideOfTriangle(const FVector& P, const FVector& Vertex0, const FVector& Vertex1, const FVector& Vertex2, float Threshold)
{
	float FaceArea  = 0.5f * FVector::CrossProduct((Vertex1 - Vertex0), (Vertex2 - Vertex0)).SizeSquared();
	float Face1Area = 0.5f * FVector::CrossProduct((Vertex0 - P),(Vertex2 - P)).SizeSquared();
	float Face2Area = 0.5f * FVector::CrossProduct((Vertex0 - P),(Vertex1 - P)).SizeSquared();
	float Face3Area = 0.5f * FVector::CrossProduct((Vertex2 - P),(Vertex1 - P)).SizeSquared();

	return (FMath::Abs(Face1Area + Face2Area + Face3Area - FaceArea) < Threshold * Threshold);
}

void FGeometryCollectionProximityUtility::UpdateProximity(FGeometryCollection* GeometryCollection)
{
	check(GeometryCollection);
	if (!GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		// Proximity attribute
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		GeometryCollection->AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	if (!GeometryCollection->HasGroup(FGeometryCollection::BreakingGroup))
	{
		// Breaking Group
		GeometryCollection->AddAttribute<int32>("BreakingFaceIndex", FGeometryCollection::BreakingGroup);
		GeometryCollection->AddAttribute<int32>("BreakingSourceTransformIndex", FGeometryCollection::BreakingGroup);
		GeometryCollection->AddAttribute<int32>("BreakingTargetTransformIndex", FGeometryCollection::BreakingGroup);
		GeometryCollection->AddAttribute<FVector>("BreakingRegionCentroid", FGeometryCollection::BreakingGroup);
		GeometryCollection->AddAttribute<FVector>("BreakingRegionNormal", FGeometryCollection::BreakingGroup);
		GeometryCollection->AddAttribute<float>("BreakingRegionRadius", FGeometryCollection::BreakingGroup);
	}

	auto start = std::chrono::high_resolution_clock::now();

	const TManagedArray<FVector>& VertexArray = GeometryCollection->Vertex;
	const TManagedArray<int32>& BoneMapArray = GeometryCollection->BoneMap;
	const TManagedArray<FIntVector>& IndicesArray = GeometryCollection->Indices;
	const TManagedArray<int32>& TransformIndexArray = GeometryCollection->TransformIndex;

	// Breaking Data
	TManagedArray<TSet<int32>>& ProximityArray = GeometryCollection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
	TManagedArray<int32>& BreakingFaceIndexArray = GeometryCollection->GetAttribute<int32>("BreakingFaceIndex", FGeometryCollection::BreakingGroup);
	TManagedArray<int32>& BreakingSourceTransformIndexArray = GeometryCollection->GetAttribute<int32>("BreakingSourceTransformIndex", FGeometryCollection::BreakingGroup);
	TManagedArray<int32>& BreakingTargetTransformIndexArray = GeometryCollection->GetAttribute<int32>("BreakingTargetTransformIndex", FGeometryCollection::BreakingGroup);
	TManagedArray<FVector>& BreakingRegionCentroidArray = GeometryCollection->GetAttribute<FVector>("BreakingRegionCentroid", FGeometryCollection::BreakingGroup);
	TManagedArray<FVector>& BreakingRegionNormalArray = GeometryCollection->GetAttribute<FVector>("BreakingRegionNormal", FGeometryCollection::BreakingGroup);
	TManagedArray<float>& BreakingRegionRadiusArray = GeometryCollection->GetAttribute<float>("BreakingRegionRadius", FGeometryCollection::BreakingGroup);

	float DistanceThreshold = 1e-2;
	float DistanceThresholdSquared = DistanceThreshold * DistanceThreshold;
	TArray<FFaceTransformData> FaceTransformDataArray;
	FaceTransformDataArray.Empty();


	//
	// Create a FaceTransformDataArray for fast <FaceIndex, TransformIndex. lookup
	// It only contains faces for GEOMETRY && !CLUSTERED
	//
	int32 NumFaces = GeometryCollection->NumElements(FGeometryCollection::FacesGroup);
	for (int32 IdxFace = 0; IdxFace < NumFaces; ++IdxFace)
	{
		int32 TransformIndex = BoneMapArray[IndicesArray[IdxFace][0]];

		//		UE_LOG(LogChaosProximity, Log, TEXT("IdxFace = %d, TransformIndex = %d>"), IdxFace, TransformIndex);

		if (GeometryCollection->IsGeometry(TransformIndex) && !GeometryCollection->IsClustered(TransformIndex))
		{
			//			UE_LOG(LogChaosProximity, Log, TEXT("ADDING TO FACETRANSFORMDATAARRAY"));

			FFaceTransformData FaceData{ IdxFace,
				TransformIndex
			};
			FaceTransformDataArray.Add(FaceData);
		}
		//		else
		//		{
		//			UE_LOG(LogChaosProximity, Log, TEXT("NOT VALID"));
		//		}
	}
	NumFaces = FaceTransformDataArray.Num();

#if WITH_EDITOR
	// Create progress indicator dialog
	static const FText SlowTaskText = NSLOCTEXT("ProximityUpdate", "UpdatingProximityBreakingText", "Updating proximity & breaking data...");

	const int32 UnitProgressOutOfLoop = FMath::Max(1, (NumFaces / 50));  // One progress frame is equivalent to a minimum of 2% of the loop progress
	const int32 NumProgressOutOfLoop = 5 * UnitProgressOutOfLoop;  // 5 tasks out of loop
	const int32 NumProgressInLoop = NumFaces;

	FScopedSlowTask SlowTask(float(NumProgressOutOfLoop + NumProgressInLoop), SlowTaskText);
	SlowTask.MakeDialog();

	// Declare progress shortcut lambdas
	auto EnterProgressFrame = [&SlowTask, UnitProgressOutOfLoop]()
	{
		SlowTask.EnterProgressFrame(float(UnitProgressOutOfLoop));
	};
	int32 PrevLoopCounter = 0;
	auto EnterProgressFrameParallelLoop = [&SlowTask, &PrevLoopCounter](int32 LoopCounter)
	{
		if (IsInGameThread())
		{
			SlowTask.EnterProgressFrame(float(LoopCounter - PrevLoopCounter));
			PrevLoopCounter = LoopCounter;
		}
	};
#else
	auto EnterProgressFrame = []() {};
	auto EnterProgressFrameParallelLoop = [](int32 /*LoopCounter*/) {};
#endif

	// Build reverse map between TransformIdx and GeometryGroup index
	EnterProgressFrame();
	TMap<int32, int32> GeometryGroupIndexMap;
	int32 NumGeometries = GeometryCollection->NumElements(FGeometryCollection::GeometryGroup);
	for (int32 Idx = 0; Idx < NumGeometries; ++Idx)
	{
		GeometryGroupIndexMap.Add(TransformIndexArray[Idx], Idx);
	}

	// Transform vertices into world space
	EnterProgressFrame();
	TArray<FTransform> GlobalTransformArray;
	GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, GlobalTransformArray);

	TArray<FVector> VertexInWorldArray;
	int32 NumVertices = GeometryCollection->NumElements(FGeometryCollection::VerticesGroup);
	VertexInWorldArray.SetNum(NumVertices);

	FBox WorldBounds;
	WorldBounds.IsValid = false;

	for (int32 IdxVertex = 0; IdxVertex < NumVertices; ++IdxVertex)
	{
		FTransform Transform = GlobalTransformArray[BoneMapArray[IdxVertex]];
		FVector VertexInWorld = Transform.TransformPosition(VertexArray[IdxVertex]);

		VertexInWorldArray[IdxVertex] = VertexInWorld;
		WorldBounds += VertexInWorld;
	}

	// Make an Octree
	EnterProgressFrame();
	TUniquePtr<FProximityTriangleOctree> MeshTriOctree = MakeUnique<FProximityTriangleOctree>(WorldBounds.GetCenter(), WorldBounds.GetExtent().GetMax());

	for (int32 ii = 0; ii < FaceTransformDataArray.Num(); ++ii)
	{
		auto& FaceTransformDataRef = FaceTransformDataArray[ii];
		int32 TriIndex = FaceTransformDataRef.FaceIdx;
		// Grab the vertex indices and points for this triangle
		FProximityTriangle MeshTri;

		MeshTri.Vertices[0] = VertexInWorldArray[IndicesArray[TriIndex][0]];
		MeshTri.Vertices[1] = VertexInWorldArray[IndicesArray[TriIndex][1]];
		MeshTri.Vertices[2] = VertexInWorldArray[IndicesArray[TriIndex][2]];
		MeshTri.Normal = FVector::CrossProduct(MeshTri.Vertices[1] - MeshTri.Vertices[0], MeshTri.Vertices[2] - MeshTri.Vertices[0]).GetSafeNormal();
		MeshTri.ArrayIndex = ii;

		FBox &TriBox = MeshTri.Bounds;
		TriBox.Min.X = FMath::Min3(MeshTri.Vertices[0].X, MeshTri.Vertices[1].X, MeshTri.Vertices[2].X);
		TriBox.Min.Y = FMath::Min3(MeshTri.Vertices[0].Y, MeshTri.Vertices[1].Y, MeshTri.Vertices[2].Y);
		TriBox.Min.Z = FMath::Min3(MeshTri.Vertices[0].Z, MeshTri.Vertices[1].Z, MeshTri.Vertices[2].Z);

		TriBox.Max.X = FMath::Max3(MeshTri.Vertices[0].X, MeshTri.Vertices[1].X, MeshTri.Vertices[2].X);
		TriBox.Max.Y = FMath::Max3(MeshTri.Vertices[0].Y, MeshTri.Vertices[1].Y, MeshTri.Vertices[2].Y);
		TriBox.Max.Z = FMath::Max3(MeshTri.Vertices[0].Z, MeshTri.Vertices[1].Z, MeshTri.Vertices[2].Z);

		FaceTransformDataRef.Bounds = TriBox;

		MeshTri.BoxCenterAndExtent = FBoxCenterAndExtent(TriBox);
		MeshTriOctree->AddElement(MeshTri);
	}

	FCriticalSection Mutex;
	TSet<FOverlappingFacePair> OverlappingFacePairSet;

	ParallelFor(FaceTransformDataArray.Num(), [&](int32 FaceTransformArrayIdx) {
		EnterProgressFrameParallelLoop(FaceTransformArrayIdx);

		const auto& FaceTransformDataRef = FaceTransformDataArray[FaceTransformArrayIdx];
		TSet<FOverlappingFacePair> LocalOverlappingFacePairSet;
		FVertexPair VertexPairArray[9];

		int32 IdxFace = FaceTransformDataRef.FaceIdx;

		VertexPairArray[0].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][0]];
		VertexPairArray[1].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][0]];
		VertexPairArray[2].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][0]];

		VertexPairArray[3].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][1]];
		VertexPairArray[4].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][1]];
		VertexPairArray[5].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][1]];

		VertexPairArray[6].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][2]];
		VertexPairArray[7].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][2]];
		VertexPairArray[8].Vertex1 = VertexInWorldArray[IndicesArray[IdxFace][2]];

		const FBox& ThisFaceBounds = FaceTransformDataRef.Bounds;
		TArray<FFaceTransformData> OtherFaceTransformDataArray;

		// 	Query the Octree
		OtherFaceTransformDataArray.Reserve(NumFaces);
		for (FProximityTriangleOctree::TConstIterator<> OctreeIt(*MeshTriOctree); OctreeIt.HasPendingNodes(); OctreeIt.Advance())
		{
			const FProximityTriangleOctree::FNode& OctreeNode = OctreeIt.GetCurrentNode();
			const FOctreeNodeContext& OctreeNodeContext = OctreeIt.GetCurrentContext();

			// Leaf nodes have no children, so don't bother iterating
			if (!OctreeNode.IsLeaf())
			{
				FOREACH_OCTREE_CHILD_NODE(ChildRef)
				{
					if (OctreeNode.HasChild(ChildRef))
					{
						const FOctreeNodeContext ChildContext = OctreeNodeContext.GetChildContext(ChildRef);

						if (ThisFaceBounds.Intersect(ChildContext.Bounds.GetBox()))
						{
							// Push it on the iterator's pending node stack.
							OctreeIt.PushChild(ChildRef);
						}
					}
				}
			}

			// All of the elements in this octree node are candidates.  Note this node may not be a leaf node, and that's OK.
			for (FProximityTriangleOctree::ElementConstIt OctreeElementIt(OctreeNode.GetElementIt()); OctreeElementIt; ++OctreeElementIt)
			{
				const FProximityTriangle& OctreePolygon = *OctreeElementIt;
				OtherFaceTransformDataArray.Add(FaceTransformDataArray[OctreePolygon.ArrayIndex]);
			}
		}

		for (auto& OtherFaceTransformDataRef : OtherFaceTransformDataArray)
		{
			int32 IdxOtherFace = OtherFaceTransformDataRef.FaceIdx;

			if (FaceTransformDataRef.TransformIndex != OtherFaceTransformDataRef.TransformIndex)
			{
				//
				// Vertex coincidence test
				//
				bool VertexCoincidenceTestFoundOverlappingFaces = false;
				{
					VertexPairArray[0].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][0]];
					VertexPairArray[1].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][1]];
					VertexPairArray[2].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][2]];

					VertexPairArray[3].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][0]];
					VertexPairArray[4].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][1]];
					VertexPairArray[5].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][2]];

					VertexPairArray[6].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][0]];
					VertexPairArray[7].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][1]];
					VertexPairArray[8].Vertex2 = VertexInWorldArray[IndicesArray[IdxOtherFace][2]];

					int32 NumCoincideVertices = 0;
					for (int32 Idx = 0, ni = 9; Idx < ni; ++Idx)
					{
						if (VertexPairArray[Idx].DistanceSquared() < DistanceThresholdSquared)
						{
							NumCoincideVertices++;
						}
					}

					if (NumCoincideVertices >= 3)
					{
						VertexCoincidenceTestFoundOverlappingFaces = true;

						if (!LocalOverlappingFacePairSet.Contains(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) }))
						{
							LocalOverlappingFacePairSet.Add(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) });
						}
					}
				}

				//
				// FaceN and OtherFaceN are parallel and points of Face are in OtherFace test
				//
				if (!VertexCoincidenceTestFoundOverlappingFaces)
				{
					FVector Edge1(VertexInWorldArray[IndicesArray[IdxFace][1]] - VertexInWorldArray[IndicesArray[IdxFace][0]]);
					FVector Edge2(VertexInWorldArray[IndicesArray[IdxFace][2]] - VertexInWorldArray[IndicesArray[IdxFace][0]]);
					FVector FaceN(Edge1 ^ Edge2);

					FVector OtherEdge1(VertexInWorldArray[IndicesArray[IdxOtherFace][1]] - VertexInWorldArray[IndicesArray[IdxOtherFace][0]]);
					FVector OtherEdge2(VertexInWorldArray[IndicesArray[IdxOtherFace][2]] - VertexInWorldArray[IndicesArray[IdxOtherFace][0]]);
					FVector OtherFaceN(OtherEdge1 ^ OtherEdge2);

					if (FVector::Parallel(FaceN, OtherFaceN, 1e-1))
					{
						FVector FaceCenter((VertexInWorldArray[IndicesArray[IdxFace][0]] + VertexInWorldArray[IndicesArray[IdxFace][1]] + VertexInWorldArray[IndicesArray[IdxFace][2]]) / 3.f);
						FVector OtherFaceCenter = (VertexInWorldArray[IndicesArray[IdxOtherFace][0]] + VertexInWorldArray[IndicesArray[IdxOtherFace][1]] + VertexInWorldArray[IndicesArray[IdxOtherFace][2]]) / 3.f;
						FVector PointInFace1((VertexInWorldArray[IndicesArray[IdxFace][0]] + FaceCenter) / 2.f);
						FVector PointInFace2((VertexInWorldArray[IndicesArray[IdxFace][1]] + FaceCenter) / 2.f);
						FVector PointInFace3((VertexInWorldArray[IndicesArray[IdxFace][2]] + FaceCenter) / 2.f);

						FVector PointInFaceA[3] = { VertexInWorldArray[IndicesArray[IdxFace][0]], VertexInWorldArray[IndicesArray[IdxFace][1]], VertexInWorldArray[IndicesArray[IdxFace][2]] };
						FVector PointInFaceB[3] = { VertexInWorldArray[IndicesArray[IdxOtherFace][0]], VertexInWorldArray[IndicesArray[IdxOtherFace][1]], VertexInWorldArray[IndicesArray[IdxOtherFace][2]] };

						int32 CoincidentVerts = 0;
						for (int32 ii = 0; ii < 3; ++ii)
						{
							for (int32 kk = 0; kk < 3; ++kk)
							{
								if ((PointInFaceA[ii] - PointInFaceB[kk]).SizeSquared() < 1e-1)
								{
									++CoincidentVerts;
								}
							}
						}

						if (CoincidentVerts > 1)
						{
							if (!LocalOverlappingFacePairSet.Contains(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) }))
							{
								LocalOverlappingFacePairSet.Add(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) });
							}
						}

						// Check if points in Face are in OtherFace
						else if ((FaceCenter - OtherFaceCenter).SizeSquared() < 1e-1)
						{
							if (!LocalOverlappingFacePairSet.Contains(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) }))
							{
								LocalOverlappingFacePairSet.Add(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) });
							}
						}
						else if (IsPointInsideOfTriangle(FaceCenter, VertexInWorldArray[IndicesArray[IdxOtherFace][0]], VertexInWorldArray[IndicesArray[IdxOtherFace][1]], VertexInWorldArray[IndicesArray[IdxOtherFace][2]], 1e-1) ||
							IsPointInsideOfTriangle(PointInFace1, VertexInWorldArray[IndicesArray[IdxOtherFace][0]], VertexInWorldArray[IndicesArray[IdxOtherFace][1]], VertexInWorldArray[IndicesArray[IdxOtherFace][2]], 1e-1) ||
							IsPointInsideOfTriangle(PointInFace2, VertexInWorldArray[IndicesArray[IdxOtherFace][0]], VertexInWorldArray[IndicesArray[IdxOtherFace][1]], VertexInWorldArray[IndicesArray[IdxOtherFace][2]], 1e-1) ||
							IsPointInsideOfTriangle(PointInFace3, VertexInWorldArray[IndicesArray[IdxOtherFace][0]], VertexInWorldArray[IndicesArray[IdxOtherFace][1]], VertexInWorldArray[IndicesArray[IdxOtherFace][2]], 1e-1))
						{
							if (!LocalOverlappingFacePairSet.Contains(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) }))
							{
								LocalOverlappingFacePairSet.Add(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) });
							}
						}
						else
						{
							PointInFace1 = (VertexInWorldArray[IndicesArray[IdxOtherFace][0]] + OtherFaceCenter) / 2.f;
							PointInFace2 = (VertexInWorldArray[IndicesArray[IdxOtherFace][1]] + OtherFaceCenter) / 2.f;
							PointInFace3 = (VertexInWorldArray[IndicesArray[IdxOtherFace][2]] + OtherFaceCenter) / 2.f;

							// Check if points in OtherFace are in Face
							if (IsPointInsideOfTriangle(OtherFaceCenter, VertexInWorldArray[IndicesArray[IdxFace][0]], VertexInWorldArray[IndicesArray[IdxFace][1]], VertexInWorldArray[IndicesArray[IdxFace][2]], 1e-1) ||
								IsPointInsideOfTriangle(PointInFace1, VertexInWorldArray[IndicesArray[IdxFace][0]], VertexInWorldArray[IndicesArray[IdxFace][1]], VertexInWorldArray[IndicesArray[IdxFace][2]], 1e-1) ||
								IsPointInsideOfTriangle(PointInFace2, VertexInWorldArray[IndicesArray[IdxFace][0]], VertexInWorldArray[IndicesArray[IdxFace][1]], VertexInWorldArray[IndicesArray[IdxFace][2]], 1e-1) ||
								IsPointInsideOfTriangle(PointInFace3, VertexInWorldArray[IndicesArray[IdxFace][0]], VertexInWorldArray[IndicesArray[IdxFace][1]], VertexInWorldArray[IndicesArray[IdxFace][2]], 1e-1))
							{
								if (!LocalOverlappingFacePairSet.Contains(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) }))
								{
									LocalOverlappingFacePairSet.Add(FOverlappingFacePair{ FMath::Min(IdxFace, IdxOtherFace), FMath::Max(IdxFace, IdxOtherFace) });
								}
							}
						}
					}
				}
			}
		}
		Mutex.Lock();
		OverlappingFacePairSet.Append(LocalOverlappingFacePairSet);
		Mutex.Unlock();
	});

	if (!OverlappingFacePairSet.Num())
	{
		return;
	}

	// Populate Proximity, BreakingFaceIndex, BreakingSourceTransformIndex, BreakingTargetTransformIndex structures
	EnterProgressFrame();
	for (int32 IdxGeometry = 0; IdxGeometry < NumGeometries; ++IdxGeometry)
	{
		ProximityArray[IdxGeometry].Empty();
	}

	TArray<int32> AllBreakingFaceIndexArray;
	TArray<int32> AllBreakingSourceTransformIndexArray;
	TArray<int32> AllBreakingTargetTransformIndexArray;

	int32 NewArraySize = 2 * OverlappingFacePairSet.Num();
	AllBreakingFaceIndexArray.SetNum(NewArraySize);
	AllBreakingSourceTransformIndexArray.SetNum(NewArraySize);
	AllBreakingTargetTransformIndexArray.SetNum(NewArraySize);

	//
	// Create the {BreakingSourceTransformIndex, BreakingTargetTransformIndex} <-> FaceIndex data in the
	// AllBreakingFaceIndexArray, AllBreakingSourceTransformIndexArray, AllBreakingTargetTransformIndexArray arrays
	// This contains every connected face pairs, a lot of data
	//
	int32 IdxBreak = 0;
	for (auto& OverlappingFacePair : OverlappingFacePairSet)
	{
		int32 TransformIndex1 = BoneMapArray[IndicesArray[OverlappingFacePair.FaceIdx1][0]];
		int32 TransformIndex2 = BoneMapArray[IndicesArray[OverlappingFacePair.FaceIdx2][0]];

		check(GeometryCollection->IsGeometry(TransformIndex1) && !GeometryCollection->IsClustered(TransformIndex1));
		check(GeometryCollection->IsGeometry(TransformIndex2) && !GeometryCollection->IsClustered(TransformIndex2));

		if (!ProximityArray[GeometryGroupIndexMap[TransformIndex1]].Contains(GeometryGroupIndexMap[TransformIndex2]))
		{
			ProximityArray[GeometryGroupIndexMap[TransformIndex1]].Add(GeometryGroupIndexMap[TransformIndex2]);
		}

		AllBreakingFaceIndexArray[IdxBreak] = OverlappingFacePair.FaceIdx1;
		AllBreakingSourceTransformIndexArray[IdxBreak] = TransformIndex1;
		AllBreakingTargetTransformIndexArray[IdxBreak] = TransformIndex2;
		IdxBreak++;

		if (!ProximityArray[GeometryGroupIndexMap[TransformIndex2]].Contains(GeometryGroupIndexMap[TransformIndex1]))
		{
			ProximityArray[GeometryGroupIndexMap[TransformIndex2]].Add(GeometryGroupIndexMap[TransformIndex1]);
		}

		AllBreakingFaceIndexArray[IdxBreak] = OverlappingFacePair.FaceIdx2;
		AllBreakingSourceTransformIndexArray[IdxBreak] = TransformIndex2;
		AllBreakingTargetTransformIndexArray[IdxBreak] = TransformIndex1;
		IdxBreak++;
	}

	//
	// Store the data as a MultiMap<{BreakingSourceTransformIndex, BreakingTargetTransformIndex}, FaceIndex>
	//
	EnterProgressFrame();
	TMultiMap<FOverlappingFacePairTransformIndex, int32> FaceByConnectedTransformsMap;
	FaceByConnectedTransformsMap.Reserve(NumFaces);
	if (AllBreakingFaceIndexArray.Num())
	{
		for (int32 Idx = 0, ni = AllBreakingFaceIndexArray.Num(); Idx < ni; ++Idx)
		{
			FaceByConnectedTransformsMap.Add(FOverlappingFacePairTransformIndex{ AllBreakingSourceTransformIndexArray[Idx], AllBreakingTargetTransformIndexArray[Idx] },
				AllBreakingFaceIndexArray[Idx]);
		}
	}

	//
	// Get all the keys from the MultiMap
	//
	TArray<FOverlappingFacePairTransformIndex> FaceByConnectedTransformsMapKeys;
	FaceByConnectedTransformsMap.GenerateKeyArray(FaceByConnectedTransformsMapKeys);

	//
	// Delete all the duplicates
	//
	TSet<FOverlappingFacePairTransformIndex> FaceByConnectedTransformsMapKeysSet;
	for (int32 Idx = 0; Idx < FaceByConnectedTransformsMapKeys.Num(); ++Idx)
	{
		if (!FaceByConnectedTransformsMapKeysSet.Contains(FaceByConnectedTransformsMapKeys[Idx]))
		{
			FaceByConnectedTransformsMapKeysSet.Add(FaceByConnectedTransformsMapKeys[Idx]);
		}
	}

	int LastIndex = GeometryCollection->AddElements(FaceByConnectedTransformsMapKeysSet.Num() - BreakingFaceIndexArray.Num(), FGeometryCollection::BreakingGroup);

	//
	// Get one Face for every {BreakingSourceTransformIndex, BreakingTargetTransformIndex} pair and store the data in
	// BreakingFaceIndexArray, BreakingSourceTransformIndexArray, BreakingTargetTransformIndexArray
	//
	IdxBreak = 0;
	for (auto& Elem : FaceByConnectedTransformsMapKeysSet)
	{
		TArray<int32> FaceIndexArray;
		FaceByConnectedTransformsMap.MultiFind(Elem, FaceIndexArray);

		// Find the centroid of the region and save it into BreakingRegionCentroidArray
		FVector Centroid = FVector(ForceInitToZero);
		float TotalArea = 0.f;
		for (int32 LocalIdxFace = 0; LocalIdxFace < FaceIndexArray.Num(); ++LocalIdxFace)
		{
			const FVector& Vertex0 = VertexArray[IndicesArray[FaceIndexArray[LocalIdxFace]][0]];
			const FVector& Vertex1 = VertexArray[IndicesArray[FaceIndexArray[LocalIdxFace]][1]];
			const FVector& Vertex2 = VertexArray[IndicesArray[FaceIndexArray[LocalIdxFace]][2]];

			FVector FaceCentroid((Vertex0 + Vertex1 + Vertex2) / 3.f);
			float FaceArea = 0.5f * ((Vertex1 - Vertex0) ^ (Vertex2 - Vertex0)).Size();

			Centroid = (TotalArea * Centroid + FaceArea * FaceCentroid) / (TotalArea + FaceArea);

			TotalArea += FaceArea;
		}
		BreakingRegionCentroidArray[IdxBreak] = Centroid;

		// Find the inner radius of the region and save it into BreakingRegionRadiusArray
		float RadiusMin = FLT_MAX;
		float RadiusMax = FLT_MIN;

		TArray<FVector> TestPoints;
		for (int32 LocalIdxFace = 0; LocalIdxFace < FaceIndexArray.Num(); ++LocalIdxFace)
		{
			for (int32 Idx = 0; Idx < 3; Idx++)
			{
				TestPoints.Add(VertexArray[IndicesArray[FaceIndexArray[LocalIdxFace]][Idx]]);
			}
		}

		for (int32 IdxPoint = 0; IdxPoint < TestPoints.Num(); ++IdxPoint)
		{
			float Distance = (Centroid - TestPoints[IdxPoint]).Size();
			if (Distance < RadiusMin)
			{
				RadiusMin = Distance;
			}
			if (Distance > RadiusMax)
			{
				RadiusMax = Distance;
			}
		}
		BreakingRegionRadiusArray[IdxBreak] = RadiusMin;

		// Normal
		const FVector& VertexA = VertexArray[IndicesArray[FaceIndexArray[0]][0]];
		const FVector& VertexB = VertexArray[IndicesArray[FaceIndexArray[0]][1]];
		const FVector& VertexC = VertexArray[IndicesArray[FaceIndexArray[0]][2]];
		BreakingRegionNormalArray[IdxBreak] = ((VertexA - VertexB) ^ (VertexC - VertexB)).GetSafeNormal();

		// grab the first face from the region and save it into BreakingFaceIndexArray
		BreakingFaceIndexArray[IdxBreak] = FaceIndexArray[0];
		BreakingSourceTransformIndexArray[IdxBreak] = Elem.TransformIdx1;
		BreakingTargetTransformIndexArray[IdxBreak] = Elem.TransformIdx2;
		IdxBreak++;
	}
	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - start;
	UE_LOG(LogChaosProximity, Log, TEXT("Elapsed Time = %fs>"), elapsed.count());
}
