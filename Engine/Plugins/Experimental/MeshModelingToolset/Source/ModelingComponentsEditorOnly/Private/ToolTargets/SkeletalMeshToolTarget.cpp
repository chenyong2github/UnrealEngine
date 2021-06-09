// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/SkeletalMeshToolTarget.h"

#include "Components/SkinnedMeshComponent.h"
#include "ConversionUtils/DynamicMeshViaMeshDescriptionUtil.h"
#include "Rendering/SkeletalMeshModel.h"

using namespace UE::Geometry;

namespace USkeletalMeshToolTargetLocals
{
	int32 LODIndex = 0;
}

bool USkeletalMeshToolTarget::IsValid() const
{
	return SkeletalMesh && !SkeletalMesh->IsPendingKillOrUnreachable() && SkeletalMesh->IsValidLowLevel();
}

int32 USkeletalMeshToolTarget::GetNumMaterials() const
{
	return ensure(IsValid()) ? SkeletalMesh->GetMaterials().Num() : 0;
}

UMaterialInterface* USkeletalMeshToolTarget::GetMaterial(int32 MaterialIndex) const
{
	return ensure(IsValid() && MaterialIndex < SkeletalMesh->GetMaterials().Num()) ? 
		SkeletalMesh->GetMaterials()[MaterialIndex].MaterialInterface : nullptr;
}

void USkeletalMeshToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const
{
	if (!ensure(IsValid())) return;
	GetMaterialSet(SkeletalMesh, MaterialSetOut, bPreferAssetMaterials);
}

void USkeletalMeshToolTarget::GetMaterialSet(const USkeletalMesh* SkeletalMeshIn, FComponentMaterialSet& MaterialSetOut,
	bool bPreferAssetMaterials)
{
	const TArray<FSkeletalMaterial>& Materials = SkeletalMeshIn->GetMaterials(); 
	MaterialSetOut.Materials.SetNum(Materials.Num());
	for (int32 k = 0; k < Materials.Num(); ++k)
	{
		MaterialSetOut.Materials[k] = Materials[k].MaterialInterface;
	}
}

bool USkeletalMeshToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!ensure(IsValid())) return false;
	return CommitMaterialSetUpdate(SkeletalMesh, MaterialSet, bApplyToAsset);
}

bool USkeletalMeshToolTarget::CommitMaterialSetUpdate(USkeletalMesh* SkeletalMeshIn, 
	const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!bApplyToAsset)
	{
		return false;
	}

	if (SkeletalMeshIn->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		UE_LOG(LogTemp, Warning, TEXT("CANNOT MODIFY BUILT-IN ENGINE ASSET %s"), *SkeletalMeshIn->GetPathName());
		return false;
	}

	// flush any pending rendering commands, which might touch a component while we are rebuilding its mesh
	FlushRenderingCommands();

	// make sure transactional flag is on
	SkeletalMeshIn->SetFlags(RF_Transactional);

	SkeletalMeshIn->Modify();

	const int NewNumMaterials = MaterialSet.Materials.Num();
	TArray<FSkeletalMaterial> &SkeletalMaterials = SkeletalMeshIn->GetMaterials(); 
	if (NewNumMaterials != SkeletalMaterials.Num())
	{
		SkeletalMaterials.SetNum(NewNumMaterials);
	}
	for (int k = 0; k < NewNumMaterials; ++k)
	{
		if (SkeletalMaterials[k].MaterialInterface != MaterialSet.Materials[k])
		{
			SkeletalMaterials[k].MaterialInterface = MaterialSet.Materials[k];
			if (SkeletalMaterials[k].MaterialSlotName.IsNone())
			{
				SkeletalMaterials[k].MaterialSlotName = MaterialSet.Materials[k]->GetFName();
			}
		}
	}

	SkeletalMeshIn->PostEditChange();

	return true;
}

FMeshDescription* USkeletalMeshToolTarget::GetMeshDescription()
{
	if (!ensure(IsValid()))
	{
		return nullptr;
	}

	if (!CachedMeshDescription.IsValid())
	{
		CachedMeshDescription = MakeUnique<FMeshDescription>();
		GetMeshDescription(SkeletalMesh, *CachedMeshDescription);
	}

	return CachedMeshDescription.Get();
}

