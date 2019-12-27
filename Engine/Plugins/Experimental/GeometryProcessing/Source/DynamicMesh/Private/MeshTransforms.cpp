// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshTransforms.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshOverlay.h"


void MeshTransforms::ApplyTransform(FDynamicMesh3& Mesh, const FTransform3d& Transform)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	for (int vid : Mesh.VertexIndicesItr())
	{
		FVector3d Position = Mesh.GetVertex(vid);
		Position = Transform.TransformPosition(Position);
		Mesh.SetVertex(vid, Position);

		if (bVertexNormals)
		{
			FVector3f Normal = Mesh.GetVertexNormal(vid);
			Normal = (FVector3f)Transform.TransformNormal((FVector3d)Normal);
			Mesh.SetVertexNormal(vid, Normal.Normalized());
		}
	}
	if (Mesh.HasAttributes())
	{
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		for (int elemid : Normals->ElementIndicesItr())
		{
			FVector3f Normal = Normals->GetElement(elemid);
			Normal = (FVector3f)Transform.TransformNormal((FVector3d)Normal);
			Normals->SetElement(elemid, Normal.Normalized());
		}
	}
}



void MeshTransforms::ApplyTransformInverse(FDynamicMesh3& Mesh, const FTransform3d& Transform)
{
	bool bVertexNormals = Mesh.HasVertexNormals();

	for (int vid : Mesh.VertexIndicesItr())
	{
		FVector3d Position = Mesh.GetVertex(vid);
		Position = Transform.InverseTransformPosition(Position);
		Mesh.SetVertex(vid, Position);

		if (bVertexNormals)
		{
			FVector3f Normal = Mesh.GetVertexNormal(vid);
			Normal = (FVector3f)Transform.InverseTransformNormal((FVector3d)Normal);
			Mesh.SetVertexNormal(vid, Normal.Normalized());
		}
	}
	if (Mesh.HasAttributes())
	{
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		for (int elemid : Normals->ElementIndicesItr())
		{
			FVector3f Normal = Normals->GetElement(elemid);
			Normal = (FVector3f)Transform.InverseTransformNormal((FVector3d)Normal);
			Normals->SetElement(elemid, Normal.Normalized());
		}
	}
}