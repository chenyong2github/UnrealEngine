// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolTargets/VolumeMeshDescriptionToolTarget.h"

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

FMeshDescription* UVolumeMeshDescriptionToolTarget::GetMeshDescription()
{
	if (!ConvertedMeshDescription.IsValid())
	{
		// Note: We can go directly from a volume to a mesh description using GetBrushMesh() in
		// Editor.h. However, that path doesn't assign polygroups to the result, which we
		// typically want when using this target, hence the path through a dynamic mesh.

		TSharedPtr<FDynamicMesh3> DynamicMesh = GetDynamicMesh();
		if (!DynamicMesh)
		{
			return nullptr;
		}

		ConvertedMeshDescription = MakeShared<FMeshDescription>();
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