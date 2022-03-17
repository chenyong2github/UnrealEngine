// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "CompGeom/ConvexHull3.h"
#include "Templates/Sorting.h"
#include "Spatial/PointHashGrid3.h"

TOptional<FGeometryCollectionConvexUtility::FGeometryCollectionConvexData> FGeometryCollectionConvexUtility::GetConvexHullDataIfPresent(FGeometryCollection* GeometryCollection)
{
	check(GeometryCollection);

	if (!GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup) ||
		!GeometryCollection->HasAttribute("ConvexHull", "Convex"))
	{
		return TOptional<FGeometryCollectionConvexUtility::FGeometryCollectionConvexData>();
	}

	FGeometryCollectionConvexUtility::FGeometryCollectionConvexData ConvexData{
		GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup),
		GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex")
	};
	return TOptional<FGeometryCollectionConvexUtility::FGeometryCollectionConvexData>(ConvexData);
}

bool FGeometryCollectionConvexUtility::HasConvexHullData(FGeometryCollection* GeometryCollection)
{
	return GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup) && GeometryCollection->HasAttribute("ConvexHull", "Convex");
}

FGeometryCollectionConvexUtility::FGeometryCollectionConvexData FGeometryCollectionConvexUtility::GetValidConvexHullData(FGeometryCollection* GeometryCollection)
{
	check (GeometryCollection)

	if (!GeometryCollection->HasGroup("Convex"))
	{
		GeometryCollection->AddGroup("Convex");
	}

	if (!GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup))
	{
		FManagedArrayCollection::FConstructionParameters ConvexDependency("Convex");
		GeometryCollection->AddAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup, ConvexDependency);
	}

	if (!GeometryCollection->HasAttribute("ConvexHull", "Convex"))
	{
		GeometryCollection->AddAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	}

	// Check for correct population. Make sure all rigid nodes should have a convex associated; leave convex hulls for transform nodes alone for now
	const TManagedArray<int32>& SimulationType = GeometryCollection->GetAttribute<int32>("SimulationType", FTransformCollection::TransformGroup);
	const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FTransformCollection::TransformGroup);
	TManagedArray<TSet<int32>>& TransformToConvexIndices = GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
	TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	
	TArray<int32> ProduceConvexHulls;
	ProduceConvexHulls.Reserve(SimulationType.Num());

	for (int32 Idx = 0; Idx < SimulationType.Num(); ++Idx)
	{
		if ((SimulationType[Idx] == FGeometryCollection::ESimulationTypes::FST_Rigid) && (TransformToConvexIndices[Idx].Num() == 0))
		{
			ProduceConvexHulls.Add(Idx);
		}
	}

	if (ProduceConvexHulls.Num())
	{
		int32 NewConvexIndexStart = GeometryCollection->AddElements(ProduceConvexHulls.Num(), "Convex");
		for (int32 Idx = 0; Idx < ProduceConvexHulls.Num(); ++Idx)
		{
			int32 GeometryIdx = TransformToGeometryIndex[ProduceConvexHulls[Idx]];
			ConvexHull[NewConvexIndexStart + Idx] = FindConvexHull(GeometryCollection, GeometryIdx);
			TransformToConvexIndices[ProduceConvexHulls[Idx]].Reset();
			TransformToConvexIndices[ProduceConvexHulls[Idx]].Add(NewConvexIndexStart + Idx);
		}
	}

	return { TransformToConvexIndices, ConvexHull };
}



