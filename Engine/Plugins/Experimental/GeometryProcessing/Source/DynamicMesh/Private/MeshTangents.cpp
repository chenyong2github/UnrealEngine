// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTangents.h"
#include "Async/ParallelFor.h"


template<typename RealType>
void TMeshTangents<RealType>::SetTangentCount(int Count, bool bClearToZero)
{
	if (Tangents.Num() < Count)
	{
		Tangents.SetNum(Count);
	}
	if (Bitangents.Num() < Count)
	{
		Bitangents.SetNum(Count);
	}
	if (bClearToZero)
	{
		for (int i = 0; i < Count; ++i)
		{
			Tangents[i] = FVector3<RealType>::Zero();
			Bitangents[i] = FVector3<RealType>::Zero();
		}
	}
}



template<typename RealType>
void TMeshTangents<RealType>::Internal_ComputePerTriangleTangents(const FDynamicMeshNormalOverlay* NormalOverlay, const FDynamicMeshUVOverlay* UVOverlay)
{
	int MaxTriangleID = Mesh->MaxTriangleID();
	InitializePerTriangleTangents(false);

	ParallelFor(MaxTriangleID, [this, NormalOverlay, UVOverlay](int TriangleID)
	{
		if (Mesh->IsTriangle(TriangleID) == false)
		{
			return;
		}

		FVector3d TriVertices[3];
		Mesh->GetTriVertices(TriangleID, TriVertices[0], TriVertices[1], TriVertices[2]);
		FVector2f TriUVs[3];
		UVOverlay->GetTriElements(TriangleID, TriUVs[0], TriUVs[1], TriUVs[2]);

		for (int j = 0; j < 3; ++j) 
		{
			FVector3d DPosition1 = TriVertices[(j+1)%3] - TriVertices[j];
			FVector3d DPosition2 = TriVertices[(j+2)%3] - TriVertices[j];
			FVector2d DUV1 = (FVector2d)TriUVs[(j+1)%3] - (FVector2d)TriUVs[j];
			FVector2d DUV2 = (FVector2d)TriUVs[(j+2)%3] - (FVector2d)TriUVs[j];

			//@todo handle degenerate edges

			double DetUV = DUV1.Cross(DUV2);
			double InvDetUV = (DetUV == 0.0f) ? 0.0f : 1.0 / DetUV;
			FVector3d Tangent = (DPosition1 * DUV2.Y - DPosition2 * DUV1.Y) * InvDetUV;
			Tangent.Normalize();
			Tangents[3*TriangleID + j] = (FVector3<RealType>)Tangent;
			
			FVector3d Bitangent = (DPosition2 * DUV1.X - DPosition1 * DUV2.X) * InvDetUV;
			Bitangent.Normalize();
			Bitangents[3*TriangleID + j] = (FVector3<RealType>)Bitangent;
		}
	});
}


template class DYNAMICMESH_API TMeshTangents<float>;
template class DYNAMICMESH_API TMeshTangents<double>;




