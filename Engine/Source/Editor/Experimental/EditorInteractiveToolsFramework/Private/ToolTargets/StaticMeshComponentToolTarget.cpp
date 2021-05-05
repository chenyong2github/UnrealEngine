// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/StaticMeshComponentToolTarget.h"

#include "ComponentReregisterContext.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "RenderingThread.h"


static void DisplayCriticalWarningMessage(const FString& Message)
{
	if (GAreScreenMessagesEnabled)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10.0f, FColor::Red, Message);
	}
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
}


void UStaticMeshComponentToolTarget::SetEditingLOD(EStaticMeshEditingLOD RequestedEditingLOD)
{
	EStaticMeshEditingLOD ValidEditingLOD = EStaticMeshEditingLOD::LOD0;

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (ensure(StaticMeshComponent != nullptr))
	{
		UStaticMesh* StaticMeshAsset = StaticMeshComponent->GetStaticMesh();
		if (ensure(StaticMeshAsset != nullptr))
		{
			if (RequestedEditingLOD == EStaticMeshEditingLOD::MaxQuality)
			{
				ValidEditingLOD = StaticMeshAsset->IsHiResMeshDescriptionValid() ? EStaticMeshEditingLOD::HiResSource : EStaticMeshEditingLOD::LOD0;
			}
			else if (RequestedEditingLOD == EStaticMeshEditingLOD::HiResSource)
			{
				ValidEditingLOD = StaticMeshAsset->IsHiResMeshDescriptionValid() ? EStaticMeshEditingLOD::HiResSource : EStaticMeshEditingLOD::LOD0;
				if (ValidEditingLOD != EStaticMeshEditingLOD::HiResSource)
				{
					DisplayCriticalWarningMessage(FString(TEXT("HiRes Source selected but not available - Falling Back to LOD0")));
				}
			}
			else
			{
				ValidEditingLOD = RequestedEditingLOD;
				int32 MaxExistingLOD = StaticMeshAsset->GetNumSourceModels() - 1;
				if ((int32)ValidEditingLOD > MaxExistingLOD)
				{
					DisplayCriticalWarningMessage(FString::Printf(TEXT("LOD%d Requested but not available - Falling Back to LOD%d"), (int32)ValidEditingLOD, MaxExistingLOD));
					ValidEditingLOD = (EStaticMeshEditingLOD)MaxExistingLOD;
				}
			}
		}
	}

	EditingLOD = ValidEditingLOD;
}


bool UStaticMeshComponentToolTarget::IsValid() const
{
	if (!UPrimitiveComponentToolTarget::IsValid())
	{
		return false;
	}
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComponent == nullptr)
	{
		return false;
	}
	UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	if (StaticMesh == nullptr)
	{
		return false;
	}

	if (EditingLOD == EStaticMeshEditingLOD::HiResSource)
	{
		if (StaticMesh->IsHiResMeshDescriptionValid() == false)
		{
			return false;
		}
	}
	else if ((int32)EditingLOD >= StaticMesh->GetNumSourceModels())
	{
		return false;
	}

	return true;
}



int32 UStaticMeshComponentToolTarget::GetNumMaterials() const
{
	return ensure(IsValid()) ? Component->GetNumMaterials() : 0;
}

UMaterialInterface* UStaticMeshComponentToolTarget::GetMaterial(int32 MaterialIndex) const
{
	return ensure(IsValid()) ? Component->GetMaterial(MaterialIndex) : nullptr;
}

void UStaticMeshComponentToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const
{
	if (!ensure(IsValid())) return;

	if (bPreferAssetMaterials)
	{
		UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();
		int32 NumMaterials = Component->GetNumMaterials();
		MaterialSetOut.Materials.SetNum(NumMaterials);
		for (int32 k = 0; k < NumMaterials; ++k)
		{
			MaterialSetOut.Materials[k] = StaticMesh->GetMaterial(k);
		}
	}
	else
	{
		int32 NumMaterials = Component->GetNumMaterials();
		MaterialSetOut.Materials.SetNum(NumMaterials);
		for (int32 k = 0; k < NumMaterials; ++k)
		{
			MaterialSetOut.Materials[k] = Component->GetMaterial(k);
		}
	}
}