namespace
{

typedef Chaos::TPlaneConcrete<Chaos::FReal, 3> FChaosPlane;

// filter points s.t. they are spaced at least more than SimplificationDistanceThreshold apart (after starting with the 4 'extreme' points to ensure we cover a volume)
void FilterHullPoints(const TArray<Chaos::FConvex::FVec3Type>& InPts, TArray<Chaos::FConvex::FVec3Type>& OutPts, double SimplificationDistanceThreshold)
{
	if (SimplificationDistanceThreshold > 0 && InPts.Num() > 0)
	{
		OutPts.Reset();

		int32 NumPts = InPts.Num();
		TArray<Chaos::FReal> DistSq;
		DistSq.SetNumUninitialized(NumPts);
		TArray<int32> PointOrder;
		PointOrder.SetNumUninitialized(NumPts);
		UE::Geometry::TPointHashGrid3<int32, Chaos::FReal> Spatial((Chaos::FReal)SimplificationDistanceThreshold, INDEX_NONE);
		Chaos::FAABB3 Bounds;
		for (int32 VIdx = 0; VIdx < NumPts; VIdx++)
		{
			Bounds.GrowToInclude(InPts[VIdx]);
			PointOrder[VIdx] = VIdx;
		}

		// Rank points by squared distance from center
		Chaos::FVec3 Center = Bounds.Center();
		for (int i = 0; i < NumPts; i++)
		{
			DistSq[i] = (InPts[i] - Center).SizeSquared();
		}

		// Start by picking the 'extreme' points to ensure we cover the volume reasonably (otherwise it's too easy to end up with a degenerate hull pieces)
		UE::Geometry::TExtremePoints3<Chaos::FReal> ExtremePoints(NumPts,
			[&InPts](int32 Idx)->UE::Math::TVector<Chaos::FReal> 
			{ 
				return (UE::Math::TVector<Chaos::FReal>)InPts[Idx];
			});
		for (int32 ExtremeIdx = 0; ExtremeIdx < ExtremePoints.Dimension + 1; ExtremeIdx++)
		{
			int32 ExtremePtIdx = ExtremePoints.Extreme[ExtremeIdx];
			if (!InPts.IsValidIndex(ExtremePtIdx))
			{
				break;
			}
			Chaos::FVec3 ExtremePt = InPts[ExtremePtIdx];
			OutPts.Add(ExtremePt);
			Spatial.InsertPointUnsafe(ExtremePtIdx, ExtremePt);
			DistSq[ExtremePtIdx] = -1; // remove these points from the distance ranking
		}

		// Sort in descending order
		Sort(&PointOrder[0], NumPts, [&DistSq](int32 i, int32 j)
			{
				return DistSq[i] > DistSq[j];
			});

		// Filter to include only w/ no other too-close points, prioritizing the farthest points
		for (int32 OrdIdx = 0; OrdIdx < NumPts; OrdIdx++)
		{
			int32 PtIdx = PointOrder[OrdIdx];
			if (DistSq[PtIdx] < 0)
			{
				break; // we've reached the extreme points that were already selected
			}
			Chaos::FVec3 Pt = InPts[PtIdx];
			TPair<int32, Chaos::FReal> NearestIdx = Spatial.FindNearestInRadius(Pt, (Chaos::FReal)SimplificationDistanceThreshold,
				[&InPts, &Pt](int32 Idx)
				{
					return (Pt - InPts[Idx]).SizeSquared();
				});
			if (NearestIdx.Key == INDEX_NONE)
			{
				Spatial.InsertPointUnsafe(PtIdx, Pt);
				OutPts.Add(Pt);
			}
		}
	}
	else
	{
		// No filtering requested -- in practice, we don't call this function in these cases below
		OutPts = InPts;
	}
}

void FilterHullPoints(TArray<Chaos::FConvex::FVec3Type>& InOutPts, double SimplificationDistanceThreshold)
{
	if (SimplificationDistanceThreshold > 0)
	{
		TArray<Chaos::FConvex::FVec3Type> FilteredPts;
		FilterHullPoints(InOutPts, FilteredPts, SimplificationDistanceThreshold);
		InOutPts = MoveTemp(FilteredPts);
	}
}

Chaos::FConvex MakeHull(const TArray<Chaos::FConvex::FVec3Type>& Pts, double SimplificationDistanceThreshold)
{
	if (SimplificationDistanceThreshold > 0)
	{
		TArray<Chaos::FConvex::FVec3Type> FilteredPts;
		FilterHullPoints(Pts, FilteredPts, SimplificationDistanceThreshold);
		
		return Chaos::FConvex(FilteredPts, KINDA_SMALL_NUMBER);
	}
	else
	{
		return Chaos::FConvex(Pts, KINDA_SMALL_NUMBER);
	}
}

/// Cut hull with plane, generating the point set of a new hull
/// @return false if plane did not cut any points on the hull
bool CutHull(const Chaos::FConvex& HullIn, FChaosPlane Plane, bool KeepSide, TArray<Chaos::FConvex::FVec3Type>& HullPtsOut)
{
	const TArray<Chaos::FConvex::FVec3Type>& Vertices = HullIn.GetVertices();
	const Chaos::FConvexStructureData& HullData = HullIn.GetStructureData();
	bool bHasOutside = false;
	for (int VertIdx = 0; VertIdx < Vertices.Num(); VertIdx++)
	{
		const Chaos::FVec3& V = Vertices[VertIdx];
		if ((Plane.SignedDistance(V) < 0) == KeepSide)
		{
			HullPtsOut.Add(V);
		}
		else
		{
			bHasOutside = true;
		}
	}

	if (!bHasOutside)
	{
		return false;
	}

	int32 NumPlanes = HullIn.NumPlanes();
	for (int PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
	{
		int32 NumPlaneVerts = HullData.NumPlaneVertices(PlaneIdx);
		for (int32 PlaneVertexIdx = 0; PlaneVertexIdx < NumPlaneVerts; PlaneVertexIdx++)
		{
			int32 NextVertIdx = (PlaneVertexIdx + 1) % NumPlaneVerts;
			const Chaos::FVec3& V0 = Vertices[HullData.GetPlaneVertex(PlaneIdx, PlaneVertexIdx)];
			const Chaos::FVec3& V1 = Vertices[HullData.GetPlaneVertex(PlaneIdx, NextVertIdx)];
			if ((Plane.SignedDistance(V0) < 0) != (Plane.SignedDistance(V1) < 0))
			{
				Chaos::Pair<Chaos::FVec3, bool> Res = Plane.FindClosestIntersection(V0, V1, 0);
				if (Res.Second)
				{
					HullPtsOut.Add(Res.First);
				}
			}
		}
	}

	return true;
}


/// Cut hull with plane, keeping both sides and generating the point set of both new hulls
/// @return false if plane did not cut any points on the hull
bool SplitHull(const Chaos::FConvex& HullIn, FChaosPlane Plane, bool KeepSide, TArray<Chaos::FVec3>& InsidePtsOut, TArray<Chaos::FVec3>& OutsidePtsOut)
{
	const TArray<Chaos::FConvex::FVec3Type>& Vertices = HullIn.GetVertices();
	const Chaos::FConvexStructureData& HullData = HullIn.GetStructureData();
	bool bHasOutside = false;
	for (int VertIdx = 0; VertIdx < Vertices.Num(); VertIdx++)
	{
		const Chaos::FVec3& V = Vertices[VertIdx];
		if ((Plane.SignedDistance(V) < 0) == KeepSide)
		{
			InsidePtsOut.Add(V);
		}
		else
		{
			OutsidePtsOut.Add(V);
			bHasOutside = true;
		}
	}

	if (!bHasOutside)
	{
		return false;
	}

	int32 NumPlanes = HullIn.NumPlanes();
	for (int PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
	{
		int32 NumPlaneVerts = HullData.NumPlaneVertices(PlaneIdx);
		for (int32 PlaneVertexIdx = 0; PlaneVertexIdx < NumPlaneVerts; PlaneVertexIdx++)
		{
			int32 NextVertIdx = (PlaneVertexIdx + 1) % NumPlaneVerts;
			const Chaos::FVec3 V0 = Vertices[HullData.GetPlaneVertex(PlaneIdx, PlaneVertexIdx)];
			const Chaos::FVec3 V1 = Vertices[HullData.GetPlaneVertex(PlaneIdx, NextVertIdx)];
			if ((Plane.SignedDistance(V0) < 0) != (Plane.SignedDistance(V1) < 0))
			{
				Chaos::Pair<Chaos::FVec3, bool> Res = Plane.FindClosestIntersection(V0, V1, 0);
				if (Res.Second)
				{
					InsidePtsOut.Add(Res.First);
					OutsidePtsOut.Add(Res.First);
				}
			}
		}
	}

	return true;
}


/// Assumptions: 
///		Convexes is initialized to one convex hull for each leaf geometry in a SHARED coordinate space
///		TransformToConvexIndices is initialized to point to the existing convex hulls
///		Parents, GeoProximity, and GeometryToTransformIndex are all initialized from the geometry collection
void CreateNonoverlappingConvexHulls(
	TArray<TUniquePtr<Chaos::FConvex>>& Convexes,
	TArray<TSet<int32>>& TransformToConvexIndices,
	TFunctionRef<bool(int32)> HasCustomConvexFn,
	const TManagedArray<int32>& SimulationType,
	int32 LeafType,
	int32 SkipType,
	const TManagedArray<int32>& Parents,
	const TManagedArray<TSet<int32>>* GeoProximity,
	const TManagedArray<int32>& GeometryToTransformIndex,
	const TManagedArray<float>* Volume,
	double FracAllowRemove,
	double SimplificationDistanceThreshold,
	double CanExceedFraction
)
{
	int32 NumBones = TransformToConvexIndices.Num();
	check(Parents.Num() == NumBones);

	auto SkipBone = [&SimulationType, SkipType](int32 Bone) -> bool
	{
		return SimulationType[Bone] == SkipType;
	};

	auto OnlyConvex = [&TransformToConvexIndices](int32 Bone) -> int32
	{
		ensure(TransformToConvexIndices[Bone].Num() <= 1);
		for (int32 ConvexIndex : TransformToConvexIndices[Bone])
		{
			return ConvexIndex;
		}
		return INDEX_NONE;
	};

	TArray<TSet<int32>> LeafProximity;
	LeafProximity.SetNum(NumBones);
	if (GeoProximity)
	{
		for (int32 GeomIdx = 0; GeomIdx < GeoProximity->Num(); GeomIdx++)
		{
			int32 TransformIdx = GeometryToTransformIndex[GeomIdx];
			for (int32 NbrGeomIdx : (*GeoProximity)[GeomIdx])
			{
				LeafProximity[TransformIdx].Add(GeometryToTransformIndex[NbrGeomIdx]);
			}
		}
	}
	else
	{
		// TODO: fill out LeafProximity by doing IsColliding calls (may have to construct Children array before this)
	}

	auto IsColliding = [&Convexes](int32 ConvexA, int32 ConvexB)
	{
		if (ConvexA == -1 || ConvexB == -1 || Convexes[ConvexA]->NumVertices() == 0 || Convexes[ConvexB]->NumVertices() == 0)
		{
			// at least one of the convex hulls was empty, so cannot be colliding
			return false;
		}
		const Chaos::TRigidTransform<Chaos::FReal, 3> IdentityTransform = Chaos::TRigidTransform<Chaos::FReal, 3>::Identity;
		return GJKIntersection(*Convexes[ConvexA], *Convexes[ConvexB], IdentityTransform);
	};

	auto IsBoneColliding = [&TransformToConvexIndices, &Convexes, &IsColliding](int32 BoneA, int32 BoneB)
	{
		for (int32 ConvexA : TransformToConvexIndices[BoneA])
		{
			for (int32 ConvexB : TransformToConvexIndices[BoneB])
			{
				if (IsColliding(ConvexA, ConvexB))
				{
					return true;
				}
			}
		}
		return false;
	};

	auto GetConvexSpan = [](const Chaos::FConvex& Convex, const Chaos::FVec3& Center, const Chaos::FVec3& Normal) -> Chaos::FVec2
	{
		int32 NumVertices = Convex.NumVertices();
		if (NumVertices == 0)
		{
			return Chaos::FVec2(0, 0);
		}
		Chaos::FReal AlongFirst = (Convex.GetVertex(0) - Center).Dot(FVector3f(Normal));
		Chaos::FVec2 Range(AlongFirst, AlongFirst);
		for (int Idx = 1; Idx < NumVertices; Idx++)
		{
			const float Along = static_cast<float>((Convex.GetVertex(Idx) - Center).Dot(FVector3f(Normal)));
			if (Along < Range.X)
			{
				Range.X = Along;
			}
			else if (Along > Range.Y)
			{
				Range.Y = Along;
			}
		}
		return Range;
	};

	// Score separating plane direction based on how well it separates (lower is better)
	//  and also compute new center for plane + normal. Normal is either the input normal or flipped.
	auto ScoreCutPlane = [&GetConvexSpan](
		const Chaos::FConvex& A, const Chaos::FConvex& B,
		const FChaosPlane& Plane, bool bOneSidedCut,
		Chaos::FVec3& OutCenter, Chaos::FVec3& OutNormal) -> Chaos::FReal
	{
		bool bRangeAValid = false, bRangeBValid = false;
		Chaos::FVec2 RangeA = GetConvexSpan(A, Plane.X(), Plane.Normal());
		Chaos::FVec2 RangeB = GetConvexSpan(B, Plane.X(), Plane.Normal());
		Chaos::FVec2 Union(FMath::Min(RangeA.X, RangeB.X), FMath::Max(RangeA.Y, RangeB.Y));
		// no intersection -- cut plane is separating, this is ideal!
		if (RangeA.X > RangeB.Y || RangeA.Y < RangeB.X)
		{
			if (RangeA.X > RangeB.Y)
			{
				OutCenter = Plane.X() + Plane.Normal() * ((RangeA.X + RangeB.Y) * (Chaos::FReal).5);
				OutNormal = -Plane.Normal();
			}
			else
			{
				OutCenter = Plane.X() + Plane.Normal() * ((RangeA.Y + RangeB.X) * (Chaos::FReal).5);
				OutNormal = Plane.Normal();
			}
			return 0;
		}
		// there was an intersection; find the actual mid plane-center and score it
		Chaos::FVec2 Intersection(FMath::Max(RangeA.X, RangeB.X), FMath::Min(RangeA.Y, RangeB.Y));
		Chaos::FReal IntersectionMid = (Intersection.X + Intersection.Y) * (Chaos::FReal).5;

		// Decide which side of the plane is kept/removed
		Chaos::FVec2 BiggerRange = RangeA;
		Chaos::FReal Sign = 1;
		if (RangeA.Y - RangeA.X < RangeB.Y - RangeB.X)
		{
			BiggerRange = RangeB;
			Sign = -1;
		}
		if (IntersectionMid - BiggerRange.X < BiggerRange.Y - IntersectionMid)
		{
			Sign *= -1;
		}
		OutNormal = Sign * Plane.Normal();

		Chaos::FReal IntersectionCut = IntersectionMid;
		if (bOneSidedCut) // if cut is one-sided, move the plane to the far end of Convex B (which it should not cut)
		{
			// which end depends on which way the output cut plane is oriented
			if (Sign > 0)
			{
				IntersectionCut = RangeB.X;
			}
			else
			{
				IntersectionCut = RangeB.Y;
			}
		}
		OutCenter = Plane.X() + Plane.Normal() * IntersectionCut;

		// Simple score favors small intersection span relative to union span
		// TODO: consider other metrics; e.g. something more directly ~ percent cut away
		return (Intersection.Y - Intersection.X) / (Union.Y - Union.X);
	};

	// Search cut plane options for the most promising one -- 
	// Usually GJK gives a good cut plane, but it can fail badly so we also test a simple 'difference between centers' plane
	// TODO: consider adding more plane options to the search
	auto FindCutPlane = [&ScoreCutPlane](const Chaos::FConvex& A, const Chaos::FConvex& B,
		Chaos::FVec3 CloseA, Chaos::FVec3 CloseB, Chaos::FVec3 Normal,
		bool bOneSidedCut,
		Chaos::FVec3& OutCenter, Chaos::FVec3& OutNormal) -> bool
	{
		OutCenter = (CloseA + CloseB) * .5;
		OutNormal = Normal;
		Chaos::FVec3 GJKCenter;
		Chaos::FReal BestScore = ScoreCutPlane(A, B, FChaosPlane(OutCenter, OutNormal), bOneSidedCut, OutCenter, OutNormal);
		Chaos::FVec3 MassSepNormal = (B.GetCenterOfMass() - A.GetCenterOfMass());
		if (MassSepNormal.Normalize() && BestScore > 0)
		{
			Chaos::FVec3 MassSepCenter = (A.GetCenterOfMass() + B.GetCenterOfMass()) * .5;
			Chaos::FReal ScoreB = ScoreCutPlane(A, B,
				FChaosPlane(MassSepCenter, MassSepNormal), bOneSidedCut, MassSepCenter, MassSepNormal);
			if (ScoreB < BestScore)
			{
				BestScore = ScoreB;
				OutCenter = MassSepCenter;
				OutNormal = MassSepNormal;
			}
		}
		if (BestScore == 0)
		{
			return false;
		}
		return true;
	};

	auto FixCollisionWithCut = [&Convexes, &FindCutPlane, &SimplificationDistanceThreshold](int32 ConvexA, int32 ConvexB)
	{
		if (Convexes[ConvexA]->NumVertices() == 0 || Convexes[ConvexB]->NumVertices() == 0)
		{
			// at least one of the convex hulls was empty, so cannot be colliding
			return false;
		}
		Chaos::FReal Depth;
		Chaos::FVec3 CloseA, CloseB, Normal;
		int32 OutIdxA, OutIdxB;
		const Chaos::TRigidTransform<Chaos::FReal, 3> IdentityTransform = Chaos::TRigidTransform<Chaos::FReal, 3>::Identity;
		bool bCollide = Chaos::GJKPenetration(*Convexes[ConvexA], *Convexes[ConvexB], IdentityTransform, Depth, CloseA, CloseB, Normal, OutIdxA, OutIdxB);
		if (bCollide)
		{
			Chaos::FVec3 IdealCenter, IdealNormal;
			if (!FindCutPlane(*Convexes[ConvexA], *Convexes[ConvexB], CloseA, CloseB, Normal, false, IdealCenter, IdealNormal))
			{
				return false;
			}
			FChaosPlane CutPlane(IdealCenter, IdealNormal);
			TArray<Chaos::FConvex::FVec3Type> CutHullPts;
			if (CutHull(*Convexes[ConvexA], CutPlane, true, CutHullPts))
			{
				*Convexes[ConvexA] = MakeHull(CutHullPts, SimplificationDistanceThreshold);
			}
			CutHullPts.Reset();
			if (CutHull(*Convexes[ConvexB], CutPlane, false, CutHullPts))
			{
				*Convexes[ConvexB] = MakeHull(CutHullPts, SimplificationDistanceThreshold);
			}
		}
		return bCollide;
	};

	// Initialize Children and Depths of tree
	// Fix collisions between input hulls using the input proximity relationships

	int32 MaxDepth = 0;
	TArray<int32> Depths;
	TArray<TArray<int32>> Children;
	Children.SetNum(NumBones);
	Depths.SetNumZeroed(NumBones);
	for (int HullIdx = 0; HullIdx < NumBones; HullIdx++)
	{
		if (Parents[HullIdx] != INDEX_NONE)
		{
			if (SimulationType[Parents[HullIdx]] != LeafType)
			{
				Children[Parents[HullIdx]].Add(HullIdx);
			}
			else
			{
				Depths[HullIdx] = -1; // child-of-leaf == embedded geometry, just ignore it
				continue;
			}
		}
		int32 Depth = 0, WalkParent = HullIdx;
		while (Parents[WalkParent] != INDEX_NONE)
		{
			Depth++;
			WalkParent = Parents[WalkParent];
		}
		Depths[HullIdx] = Depth;
		MaxDepth = FMath::Max(Depth, MaxDepth);

		if (!HasCustomConvexFn(HullIdx) && TransformToConvexIndices[HullIdx].Num() > 0)
		{
			const TSet<int32>& Neighbors = LeafProximity[HullIdx];
			for (int32 NbrIdx : Neighbors)
			{
				if (!HasCustomConvexFn(NbrIdx) && NbrIdx < HullIdx && TransformToConvexIndices[NbrIdx].Num() > 0)
				{
					// TODO: consider a one-sided cut if one of the bones has custom convexes
					FixCollisionWithCut(OnlyConvex(HullIdx), OnlyConvex(NbrIdx));
				}
			}
		}
	}

	auto AddLeaves = [&Children, &SimulationType, LeafType](int32 Bone, TArray<int32>& Leaves)
	{
		TArray<int32> ToExpand;
		ToExpand.Add(Bone);
		while (ToExpand.Num() > 0)
		{
			int32 ToProcess = ToExpand.Pop(false);
			if (SimulationType[ToProcess] == LeafType)
			{
				Leaves.Add(ToProcess);
			}
			else if (Children[ToProcess].Num() > 0)
			{
				ToExpand.Append(Children[ToProcess]);
			}
		}
	};

	// Fill OutChildren with the shallowest descendents of Bone that have convex(es) (i.e., the direct children if they all have convexes, otherwise descend further to grandchildren searching for convex hulls)
	auto AddDescendentsWithHulls = [&Children, &TransformToConvexIndices, &SimulationType, LeafType](int32 Bone, TArray<int32>& OutChildren)
	{
		TArray<int32> ToExpand = Children[Bone];
		while (ToExpand.Num() > 0)
		{
			int32 ToProcess = ToExpand.Pop(false);
			if (TransformToConvexIndices[ToProcess].Num() > 0)
			{
				OutChildren.Add(ToProcess);
			}
			else if (Children[ToProcess].Num() > 0)
			{
				ToExpand.Append(Children[ToProcess]);
			}
		}
	};

	auto FixLeafCollisions = [&AddLeaves, &FixCollisionWithCut, &OnlyConvex, &HasCustomConvexFn](int32 BoneA, int32 BoneB)
	{
		TArray<int32> LeavesA, LeavesB;
		AddLeaves(BoneA, LeavesA);
		AddLeaves(BoneB, LeavesB);
		bool bAnyCollides = false;
		for (int32 LeafA : LeavesA)
		{
			if (HasCustomConvexFn(LeafA))
			{
				continue;
			}
			for (int32 LeafB : LeavesB)
			{
				if (HasCustomConvexFn(LeafB))
				{
					continue;
				}
				// TODO: Instead of skipping all custom convex, if only one of the leaves is custom, we 
				// could iterate through the custom leaf convexes and clip the non-custom convex.
				// But need to decide a fallback if this would delete the whole non-custom convex.
				// For now, if you have custom convex leaves we just assume you they're set up to not explode the sim.
				bool bCollides = FixCollisionWithCut(OnlyConvex(LeafA), OnlyConvex(LeafB));
				bAnyCollides |= bCollides;
			}
		}
		return bAnyCollides;
	};

	// Compute initial hulls at all levels, and use these to fill out the full proximity links
	// Fix collisions between any two leaf hulls

	TArray<TSet<int32>> ClusterProximity;
	ClusterProximity.SetNum(NumBones);
	int ProcessDepth = MaxDepth;
	TArray<Chaos::FConvex::FVec3Type> JoinedHullPts;
	TArray<int32> ChildrenWithHulls;
	while (--ProcessDepth >= 0)
	{
		TSet<int32> ToProcess;
		for (int Bone = 0; Bone < Parents.Num(); Bone++)
		{
			if (Depths[Bone] == ProcessDepth)
			{
				if (TransformToConvexIndices[Bone].Num() == 0 && !HasCustomConvexFn(Bone))
				{
					JoinedHullPts.Reset();
					ChildrenWithHulls.Reset();
					AddDescendentsWithHulls(Bone, ChildrenWithHulls);
					for (int32 Child : ChildrenWithHulls)
					{
						for (int32 ConvexIdx : TransformToConvexIndices[Child])
						{
							JoinedHullPts.Append(Convexes[ConvexIdx]->GetVertices());
						}
					}
					if (JoinedHullPts.Num() > 0)
					{
						FilterHullPoints(JoinedHullPts, SimplificationDistanceThreshold);
						TUniquePtr<Chaos::FConvex> Hull = MakeUnique<Chaos::FConvex>(JoinedHullPts, KINDA_SMALL_NUMBER);
						bool bIsTooBig = false;
						if (Volume)
						{
							Chaos::FReal HullVolume = Hull->GetVolume();
							if (HullVolume > Chaos::FReal((*Volume)[Bone]) * (1.0 + CanExceedFraction))
							{
								bIsTooBig = true;
							}
						}
						if (!bIsTooBig)
						{
							int32 ConvexIdx = Convexes.Add(MoveTemp(Hull));
							TransformToConvexIndices[Bone].Add(ConvexIdx);
							ToProcess.Add(Bone);
						}
					}
				}
				else
				{
					ToProcess.Add(Bone);
				}
			}
		}

		// We don't have proximity at the cluster level, so reconstruct from n^2 collision tests for now
		for (int32 BoneA : ToProcess)
		{
			for (int32 BoneB : ToProcess)
			{
				if (BoneB < BoneA)
				{
					if (IsBoneColliding(BoneA, BoneB))
					{
						FixLeafCollisions(BoneA, BoneB);
						int32 Bones[2]{ BoneA, BoneB };
						for (int32 BoneIdx = 0; BoneIdx < 2; BoneIdx++)
						{
							int32 ParentBone = Bones[BoneIdx];
							int32 OtherBone = Bones[1 - BoneIdx];
							// if leaves changed flag that all hulls using those leaves must also change
							TArray<int32> TraverseBones;
							TraverseBones.Append(Children[ParentBone]);
							while (TraverseBones.Num() > 0)
							{
								int32 ToProc = TraverseBones.Pop(false);
								if (IsBoneColliding(OtherBone, ToProc))
								{
									ClusterProximity[OtherBone].Add(ToProc);
									ClusterProximity[ToProc].Add(OtherBone);
									TraverseBones.Append(Children[ToProc]);
								}
							}
						}

						ClusterProximity[BoneA].Add(BoneB);
						ClusterProximity[BoneB].Add(BoneA);
					}
				}
			}
		}
	}

	// Now that leaves don't intersect, re-compute all non-leaf hulls on the clipped leaves
	// and record volumes for each hull

	TArray<double> NonLeafVolumes; // Original volumes of non-leaf hulls (to compare against progressively cut-down volume as intersections are removed)
	NonLeafVolumes.SetNumZeroed(Convexes.Num());
	ProcessDepth = MaxDepth;
	while (--ProcessDepth > 0)
	{
		TSet<int32> ToProcess;
		for (int32 Bone = 0; Bone < NumBones; Bone++)
		{
			bool bCustom = HasCustomConvexFn(Bone);
			if (!bCustom && Depths[Bone] == ProcessDepth && // if we need an automatic hull and are at the current depth
				Children[Bone].Num() > 0 && !SkipBone(Bone) && // and we have children && aren't embedded geo
				TransformToConvexIndices[Bone].Num() == 1 // and hull wasn't already ruled out by CanExceedFraction
				)
			{
				JoinedHullPts.Reset();
				ChildrenWithHulls.Reset();
				AddDescendentsWithHulls(Bone, ChildrenWithHulls);
				for (int32 Child : ChildrenWithHulls)
				{
					for (int32 ConvexIdx : TransformToConvexIndices[Child])
					{
						JoinedHullPts.Append(Convexes[ConvexIdx]->GetVertices());
					}
				}
				int32 ConvexIdx = OnlyConvex(Bone);
				if (ConvexIdx)
				{
					*Convexes[ConvexIdx] = MakeHull(JoinedHullPts, SimplificationDistanceThreshold);
					NonLeafVolumes[ConvexIdx] = Convexes[ConvexIdx]->GetVolume();
				}
			}
		}
	}

	// if bOneSidedCut, then only ConvexA is cut; ConvexB is left unchanged
	auto CutIfOk = [&Convexes, &NonLeafVolumes, &FindCutPlane, &FracAllowRemove, &SimplificationDistanceThreshold](bool bOneSidedCut, int32 ConvexA, int32 ConvexB) -> bool
	{
		Chaos::FReal Depth;
		Chaos::FVec3 CloseA, CloseB, Normal;
		int32 OutIdxA, OutIdxB;
		const Chaos::TRigidTransform<Chaos::FReal, 3> IdentityTransform = Chaos::TRigidTransform<Chaos::FReal, 3>::Identity;
		bool bCollide = Chaos::GJKPenetration(*Convexes[ConvexA], *Convexes[ConvexB], IdentityTransform, Depth, CloseA, CloseB, Normal, OutIdxA, OutIdxB);
		if (bCollide)
		{
			Chaos::FVec3 IdealCenter, IdealNormal;
			FindCutPlane(*Convexes[ConvexA], *Convexes[ConvexB], CloseA, CloseB, Normal, bOneSidedCut, IdealCenter, IdealNormal);
			FChaosPlane CutPlane(IdealCenter, IdealNormal);

			// Tentatively create the clipped hulls
			Chaos::FConvex CutHullA, CutHullB;
			bool bCreatedA = false, bCreatedB = false;
			TArray<Chaos::FConvex::FVec3Type> CutHullPts;
			if (CutHull(*Convexes[ConvexA], CutPlane, true, CutHullPts))
			{
				if (CutHullPts.Num() < 4) // immediate reject zero-volume results
				{
					return false;
				}
				CutHullA = MakeHull(CutHullPts, SimplificationDistanceThreshold);
				bCreatedA = true;
			}
			if (!bOneSidedCut)
			{
				CutHullPts.Reset();
				if (CutHull(*Convexes[ConvexB], CutPlane, false, CutHullPts))
				{
					CutHullB = MakeHull(CutHullPts, SimplificationDistanceThreshold);
					bCreatedB = true;
				}
			}

			// Test if the clipped hulls have become too small vs the original volumes
			if (ensure(ConvexA < NonLeafVolumes.Num()) && NonLeafVolumes[ConvexA] > 0 && bCreatedA && CutHullA.GetVolume() / NonLeafVolumes[ConvexA] < 1 - FracAllowRemove)
			{
				return false;
			}
			if (!bOneSidedCut)
			{
				if (ensure(ConvexB < NonLeafVolumes.Num()) && NonLeafVolumes[ConvexB] > 0 && bCreatedB && CutHullB.GetVolume() / NonLeafVolumes[ConvexB] < 1 - FracAllowRemove)
				{
					return false;
				}
			}

			// If the clipped hulls were large enough, go ahead and set them as the new hulls
			if (bCreatedA)
			{
				*Convexes[ConvexA] = MoveTemp(CutHullA);
			}
			if (bCreatedB)
			{
				*Convexes[ConvexB] = MoveTemp(CutHullB);
			}

			return true;
		}
		else
		{
			return true; // no cut needed, so was ok
		}
	};

	// re-process all non-leaf bones
	ProcessDepth = MaxDepth;
	while (--ProcessDepth > 0)
	{
		TSet<int32> ToProcess;
		for (int32 Bone = 0; Bone < NumBones; Bone++)
		{
			if (Depths[Bone] == ProcessDepth && Children[Bone].Num() > 0)
			{
				ToProcess.Add(Bone);
			}
		}

		TSet<int32> WasNotOk;

		for (int32 Bone : ToProcess)
		{
			bool bCustom = HasCustomConvexFn(Bone);
			if (bCustom || TransformToConvexIndices[Bone].Num() == 0 || WasNotOk.Contains(Bone))
			{
				continue;
			}
			for (int32 Nbr : ClusterProximity[Bone])
			{
				if (WasNotOk.Contains(Nbr) || TransformToConvexIndices[Nbr].Num() == 0)
				{
					continue;
				}
				bool bNbrCustom = HasCustomConvexFn(Nbr);
				bool bAllOk = true;
				for (int32 ConvexBone : TransformToConvexIndices[Bone])
				{
					for (int32 ConvexNbr : TransformToConvexIndices[Nbr])
					{
						bool bOneSidedCut = Depths[Bone] != Depths[Nbr] || Children[Nbr].Num() == 0 || bNbrCustom;
						// if the neighbor is less deep and not a leaf, skip processing this to favor processing the neighbor instead
						if (Depths[Bone] > Depths[Nbr] && Children[Nbr].Num() > 0)
						{
							continue;
						}

						bool bWasOk = CutIfOk(bOneSidedCut, ConvexBone, ConvexNbr);

						// cut would have removed too much; just fall back to using the hulls of children
						// TODO: attempt splitting hulls before fully falling back to this
						if (!bWasOk)
						{
							bAllOk = false;
							auto ResetBoneHulls = [&WasNotOk, &TransformToConvexIndices, &Convexes](int32 ToReset)
							{
								WasNotOk.Add(ToReset);
								for (int32 ConvexIdx : TransformToConvexIndices[ToReset])
								{
									// we just leave these hulls in as null, without any references here
									// and come through and clear them later after everything is set up in the geometry collection
									// (because that has the built-in machinery to update the TransformToConvexIndices indices accordingly)
									Convexes[ConvexIdx].Reset();
								}
								TransformToConvexIndices[ToReset].Reset();
							};
							ResetBoneHulls(Bone);
							if (!bOneSidedCut)
							{
								ResetBoneHulls(Nbr);
							}
							break;
						}
					}
					if (!bAllOk)
					{
						break;
					}
				}
			}
		}
	}

	return;
}


/// Helper to get convex hulls from a geometry collection in the format required by CreateNonoverlappingConvexHulls
void HullsFromGeometry(
	FGeometryCollection& Geometry,
	TArray<FTransform>& GlobalTransformArray,
	TFunctionRef<bool(int32)> HasCustomConvexFn,
	TArray<TUniquePtr<Chaos::FConvex>>& Convexes,
	TArray<TSet<int32>>& TransformToConvexIndices,
	const TManagedArray<int32>& SimulationType,
	int32 RigidType,
	double SimplificationDistanceThreshold
)
{
	TArray<FVector> GlobalVertices;

	TOptional<FGeometryCollectionConvexUtility::FGeometryCollectionConvexData> OrigConvexData = FGeometryCollectionConvexUtility::GetConvexHullDataIfPresent(&Geometry);
	
	GlobalVertices.SetNum(Geometry.Vertex.Num());
	for (int32 Idx = 0; Idx < GlobalVertices.Num(); Idx++)
	{
		GlobalVertices[Idx] = GlobalTransformArray[Geometry.BoneMap[Idx]].TransformPosition(FVector(Geometry.Vertex[Idx]));
	}

	int32 NumBones = Geometry.TransformToGeometryIndex.Num();
	TransformToConvexIndices.SetNum(NumBones);
	TArray<Chaos::FConvex::FVec3Type> HullPts;
	for (int32 Idx = 0; Idx < NumBones; Idx++)
	{
		int32 GeomIdx = Geometry.TransformToGeometryIndex[Idx];
		if (OrigConvexData.IsSet() && HasCustomConvexFn(Idx))
		{
			// custom convex hulls are kept, but transformed to global space + copied to the new data structure
			const FTransform& Transform = GlobalTransformArray[Idx];
			for (int32 OrigConvexIdx : OrigConvexData->TransformToConvexIndices[Idx])
			{
				HullPts.Reset();
				for (const Chaos::FConvex::FVec3Type& P : OrigConvexData->ConvexHull[OrigConvexIdx]->GetVertices())
				{
					HullPts.Add(FVector(Transform.TransformPosition((FVector)P)));
				}
				// Do not simplify hulls when we're just trying to transform them
				int32 NewConvexIdx = Convexes.Add(MakeUnique<Chaos::FConvex>(HullPts, KINDA_SMALL_NUMBER));
				TransformToConvexIndices[Idx].Add(NewConvexIdx);
			}
		}
		else if (SimulationType[Idx] == RigidType && GeomIdx != INDEX_NONE)
		{
			int32 VStart = Geometry.VertexStart[GeomIdx];
			int32 VCount = Geometry.VertexCount[GeomIdx];
			int32 VEnd = VStart + VCount;
			HullPts.Reset();
			HullPts.Reserve(VCount);
			for (int32 VIdx = VStart; VIdx < VEnd; VIdx++)
			{
				HullPts.Add(GlobalVertices[VIdx]);
			}
			ensure(HullPts.Num() > 0);
			FilterHullPoints(HullPts, SimplificationDistanceThreshold);
			int32 ConvexIdx = Convexes.Add(MakeUnique<Chaos::FConvex>(HullPts, KINDA_SMALL_NUMBER));
			if (Convexes[ConvexIdx]->NumVertices() == 0 && HullPts.Num() > 0)
			{
				// if we've failed to make a convex hull, add a tiny bounding box to try to give it a hull anyway
				// (note it may still end up with an empty hull after cutting the hull to remove collisions with neighboring hulls; this just reduces the chance/number of empty hulls)
				Chaos::FConvex::FAABB3Type AABB = Convexes[ConvexIdx]->GetLocalBoundingBox();
				Chaos::FVec3 Extents = AABB.Extents();
				constexpr Chaos::FReal SmallestConvexWidth = (Chaos::FReal).11; // Convexes that span less distance than this will become empty hulls due to the size thresholds in FConvex
				if (Extents.GetMax() < SmallestConvexWidth * 2) // only apply this logic if the bounding box was actually small (in case FConvex fails to make a hull even for larger hulls)
				{
					// expand the hull to cover a .11 ^ 3 volume, so it won't disappear
					constexpr Chaos::FReal MinExpand = (Chaos::FReal).01;
					Chaos::FVec3 GrowVec;
					for (int Axis = 0; Axis < 3; Axis++)
					{
						GrowVec[Axis] = FMath::Max((Chaos::FReal).5 * (SmallestConvexWidth - Extents[Axis]), MinExpand);
					}
					AABB.ThickenSymmetrically(GrowVec);
					Chaos::FConvex::FVec3Type Min = AABB.Min();
					Chaos::FConvex::FVec3Type Max = AABB.Max();
					HullPts.Reset(); // we're adding the bounding box, so no need to keep the points inside
					HullPts.Add(Min);
					HullPts.Add(Max);
					HullPts.Add({ Min.X, Min.Y, Max.Z });
					HullPts.Add({ Min.X, Max.Y, Max.Z });
					HullPts.Add({ Max.X, Min.Y, Max.Z });
					HullPts.Add({ Max.X, Max.Y, Min.Z });
					HullPts.Add({ Max.X, Min.Y, Min.Z });
					HullPts.Add({ Min.X, Max.Y, Min.Z });
					// note: Do not use SimplificationDistanceThreshold for this fixed tiny hull
					*Convexes[ConvexIdx] = Chaos::FConvex(HullPts, KINDA_SMALL_NUMBER);
				}
			}
			TransformToConvexIndices[Idx].Add(ConvexIdx);
		}
	}
}

void TransformHullsToLocal(
	TArray<FTransform>& GlobalTransformArray,
	TArray<TUniquePtr<Chaos::FConvex>>& Convexes,
	TArray<TSet<int32>>& TransformToConvexIndices
)
{
	for (int32 Bone = 0; Bone < TransformToConvexIndices.Num(); Bone++)
	{
		FTransform& Transform = GlobalTransformArray[Bone];
		TArray<Chaos::FConvex::FVec3Type> HullPts;
		for (int32 ConvexIdx : TransformToConvexIndices[Bone])
		{
			HullPts.Reset();
			for (const Chaos::FConvex::FVec3Type& P : Convexes[ConvexIdx]->GetVertices())
			{
				HullPts.Add(FVector(Transform.InverseTransformPosition((FVector)P)));
			}
			// Do not simplify hulls when we're just trying to transform them
			*Convexes[ConvexIdx] = Chaos::FConvex(HullPts, KINDA_SMALL_NUMBER);
		}
	}
}

// copy hulls from InBone to OutBone
// Store the results in a temporary array, to be added in bulk later
bool CopyHulls(
	const TArray<FTransform>& InGlobalTransformArray,
	const TManagedArray<TUniquePtr<Chaos::FConvex>>& InConvexes,
	const TManagedArray<TSet<int32>>& InTransformToConvexIndices,
	int32 InBone,
	const TArray<FTransform>& OutGlobalTransformArray,
	TArray<TUniquePtr<Chaos::FConvex>>& OutConvexes,
	TArray<TSet<int32>>& OutTransformToConvexIndices,
	int32 OutBone
)
{
	const FTransform& InTransform = InGlobalTransformArray[InBone];
	const FTransform& OutTransform = InGlobalTransformArray[OutBone];
	TArray<Chaos::FConvex::FVec3Type> HullPts;
	for (int32 ConvexIdx : InTransformToConvexIndices[InBone])
	{
		HullPts.Reset();
		for (const Chaos::FConvex::FVec3Type& P : InConvexes[ConvexIdx]->GetVertices())
		{
			HullPts.Add(OutTransform.InverseTransformPosition(InTransform.TransformPosition(FVector(P))));
		}
		// Do not simplify hulls when we're just trying to transform them
		int32 OutIdx = OutConvexes.Add(MakeUnique<Chaos::FConvex>(HullPts, KINDA_SMALL_NUMBER));
		OutTransformToConvexIndices[OutBone].Add(OutIdx);
	}

	return true;
}

// helper to compute the volume of an individual piece of geometry
double ComputeGeometryVolume(
	const FGeometryCollection* Collection,
	int32 GeometryIdx,
	const FTransform& GlobalTransform,
	double ScalePerDimension
)
{
	int32 VStart = Collection->VertexStart[GeometryIdx];
	int32 VEnd = VStart + Collection->VertexCount[GeometryIdx];
	if (VStart == VEnd)
	{
		return 0.0;
	}
	FVector3d Center = FVector::ZeroVector;
	for (int32 VIdx = VStart; VIdx < VEnd; VIdx++)
	{
		FVector Pos = GlobalTransform.TransformPosition((FVector)Collection->Vertex[VIdx]);
		Center += (FVector3d)Pos;
	}
	Center /= double(VEnd - VStart);
	int32 FStart = Collection->FaceStart[GeometryIdx];
	int32 FEnd = FStart + Collection->FaceCount[GeometryIdx];
	double VolOut = 0;
	for (int32 FIdx = FStart; FIdx < FEnd; FIdx++)
	{
		FIntVector Tri = Collection->Indices[FIdx];
		FVector3d V0 = (FVector3d)GlobalTransform.TransformPosition((FVector)Collection->Vertex[Tri.X]);
		FVector3d V1 = (FVector3d)GlobalTransform.TransformPosition((FVector)Collection->Vertex[Tri.Y]);
		FVector3d V2 = (FVector3d)GlobalTransform.TransformPosition((FVector)Collection->Vertex[Tri.Z]);

		// add volume of the tetrahedron formed by the triangles and the reference point
		FVector3d V1mRef = (V1 - Center) * ScalePerDimension;
		FVector3d V2mRef = (V2 - Center) * ScalePerDimension;
		FVector3d N = V2mRef.Cross(V1mRef);

		VolOut += ((V0 - Center) * ScalePerDimension).Dot(N) / 6.0;
	}
	return VolOut;
}


}

FGeometryCollectionConvexUtility::FGeometryCollectionConvexData FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(FGeometryCollection* GeometryCollection, double FracAllowRemove, double SimplificationDistanceThreshold, double CanExceedFraction)
{
	check(GeometryCollection);

	TManagedArray<int32>* CustomConvexFlags = GetCustomConvexFlags(GeometryCollection, false);
	auto ConvexFlagsAlwaysFalse = [](int32) -> bool { return false; };
	auto ConvexFlagsFromArray = [&CustomConvexFlags](int32 TransformIdx) -> bool { return (bool)(*CustomConvexFlags)[TransformIdx]; };
	TFunctionRef<bool(int32)> HasCustomConvexFn = (CustomConvexFlags != nullptr) ? (TFunctionRef<bool(int32)>)ConvexFlagsFromArray : (TFunctionRef<bool(int32)>)ConvexFlagsAlwaysFalse;

	bool bHasProximity = GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup);
	if (!bHasProximity)
	{
		FGeometryCollectionProximityUtility ProximityUtility(GeometryCollection);
		ProximityUtility.UpdateProximity();
	}
	SetVolumeAttributes(GeometryCollection);

	const TManagedArray<TSet<int32>>* GCProximity = GeometryCollection->FindAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
	const TManagedArray<float>* Volume = GeometryCollection->FindAttribute<float>("Volume", FGeometryCollection::TransformGroup);
	TArray<TUniquePtr<Chaos::FConvex>> Convexes;
	TArray<TSet<int32>> TransformToConvexIndexArr;
	TArray<FTransform> GlobalTransformArray;
	GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, GlobalTransformArray);
	HullsFromGeometry(*GeometryCollection, GlobalTransformArray, HasCustomConvexFn, Convexes, TransformToConvexIndexArr, GeometryCollection->SimulationType, FGeometryCollection::ESimulationTypes::FST_Rigid, SimplificationDistanceThreshold);

