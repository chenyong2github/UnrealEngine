// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProceduralMeshComponentToolTarget.h"

#include "MeshDescription.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralMeshConversion.h"

FMeshDescription* UProceduralMeshComponentToolTarget::GetMeshDescription()
{
	if (!MeshDescription.IsValid())
	{
		MeshDescription = MakeShared<FMeshDescription>();
	}
	*MeshDescription = BuildMeshDescription(Cast<UProceduralMeshComponent>(Component));
	return MeshDescription.Get();
}

void UProceduralMeshComponentToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	if (!MeshDescription.IsValid())
	{
		MeshDescription = MakeShared<FMeshDescription>();
	}
	Committer({ MeshDescription.Get() });
	MeshDescriptionToProcMesh(*MeshDescription, Cast<UProceduralMeshComponent>(Component));
}

bool UProceduralMeshComponentToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	return Cast<UProceduralMeshComponent>(SourceObject) && Requirements.AreSatisfiedBy(UProceduralMeshComponentToolTarget::StaticClass());
}

UToolTarget* UProceduralMeshComponentToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UProceduralMeshComponentToolTarget* Target = NewObject<UProceduralMeshComponentToolTarget>();
	Target->Component = Cast<UProceduralMeshComponent>(SourceObject);
	check(Target->Component && Requirements.AreSatisfiedBy(Target));
	return Target;
}