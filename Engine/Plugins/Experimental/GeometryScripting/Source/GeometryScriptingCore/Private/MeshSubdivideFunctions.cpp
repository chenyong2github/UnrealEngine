// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshSubdivideFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/PNTriangles.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshSubdivideFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyRecursivePNTessellation(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPNTessellateOptions Options,
	int NumIterations,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyPNTessellation_InvalidInput", "ApplyPNTessellation: TargetMesh is Null"));
		return TargetMesh;
	}
	if (NumIterations <= 0)
	{
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FPNTriangles Tessellator(&EditMesh);
		Tessellator.TesselationLevel = NumIterations;
		Tessellator.Compute();

		if (Options.bRecomputeNormals && EditMesh.HasAttributes() && EditMesh.Attributes()->PrimaryNormals() != nullptr)
		{
			FMeshNormals MeshNormals(&EditMesh);
			MeshNormals.RecomputeOverlayNormals(EditMesh.Attributes()->PrimaryNormals(), true, true);
			MeshNormals.CopyToOverlay(EditMesh.Attributes()->PrimaryNormals(), false);
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




#undef LOCTEXT_NAMESPACE