	CreateNonoverlappingConvexHulls(Convexes, TransformToConvexIndexArr, HasCustomConvexFn, GeometryCollection->SimulationType, FGeometryCollection::ESimulationTypes::FST_Rigid, FGeometryCollection::ESimulationTypes::FST_None, GeometryCollection->Parent, GCProximity, GeometryCollection->TransformIndex, Volume, FracAllowRemove, SimplificationDistanceThreshold, CanExceedFraction);

	TransformHullsToLocal(GlobalTransformArray, Convexes, TransformToConvexIndexArr);

	if (!GeometryCollection->HasGroup("Convex"))
	{
		GeometryCollection->AddGroup("Convex");
	}

	if (!GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup))
	{
		FManagedArrayCollection::FConstructionParameters ConvexDependency("Convex");
		GeometryCollection->AddAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup, ConvexDependency);
	}

	if (!GeometryCollection->HasAttribute("ConvexHull", "Convex"))
	{
		GeometryCollection->AddAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	}

	TManagedArray<TSet<int32>>& TransformToConvexIndices = GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
	TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	TransformToConvexIndices = MoveTemp(TransformToConvexIndexArr);
	GeometryCollection->EmptyGroup("Convex");
	GeometryCollection->Resize(Convexes.Num(), "Convex");
	ConvexHull = MoveTemp(Convexes);

	// clear all null and empty hulls
	TArray<int32> EmptyConvex;
	for (int32 ConvexIdx = 0; ConvexIdx < ConvexHull.Num(); ConvexIdx++)
	{
		if (ConvexHull[ConvexIdx].IsValid())
		{
			if (ConvexHull[ConvexIdx]->NumVertices() == 0)
			{
				ConvexHull[ConvexIdx].Reset();
				EmptyConvex.Add(ConvexIdx);
			}
		}
		else // (!ConvexHull[ConvexIdx].IsValid())
		{
			EmptyConvex.Add(ConvexIdx);
		}
	}
	GeometryCollection->RemoveElements("Convex", EmptyConvex);

	checkSlow(FGeometryCollectionConvexUtility::ValidateConvexData(GeometryCollection));

	return { TransformToConvexIndices, ConvexHull };
}


