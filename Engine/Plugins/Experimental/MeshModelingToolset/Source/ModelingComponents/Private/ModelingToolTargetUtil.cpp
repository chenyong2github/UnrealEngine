// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolTargetUtil.h"

#include "ToolTargets/ToolTarget.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#include "ModelingObjectsCreationAPI.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Volume.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

using namespace UE::Geometry;

AActor* UE::ToolTarget::GetTargetActor(UToolTarget* Target)
{
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	if (TargetComponent)
	{
		return TargetComponent->GetOwnerActor();
	}
	ensure(false);
	return nullptr;
}

UPrimitiveComponent* UE::ToolTarget::GetTargetComponent(UToolTarget* Target)
{
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	if (TargetComponent)
	{
		return TargetComponent->GetOwnerComponent();
	}
	ensure(false);
	return nullptr;
}

bool UE::ToolTarget::HideSourceObject(UToolTarget* Target)
{
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	if (TargetComponent)
	{
		TargetComponent->SetOwnerVisibility(false);
		return true;
	}
	ensure(false);
	return false;
}

bool UE::ToolTarget::ShowSourceObject(UToolTarget* Target)
{
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	if (TargetComponent)
	{
		TargetComponent->SetOwnerVisibility(true);
		return true;
	}
	ensure(false);
	return false;
}


FTransform3d UE::ToolTarget::GetLocalToWorldTransform(UToolTarget* Target)
{
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	if (TargetComponent)
	{
		return (FTransform3d)TargetComponent->GetWorldTransform();
	}
	ensure(false);
	return FTransform3d();
}

FComponentMaterialSet UE::ToolTarget::GetMaterialSet(UToolTarget* Target, bool bPreferAssetMaterials)
{
	FComponentMaterialSet MaterialSet;
	IMaterialProvider* MaterialProvider = Cast<IMaterialProvider>(Target);
	if (ensure(MaterialProvider))
	{
		MaterialProvider->GetMaterialSet(MaterialSet, bPreferAssetMaterials);
	}
	return MaterialSet;
}


const FMeshDescription* UE::ToolTarget::GetMeshDescription(UToolTarget* Target)
{
	static FMeshDescription EmptyMeshDescription;

	IMeshDescriptionProvider* MeshDescriptionProvider = Cast<IMeshDescriptionProvider>(Target);
	if (MeshDescriptionProvider)
	{
		return MeshDescriptionProvider->GetMeshDescription();
	}
	ensure(false);
	return &EmptyMeshDescription;
}


FDynamicMesh3 UE::ToolTarget::GetDynamicMeshCopy(UToolTarget* Target)
{
	IMeshDescriptionProvider* MeshDescriptionProvider = Cast<IMeshDescriptionProvider>(Target);
	FDynamicMesh3 Mesh(EMeshComponents::FaceGroups);
	Mesh.EnableAttributes();
	if (MeshDescriptionProvider)
	{
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(MeshDescriptionProvider->GetMeshDescription(), Mesh);
		return Mesh;
	}

	ensure(false);
	return Mesh;
}



UE::ToolTarget::EDynamicMeshUpdateResult UE::ToolTarget::CommitDynamicMeshUVUpdate(UToolTarget* Target, const UE::Geometry::FDynamicMesh3* UpdatedMesh)
{
	IMeshDescriptionCommitter* MeshDescriptionCommitter = Cast<IMeshDescriptionCommitter>(Target);

	EDynamicMeshUpdateResult Result = EDynamicMeshUpdateResult::Failed;
	MeshDescriptionCommitter->CommitMeshDescription([UpdatedMesh, &Result](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
	{
		FMeshDescription* MeshDescription = CommitParams.MeshDescriptionOut;

		bool bVerticesOnly = false;
		bool bAttributesOnly = true;
		if (FDynamicMeshToMeshDescription::HaveMatchingElementCounts(UpdatedMesh, MeshDescription, bVerticesOnly, bAttributesOnly))
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.UpdateAttributes(UpdatedMesh, *MeshDescription, false, false, true/*update uvs*/);
			Result = EDynamicMeshUpdateResult::Ok;
		}
		else
		{
			// must have been duplicate tris in the mesh description; we can't count on 1-to-1 mapping of TriangleIDs.  Just convert 
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(UpdatedMesh, *MeshDescription);
			Result = EDynamicMeshUpdateResult::Ok_ForcedFullUpdate;
		}
	});
	return Result;
}



bool UE::ToolTarget::ConfigureCreateMeshObjectParams(UToolTarget* SourceTarget, FCreateMeshObjectParams& DerivedParamsOut)
{
	IPrimitiveComponentBackedTarget* ComponentTarget = Cast<IPrimitiveComponentBackedTarget>(SourceTarget);
	if (ComponentTarget)
	{
		if (Cast<UStaticMeshComponent>(ComponentTarget->GetOwnerComponent()) != nullptr)
		{
			DerivedParamsOut.TypeHint = ECreateObjectTypeHint::StaticMesh;
			return true;
		}

		AVolume* VolumeActor = Cast<AVolume>(ComponentTarget->GetOwnerActor());
		if (VolumeActor != nullptr)
		{
			DerivedParamsOut.TypeHint = ECreateObjectTypeHint::Volume;
			DerivedParamsOut.TypeHintClass = VolumeActor->GetClass();
			return true;
		}
	}
	return false;
}