// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/StaticMeshToolTarget.h"

#include "AssetUtils/MeshDescriptionUtil.h"
#include "ComponentReregisterContext.h"
#include "ConversionUtils/DynamicMeshViaMeshDescriptionUtil.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "RenderingThread.h"

using namespace UE::Geometry;

namespace StaticMeshToolTargetLocals
{
	static void DisplayCriticalWarningMessage(const FString& Message)
	{
		if (GAreScreenMessagesEnabled)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10.0f, FColor::Red, Message);
		}
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	}
}

void UStaticMeshToolTarget::SetEditingLOD(EStaticMeshEditingLOD RequestedEditingLOD)
{
	EditingLOD = GetValidEditingLOD(StaticMesh, RequestedEditingLOD);
}

EStaticMeshEditingLOD UStaticMeshToolTarget::GetValidEditingLOD(const UStaticMesh* StaticMeshIn, 
	EStaticMeshEditingLOD RequestedEditingLOD)
{
	using namespace StaticMeshToolTargetLocals;

	EStaticMeshEditingLOD ValidEditingLOD = EStaticMeshEditingLOD::LOD0;

	if (ensure(StaticMeshIn != nullptr))
	{
		if (RequestedEditingLOD == EStaticMeshEditingLOD::MaxQuality)
		{
			ValidEditingLOD = StaticMeshIn->IsHiResMeshDescriptionValid() ? EStaticMeshEditingLOD::HiResSource : EStaticMeshEditingLOD::LOD0;
		}
		else if (RequestedEditingLOD == EStaticMeshEditingLOD::HiResSource)
		{
			ValidEditingLOD = StaticMeshIn->IsHiResMeshDescriptionValid() ? EStaticMeshEditingLOD::HiResSource : EStaticMeshEditingLOD::LOD0;
			if (ValidEditingLOD != EStaticMeshEditingLOD::HiResSource)
			{
				DisplayCriticalWarningMessage(FString(TEXT("HiRes Source selected but not available - Falling Back to LOD0")));
			}
		}
		else
		{
			ValidEditingLOD = RequestedEditingLOD;
			int32 MaxExistingLOD = StaticMeshIn->GetNumSourceModels() - 1;
			if ((int32)ValidEditingLOD > MaxExistingLOD)
			{
				DisplayCriticalWarningMessage(FString::Printf(TEXT("LOD%d Requested but not available - Falling Back to LOD%d"), (int32)ValidEditingLOD, MaxExistingLOD));
				ValidEditingLOD = (EStaticMeshEditingLOD)MaxExistingLOD;
			}
		}
	}

	return ValidEditingLOD;
}


bool UStaticMeshToolTarget::IsValid() const
{
	return IsValid(StaticMesh, EditingLOD);
}

bool UStaticMeshToolTarget::IsValid(const UStaticMesh* StaticMeshIn, EStaticMeshEditingLOD EditingLODIn)
{
	if (!StaticMeshIn || StaticMeshIn->IsPendingKillOrUnreachable() || !StaticMeshIn->IsValidLowLevel())
	{
		return false;
	}

	if (EditingLODIn == EStaticMeshEditingLOD::HiResSource)
	{
		if (StaticMeshIn->IsHiResMeshDescriptionValid() == false)
		{
			return false;
		}
	}
	else if ((int32)EditingLODIn >= StaticMeshIn->GetNumSourceModels())
	{
		return false;
	}

	return true;
}


int32 UStaticMeshToolTarget::GetNumMaterials() const
{
	return ensure(IsValid()) ? StaticMesh->GetStaticMaterials().Num() : 0;
}

UMaterialInterface* UStaticMeshToolTarget::GetMaterial(int32 MaterialIndex) const
{
	return ensure(IsValid()) ? StaticMesh->GetMaterial(MaterialIndex) : nullptr;
}

void UStaticMeshToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const
{
	if (!ensure(IsValid())) return;

	GetMaterialSet(StaticMesh, MaterialSetOut, bPreferAssetMaterials);
}

void UStaticMeshToolTarget::GetMaterialSet(const UStaticMesh* StaticMeshIn, 
	FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials)
{
	int32 NumMaterials = StaticMeshIn->GetStaticMaterials().Num();
	MaterialSetOut.Materials.SetNum(NumMaterials);
	for (int32 k = 0; k < NumMaterials; ++k)
	{
		MaterialSetOut.Materials[k] = StaticMeshIn->GetMaterial(k);
	}
}

bool UStaticMeshToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!ensure(IsValid())) return false;

	return CommitMaterialSetUpdate(StaticMesh, MaterialSet, bApplyToAsset);
}