bool FGeometryCollectionConvexUtility::ValidateConvexData(const FGeometryCollection* GeometryCollection)
{
	if (!GeometryCollection->HasAttribute("ConvexHull", "Convex") || !GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup))
	{
		return false;
	}
	const TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	for (int32 ConvexIdx = 0; ConvexIdx < ConvexHull.Num(); ConvexIdx++)
	{
		if (!ConvexHull[ConvexIdx].IsValid())
		{
			return false;
		}
	}
	const TManagedArray<TSet<int32>>& TransformToConvexIndices = GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
	for (int32 TransformIdx = 0; TransformIdx < TransformToConvexIndices.Num(); TransformIdx++)
	{
		for (int32 ConvexIdx : TransformToConvexIndices[TransformIdx])
		{
			if (ConvexIdx < 0 || ConvexIdx >= ConvexHull.Num())
			{
				return false;
			}
		}
	}
	return true;
}

TUniquePtr<Chaos::FConvex> FGeometryCollectionConvexUtility::FindConvexHull(const FGeometryCollection* GeometryCollection, int32 GeometryIndex)
{
	check(GeometryCollection);

	int32 VertexCount = GeometryCollection->VertexCount[GeometryIndex];
	int32 VertexStart = GeometryCollection->VertexStart[GeometryIndex];

	TArray<Chaos::FConvex::FVec3Type> Vertices;
	Vertices.SetNum(VertexCount);
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Vertices[VertexIndex] = GeometryCollection->Vertex[VertexStart+VertexIndex];
	}

	return MakeUnique<Chaos::FConvex>(Vertices, 0.0f);
}


