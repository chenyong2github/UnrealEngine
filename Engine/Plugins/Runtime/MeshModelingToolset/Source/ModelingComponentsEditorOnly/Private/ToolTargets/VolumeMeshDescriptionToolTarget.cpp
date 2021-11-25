// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolTargets/VolumeMeshDescriptionToolTarget.h"

#include "Components/BrushComponent.h"
#include "DynamicMeshToMeshDescription.h"
#include "GameFramework/Volume.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMesh/MeshNormals.h"
#include "StaticMeshAttributes.h"

using namespace UE::Geometry;

const FMeshDescription* UVolumeMeshDescriptionToolTarget::GetMeshDescription()
{
	if (!ConvertedMeshDescription.IsValid())
	{
		// Note: We can go directly from a volume to a mesh description using GetBrushMesh() in
		// Editor.h. However, that path doesn't assign polygroups to the result, which we
		// typically want when using this target, hence the path through a dynamic mesh.

		TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> DynamicMesh = 
			MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>(GetDynamicMesh());

		ConvertedMeshDescription = MakeShared<FMeshDescription, ESPMode::ThreadSafe>();
		FStaticMeshAttributes Attributes(*ConvertedMeshDescription);
		Attributes.Register();
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(DynamicMesh.Get(), *ConvertedMeshDescription);
	}
	return ConvertedMeshDescription.Get();
}

void UVolumeMeshDescriptionToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	check(IsValid());

	// Let the user fill our mesh description with the Committer
	if (!ConvertedMeshDescription.IsValid())
	{
		ConvertedMeshDescription = MakeShared<FMeshDescription, ESPMode::ThreadSafe>();
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

	CommitDynamicMesh(DynamicMesh);
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