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
	EEdgeRefineFlags SeamEdgeContraint = EEdgeRefineFlags::NoFlip;
	if (!bAllowSeamSplits)
	{
		SeamEdgeContraint = EEdgeRefineFlags((uint8)SeamEdgeContraint | (uint8)EEdgeRefineFlags::NoSplit);
	}
	if (!bAllowSeamCollapse)
	{
		SeamEdgeContraint = EEdgeRefineFlags((uint8)SeamEdgeContraint | (uint8)EEdgeRefineFlags::NoCollapse);
	}

	FCriticalSection ConstraintSetLock;

	int32 NumEdges = Mesh.MaxEdgeID();
	bool bHaveGroups = Mesh.HasTriangleGroups();
	ParallelFor(NumEdges, [&](int EdgeID)
	{
		bool bIsEdge = Mesh.IsEdge(EdgeID);
		if (Mesh.IsEdge(EdgeID))
		{
			const bool bIsMeshBoundary = Mesh.IsBoundaryEdge(EdgeID);
			const bool bIsGroupBoundary = bHaveGroups && Mesh.IsGroupBoundaryEdge(EdgeID);
			const bool bIsMaterialBoundary = Attributes && Attributes->IsMaterialBoundaryEdge(EdgeID);
			const bool bIsSeam = Attributes && Attributes->IsSeamEdge(EdgeID);
			FVertexConstraint VtxConstraint = FVertexConstraint::Unconstrained();
			EEdgeRefineFlags EdgeFlags{};

			auto ApplyBoundaryConstraint = [&VtxConstraint, &EdgeFlags](uint8 BoundaryConstraint)
				{
				VtxConstraint.bCannotDelete = VtxConstraint.bCannotDelete ||
						(BoundaryConstraint == (uint8)EEdgeRefineFlags::FullyConstrained) ||
						(BoundaryConstraint == (uint8)EEdgeRefineFlags::SplitsOnly);
				VtxConstraint.bCanMove = VtxConstraint.bCanMove &&
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

				VtxConstraint.bCanMove = VtxConstraint.bCanMove && (bAllowSeamSmoothing || bAllowSeamCollapse);
				VtxConstraint.bCannotDelete = VtxConstraint.bCannotDelete  || !bAllowSeamCollapse;
				EdgeFlags = EEdgeRefineFlags((uint8)EdgeFlags |
											 (uint8)( SeamEdgeContraint ));

				// Additional logic to add the NoCollapse flag to any edge that is the start or end of a seam.
				if (bAllowSeamCollapse)
				{
					FIndex2i et = Mesh.GetEdgeT(EdgeID);
					// test if two double attribute edge shares one element: call this a seam end
					auto IsSeamWithEnd = [&, et](auto& Overlay)->bool
					{
						if (et.A == -1 || et.B == -1)
						{
							return false;
						}
						bool bASet = Overlay.IsSetTriangle(et.A), bBSet = Overlay.IsSetTriangle(et.B);
						if (!bASet || !bBSet)
						{
							return false;
						}

						TArray<int, TInlineAllocator<6>> UniqueElements;
						FIndex3i Triangle0 = Overlay.GetTriangle(et.A);
						UniqueElements.AddUnique(Triangle0[0]);
						UniqueElements.AddUnique(Triangle0[1]);
						UniqueElements.AddUnique(Triangle0[2]);
						FIndex3i Triangle1 = Overlay.GetTriangle(et.B);
						UniqueElements.AddUnique(Triangle1[0]);
						UniqueElements.AddUnique(Triangle1[1]);
						UniqueElements.AddUnique(Triangle1[2]);

						return UniqueElements.Num() == 5;
					};

					bool bHasSeamEnd = false;
					for (int i = 0; i < Attributes->NumUVLayers(); ++i)
					{
						bool bIsEnd = IsSeamWithEnd(*Attributes->GetUVLayer(i));

						bHasSeamEnd = bHasSeamEnd || bIsEnd;
					}
					bHasSeamEnd = bHasSeamEnd || IsSeamWithEnd(*Attributes->PrimaryNormals());

					if (bHasSeamEnd)
					{
						EdgeFlags = EEdgeRefineFlags((uint8)EdgeFlags | (uint8)EEdgeRefineFlags::NoCollapse);
					}
				}
			}
			if (bIsMeshBoundary||bIsGroupBoundary||bIsMaterialBoundary||bIsSeam)
			{
				FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);

				FEdgeConstraint EdgeConstraint(EdgeFlags);

				// don't update with a phantom constraint (i.e. an unconstrained constraint )
				if (!(EdgeConstraint.IsUnconstrained() && VtxConstraint.IsUnconstrained()))
				{
					ConstraintSetLock.Lock();

					Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);

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