void FGeometryCollectionConvexUtility::RemoveConvexHulls(FGeometryCollection* GeometryCollection, const TArray<int32>& SortedTransformDeletes)
{
	if (GeometryCollection->HasGroup("Convex") && GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup))
	{
		TManagedArray<TSet<int32>>& TransformToConvexIndices = GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
		TArray<int32> ConvexIndices;
		for (int32 TransformIdx : SortedTransformDeletes)
		{
			if (TransformToConvexIndices[TransformIdx].Num() > 0)
			{
				for (int32 ConvexIdx : TransformToConvexIndices[TransformIdx])
				{
					ConvexIndices.Add(ConvexIdx);
				}
				TransformToConvexIndices[TransformIdx].Empty();
			}
		}

		if (ConvexIndices.Num())
		{
			ConvexIndices.Sort();
			GeometryCollection->RemoveElements("Convex", ConvexIndices);
		}
	}
}


void FGeometryCollectionConvexUtility::SetDefaults(FGeometryCollection* GeometryCollection, FName Group, uint32 StartSize, uint32 NumElements)
{
}


TManagedArray<int32>* FGeometryCollectionConvexUtility::GetCustomConvexFlags(FGeometryCollection* GeometryCollection, bool bAddIfMissing)
{
	if (!GeometryCollection->HasAttribute("HasCustomConvex", FTransformCollection::TransformGroup))
	{
		if (!bAddIfMissing)
		{
			return nullptr;
		}
		return &GeometryCollection->AddAttribute<int32>("HasCustomConvex", FTransformCollection::TransformGroup);
	}
	return &GeometryCollection->GetAttribute<int32>("HasCustomConvex", FTransformCollection::TransformGroup);
}


