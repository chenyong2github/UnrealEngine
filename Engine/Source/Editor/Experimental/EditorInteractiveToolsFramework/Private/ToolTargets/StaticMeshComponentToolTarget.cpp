// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolTargets/StaticMeshComponentToolTarget.h"

#include "ComponentReregisterContext.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "RenderingThread.h"

namespace UStaticMeshComponentToolTargetLocals
{
	int32 LODIndex = 0;
}

void UStaticMeshComponentToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet)
{
	check(IsValid());

	UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();

	// flush any pending rendering commands, which might touch this component while we are rebuilding it's mesh
	FlushRenderingCommands();

	// unregister the component while we update it's static mesh
	TUniquePtr<FComponentReregisterContext> ComponentReregisterContext = MakeUnique<FComponentReregisterContext>(Component);

	// make sure transactional flag is on
	StaticMesh->SetFlags(RF_Transactional);

	bool bSavedToTransactionBuffer = StaticMesh->Modify();
	check(bSavedToTransactionBuffer);

	int NewNumMaterials = MaterialSet.Materials.Num();
	if (NewNumMaterials != StaticMesh->GetStaticMaterials().Num())
	{
		StaticMesh->GetStaticMaterials().SetNum(NewNumMaterials);
	}
	for (int k = 0; k < NewNumMaterials; ++k)
	{
		if (StaticMesh->GetMaterial(k) != MaterialSet.Materials[k])
		{
			StaticMesh->SetMaterial(k, MaterialSet.Materials[k]);
		}
	}

	// right?
	StaticMesh->PostEditChange();
}

FMeshDescription* UStaticMeshComponentToolTarget::GetMeshDescription()
{
	using namespace UStaticMeshComponentToolTargetLocals;

	return IsValid() ? Cast<UStaticMeshComponent>(Component)->GetStaticMesh()->GetMeshDescription(LODIndex) : nullptr;
}

void UStaticMeshComponentToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	using namespace UStaticMeshComponentToolTargetLocals;
	check(IsValid());

	UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();

	// flush any pending rendering commands, which might touch this component while we are rebuilding it's mesh
	FlushRenderingCommands();

	// unregister the component while we update its static mesh
	FComponentReregisterContext ComponentReregisterContext(Component);

	// make sure transactional flag is on for this asset
	StaticMesh->SetFlags(RF_Transactional);

	bool bSavedToTransactionBuffer = StaticMesh->Modify();
	check(bSavedToTransactionBuffer);

	FCommitterParams CommitterParams;
	CommitterParams.MeshDescriptionOut = StaticMesh->GetMeshDescription(LODIndex);

	Committer(CommitterParams);

	StaticMesh->CommitMeshDescription(LODIndex);
	StaticMesh->PostEditChange();

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();
}


// Factory

bool UStaticMeshComponentToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	return Cast<UStaticMeshComponent>(SourceObject) && Requirements.AreSatisfiedBy(UStaticMeshComponentToolTarget::StaticClass());
}

UToolTarget* UStaticMeshComponentToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UStaticMeshComponentToolTarget* Target = NewObject<UStaticMeshComponentToolTarget>();// TODO: Should we set an outer here?
	Target->Component = Cast<UStaticMeshComponent>(SourceObject);
	check(Target->Component && Requirements.AreSatisfiedBy(Target));

	return Target;
}