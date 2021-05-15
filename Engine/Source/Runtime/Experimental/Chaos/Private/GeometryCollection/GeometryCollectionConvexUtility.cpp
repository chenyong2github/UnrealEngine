// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"


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
		TManagedArray<TSet<int32>>& IndexArray = GeometryCollection->AddAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup, ConvexDependency);
	}

	if (!GeometryCollection->HasAttribute("ConvexHull", "Convex"))
	{
		GeometryCollection->AddAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	}

	// Check for correct population. All (and only) rigid nodes should have a convex associated.
	const TManagedArray<int32>& SimulationType = GeometryCollection->GetAttribute<int32>("SimulationType", FTransformCollection::TransformGroup);
	const TManagedArray<int32>& TransformToGeometryIndex = GeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FTransformCollection::TransformGroup);
	TManagedArray<TSet<int32>>& TransformToConvexIndices = GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
	TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");

	TArray<int32> ProduceConvexHulls;
	ProduceConvexHulls.Reserve(SimulationType.Num());
	TArray<int32> EliminateConvexHulls;
	EliminateConvexHulls.Reserve(SimulationType.Num());

	for (int32 Idx = 0; Idx < SimulationType.Num(); ++Idx)
	{
		if ((SimulationType[Idx] == FGeometryCollection::ESimulationTypes::FST_Rigid) && (TransformToConvexIndices[Idx].Num() == 0))
		{
			ProduceConvexHulls.Add(Idx);
		}
		else if ((SimulationType[Idx] != FGeometryCollection::ESimulationTypes::FST_Rigid) && (TransformToConvexIndices[Idx].Num() > 0))
		{
			EliminateConvexHulls.Add(Idx);
		}
	}

	if (EliminateConvexHulls.Num())
	{
		GeometryCollection->RemoveElements("Convex", EliminateConvexHulls);
		for (int32 Idx : EliminateConvexHulls)
		{
			TransformToConvexIndices[Idx].Reset();
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

/// Cut hull with plane, generating the point set of a new hull
/// @return false if plane did not cut any points on the hull
bool CutHull(const Chaos::FConvex& HullIn, FChaosPlane Plane, bool KeepSide, TArray<Chaos::FVec3>& HullPtsOut)
{
	const TArray<Chaos::FVec3>& Vertices = HullIn.GetVertices();
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
	const TArray<Chaos::FVec3>& Vertices = HullIn.GetVertices();
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
			const Chaos::FVec3& V0 = Vertices[HullData.GetPlaneVertex(PlaneIdx, PlaneVertexIdx)];
			const Chaos::FVec3& V1 = Vertices[HullData.GetPlaneVertex(PlaneIdx, NextVertIdx)];
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
	const TManagedArray<int32>& SimulationType,
	int32 LeafType,
	const TManagedArray<int32>& Parents,
	const TManagedArray<TSet<int32>>& GeoProximity,
	const TManagedArray<int32>& GeometryToTransformIndex,
	double FracAllowRemove
)
{
	int32 NumBones = TransformToConvexIndices.Num();
	check(Parents.Num() == NumBones);

	auto OnlyConvex = [&TransformToConvexIndices](int32 Bone) -> int32
	{
		ensure(TransformToConvexIndices[Bone].Num() == 1);
		for (int32 ConvexIndex : TransformToConvexIndices[Bone])
		{
			return ConvexIndex;
		}
		return INDEX_NONE;
	};

	TArray<TSet<int32>> LeafProximity;
	LeafProximity.SetNum(NumBones);
	for (int32 GeomIdx = 0; GeomIdx < GeoProximity.Num(); GeomIdx++)
	{
		int32 TransformIdx = GeometryToTransformIndex[GeomIdx];
		for (int32 NbrGeomIdx : GeoProximity[GeomIdx])
		{
			LeafProximity[TransformIdx].Add(GeometryToTransformIndex[NbrGeomIdx]);
		}
	}

	auto IsColliding2 = [&Convexes](int32 ConvexA, int32 ConvexB)
	{
		if (!ensure(Convexes[ConvexA]->NumVertices() > 0 && Convexes[ConvexB]->NumVertices() > 0))
		{
			// at least one of the convex hulls was empty, so cannot be colliding
			// TODO: eventually remove this ensure; and add more handling for empty hulls
			return false;
		}
		const Chaos::TRigidTransform<Chaos::FReal, 3> IdentityTransform = Chaos::TRigidTransform<Chaos::FReal, 3>::Identity;
		return GJKIntersection(*Convexes[ConvexA], *Convexes[ConvexB], IdentityTransform);
	};

	auto FixCollisionWithCut2 = [&Convexes](int32 ConvexA, int32 ConvexB)
	{
		if (!ensure(Convexes[ConvexA]->NumVertices() > 0 && Convexes[ConvexB]->NumVertices() > 0))
		{
			// at least one of the convex hulls was empty, so cannot be colliding
			// TODO: eventually remove this ensure; and add more handling for empty hulls
			return false;
		}
		Chaos::FReal Depth;
		Chaos::FVec3 CloseA, CloseB, Normal;
		int32 OutIdxA, OutIdxB;
		const Chaos::TRigidTransform<Chaos::FReal, 3> IdentityTransform = Chaos::TRigidTransform<Chaos::FReal, 3>::Identity;
		bool bCollide = Chaos::GJKPenetration(*Convexes[ConvexA], *Convexes[ConvexB], IdentityTransform, Depth, CloseA, CloseB, Normal, OutIdxA, OutIdxB);
		if (bCollide)
		{
			FChaosPlane CutPlane((CloseA + CloseB) * .5, Normal);
			TArray<Chaos::FVec3> CutHullPts;
			if (CutHull(*Convexes[ConvexA], CutPlane, true, CutHullPts))
			{
				*Convexes[ConvexA] = Chaos::FConvex(CutHullPts, KINDA_SMALL_NUMBER);
			}
			CutHullPts.Reset();
			if (CutHull(*Convexes[ConvexB], CutPlane, false, CutHullPts))
			{
				*Convexes[ConvexB] = Chaos::FConvex(CutHullPts, KINDA_SMALL_NUMBER);
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

		if (TransformToConvexIndices[HullIdx].Num() > 0)
		{
			const TSet<int32>& Neighbors = LeafProximity[HullIdx];
			for (int32 NbrIdx : Neighbors)
			{
				if (NbrIdx < HullIdx && TransformToConvexIndices[NbrIdx].Num() > 0)
				{
					FixCollisionWithCut2(OnlyConvex(HullIdx), OnlyConvex(NbrIdx));
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
			if (Children[ToProcess].Num() > 0 && SimulationType[ToProcess] != LeafType)
			{
				ToExpand.Append(Children[ToProcess]);
			}
			else
			{
				Leaves.Add(ToProcess);
			}
		}
	};

	auto FixLeafCollisions = [&AddLeaves, &FixCollisionWithCut2, &OnlyConvex](int32 BoneA, int32 BoneB)
	{
		TArray<int32> LeavesA, LeavesB;
		AddLeaves(BoneA, LeavesA);
		AddLeaves(BoneB, LeavesB);
		bool bAnyCollides = false;
		for (int32 LeafA : LeavesA)
		{
			for (int32 LeafB : LeavesB)
			{
				bool bCollides = FixCollisionWithCut2(OnlyConvex(LeafA), OnlyConvex(LeafB));
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
	while (--ProcessDepth >= 0)
	{
		TSet<int32> ToProcess;
		for (int Bone = 0; Bone < Parents.Num(); Bone++)
		{
			if (Depths[Bone] == ProcessDepth)
			{
				if (TransformToConvexIndices[Bone].Num() == 0)
				{
					TArray<Chaos::FVec3> JoinedHullPts;
					for (int32 Child : Children[Bone])
					{
						for (int32 ConvexIdx : TransformToConvexIndices[Child])
						{
							JoinedHullPts.Append(Convexes[ConvexIdx]->GetVertices());
						}
					}
					if (JoinedHullPts.Num() > 0)
					{
						int32 ConvexIdx = Convexes.Add(MakeUnique<Chaos::FConvex>(JoinedHullPts, KINDA_SMALL_NUMBER));
						TransformToConvexIndices[Bone].Add(ConvexIdx);
						ToProcess.Add(Bone);
					}
				}
				else
				{
					ToProcess.Add(Bone);
				}
			}
		}

		ensure(ToProcess.Num() > 0);

		// We don't have proximity at the cluster level, so reconstruct from n^2 collision tests for now
		for (int32 BoneA : ToProcess)
		{
			for (int32 BoneB : ToProcess)
			{
				if (BoneB < BoneA)
				{
					if (IsColliding2(OnlyConvex(BoneA), OnlyConvex(BoneB)))
					{
						bool bLeavesCollide = FixLeafCollisions(BoneA, BoneB);
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
								if (IsColliding2(OnlyConvex(OtherBone), OnlyConvex(ToProc)))
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
			if (Depths[Bone] == ProcessDepth && Children[Bone].Num() > 0)
			{
				TArray<Chaos::FVec3> JoinedHullPts;
				for (int32 Child : Children[Bone])
				{
					for (int32 ConvexIdx : TransformToConvexIndices[Child])
					{
						JoinedHullPts.Append(Convexes[ConvexIdx]->GetVertices());
					}
				}
				int32 ConvexIdx = OnlyConvex(Bone);
				*Convexes[ConvexIdx] = Chaos::FConvex(JoinedHullPts, KINDA_SMALL_NUMBER);
				NonLeafVolumes[ConvexIdx] = Convexes[ConvexIdx]->GetVolume();
			}
		}
	}

	auto CutIfOk = [&Convexes, &OnlyConvex, &NonLeafVolumes, &FracAllowRemove](bool bOneSidedCut, int32 ConvexA, int32 ConvexB) -> bool
	{
		Chaos::FReal Depth;
		Chaos::FVec3 CloseA, CloseB, Normal;
		int32 OutIdxA, OutIdxB;
		const Chaos::TRigidTransform<Chaos::FReal, 3> IdentityTransform = Chaos::TRigidTransform<Chaos::FReal, 3>::Identity;
		bool bCollide = Chaos::GJKPenetration(*Convexes[ConvexA], *Convexes[ConvexB], IdentityTransform, Depth, CloseA, CloseB, Normal, OutIdxA, OutIdxB);
		if (bCollide)
		{
			Chaos::FVec3 CutPt = CloseB;
			if (!bOneSidedCut)
			{
				CutPt = (CloseA + CloseB) * .5;
			}
			FChaosPlane CutPlane(CutPt, Normal);

			// Tentatively create the clipped hulls
			Chaos::FConvex CutHullA, CutHullB;
			bool bCreatedA = false, bCreatedB = false;
			TArray<Chaos::FVec3> CutHullPts;
			if (CutHull(*Convexes[ConvexA], CutPlane, true, CutHullPts))
			{
				CutHullA = Chaos::FConvex(CutHullPts, KINDA_SMALL_NUMBER);
				bCreatedA = true;
			}
			if (!bOneSidedCut)
			{
				CutHullPts.Reset();
				if (CutHull(*Convexes[ConvexB], CutPlane, false, CutHullPts))
				{
					CutHullB = Chaos::FConvex(CutHullPts, KINDA_SMALL_NUMBER);
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
			if (WasNotOk.Contains(Bone) || TransformToConvexIndices[Bone].Num() == 0)
			{
				continue;
			}
			for (int32 Nbr : ClusterProximity[Bone])
			{
				if (WasNotOk.Contains(Nbr) || TransformToConvexIndices[Nbr].Num() == 0)
				{
					continue;
				}
				bool bAllOk = true;
				for (int32 ConvexBone : TransformToConvexIndices[Bone])
				{
					for (int32 ConvexNbr : TransformToConvexIndices[Nbr])
					{
						bool bOneSidedCut = Depths[Bone] != Depths[Nbr] || Children[Nbr].Num() == 0;
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
							auto ResetBoneHulls = [&WasNotOk, &TransformToConvexIndices](int32 ToReset)
							{
								WasNotOk.Add(ToReset);
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
	const FGeometryCollection& Geometry,
	TArray<FTransform>& GlobalTransformArray,
	TArray<TUniquePtr<Chaos::FConvex>>& Convexes,
	TArray<TSet<int32>>& TransformToConvexIndices,
	const TManagedArray<int32>& SimulationType,
	int32 RigidType
)
{
	TArray<FVector> GlobalVertices;
	
	GlobalVertices.SetNum(Geometry.Vertex.Num());
	for (int32 Idx = 0; Idx < GlobalVertices.Num(); Idx++)
	{
		GlobalVertices[Idx] = GlobalTransformArray[Geometry.BoneMap[Idx]].TransformPosition(Geometry.Vertex[Idx]);
	}

	int32 NumBones = Geometry.TransformToGeometryIndex.Num();
	TransformToConvexIndices.SetNum(NumBones);
	for (int32 Idx = 0; Idx < NumBones; Idx++)
	{
		if (SimulationType[Idx] != RigidType)
		{
			continue;
		}
		int32 GeomIdx = Geometry.TransformToGeometryIndex[Idx];
		if (GeomIdx != INDEX_NONE)
		{
			int32 VStart = Geometry.VertexStart[GeomIdx];
			int32 VCount = Geometry.VertexCount[GeomIdx];
			int32 VEnd = VStart + VCount;
			TArray<Chaos::FVec3> CutHullPts;
			CutHullPts.Reserve(VCount);
			for (int32 VIdx = VStart; VIdx < VEnd; VIdx++)
			{
				CutHullPts.Add(GlobalVertices[VIdx]);
			}
			ensure(CutHullPts.Num() > 0);
			int32 ConvexIdx = Convexes.Add(MakeUnique<Chaos::FConvex>(CutHullPts, KINDA_SMALL_NUMBER));
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
		TArray<Chaos::FVec3> HullPts;
		for (int32 ConvexIdx : TransformToConvexIndices[Bone])
		{
			HullPts.Reset();
			for (const Chaos::FVec3& P : Convexes[ConvexIdx]->GetVertices())
			{
				HullPts.Add(Transform.InverseTransformPosition(P));
			}
			*Convexes[ConvexIdx] = Chaos::FConvex(HullPts, KINDA_SMALL_NUMBER);
		}
	}
}


}


FGeometryCollectionConvexUtility::FGeometryCollectionConvexData FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(FGeometryCollection* GeometryCollection, double FracAllowRemove)
{
	check(GeometryCollection);

	// TODO: add fallback in case we don't have proximity data
	bool bHasProximity = GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup);
	ensure(bHasProximity);
	
	const TManagedArray<TSet<int32>>& GCProximity = GeometryCollection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
	TArray<TUniquePtr<Chaos::FConvex>> Convexes;
	TArray<TSet<int32>> TransformToConvexIndexArr;
	TArray<FTransform> GlobalTransformArray;
	GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, GlobalTransformArray);
	HullsFromGeometry(*GeometryCollection, GlobalTransformArray, Convexes, TransformToConvexIndexArr, GeometryCollection->SimulationType, FGeometryCollection::ESimulationTypes::FST_Rigid);

	CreateNonoverlappingConvexHulls(Convexes, TransformToConvexIndexArr, GeometryCollection->SimulationType, FGeometryCollection::ESimulationTypes::FST_Rigid, GeometryCollection->Parent, GCProximity, GeometryCollection->TransformIndex, FracAllowRemove);

	TransformHullsToLocal(GlobalTransformArray, Convexes, TransformToConvexIndexArr);

	if (!GeometryCollection->HasGroup("Convex"))
	{
		GeometryCollection->AddGroup("Convex");
	}

	if (!GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup))
	{
		FManagedArrayCollection::FConstructionParameters ConvexDependency("Convex");
		TManagedArray<TSet<int32>>& IndexArray = GeometryCollection->AddAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup, ConvexDependency);
	}

	if (!GeometryCollection->HasAttribute("ConvexHull", "Convex"))
	{
		GeometryCollection->AddAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	}

	TManagedArray<TSet<int32>>& TransformToConvexIndices = GeometryCollection->GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
	TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull = GeometryCollection->GetAttribute<TUniquePtr<Chaos::FConvex>>("ConvexHull", "Convex");
	TransformToConvexIndices = MoveTemp(TransformToConvexIndexArr);
	ConvexHull = MoveTemp(Convexes);

	return { TransformToConvexIndices, ConvexHull };
}



TUniquePtr<Chaos::FConvex> FGeometryCollectionConvexUtility::FindConvexHull(const FGeometryCollection* GeometryCollection, int32 GeometryIndex)
{
	check(GeometryCollection);

	int32 VertexCount = GeometryCollection->VertexCount[GeometryIndex];
	int32 VertexStart = GeometryCollection->VertexStart[GeometryIndex];

	TArray<Chaos::FVec3> Vertices;
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
		TManagedArray<int32>& TransformToConvexIndices = GeometryCollection->GetAttribute<int32>("TransformToConvexIndices", FTransformCollection::TransformGroup);
		TArray<int32> ConvexIndices;
		for (int32 TransformIdx : SortedTransformDeletes)
		{
			if (TransformToConvexIndices[TransformIdx] > INDEX_NONE)
			{
				ConvexIndices.Add(TransformToConvexIndices[TransformIdx]);
				TransformToConvexIndices[TransformIdx] = INDEX_NONE;
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
	if (Group == FTransformCollection::TransformGroup)
	{
		if (GeometryCollection->HasAttribute("TransformToConvexIndices", FTransformCollection::TransformGroup))
		{
			TManagedArray<int32>& TransformToConvexIndices = GeometryCollection->GetAttribute<int32>("TransformToConvexIndices", FTransformCollection::TransformGroup);

			for (uint32 Idx = StartSize; Idx < StartSize + NumElements; ++Idx)
			{
				TransformToConvexIndices[Idx] = INDEX_NONE;
			}
		}
	}
}