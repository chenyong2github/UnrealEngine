// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/SkeletalMeshComponentToolTarget.h"

#include "Components/SkinnedMeshComponent.h"
#include "ConversionUtils/DynamicMeshViaMeshDescriptionUtil.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ToolTargets/SkeletalMeshToolTarget.h"

using namespace UE::Geometry;

namespace USkeletalMeshComponentToolTargetLocals
{
	int32 LODIndex = 0;
}


int32 USkeletalMeshComponentToolTarget::GetNumMaterials() const
{
	return ensure(IsValid()) ? Component->GetNumMaterials() : 0;
}

UMaterialInterface* USkeletalMeshComponentToolTarget::GetMaterial(int32 MaterialIndex) const
{
	return ensure(IsValid()) ? Component->GetMaterial(MaterialIndex) : nullptr;
}

void USkeletalMeshComponentToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const
{
	if (!ensure(IsValid())) return;

	if (bPreferAssetMaterials)
	{
		const USkeletalMesh* SkeletalMesh = Cast<USkinnedMeshComponent>(Component)->SkeletalMesh;
		USkeletalMeshToolTarget::GetMaterialSet(SkeletalMesh, MaterialSetOut, bPreferAssetMaterials);
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

bool USkeletalMeshComponentToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!ensure(IsValid())) return false;

	if (bApplyToAsset)
	{
		USkeletalMesh* SkeletalMesh = Cast<USkinnedMeshComponent>(Component)->SkeletalMesh;

		// unregister the component while we update it's static mesh
		TUniquePtr<FComponentReregisterContext> ComponentReregisterContext = MakeUnique<FComponentReregisterContext>(Component);

		return USkeletalMeshToolTarget::CommitMaterialSetUpdate(SkeletalMesh, MaterialSet, bApplyToAsset);
	}
	else
	{
		const int32 NumMaterialsNeeded = Component->GetNumMaterials();
		const int32 NumMaterialsGiven = MaterialSet.Materials.Num();

		// We wrote the below code to support a mismatch in the number of materials.
		// However, it is not yet clear whether this might be desirable, and we don't
		// want to inadvertantly hide bugs in the meantime. So, we keep this ensure here
		// for now, and we can remove it if we decide that we want the ability.
		ensure(NumMaterialsNeeded == NumMaterialsGiven);

		check(NumMaterialsGiven > 0);

		for (int32 i = 0; i < NumMaterialsNeeded; ++i)
		{
			const int32 MaterialToUseIndex = FMath::Min(i, NumMaterialsGiven - 1);
			Component->SetMaterial(i, MaterialSet.Materials[MaterialToUseIndex]);
		}
	}

	return true;
}

FMeshDescription* USkeletalMeshComponentToolTarget::GetMeshDescription()
{
	if (!ensure(IsValid()))
	{
		return nullptr;
	}

	if (!CachedMeshDescription.IsValid())
	{
		CachedMeshDescription = MakeUnique<FMeshDescription>();
		const USkeletalMesh* SkeletalMesh = Cast<USkinnedMeshComponent>(Component)->SkeletalMesh;
		USkeletalMeshToolTarget::GetMeshDescription(SkeletalMesh, *CachedMeshDescription);
	}
	
	return CachedMeshDescription.Get();
}

void USkeletalMeshComponentToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	if (ensure(IsValid()) == false) return;

	USkeletalMesh* SkeletalMesh = Cast<USkinnedMeshComponent>(Component)->SkeletalMesh;

	// unregister the component while we update its skeletal mesh
	FComponentReregisterContext ComponentReregisterContext(Component);

	USkeletalMeshToolTarget::CommitMeshDescription(SkeletalMesh, GetMeshDescription(), Committer);

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();

	CachedMeshDescription.Reset();
}

TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> USkeletalMeshComponentToolTarget::GetDynamicMesh()
{
	return GetDynamicMeshViaMeshDescription(*this);
}

void USkeletalMeshComponentToolTarget::CommitDynamicMesh(const FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo& CommitInfo)
{
	CommitDynamicMeshViaMeshDescription(*this, Mesh, CommitInfo);
}

USkeletalMesh* USkeletalMeshComponentToolTarget::GetSkeletalMesh() const
{
	return IsValid() ? Cast<USkinnedMeshComponent>(Component)->SkeletalMesh : nullptr;
}


// Factory

bool USkeletalMeshComponentToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	return Cast<USkinnedMeshComponent>(SourceObject) && Cast<USkinnedMeshComponent>(SourceObject)->SkeletalMesh &&
		Requirements.AreSatisfiedBy(USkeletalMeshComponentToolTarget::StaticClass());
}

UToolTarget* USkeletalMeshComponentToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	USkeletalMeshComponentToolTarget* Target = NewObject<USkeletalMeshComponentToolTarget>();
	Target->Component = Cast<USkinnedMeshComponent>(SourceObject);
	check(Target->Component && Requirements.AreSatisfiedBy(Target));

	return Target;
}
