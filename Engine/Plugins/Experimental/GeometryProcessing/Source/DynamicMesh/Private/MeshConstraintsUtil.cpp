// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshConstraintsUtil.h"
#include "Async/ParallelFor.h"


void FMeshConstraintsUtil::ConstrainAllSeams(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, bool bAllowSplits, bool bAllowSmoothing, bool bParallel)
{
	if (Mesh.HasAttributes() == false)
	{
		return;
	}
	const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();

	FEdgeConstraint EdgeConstraint = (bAllowSplits) ? FEdgeConstraint::SplitsOnly() : FEdgeConstraint::FullyConstrained();
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PermanentMovable() : FVertexConstraint::FullyConstrained();

	FCriticalSection ConstraintSetLock;

	int32 NumEdges = Mesh.MaxEdgeID();
	ParallelFor(NumEdges, [&](int EdgeID)
	{
		if (Mesh.IsEdge(EdgeID))
		{
			if (Attributes->IsSeamEdge(EdgeID))
			{
				FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);

				ConstraintSetLock.Lock();
				Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, VtxConstraint);
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, VtxConstraint);
				ConstraintSetLock.Unlock();
			}
		}
	}, (bParallel==false) );
}

void
FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(FMeshConstraints& Constraints,
													 const FDynamicMesh3& Mesh,
													 EEdgeRefineFlags MeshBoundaryConstraint,
													 EEdgeRefineFlags GroupBoundaryConstraint,
													 EEdgeRefineFlags MaterialBoundaryConstraint,
													 bool bAllowSeamSplits, bool bAllowSeamSmoothing, bool bAllowSeamCollapse, 
													 bool bParallel)
{
	const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();

	// Seam edge can never flip, it is never fully unconstrained 
	EEdgeRefineFlags SeamEdgeConstraint = EEdgeRefineFlags::NoFlip;
	if (!bAllowSeamSplits)
	{
		SeamEdgeConstraint = EEdgeRefineFlags((int)SeamEdgeConstraint | (int)EEdgeRefineFlags::NoSplit);
	}
	if (!bAllowSeamCollapse)
	{
		SeamEdgeConstraint = EEdgeRefineFlags((int)SeamEdgeConstraint | (int)EEdgeRefineFlags::NoCollapse);
	}

	FCriticalSection ConstraintSetLock;

	int32 NumEdges = Mesh.MaxEdgeID();
	bool bHaveGroups = Mesh.HasTriangleGroups();


	ParallelFor(NumEdges, [&](int EdgeID)
	{
		FVertexConstraint VtxConstraintA = FVertexConstraint::Unconstrained();
		FVertexConstraint VtxConstraintB = FVertexConstraint::Unconstrained();

		FEdgeConstraint EdgeConstraint(EEdgeRefineFlags::NoConstraint);

		// compute the edge and vertex constraints.
		bool bHasUpdate = ConstrainEdgeBoundariesAndSeams(
		        EdgeID,
				Mesh,
				MeshBoundaryConstraint,
				GroupBoundaryConstraint,
				MaterialBoundaryConstraint,
				SeamEdgeConstraint,
				bAllowSeamSmoothing,
				EdgeConstraint, VtxConstraintA, VtxConstraintB);
		
		if (bHasUpdate)
		{
			// have updates - merge with existing constraints
			ConstraintSetLock.Lock();
			
			FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);
			
			Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);

			VtxConstraintA.CombineConstraint(Constraints.GetVertexConstraint(EdgeVerts.A));
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, VtxConstraintA);

			VtxConstraintB.CombineConstraint(Constraints.GetVertexConstraint(EdgeVerts.B));
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, VtxConstraintB);

			ConstraintSetLock.Unlock();
		}
	}, (bParallel == false) );
}

void FMeshConstraintsUtil::ConstrainSeamsInEdgeROI(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, const TArray<int>& EdgeROI, bool bAllowSplits, bool bAllowSmoothing, bool bParallel)
{
	if (Mesh.HasAttributes() == false)
	{
		return;
	}
	const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();

	FEdgeConstraint EdgeConstraint = (bAllowSplits) ? FEdgeConstraint::SplitsOnly() : FEdgeConstraint::FullyConstrained();
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PermanentMovable() : FVertexConstraint::FullyConstrained();

	FCriticalSection ConstraintSetLock;

	int32 NumEdges = EdgeROI.Num();
	ParallelFor(NumEdges, [&](int k)
	{
		int EdgeID = EdgeROI[k];
		FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);

		if (Attributes->IsSeamEdge(EdgeID))
		{
			ConstraintSetLock.Lock();
			Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, VtxConstraint);
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, VtxConstraint);
			ConstraintSetLock.Unlock();
		}
		else
		{
			// Constrain edge end points if they belong to seams.
			// NOTE: It is possible that one (or both) of these vertices belongs to a seam edge that is not in EdgeROI. 
			// In such a case, we still want to constrain that vertex.
			for (int VertexID : {EdgeVerts[0], EdgeVerts[1]})
			{
				if (Attributes->IsSeamVertex(VertexID, true))
				{
					ConstraintSetLock.Lock();
					Constraints.SetOrUpdateVertexConstraint(VertexID, VtxConstraint);
					ConstraintSetLock.Unlock();
				}
			}
		}

	}, (bParallel==false) );
}

