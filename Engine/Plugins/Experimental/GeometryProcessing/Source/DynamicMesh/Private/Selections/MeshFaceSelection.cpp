// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selections/MeshFaceSelection.h"
#include "Selections/MeshVertexSelection.h"
#include "DynamicMesh3.h"


// convert vertex selection to face selection. Require at least minCount verts of
// tri to be selected (valid values are 1,2,3)
FMeshFaceSelection::FMeshFaceSelection(const FDynamicMesh3* mesh, const FMeshVertexSelection& convertV, int minCount) : Mesh(mesh)
{
	minCount = FMathd::Clamp(minCount, 1, 3);

	if (minCount == 1)
	{
		for ( int vid : convertV )
		{
			for (int tid : Mesh->VtxTrianglesItr(vid))
			{
				add(tid);
			}
		}
	} else {
		for (int tid : Mesh->TriangleIndicesItr()) {
			FIndex3i tri = Mesh->GetTriangle(tid);
			if (minCount == 3)
			{
				if (convertV.IsSelected(tri.A) && convertV.IsSelected(tri.B) && convertV.IsSelected(tri.C))
				{
					add(tid);
				}
			}
			else
			{
				int n = (convertV.IsSelected(tri.A) ? 1 : 0) +
					(convertV.IsSelected(tri.B) ? 1 : 0) +
					(convertV.IsSelected(tri.C) ? 1 : 0);
				if (n >= minCount)
				{
					add(tid);
				}
			}
		}
	}
}