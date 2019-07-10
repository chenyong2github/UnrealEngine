// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshConstraintsUtil.h"


void FMeshConstraintsUtil::ConstrainAllSeams(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, bool bAllowSplits, bool bAllowSmoothing)
{
	if (Mesh.HasAttributes() == false)
	{
		return;
	}
	const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();

	FEdgeConstraint EdgeConstraint = (bAllowSplits) ? FEdgeConstraint::SplitsOnly() : FEdgeConstraint::FullyConstrained();
	FVertexConstraint VtxConstraint = (bAllowSmoothing) ? FVertexConstraint::PinnedMovable() : FVertexConstraint::Pinned();

	for (int EdgeID : Mesh.EdgeIndicesItr())
	{
		if (Attributes->IsSeamEdge(EdgeID))
		{
			Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);
			FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, VtxConstraint);
			Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, VtxConstraint);
		}
	}
}