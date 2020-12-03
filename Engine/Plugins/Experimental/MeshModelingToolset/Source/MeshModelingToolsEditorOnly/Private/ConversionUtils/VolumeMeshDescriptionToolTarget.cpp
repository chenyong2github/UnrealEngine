// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConversionUtils/VolumeMeshDescriptionToolTarget.h"

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

UVolumeMeshDescriptionToolTarget::UVolumeMeshDescriptionToolTarget()
{
	// TODO: These should be user-configurable somewhere
	VolumeToMeshOptions.bInWorldSpace = false;
	VolumeToMeshOptions.bSetGroups = true;
	VolumeToMeshOptions.bMergeVertices = true;
	VolumeToMeshOptions.bAutoRepairMesh = true;
	VolumeToMeshOptions.bOptimizeMesh = true;
}

void UVolumeMeshDescriptionToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut) const
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

void UVolumeMeshDescriptionToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet)
{
	check(IsValid());
	// Do nothing
}

FMeshDescription* UVolumeMeshDescriptionToolTarget::GetMeshDescription()
{
	FTransform transform = GetWorldTransform();
	if (!ConvertedMeshDescription.IsValid())
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

		// Note: We can go directly from a volume to a mesh description using GetBrushMesh() in
		// Editor.h. However, that path doesn't assign polygroups to the result, which we
		// typically want when using this target, hence the two-step path we use here.
		FDynamicMesh3 DynamicMesh;
		UE::Conversion::VolumeToDynamicMesh(Volume, DynamicMesh, GetVolumeToMeshOptions());
		FMeshNormals::InitializeMeshToPerTriangleNormals(&DynamicMesh);

		ConvertedMeshDescription = MakeShared<FMeshDescription>();
		FStaticMeshAttributes Attributes(*ConvertedMeshDescription);
		Attributes.Register();
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&DynamicMesh, *ConvertedMeshDescription);
	}
	FTransform transform2 = GetWorldTransform();
	return ConvertedMeshDescription.Get();
}

void UVolumeMeshDescriptionToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	check(IsValid());

	UBrushComponent* BrushComponent = Cast<UBrushComponent>(Component);
	check(BrushComponent);
	AVolume* Volume = Cast<AVolume>(BrushComponent->GetOwner());
	check(Volume);

	// Let the user fill our mesh description with the Committer
	if (!ConvertedMeshDescription.IsValid())
	{
		ConvertedMeshDescription = MakeShared<FMeshDescription>();
		FStaticMeshAttributes Attributes(*ConvertedMeshDescription);
		Attributes.Register();
	}
	FCommitterParams CommitParams;
	CommitParams.MeshDescriptionOut = ConvertedMeshDescription.Get();
	Committer(CommitParams);

	// The conversion we have right now is from dynamic mesh to volume, so we convert
	// to dynamic mesh first.
	FDynamicMesh3 DynamicMesh;
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ConvertedMeshDescription.Get(), DynamicMesh);

	FTransform Transform = GetWorldTransform();
	UE::Conversion::DynamicMeshToVolume(DynamicMesh, Volume);

	Volume->SetActorTransform(Transform);
	Volume->PostEditChange();
}


// Factory

bool UVolumeMeshDescriptionToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	UBrushComponent* BrushComponent = Cast<UBrushComponent>(SourceObject);
	if (!BrushComponent)
	{
		return false;
	}

	return Cast<AVolume>(BrushComponent->GetOwner()) && Requirements.AreSatisfiedBy(UVolumeMeshDescriptionToolTarget::StaticClass());
}

UToolTarget* UVolumeMeshDescriptionToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UVolumeMeshDescriptionToolTarget* Target = NewObject<UVolumeMeshDescriptionToolTarget>();
	Target->Component = Cast<UBrushComponent>(SourceObject);

	check(Target->Component && Requirements.AreSatisfiedBy(Target)
		&& Cast<AVolume>(Cast<UBrushComponent>(SourceObject)->GetOwner()));

	return Target;
}