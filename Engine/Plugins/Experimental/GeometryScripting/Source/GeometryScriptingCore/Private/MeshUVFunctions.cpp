// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshUVFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Polygroups/PolygroupSet.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "Selections/MeshConnectedComponents.h"
#include "Async/ParallelFor.h"
#include "UDynamicMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshUVFunctions"



UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::SetNumUVSets(
	UDynamicMesh* TargetMesh,
	int NumUVSets,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetNumUVSets_InvalidInput", "SetNumUVSets: TargetMesh is Null"));
		return TargetMesh;
	}
	if (NumUVSets > 8)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetNumUVSets_InvalidNumUVSets", "SetNumUVSets: Maximum of 8 UV Sets are supported"));
		return TargetMesh;
	}
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
	{
		if (EditMesh.HasAttributes() == false)
		{
			EditMesh.EnableAttributes();
		}
		if (NumUVSets != EditMesh.Attributes()->NumUVLayers())
		{
			EditMesh.Attributes()->SetNumUVLayers(NumUVSets);
		}
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}



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





void ApplyMeshUVEditorOperation(UDynamicMesh* TargetMesh, int32 UVSetIndex, bool& bHasUVSet, UGeometryScriptDebug* Debug,
	TFunctionRef<void(FDynamicMesh3& Mesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)> EditFunc)
{
	bHasUVSet = false;
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
	{
		if (EditMesh.HasAttributes() == false
			|| UVSetIndex >= EditMesh.Attributes()->NumUVLayers()
			|| EditMesh.Attributes()->GetUVLayer(UVSetIndex) == nullptr)
		{
			return;
		}

		bHasUVSet = true;
		FDynamicMeshUVOverlay* UVOverlay = EditMesh.Attributes()->GetUVLayer(UVSetIndex);
		FDynamicMeshUVEditor Editor(&EditMesh, UVOverlay);
		EditFunc(EditMesh, UVOverlay, Editor);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
}




UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::TranslateMeshUVs(
	UDynamicMesh* TargetMesh,
	int UVSetIndex,
	FVector2D Translation,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("TranslateMeshUVs_InvalidInput", "TranslateMeshUVs: TargetMesh is Null"));
		return TargetMesh;
	}

	bool bHasUVSet = false;
	ApplyMeshUVEditorOperation(TargetMesh, UVSetIndex, bHasUVSet, Debug,
		[&](FDynamicMesh3& EditMesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)
	{
		for (int32 elemid : UVOverlay->ElementIndicesItr())
		{
			FVector2f UV = UVOverlay->GetElement(elemid);
			UVOverlay->SetElement(elemid, UV + (FVector2f)Translation);
		}
	});
	if (bHasUVSet == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("TranslateMeshUVs_InvalidUVSet", "TranslateMeshUVs: UVSetIndex does not exist on TargetMesh"));
	}

	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::ScaleMeshUVs(
	UDynamicMesh* TargetMesh,
	int UVSetIndex,
	FVector2D Scale,
	FVector2D ScaleOrigin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ScaleMeshUVs_InvalidInput", "ScaleMeshUVs: TargetMesh is Null"));
		return TargetMesh;
	}

	FVector2f UseScale = Scale;
	if (UseScale.Length() < 0.0001)
	{
		UseScale = FVector2f::One();
	}
	FVector2f UseOrigin = (FVector2f)ScaleOrigin;

	bool bHasUVSet = false;
	ApplyMeshUVEditorOperation(TargetMesh, UVSetIndex, bHasUVSet, Debug,
		[&](FDynamicMesh3& EditMesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)
	{
		for (int32 elemid : UVOverlay->ElementIndicesItr())
		{
			FVector2f UV = UVOverlay->GetElement(elemid);
			UV = (UV - UseOrigin) * UseScale + UseOrigin;
			UVOverlay->SetElement(elemid, UV);
		}
	});
	if (bHasUVSet == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ScaleMeshUVs_InvalidUVSet", "ScaleMeshUVs: UVSetIndex does not exist on TargetMesh"));
	}

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::RotateMeshUVs(
	UDynamicMesh* TargetMesh,
	int UVSetIndex,
	float RotationAngle,
	FVector2D RotationOrigin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("RotateMeshUVs_InvalidInput", "RotateMeshUVs: TargetMesh is Null"));
		return TargetMesh;
	}

	FMatrix2f RotationMat = FMatrix2f::RotationDeg(RotationAngle);
	FVector2f UseOrigin = (FVector2f)RotationOrigin;

	bool bHasUVSet = false;
	ApplyMeshUVEditorOperation(TargetMesh, UVSetIndex, bHasUVSet, Debug,
		[&](FDynamicMesh3& EditMesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)
	{
		for (int32 elemid : UVOverlay->ElementIndicesItr())
		{
			FVector2f UV = UVOverlay->GetElement(elemid);
			UV = RotationMat* (UV - UseOrigin) + UseOrigin;
			UVOverlay->SetElement(elemid, UV);
		}
	});
	if (bHasUVSet == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("RotateMeshUVs_InvalidUVSet", "RotateMeshUVs: UVSetIndex does not exist on TargetMesh"));
	}

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromPlanarProjection(
	UDynamicMesh* TargetMesh,
	int UVSetIndex,
	FTransform PlaneTransform,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshUVsFromPlanarProjection_InvalidInput", "SetMeshUVsFromPlanarProjection: TargetMesh is Null"));
		return TargetMesh;
	}

	bool bHasUVSet = false;
	ApplyMeshUVEditorOperation(TargetMesh, UVSetIndex, bHasUVSet, Debug,
		[&](FDynamicMesh3& EditMesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)
	{
		TArray<int32> AllTriangles;
		for (int32 tid : EditMesh.TriangleIndicesItr())
		{
			AllTriangles.Add(tid);
		}

		FFrame3d ProjectionFrame(PlaneTransform);
		FVector Scale = PlaneTransform.GetScale3D();
		FVector2d Dimensions(Scale.X, Scale.Y);

		UVEditor.SetTriangleUVsFromPlanarProjection(AllTriangles, [&](const FVector3d& Pos) { return Pos; },
			ProjectionFrame, Dimensions);
	});
	if (bHasUVSet == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshUVsFromPlanarProjection_InvalidUVSet", "SetMeshUVsFromPlanarProjection: UVSetIndex does not exist on TargetMesh"));
	}

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromBoxProjection(
	UDynamicMesh* TargetMesh,
	int UVSetIndex,
	FTransform PlaneTransform,
	int MinIslandTriCount,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshUVsFromBoxProjection_InvalidInput", "SetMeshUVsFromBoxProjection: TargetMesh is Null"));
		return TargetMesh;
	}

	bool bHasUVSet = false;
	ApplyMeshUVEditorOperation(TargetMesh, UVSetIndex, bHasUVSet, Debug,
		[&](FDynamicMesh3& EditMesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)
	{
		TArray<int32> AllTriangles;
		for (int32 tid : EditMesh.TriangleIndicesItr())
		{
			AllTriangles.Add(tid);
		}

		FFrame3d ProjectionFrame(PlaneTransform);
		FVector Scale = PlaneTransform.GetScale3D();
		FVector3d Dimensions = (FVector)Scale;
		UVEditor.SetTriangleUVsFromBoxProjection(AllTriangles, [&](const FVector3d& Pos) { return Pos; },
			ProjectionFrame, Dimensions, MinIslandTriCount);
	});
	if (bHasUVSet == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshUVsFromBoxProjection_InvalidUVSet", "SetMeshUVsFromBoxProjection: UVSetIndex does not exist on TargetMesh"));
	}

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshUVFunctions::RepackMeshUVs( 
	UDynamicMesh* TargetMesh, 
	int UVSetIndex,
	FGeometryScriptRepackUVsOptions RepackOptions,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("RepackMeshUVs_InvalidInput", "RepackMeshUVs: TargetMesh is Null"));
		return TargetMesh;
	}

	bool bHasUVSet = false;
	ApplyMeshUVEditorOperation(TargetMesh, UVSetIndex, bHasUVSet, Debug,
		[&](FDynamicMesh3& EditMesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshUVEditor& UVEditor)
	{
		if (RepackOptions.bOptimizeIslandRotation)
		{
			FMeshConnectedComponents UVComponents(&EditMesh);
			UVComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
				return UVOverlay->AreTrianglesConnected(Triangle0, Triangle1);
			});

			ParallelFor(UVComponents.Num(), [&](int32 k)
			{
				UVEditor.AutoOrientUVArea(UVComponents[k].Indices);
			});
		}

		UVEditor.QuickPack(FMath::Max(16, RepackOptions.TargetImageWidth));
	});
	if (bHasUVSet == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("RepackMeshUVs_InvalidUVSet", "RepackMeshUVs: UVSetIndex does not exist on TargetMesh"));
	}

	return TargetMesh;
}



#undef LOCTEXT_NAMESPACE