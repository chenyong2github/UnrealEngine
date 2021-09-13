// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshUVFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Polygroups/PolygroupSet.h"
#include "UDynamicMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshUVFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::SetMeshTriangleUVs(
	UDynamicMesh* TargetMesh,
	int UVSetIndex,
	int TriangleID, 
	FGeometryScriptUVTriangle UVs,
	bool& bIsValidTriangle,
	bool bDeferChangeNotifications)
{
	bIsValidTriangle = false;
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			if (EditMesh.IsTriangle(TriangleID) && EditMesh.HasAttributes() && UVSetIndex < EditMesh.Attributes()->NumUVLayers() )
			{
				FDynamicMeshUVOverlay* UVOverlay = EditMesh.Attributes()->GetUVLayer(UVSetIndex);
				if (UVOverlay != nullptr)
				{
					bIsValidTriangle = true;
					int32 Elem0 = UVOverlay->AppendElement((FVector2f)UVs.UV0);
					int32 Elem1 = UVOverlay->AppendElement((FVector2f)UVs.UV1);
					int32 Elem2 = UVOverlay->AppendElement((FVector2f)UVs.UV2);
					UVOverlay->SetTriangle(TriangleID, FIndex3i(Elem0, Elem1, Elem2), true);
				}
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;	
}





#undef LOCTEXT_NAMESPACE