bool UStaticMeshToolTarget::CommitMaterialSetUpdate(UStaticMesh* StaticMeshIn,
	const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!bApplyToAsset)
	{
		return false;
	}

	if (StaticMeshIn->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		UE_LOG(LogTemp, Warning, TEXT("CANNOT MODIFY BUILT-IN ENGINE ASSET %s"), *StaticMeshIn->GetPathName());
		return false;
	}

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

	// flush any pending rendering commands, which might touch this component while we are rebuilding its mesh
	FlushRenderingCommands();

	// make sure transactional flag is on
	StaticMeshIn->SetFlags(RF_Transactional);

	StaticMeshIn->Modify();

	int NewNumMaterials = FilteredMaterials.Num();
	if (NewNumMaterials != StaticMeshIn->GetStaticMaterials().Num())
	{
		StaticMeshIn->GetStaticMaterials().SetNum(NewNumMaterials);
	}
	for (int k = 0; k < NewNumMaterials; ++k)
	{
		if (StaticMeshIn->GetMaterial(k) != FilteredMaterials[k])
		{
			StaticMeshIn->SetMaterial(k, FilteredMaterials[k]);
		}
	}

	StaticMeshIn->PostEditChange();

	return true;
}

FMeshDescription* UStaticMeshToolTarget::GetMeshDescription()
{
	if (ensure(IsValid()))
	{
		return (EditingLOD == EStaticMeshEditingLOD::HiResSource) ?
			StaticMesh->GetHiResMeshDescription() : StaticMesh->GetMeshDescription((int32)EditingLOD);
	}
	return nullptr;
}

void UStaticMeshToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	if (ensure(IsValid()) == false) return;

	CommitMeshDescription(StaticMesh, GetMeshDescription(), Committer, EditingLOD);
}

void UStaticMeshToolTarget::CommitMeshDescription(UStaticMesh* StaticMeshIn, FMeshDescription* MeshDescription,
	const FCommitter& Committer, EStaticMeshEditingLOD EditingLODIn)
{
	using namespace StaticMeshToolTargetLocals;

	if (StaticMeshIn->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		DisplayCriticalWarningMessage(FString::Printf(TEXT("CANNOT MODIFY BUILT-IN ENGINE ASSET %s"), *StaticMeshIn->GetPathName()));
		return;
	}

	// flush any pending rendering commands, which might touch this component while we are rebuilding it's mesh
	FlushRenderingCommands();

	// make sure transactional flag is on for this asset
	StaticMeshIn->SetFlags(RF_Transactional);

	verify(StaticMeshIn->Modify());

	// disable auto-generated normals StaticMesh build setting
	UE::MeshDescription::FStaticMeshBuildSettingChange SettingsChange;
	SettingsChange.AutoGeneratedNormals = UE::MeshDescription::EBuildSettingBoolChange::Disable;
	UE::MeshDescription::ConfigureBuildSettings(StaticMeshIn, 0, SettingsChange);

	if (EditingLODIn == EStaticMeshEditingLOD::HiResSource)
	{
		verify(StaticMeshIn->ModifyHiResMeshDescription());
	}
	else
	{
		verify(StaticMeshIn->ModifyMeshDescription((int32)EditingLODIn));
	}

	FCommitterParams CommitterParams;
	CommitterParams.MeshDescriptionOut = MeshDescription;

	Committer(CommitterParams);

	if (EditingLODIn == EStaticMeshEditingLOD::HiResSource)
	{
		StaticMeshIn->CommitHiResMeshDescription();
	}
	else
	{
		StaticMeshIn->CommitMeshDescription((int32)EditingLODIn);
	}

	StaticMeshIn->PostEditChange();
}

TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> UStaticMeshToolTarget::GetDynamicMesh()
{
	return GetDynamicMeshViaMeshDescription(*this);
}

void UStaticMeshToolTarget::CommitDynamicMesh(const FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo& CommitInfo)
{
	CommitDynamicMeshViaMeshDescription(*this, Mesh, CommitInfo);
}

UStaticMesh* UStaticMeshToolTarget::GetStaticMesh() const
{
	return IsValid() ? StaticMesh : nullptr;
}


// Factory

bool UStaticMeshToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	const UStaticMesh* StaticMesh = Cast<UStaticMesh>(SourceObject);
	return StaticMesh && !StaticMesh->IsPendingKillOrUnreachable() && StaticMesh->IsValidLowLevel()
		&& (StaticMesh->GetNumSourceModels() > 0)
		&& Requirements.AreSatisfiedBy(UStaticMeshToolTarget::StaticClass());
}

UToolTarget* UStaticMeshToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UStaticMeshToolTarget* Target = NewObject<UStaticMeshToolTarget>();// TODO: Should we set an outer here?
	Target->StaticMesh = Cast<UStaticMesh>(SourceObject);
	Target->SetEditingLOD(EditingLOD);
	check(Target->StaticMesh && Requirements.AreSatisfiedBy(Target));

	return Target;
}


void UStaticMeshToolTargetFactory::SetActiveEditingLOD(EStaticMeshEditingLOD NewEditingLOD)
{
	EditingLOD = NewEditingLOD;
}