// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/PolyEditPreviewMesh.h"
#include "DynamicSubmesh3.h"
#include "MeshTransforms.h"
#include "Operations/ExtrudeMesh.h"
#include "Operations/InsetMeshRegion.h"
#include "Selections/MeshVertexSelection.h"
#include "MeshBoundaryLoops.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"



void UPolyEditPreviewMesh::InitializeExtrudeType(
	const FDynamicMesh3* SourceMesh, const TArray<int32>& Triangles,
	const FVector3d& TransformedOffsetDirection,
	const FTransform3d* MeshTransformIn,
	bool bDeleteExtrudeBaseFaces)
{
	// extract submesh
	ActiveSubmesh = MakeUnique<FDynamicSubmesh3>(SourceMesh, Triangles, (int32)(EMeshComponents::FaceGroups) | (int32)(EMeshComponents::VertexNormals), true);
	FDynamicMesh3& EditPatch = ActiveSubmesh->GetSubmesh();

	check(EditPatch.IsCompact());

	// do we want to apply a transform?
	bHaveMeshTransform = (MeshTransformIn != nullptr);
	if (bHaveMeshTransform)
	{
		MeshTransform = *MeshTransformIn;
		MeshTransforms::ApplyTransform(EditPatch, MeshTransform);
	}
	//FMeshNormals::QuickComputeVertexNormals(EditPatch);

	// save copy of initial patch
	InitialEditPatch = EditPatch;
	InitialEditPatchBVTree.SetMesh(&InitialEditPatch);

	// extrude initial patch by a tiny amount so that we get normals
	FExtrudeMesh Extruder(&EditPatch);
	Extruder.DefaultExtrudeDistance = 0.01;
	Extruder.Apply();

	// get set of extrude vertices
	EditVertices.Reset();
	FMeshVertexSelection Vertices(&EditPatch);
	for (const FExtrudeMesh::FExtrusionInfo& Extrusion : Extruder.Extrusions)
	{
		Vertices.SelectTriangleVertices(Extrusion.OffsetTriangles);
	}
	EditVertices = Vertices.AsArray();

	// save initial extrude positions
	InitialPositions.Reset();
	InitialNormals.Reset();
	for (int32 vid : EditVertices)
	{
		InitialPositions.Add(EditPatch.GetVertex(vid));
		InitialNormals.Add((FVector3d)EditPatch.GetVertexNormal(vid));
	}

	if (bDeleteExtrudeBaseFaces)
	{
		FDynamicMeshEditor Editor(&EditPatch);
		for (const FExtrudeMesh::FExtrusionInfo& Extrusion : Extruder.Extrusions)
		{
			Editor.RemoveTriangles(Extrusion.InitialTriangles, false);
		}
	}

	InputDirection = TransformedOffsetDirection;

	// initialize the preview mesh
	UpdatePreview(&EditPatch);
}

void UPolyEditPreviewMesh::InitializeExtrudeType(FDynamicMesh3&& BaseMesh,
	const FVector3d& TransformedOffsetDirection,
	const FTransform3d* MeshTransformIn,
	bool bDeleteExtrudeBaseFaces)
{
	InitialEditPatch = MoveTemp(BaseMesh);

	// do we want to apply a transform?
	bHaveMeshTransform = (MeshTransformIn != nullptr);
	if (bHaveMeshTransform)
	{
		MeshTransform = *MeshTransformIn;
		MeshTransforms::ApplyTransform(InitialEditPatch, MeshTransform);
	}

	// save copy of initial patch
	InitialEditPatchBVTree.SetMesh(&InitialEditPatch);

	// extrude initial patch by a tiny amount so that we get normals
	FDynamicMesh3 EditPatch(InitialEditPatch);
	FExtrudeMesh Extruder(&EditPatch);
	Extruder.DefaultExtrudeDistance = 0.01;
	Extruder.Apply();

	// get set of extrude vertices
	EditVertices.Reset();
	FMeshVertexSelection Vertices(&EditPatch);
	for (const FExtrudeMesh::FExtrusionInfo& Extrusion : Extruder.Extrusions)
	{
		Vertices.SelectTriangleVertices(Extrusion.OffsetTriangles);
	}
	EditVertices = Vertices.AsArray();

	// save initial extrude positions
	InitialPositions.Reset();
	InitialNormals.Reset();
	for (int32 vid : EditVertices)
	{
		InitialPositions.Add(EditPatch.GetVertex(vid));
		InitialNormals.Add((FVector3d)EditPatch.GetVertexNormal(vid));
	}

	if (bDeleteExtrudeBaseFaces)
	{
		FDynamicMeshEditor Editor(&EditPatch);
		for (const FExtrudeMesh::FExtrusionInfo& Extrusion : Extruder.Extrusions)
		{
			Editor.RemoveTriangles(Extrusion.InitialTriangles, false);
		}
	}

	InputDirection = TransformedOffsetDirection;

	// initialize the preview mesh
	UpdatePreview(&EditPatch);
}