void FGeometryCollectionConvexUtility::CopyChildConvexes(const FGeometryCollection* FromCollection, const TArrayView<const int32>& FromTransformIdx, FGeometryCollection* ToCollection, const TArrayView<const int32>& ToTransformIdx, bool bLeafOnly)
{
	const TManagedArray<TSet<int32>>* InTransformToConvexIndices = FromCollection->FindAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
	const TManagedArray<TUniquePtr<Chaos::FConvex>>* InConvexHull = FromCollection->FindAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	TOptional<FGeometryCollectionConvexUtility::FGeometryCollectionConvexData> OutConvex = FGeometryCollectionConvexUtility::GetConvexHullDataIfPresent(ToCollection);
	TManagedArray<int32>* OutCustomFlags = GetCustomConvexFlags(ToCollection, true);

	if (!ensure(InTransformToConvexIndices != nullptr && InConvexHull != nullptr) || !ensure(OutConvex.IsSet())) // missing convex data on collection(s); cannot copy
	{
		return;
	}

	if (!ensure(FromTransformIdx.Num() == ToTransformIdx.Num())) // these arrays must be matched up
	{
		return;
	}

	int32 InNumBones = FromCollection->NumElements(FGeometryCollection::TransformGroup);
	int32 LeafType = FGeometryCollection::ESimulationTypes::FST_Rigid;
	TArray<TArray<int32>> Children;
	Children.SetNum(InNumBones);
	for (int InBone = 0; InBone < InNumBones; InBone++)
	{
		if (FromCollection->SimulationType[InBone] == FGeometryCollection::ESimulationTypes::FST_None)
		{
			continue; // skip embedded geo
		}

		int32 ParentBone = FromCollection->Parent[InBone];
		if (ParentBone != INDEX_NONE)
		{
			if (FromCollection->SimulationType[ParentBone] != LeafType)
			{
				Children[ParentBone].Add(InBone);
			}
			else
			{
				// parent is leaf (should only happen for embedded geo, which we've already skipped above)
				continue;
			}
		}
	}

	auto GetChildrenWithConvex = [&FromCollection, bLeafOnly, &InTransformToConvexIndices, LeafType, &Children](int32 Bone, TArray<int32>& OutChildren)
	{
		TArray<int32> ToExpand = Children[Bone];
		if (ToExpand.IsEmpty()) // if no children, fall back to copying the convex on the bone itself
		{
			ToExpand.Add(Bone);
		}
		while (ToExpand.Num() > 0)
		{
			int32 ToProcess = ToExpand.Pop();
			bool bIsLeaf = FromCollection->SimulationType[ToProcess] == LeafType;
			if (bIsLeaf || (!bLeafOnly && (*InTransformToConvexIndices)[ToProcess].Num() > 0))
			{
				OutChildren.Add(ToProcess);
			}
			else if (Children[ToProcess].Num() > 0)
			{
				ToExpand.Append(Children[ToProcess]);
			}
		}
	};

	TArray<FTransform> InGlobalTransformArray, OutGlobalTransformArray;
	GeometryCollectionAlgo::GlobalMatrices(FromCollection->Transform, FromCollection->Parent, InGlobalTransformArray);
	if (FromCollection == ToCollection)
	{
		OutGlobalTransformArray = InGlobalTransformArray;
	}
	else
	{
		GeometryCollectionAlgo::GlobalMatrices(ToCollection->Transform, ToCollection->Parent, OutGlobalTransformArray);
	}
	
	// build the new convex data in separate arrays (which support incremental add)
	TArray<TUniquePtr<Chaos::FConvex>> ConvexToAdd;
	TArray<TSet<int32>> OutTransformToConvexIndices;
	OutTransformToConvexIndices.SetNum(OutConvex->TransformToConvexIndices.Num());
	TSet<int32> ToRemove;

	for (int32 i = 0; i < ToTransformIdx.Num(); i++)
	{
		int32 InBone = FromTransformIdx[i];
		int32 OutBone = ToTransformIdx[i];
		TArray<int32> BonesWithHulls;
		GetChildrenWithConvex(InBone, BonesWithHulls);

		ToRemove.Add(OutBone);
		(*OutCustomFlags)[OutBone] = true;
		OutTransformToConvexIndices[OutBone].Reset();
		for (int32 InBoneWithHulls : BonesWithHulls)
		{
			CopyHulls(
				InGlobalTransformArray, *InConvexHull, *InTransformToConvexIndices, InBoneWithHulls,
				OutGlobalTransformArray, ConvexToAdd, OutTransformToConvexIndices, OutBone);
		}
	}

	TArray<int32> ToRemoveArr = ToRemove.Array();
	ToRemoveArr.Sort();
	if (ToRemoveArr.Num() > 0)
	{
		RemoveConvexHulls(ToCollection, ToRemoveArr);
	}

	int32 NewNumConvex = ToCollection->NumElements("Convex");

	for (int32 OutBone : ToTransformIdx)
	{
		OutConvex->TransformToConvexIndices[OutBone] = MoveTemp(OutTransformToConvexIndices[OutBone]);
		for (int32& ConvexInd : OutConvex->TransformToConvexIndices[OutBone])
		{
			ConvexInd += NewNumConvex;
		}
	}
	
	ToCollection->Resize(NewNumConvex + ConvexToAdd.Num(), "Convex");
	for (int32 i = 0; i < ConvexToAdd.Num(); i++)
	{
		OutConvex->ConvexHull[NewNumConvex + i] = MoveTemp(ConvexToAdd[i]);
	}
}