void USkeletalMeshToolTarget::GetMeshDescription(const USkeletalMesh* SkeletalMeshIn, FMeshDescription& MeshDescription)
{
	using namespace USkeletalMeshToolTargetLocals;

	// Check first if we have bulk data available and non-empty.
	if (SkeletalMeshIn->IsLODImportedDataBuildAvailable(LODIndex) && !SkeletalMeshIn->IsLODImportedDataEmpty(LODIndex))
	{
		FSkeletalMeshImportData SkeletalMeshImportData;
		SkeletalMeshIn->LoadLODImportedData(LODIndex, SkeletalMeshImportData);
		SkeletalMeshImportData.GetMeshDescription(MeshDescription);
	}
	else
	{
		// Fall back on the LOD model directly if no bulk data exists. When we commit
		// the mesh description, we override using the bulk data. This can happen for older
		// skeletal meshes, from UE 4.24 and earlier.
		const FSkeletalMeshModel* SkeletalMeshModel = SkeletalMeshIn->GetImportedModel();
		if (SkeletalMeshModel && SkeletalMeshModel->LODModels.IsValidIndex(LODIndex))
		{
			SkeletalMeshModel->LODModels[LODIndex].GetMeshDescription(MeshDescription, SkeletalMeshIn);
		}			
	}
}

void USkeletalMeshToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	if (ensure(IsValid()) == false) return;
	CommitMeshDescription(SkeletalMesh, GetMeshDescription(), Committer);
}

void USkeletalMeshToolTarget::CommitMeshDescription(USkeletalMesh* SkeletalMeshIn,
	FMeshDescription* MeshDescription, const FCommitter& Committer)
{
	using namespace USkeletalMeshToolTargetLocals;

	if (SkeletalMeshIn->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		const FString DebugMessage = FString::Printf(TEXT("CANNOT MODIFY BUILT-IN ENGINE ASSET %s"), *SkeletalMeshIn->GetPathName());
		if (GAreScreenMessagesEnabled)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10.0f, FColor::Red, DebugMessage);
		}
		UE_LOG(LogTemp, Warning, TEXT("%s"), *DebugMessage);
		return;
	}

	// flush any pending rendering commands, which might touch a component while we are rebuilding it's mesh
	FlushRenderingCommands();

	// make sure transactional flag is on for this asset
	SkeletalMeshIn->SetFlags(RF_Transactional);

	verify(SkeletalMeshIn->Modify());

	FCommitterParams CommitterParams;

	CommitterParams.MeshDescriptionOut = MeshDescription;

	Committer(CommitterParams);

	FSkeletalMeshImportData SkeletalMeshImportData = 
		FSkeletalMeshImportData::CreateFromMeshDescription(*CommitterParams.MeshDescriptionOut);
	SkeletalMeshIn->SaveLODImportedData(LODIndex, SkeletalMeshImportData);

	// Make sure the mesh builder knows it's the latest variety, so that the render data gets
	// properly rebuilt.
	SkeletalMeshIn->SetLODImportedDataVersions(LODIndex, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
	SkeletalMeshIn->SetUseLegacyMeshDerivedDataKey(false);

	SkeletalMeshIn->PostEditChange();
}

TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> USkeletalMeshToolTarget::GetDynamicMesh()
{
	return GetDynamicMeshViaMeshDescription(*this);
}

void USkeletalMeshToolTarget::CommitDynamicMesh(const FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo& CommitInfo)
{
	CommitDynamicMeshViaMeshDescription(*this, Mesh, CommitInfo);
}

USkeletalMesh* USkeletalMeshToolTarget::GetSkeletalMesh() const
{
	return IsValid() ? SkeletalMesh : nullptr;
}


// Factory

bool USkeletalMeshToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	return Cast<USkeletalMesh>(SourceObject) 
		&& Requirements.AreSatisfiedBy(USkeletalMeshToolTarget::StaticClass());
}

UToolTarget* USkeletalMeshToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	USkeletalMeshToolTarget* Target = NewObject<USkeletalMeshToolTarget>();
	Target->SkeletalMesh = Cast<USkeletalMesh>(SourceObject);
	check(Target->SkeletalMesh && Requirements.AreSatisfiedBy(Target));

	return Target;
}
