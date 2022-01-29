// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/SceneUtilityFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/MeshNormals.h"

#include "Components/StaticMeshComponent.h"
#include "Components/BrushComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "ConversionUtils/VolumeToDynamicMesh.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_SceneUtilityFunctions"

UDynamicMeshPool* UGeometryScriptLibrary_SceneUtilityFunctions::CreateDynamicMeshPool()
{
	return NewObject<UDynamicMeshPool>();
}


UDynamicMesh* UGeometryScriptLibrary_SceneUtilityFunctions::CopyMeshFromComponent(
	USceneComponent* Component,
	UDynamicMesh* ToDynamicMesh,
	FGeometryScriptCopyMeshFromComponentOptions Options,
	bool bTransformToWorld,
	FTransform& LocalToWorld,
	TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
	UGeometryScriptDebug* Debug)
{
	Outcome = EGeometryScriptOutcomePins::Failure;

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		LocalToWorld = StaticMeshComponent->GetComponentTransform();
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		if (StaticMesh)
		{
			FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
			AssetOptions.bApplyBuildSettings = (Options.bWantNormals || Options.bWantTangents);
			AssetOptions.bRequestTangents = Options.bWantTangents;
			UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(
				StaticMesh, ToDynamicMesh, AssetOptions, Options.RequestedLOD, Outcome, Debug);	// will set Outcome pin
		}
		else
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromComponent_MissingStaticMesh", "CopyMeshFromComponent: StaticMeshComponent has a null StaticMesh"));
		}
	}
	else if (UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Component))
	{
		LocalToWorld = DynamicMeshComponent->GetComponentTransform();
		UDynamicMesh* CopyDynamicMesh = DynamicMeshComponent->GetDynamicMesh();
		if (CopyDynamicMesh)
		{
			CopyDynamicMesh->ProcessMesh([&](const FDynamicMesh3& Mesh)
			{
				ToDynamicMesh->SetMesh(Mesh);
			});
			Outcome = EGeometryScriptOutcomePins::Success;
		}
		else
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromComponent_MissingDynamicMesh", "CopyMeshFromComponent: DynamicMeshComponent has a null DynamicMesh"));
		}
	}
	else if (UBrushComponent* BrushComponent = Cast<UBrushComponent>(Component))
	{
		LocalToWorld = BrushComponent->GetComponentTransform();

		UE::Conversion::FVolumeToMeshOptions VolOptions;
		VolOptions.bMergeVertices = true;
		VolOptions.bAutoRepairMesh = true;
		VolOptions.bOptimizeMesh = true;
		VolOptions.bSetGroups = true;

		FDynamicMesh3 ConvertedMesh(EMeshComponents::FaceGroups);
		UE::Conversion::BrushComponentToDynamicMesh(BrushComponent, ConvertedMesh, VolOptions);

		// compute normals for current polygroup topology
		ConvertedMesh.EnableAttributes();
		if (Options.bWantNormals)
		{
			FDynamicMeshNormalOverlay* Normals = ConvertedMesh.Attributes()->PrimaryNormals();
			FMeshNormals::InitializeOverlayTopologyFromFaceGroups(&ConvertedMesh, Normals);
			FMeshNormals::QuickRecomputeOverlayNormals(ConvertedMesh);
		}

		if (ConvertedMesh.TriangleCount() > 0)
		{
			ToDynamicMesh->SetMesh(MoveTemp(ConvertedMesh));
			Outcome = EGeometryScriptOutcomePins::Success;
		}
		else
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("CopyMeshFromComponent_InvalidBrushConversion", "CopyMeshFromComponent: BrushComponent conversion produced 0 triangles"));
		}
	}

	// transform mesh to world
	if (Outcome == EGeometryScriptOutcomePins::Success && bTransformToWorld)
	{
		ToDynamicMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
		{
			MeshTransforms::ApplyTransform(EditMesh, (FTransformSRT3d)LocalToWorld);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);	
	}

	return ToDynamicMesh;
}



void UGeometryScriptLibrary_SceneUtilityFunctions::SetComponentMaterialList(
	UPrimitiveComponent* Component,
	const TArray<UMaterialInterface*>& MaterialList,
	UGeometryScriptDebug* Debug)
{
	if (Component == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetComponentMaterialList_InvalidInput1", "SetComponentMaterialList: FromStaticMeshAsset is Null"));
		return;
	}

	for (int32 k = 0; k < MaterialList.Num(); ++k)
	{
		Component->SetMaterial(k, MaterialList[k]);
	}
}




#undef LOCTEXT_NAMESPACE