void FGeometryCollectionConvexUtility::SetVolumeAttributes(FGeometryCollection* Collection)
{
	if (!Collection->HasAttribute("Volume", FGeometryCollection::TransformGroup))
	{
		Collection->AddAttribute<float>("Volume", FGeometryCollection::TransformGroup);
	}
	if (!Collection->HasAttribute("Size", FGeometryCollection::TransformGroup))
	{
		Collection->AddAttribute<float>("Size", FGeometryCollection::TransformGroup);
	}
	TManagedArray<float>& Volumes = Collection->GetAttribute<float>("Volume", FTransformCollection::TransformGroup);
	TManagedArray<float>& Sizes = Collection->GetAttribute<float>("Size", FTransformCollection::TransformGroup);

	const TManagedArray<int32>& SimulationType = Collection->SimulationType;
	const TManagedArray<int32>& TransformToGeometryIndex = Collection->TransformToGeometryIndex;
	const TManagedArray<int32>& Parent = Collection->Parent;

	TArray<FTransform> Transforms;
	GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, Transforms);
	TArray<int32> RecursiveOrder = GeometryCollectionAlgo::ComputeRecursiveOrder(*Collection);
	float MaxGeoVolume = 0.0f;
	Volumes.Fill(0.0f);
	for (int32 Bone : RecursiveOrder)
	{
		if (SimulationType[Bone] == FGeometryCollection::ESimulationTypes::FST_Rigid)
		{
			int32 GeoIdx = TransformToGeometryIndex[Bone];
			if (GeoIdx == INDEX_NONE)
			{
				Volumes[Bone] = 0.0f;
				continue;
			}
			else
			{
				float GeoVolume = (float)ComputeGeometryVolume(Collection, GeoIdx, Transforms[Bone], 1.0);
				MaxGeoVolume = FMath::Max(MaxGeoVolume, GeoVolume);
				Volumes[Bone] = GeoVolume;
			}
		}
		int32 ParentIdx = Parent[Bone];
		if (ParentIdx != INDEX_NONE)
		{
			Volumes[ParentIdx] += Volumes[Bone];
		}
	}
	float SizeScaleFactor = MaxGeoVolume > 0 ? (1.0f / FGenericPlatformMath::Pow(MaxGeoVolume, (1.0f / 3.0f))) : 1.0f;
	for (int32 BoneIdx = 0; BoneIdx < Volumes.Num(); BoneIdx++)
	{
		float Rt3Volume = FGenericPlatformMath::Pow(Volumes[BoneIdx], 1.0f / 3.0f);
		Sizes[BoneIdx] = Rt3Volume * SizeScaleFactor;
	}
}

