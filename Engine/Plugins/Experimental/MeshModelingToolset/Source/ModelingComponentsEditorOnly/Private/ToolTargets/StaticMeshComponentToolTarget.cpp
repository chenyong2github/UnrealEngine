// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/StaticMeshComponentToolTarget.h"

#include "ComponentReregisterContext.h"
#include "Components/StaticMeshComponent.h"
#include "ConversionUtils/DynamicMeshViaMeshDescriptionUtil.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "RenderingThread.h"
#include "ToolTargets/StaticMeshToolTarget.h"

using namespace UE::Geometry;

void UStaticMeshComponentToolTarget::SetEditingLOD(EStaticMeshEditingLOD RequestedEditingLOD)
{
	EStaticMeshEditingLOD ValidEditingLOD = EStaticMeshEditingLOD::LOD0;

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (ensure(StaticMeshComponent != nullptr))
	{
		UStaticMesh* StaticMeshAsset = StaticMeshComponent->GetStaticMesh();
		ValidEditingLOD = UStaticMeshToolTarget::GetValidEditingLOD(StaticMeshAsset, RequestedEditingLOD);
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
	
	return UStaticMeshToolTarget::IsValid(StaticMesh, EditingLOD);
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
		UStaticMeshToolTarget::GetMaterialSet(StaticMesh, MaterialSetOut, bPreferAssetMaterials);
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

		// unregister the component while we update it's static mesh
		TUniquePtr<FComponentReregisterContext> ComponentReregisterContext = MakeUnique<FComponentReregisterContext>(Component);

		return UStaticMeshToolTarget::CommitMaterialSetUpdate(StaticMesh, MaterialSet, bApplyToAsset);
	}
	else
	{
		// filter out any Engine materials that we don't want to be permanently assigning
		TArray<UMaterialInterface*> FilteredMaterials = MaterialSet.Materials;
		for (int32 k = 0; k < FilteredMaterials.Num(); ++k)
		{
			FString AssetPath = FilteredMaterials[k]->GetPathName();
			if (AssetPath.StartsWith(TEXT("/MeshModelingToolset/")))
			{
				FilteredMaterials[k] = UMaterial::GetDefaultMaterial(MD_Surface);
			}
		}

		int32 NumMaterialsNeeded = Component->GetNumMaterials();
		int32 NumMaterialsGiven = FilteredMaterials.Num();

		// We wrote the below code to support a mismatch in the number of materials.
		// However, it is not yet clear whether this might be desirable, and we don't
		// want to inadvertantly hide bugs in the meantime. So, we keep this ensure here
		// for now, and we can remove it if we decide that we want the ability.
		ensure(NumMaterialsNeeded == NumMaterialsGiven);

		check(NumMaterialsGiven > 0);

		for (int32 i = 0; i < NumMaterialsNeeded; ++i)
		{
			int32 MaterialToUseIndex = FMath::Min(i, NumMaterialsGiven - 1);
			Component->SetMaterial(i, FilteredMaterials[MaterialToUseIndex]);
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

	// unregister the component while we update its static mesh
	FComponentReregisterContext ComponentReregisterContext(Component);

	UStaticMeshToolTarget::CommitMeshDescription(StaticMesh, GetMeshDescription(), Committer, EditingLOD);

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();
}

TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> UStaticMeshComponentToolTarget::GetDynamicMesh()
{
	return GetDynamicMeshViaMeshDescription(*this);
}

void UStaticMeshComponentToolTarget::CommitDynamicMesh(const FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo& CommitInfo)
{
	CommitDynamicMeshViaMeshDescription(*this, Mesh, CommitInfo);
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