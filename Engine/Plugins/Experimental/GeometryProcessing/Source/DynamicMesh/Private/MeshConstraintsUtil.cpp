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
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PinnedMovable() : FVertexConstraint::Pinned();

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
													 bool bAllowSeamSplits, bool bAllowSeamSmoothing,
													 bool bParallel)
{
	const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();

	FCriticalSection ConstraintSetLock;

	int32 NumEdges = Mesh.MaxEdgeID();
	ParallelFor(NumEdges, [&](int EdgeID)
	{
		bool bIsEdge = Mesh.IsEdge(EdgeID);
		if (Mesh.IsEdge(EdgeID))
		{
			const bool bIsMeshBoundary = Mesh.IsBoundaryEdge(EdgeID);
			const bool bIsGroupBoundary = Mesh.IsGroupBoundaryEdge(EdgeID);
			const bool bIsMaterialBoundary = Attributes && Attributes->IsMaterialBoundaryEdge(EdgeID);
			const bool bIsSeam = Attributes && Attributes->IsSeamEdge(EdgeID);
			FVertexConstraint VtxConstraint = FVertexConstraint::Unconstrained();
			EEdgeRefineFlags EdgeFlags{};
			auto ApplyBoundaryConstraint =
				[&VtxConstraint, &EdgeFlags](uint8 BoundaryConstraint)
				{
					VtxConstraint.Fixed = VtxConstraint.Fixed ||
						(BoundaryConstraint == (uint8)EEdgeRefineFlags::FullyConstrained) ||
						(BoundaryConstraint == (uint8)EEdgeRefineFlags::SplitsOnly);
					VtxConstraint.Movable = VtxConstraint.Movable &&
						(BoundaryConstraint != (uint8)EEdgeRefineFlags::FullyConstrained) &&
						(BoundaryConstraint != (uint8)EEdgeRefineFlags::SplitsOnly);
					EdgeFlags = EEdgeRefineFlags((uint8)EdgeFlags |
												 (uint8)BoundaryConstraint);
				};
			if ( bIsMeshBoundary )
			{
				ApplyBoundaryConstraint((uint8)MeshBoundaryConstraint);
			}
			if ( bIsGroupBoundary )
			{
				ApplyBoundaryConstraint((uint8)GroupBoundaryConstraint);
			}
			if ( bIsMaterialBoundary )
			{
				ApplyBoundaryConstraint((uint8)MaterialBoundaryConstraint);
			}
			if ( bIsSeam )
			{
				VtxConstraint.Movable = VtxConstraint.Movable && bAllowSeamSmoothing;
				VtxConstraint.Fixed = true;
				EdgeFlags = EEdgeRefineFlags((uint8)EdgeFlags |
											 (uint8)(bAllowSeamSplits ?
													 EEdgeRefineFlags::SplitsOnly : EEdgeRefineFlags::FullyConstrained));
			}
			if (bIsMeshBoundary||bIsGroupBoundary||bIsMaterialBoundary||bIsSeam)
			{
				FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);

				ConstraintSetLock.Lock();

				Constraints.SetOrUpdateEdgeConstraint(EdgeID, FEdgeConstraint{ EdgeFlags });

				// If any vertex constraints exist, we can only make them more restrictive!
							
				FVertexConstraint ConstraintA = VtxConstraint;
				ConstraintA.CombineConstraint(Constraints.GetVertexConstraint(EdgeVerts.A));
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, ConstraintA);

				FVertexConstraint ConstraintB = VtxConstraint;
				ConstraintB.CombineConstraint(Constraints.GetVertexConstraint(EdgeVerts.B));
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, ConstraintB);

				ConstraintSetLock.Unlock();
			}
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
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PinnedMovable() : FVertexConstraint::Pinned();

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
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PinnedMovable() : FVertexConstraint::Pinned();

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
