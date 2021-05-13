// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolTargets/VolumeDynamicMeshToolTarget.h"

#include "Components/BrushComponent.h"
#include "ConversionUtils/VolumeToDynamicMesh.h"
#include "ConversionUtils/DynamicMeshToVolume.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/Brush.h"
#include "GameFramework/Volume.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshNormals.h"
#include "StaticMeshAttributes.h"
#include "ToolSetupUtil.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

UVolumeDynamicMeshToolTarget::UVolumeDynamicMeshToolTarget()
{
	// TODO: These should be user-configurable somewhere
	VolumeToMeshOptions.bInWorldSpace = false;
	VolumeToMeshOptions.bSetGroups = true;
	VolumeToMeshOptions.bMergeVertices = true;

	// When a volume has cracks, this option seems to make the geometry
	// worse rather than better, since the filled in triangles are sometimes
	// degenerate, folded in on themselves, etc.
	VolumeToMeshOptions.bAutoRepairMesh = false;

	VolumeToMeshOptions.bOptimizeMesh = true;
}

int32 UVolumeDynamicMeshToolTarget::GetNumMaterials() const
{
	return IsValid() ? 1 : 0;
}

UMaterialInterface* UVolumeDynamicMeshToolTarget::GetMaterial(int32 MaterialIndex) const
{
	if (!IsValid() || MaterialIndex > 0)
	{
		return nullptr;
	}

	return ToolSetupUtil::GetDefaultEditVolumeMaterial();
}

void UVolumeDynamicMeshToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const
{
	MaterialSetOut.Materials.Reset();
	if (IsValid() == false)
	{
		return;
	}

	UMaterialInterface* Material = ToolSetupUtil::GetDefaultEditVolumeMaterial();
	if (Material)
	{
		MaterialSetOut.Materials.Add(Material);
	}
}

TSharedPtr<UE::Geometry::FDynamicMesh3> UVolumeDynamicMeshToolTarget::GetDynamicMesh()
{
	UBrushComponent* BrushComponent = Cast<UBrushComponent>(Component);
	if (!BrushComponent)
	{
		return nullptr;
	}
	AVolume* Volume = Cast<AVolume>(BrushComponent->GetOwner());
	if (!Volume)
	{
		return nullptr;
	}

	TSharedPtr<FDynamicMesh3> DynamicMesh = MakeShared<FDynamicMesh3>();
	UE::Conversion::VolumeToDynamicMesh(Volume, *DynamicMesh, GetVolumeToMeshOptions());
	FMeshNormals::InitializeMeshToPerTriangleNormals(DynamicMesh.Get());

	return DynamicMesh;
}

void UVolumeDynamicMeshToolTarget::CommitDynamicMesh(const UE::Geometry::FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo&)
{
	check(IsValid());

	UBrushComponent* BrushComponent = Cast<UBrushComponent>(Component);
	check(BrushComponent);
	AVolume* Volume = Cast<AVolume>(BrushComponent->GetOwner());
	check(Volume);

	FTransform Transform = GetWorldTransform();
	UE::Conversion::DynamicMeshToVolume(Mesh, Volume);

	Volume->SetActorTransform(Transform);
	Volume->PostEditChange();
}


// Factory

bool UVolumeDynamicMeshToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	UBrushComponent* BrushComponent = Cast<UBrushComponent>(SourceObject);
	if (!BrushComponent)
	{
		return false;
	}

	return Cast<AVolume>(BrushComponent->GetOwner()) && Requirements.AreSatisfiedBy(UVolumeDynamicMeshToolTarget::StaticClass());
}

UToolTarget* UVolumeDynamicMeshToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UVolumeDynamicMeshToolTarget* Target = NewObject<UVolumeDynamicMeshToolTarget>();
	Target->Component = Cast<UBrushComponent>(SourceObject);

	check(Target->Component && Requirements.AreSatisfiedBy(Target)
		&& Cast<AVolume>(Cast<UBrushComponent>(SourceObject)->GetOwner()));

	return Target;
}