void FMeshConstraintsUtil::ConstrainROIBoundariesInEdgeROI(FMeshConstraints& Constraints,
	const FDynamicMesh3& Mesh,
	const TSet<int>& EdgeROI,
	const TSet<int>& TriangleROI,
	bool bAllowSplits,
	bool bAllowSmoothing)
{
	FEdgeConstraint EdgeConstraint = (bAllowSplits) ? FEdgeConstraint::SplitsOnly() : FEdgeConstraint::FullyConstrained();
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PermanentMovable() : FVertexConstraint::FullyConstrained();

	for (int EdgeID : EdgeROI)
	{
		FIndex2i EdgeTris = Mesh.GetEdgeT(EdgeID);
		bool bIsROIBoundary = (TriangleROI.Contains(EdgeTris.A) != TriangleROI.Contains(EdgeTris.B));
		if (bIsROIBoundary)
		{
			FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);
			Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, VtxConstraint);
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, VtxConstraint);
		}
	}
}

bool FMeshConstraintsUtil::ConstrainEdgeBoundariesAndSeams(const int EdgeID,
	                                                       const FDynamicMesh3& Mesh,
	                                                       const EEdgeRefineFlags MeshBoundaryConstraintFlags,
	                                                       const EEdgeRefineFlags GroupBoundaryConstraintFlags,
	                                                       const EEdgeRefineFlags MaterialBoundaryConstraintFlags,
	                                                       const EEdgeRefineFlags SeamEdgeConstraintFlags,
	                                                       const bool bAllowSeamSmoothing,
	                                                       FEdgeConstraint& EdgeConstraint,
	                                                       FVertexConstraint& VertexConstraintA,
	                                                       FVertexConstraint& VertexConstraintB)
{
	const bool bAllowSeamCollapse = FEdgeConstraint::CanCollapse(SeamEdgeConstraintFlags);

	// initialize constraints
	VertexConstraintA = FVertexConstraint::Unconstrained();
	VertexConstraintB = FVertexConstraint::Unconstrained();
	EdgeConstraint = FEdgeConstraint::Unconstrained();

	const bool bIsEdge = Mesh.IsEdge(EdgeID);
	if (!bIsEdge)  return false;

	const bool bHaveGroups = Mesh.HasTriangleGroups();
	const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();

	const bool bIsMeshBoundary = Mesh.IsBoundaryEdge(EdgeID);
	const bool bIsGroupBoundary = bHaveGroups && Mesh.IsGroupBoundaryEdge(EdgeID);
	const bool bIsMaterialBoundary = Attributes && Attributes->IsMaterialBoundaryEdge(EdgeID);
	const bool bIsSeam = Attributes && Attributes->IsSeamEdge(EdgeID);

	FVertexConstraint CurVtxConstraint = FVertexConstraint::Unconstrained(); // note: this is needed since the default constructor is constrained.
	EEdgeRefineFlags EdgeFlags{};

	auto ApplyBoundaryConstraint = [&CurVtxConstraint, &EdgeFlags](EEdgeRefineFlags BoundaryConstraintFlags)
	{

		CurVtxConstraint.bCannotDelete = CurVtxConstraint.bCannotDelete ||
			(!FEdgeConstraint::CanCollapse(BoundaryConstraintFlags) &&
				!FEdgeConstraint::CanFlip(BoundaryConstraintFlags)
				);

		CurVtxConstraint.bCanMove = CurVtxConstraint.bCanMove &&
			(FEdgeConstraint::CanCollapse(BoundaryConstraintFlags) ||
				FEdgeConstraint::CanFlip(BoundaryConstraintFlags)
				);

		EdgeFlags = EEdgeRefineFlags((int)EdgeFlags | (int)BoundaryConstraintFlags);
	};


	if (bIsMeshBoundary)
	{
		ApplyBoundaryConstraint(MeshBoundaryConstraintFlags);
	}
	if (bIsGroupBoundary)
	{
		ApplyBoundaryConstraint(GroupBoundaryConstraintFlags);
	}
	if (bIsMaterialBoundary)
	{
		ApplyBoundaryConstraint(MaterialBoundaryConstraintFlags);
	}
	if (bIsSeam)
	{
		CurVtxConstraint.bCannotDelete = CurVtxConstraint.bCannotDelete || !bAllowSeamCollapse;
		CurVtxConstraint.bCanMove = CurVtxConstraint.bCanMove && (bAllowSeamSmoothing || bAllowSeamCollapse);

		EdgeFlags = EEdgeRefineFlags((int)EdgeFlags | (int)(SeamEdgeConstraintFlags));

		// Additional logic to add the NoCollapse flag to any edge that is the start or end of a seam.
		if (bAllowSeamCollapse)
		{

			bool bHasSeamEnd = false;
			for (int32 i = 0; !bHasSeamEnd && (i < Attributes->NumUVLayers()); ++i)
			{
				const FDynamicMeshUVOverlay* UVLayer = Attributes->GetUVLayer(i);
				bHasSeamEnd = bHasSeamEnd || UVLayer->IsSeamEndEdge(EdgeID);
			}
			for (int32 i = 0; !bHasSeamEnd && (i < Attributes->NumNormalLayers()); ++i)
			{
				const FDynamicMeshNormalOverlay* NormalOverlay = Attributes->GetNormalLayer(i);
				bHasSeamEnd = bHasSeamEnd || NormalOverlay->IsSeamEndEdge(EdgeID);
			}

			if (bHasSeamEnd)
			{
				EdgeFlags = EEdgeRefineFlags((int)EdgeFlags | (int)EEdgeRefineFlags::NoCollapse);
			}
		}
	}
	if (bIsMeshBoundary || bIsGroupBoundary || bIsMaterialBoundary || bIsSeam)
	{
		EdgeConstraint = FEdgeConstraint(EdgeFlags);

		// only return true if we have a constraint
		if (!EdgeConstraint.IsUnconstrained() || !CurVtxConstraint.IsUnconstrained())
		{
			VertexConstraintA.CombineConstraint(CurVtxConstraint);
			VertexConstraintB.CombineConstraint(CurVtxConstraint);
			return  true;
		}
	}
	return false;
}