void UPolyEditPreviewMesh::UpdateExtrudeType(double NewOffset, bool bUseNormalDirection)
{
	EditMesh([&](FDynamicMesh3& Mesh)
	{
		int32 NumVertices = EditVertices.Num();
		for (int32 k = 0; k < NumVertices; ++k)
		{
			int vid = EditVertices[k];
			FVector3d InitialPos = InitialPositions[k];
			FVector3d NewPos = InitialPos + NewOffset * (bUseNormalDirection ? InitialNormals[k] : InputDirection);
			Mesh.SetVertex(vid, NewPos);
		}
	});
}


void UPolyEditPreviewMesh::UpdateExtrudeType(TFunctionRef<void(FDynamicMesh3&)> UpdateMeshFunc, bool bFullRecalculate)
{
	if (bFullRecalculate)
	{
		FDynamicMesh3 TempMesh(InitialEditPatch);
		UpdateMeshFunc(TempMesh);
		ReplaceMesh(MoveTemp(TempMesh));
	}
	else
	{
		EditMesh(UpdateMeshFunc);
	}
}


void UPolyEditPreviewMesh::MakeExtrudeTypeHitTargetMesh(FDynamicMesh3& TargetMesh, bool bUseNormalDirection)
{
	FVector3d ExtrudeDirection = InputDirection;
	double Length = 99999.0;

	TargetMesh = InitialEditPatch;
	MeshTransforms::Translate(TargetMesh, -Length * ExtrudeDirection);

	FExtrudeMesh Extruder(&TargetMesh);
	Extruder.ExtrudedPositionFunc = [&](const FVector3d& Position, const FVector3f& Normal, int VertexID)
	{
		return Position + 2.0 * Length * (bUseNormalDirection ? (FVector3d)Normal : ExtrudeDirection);
	};
	Extruder.Apply();
}



void UPolyEditPreviewMesh::InitializeInsetType(const FDynamicMesh3* SourceMesh, const TArray<int32>& Triangles,
	const FTransform3d* MeshTransformIn)
{
	// extract submesh
	ActiveSubmesh = MakeUnique<FDynamicSubmesh3>(SourceMesh, Triangles, (int32)EMeshComponents::FaceGroups, true);
	FDynamicMesh3& EditPatch = ActiveSubmesh->GetSubmesh();

	check(EditPatch.IsCompact());

	// do we want to apply a transform?
	bHaveMeshTransform = (MeshTransformIn != nullptr);
	if (bHaveMeshTransform)
	{
		MeshTransform = *MeshTransformIn;
		MeshTransforms::ApplyTransform(EditPatch, MeshTransform);
	}

	// save copy of initial patch
	InitialEditPatch = EditPatch;
	InitialEditPatchBVTree.SetMesh(&InitialEditPatch);

	// initialize the preview mesh
	UpdatePreview(&EditPatch);
}


void UPolyEditPreviewMesh::UpdateInsetType(double NewOffset)
{
	FDynamicMesh3 EditPatch(InitialEditPatch);
	FInsetMeshRegion Inset(&EditPatch);
	for (int32 tid : EditPatch.TriangleIndicesItr())
	{
		Inset.Triangles.Add(tid);
	}
	Inset.InsetDistance = NewOffset;
	Inset.Apply();

	FMeshNormals::QuickRecomputeOverlayNormals(EditPatch);

	UpdatePreview(&EditPatch);
}


void UPolyEditPreviewMesh::MakeInsetTypeTargetMesh(FDynamicMesh3& TargetMesh)
{
	TargetMesh = InitialEditPatch;
}





void UPolyEditPreviewMesh::InitializeStaticType(const FDynamicMesh3* SourceMesh, const TArray<int32>& Triangles,
	const FTransform3d* MeshTransformIn)
{
	// extract submesh
	ActiveSubmesh = MakeUnique<FDynamicSubmesh3>(SourceMesh, Triangles, (int32)EMeshComponents::FaceGroups, true);
	FDynamicMesh3& EditPatch = ActiveSubmesh->GetSubmesh();

	check(EditPatch.IsCompact());

	// do we want to apply a transform?
	bHaveMeshTransform = (MeshTransformIn != nullptr);
	if (bHaveMeshTransform)
	{
		MeshTransform = *MeshTransformIn;
		MeshTransforms::ApplyTransform(EditPatch, MeshTransform);
	}

	// save copy of initial patch
	InitialEditPatch = EditPatch;
	InitialEditPatchBVTree.SetMesh(&InitialEditPatch);

	// initialize the preview mesh
	UpdatePreview(&EditPatch);
}


void UPolyEditPreviewMesh::UpdateStaticType(TFunctionRef<void(FDynamicMesh3&)> UpdateMeshFunc, bool bFullRecalculate)
{
	if (bFullRecalculate)
	{
		FDynamicMesh3 TempMesh(InitialEditPatch);
		UpdateMeshFunc(TempMesh);
		ReplaceMesh(MoveTemp(TempMesh));
	}
	else
	{
		EditMesh(UpdateMeshFunc);
	}
}

void UPolyEditPreviewMesh::MakeStaticTypeTargetMesh(FDynamicMesh3& TargetMesh)
{
	TargetMesh = InitialEditPatch;
}