bool UStaticMeshComponentToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!ensure(IsValid())) return false;

	if (bApplyToAsset)
	{
		UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();

		if (StaticMesh->GetPathName().StartsWith(TEXT("/Engine/")))
		{
			UE_LOG(LogTemp, Warning, TEXT("CANNOT MODIFY BUILT-IN ENGINE ASSET %s"), *StaticMesh->GetPathName());
			return false;
		}

		// flush any pending rendering commands, which might touch this component while we are rebuilding its mesh
		FlushRenderingCommands();

		// unregister the component while we update it's static mesh
		TUniquePtr<FComponentReregisterContext> ComponentReregisterContext = MakeUnique<FComponentReregisterContext>(Component);

		// make sure transactional flag is on
		StaticMesh->SetFlags(RF_Transactional);

		StaticMesh->Modify();

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

		StaticMesh->PostEditChange();
	}
	else
	{
		int32 NumMaterialsNeeded = Component->GetNumMaterials();
		int32 NumMaterialsGiven = MaterialSet.Materials.Num();

		// We wrote the below code to support a mismatch in the number of materials.
		// However, it is not yet clear whether this might be desirable, and we don't
		// want to inadvertantly hide bugs in the meantime. So, we keep this ensure here
		// for now, and we can remove it if we decide that we want the ability.
		ensure(NumMaterialsNeeded == NumMaterialsGiven);

		check(NumMaterialsGiven > 0);

		for (int32 i = 0; i < NumMaterialsNeeded; ++i)
		{
			int32 MaterialToUseIndex = FMath::Min(i, NumMaterialsGiven - 1);
			Component->SetMaterial(i, MaterialSet.Materials[MaterialToUseIndex]);
		}
	}

	return true;
}


FMeshDescription* UStaticMeshComponentToolTarget::GetMeshDescription()
{
	if (ensure(IsValid()))
	{
		UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();
		return (EditingLOD == EStaticMeshEditingLOD::HiResSource) ?
			StaticMesh->GetHiResMeshDescription() : StaticMesh->GetMeshDescription((int32)EditingLOD);
	}
	return nullptr;
}


void UStaticMeshComponentToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	if (ensure(IsValid()) == false) return;

	UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();

	if (StaticMesh->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		DisplayCriticalWarningMessage(FString::Printf(TEXT("CANNOT MODIFY BUILT-IN ENGINE ASSET %s"), *StaticMesh->GetPathName()));
		return;
	}

	// flush any pending rendering commands, which might touch this component while we are rebuilding it's mesh
	FlushRenderingCommands();

	// unregister the component while we update its static mesh
	FComponentReregisterContext ComponentReregisterContext(Component);

	// make sure transactional flag is on for this asset
	StaticMesh->SetFlags(RF_Transactional);

	verify(StaticMesh->Modify());
	if (EditingLOD == EStaticMeshEditingLOD::HiResSource)
	{
		verify(StaticMesh->ModifyHiResMeshDescription());
	}
	else
	{
		verify(StaticMesh->ModifyMeshDescription((int32)EditingLOD));
	}

	FCommitterParams CommitterParams;
	CommitterParams.MeshDescriptionOut = GetMeshDescription();

	Committer(CommitterParams);

	if (EditingLOD == EStaticMeshEditingLOD::HiResSource)
	{
		StaticMesh->CommitHiResMeshDescription();
	}
	else
	{
		StaticMesh->CommitMeshDescription((int32)EditingLOD);
	}

	StaticMesh->PostEditChange();

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();
}

UStaticMesh* UStaticMeshComponentToolTarget::GetStaticMesh() const
{
	return IsValid() ? Cast<UStaticMeshComponent>(Component)->GetStaticMesh() : nullptr;
}


// Factory

bool UStaticMeshComponentToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(SourceObject);
	return Component && !Component->IsPendingKillOrUnreachable() && Component->IsValidLowLevel() && Component->GetStaticMesh()
		&& (Component->GetStaticMesh()->GetNumSourceModels() > 0)
		&& Requirements.AreSatisfiedBy(UStaticMeshComponentToolTarget::StaticClass());
}

UToolTarget* UStaticMeshComponentToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UStaticMeshComponentToolTarget* Target = NewObject<UStaticMeshComponentToolTarget>();// TODO: Should we set an outer here?
	Target->Component = Cast<UStaticMeshComponent>(SourceObject);
	Target->SetEditingLOD(EditingLOD);
	check(Target->Component && Requirements.AreSatisfiedBy(Target));

	return Target;
}


void UStaticMeshComponentToolTargetFactory::SetActiveEditingLOD(EStaticMeshEditingLOD NewEditingLOD)
{
	EditingLOD = NewEditingLOD;
}