// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/DynamicMeshComponentToolTarget.h"

#include "Components/DynamicMeshComponent.h"
#include "ComponentReregisterContext.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "RenderingThread.h"
#include "Materials/Material.h"

#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "UDynamicMeshComponentToolTarget"


bool UDynamicMeshComponentToolTarget::IsValid() const
{
	if (!UPrimitiveComponentToolTarget::IsValid())
	{
		return false;
	}
	UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Component);
	if (DynamicMeshComponent == nullptr)
	{
		return false;
	}
	UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh();
	if (DynamicMesh == nullptr)
	{
		return false;
	}

	return true;
}


int32 UDynamicMeshComponentToolTarget::GetNumMaterials() const
{
	return ensure(IsValid()) ? Component->GetNumMaterials() : 0;
}

UMaterialInterface* UDynamicMeshComponentToolTarget::GetMaterial(int32 MaterialIndex) const
{
	return ensure(IsValid()) ? Component->GetMaterial(MaterialIndex) : nullptr;
}

void UDynamicMeshComponentToolTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const
{
	if (!ensure(IsValid())) return;

	int32 NumMaterials = Component->GetNumMaterials();
	MaterialSetOut.Materials.SetNum(NumMaterials);
	for (int32 k = 0; k < NumMaterials; ++k)
	{
		MaterialSetOut.Materials[k] = Component->GetMaterial(k);
	}
}

bool UDynamicMeshComponentToolTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	if (!ensure(IsValid())) return false;

	// filter out any Engine materials that we don't want to be permanently assigning
	TArray<UMaterialInterface*> FilteredMaterials = MaterialSet.Materials;
	for (int32 k = 0; k < FilteredMaterials.Num(); ++k)
	{
		FString AssetPath = FilteredMaterials[k]->GetPathName();
		if (AssetPath.StartsWith(TEXT("/MeshModelingToolsetExp/")))
		{
			FilteredMaterials[k] = UMaterial::GetDefaultMaterial(MD_Surface);
		}
	}

	UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Component);

	int32 NumMaterialsNeeded = Component->GetNumMaterials();
	int32 NumMaterialsGiven = FilteredMaterials.Num();

	DynamicMeshComponent->Modify();
	for (int32 k = 0; k < NumMaterialsGiven; ++k)
	{
		DynamicMeshComponent->SetMaterial(k, FilteredMaterials[k]);
	}

	return true;
}


const FMeshDescription* UDynamicMeshComponentToolTarget::GetMeshDescription()
{
	if (ensure(IsValid()))
	{
		if (bHaveMeshDescription)
		{
			return ConvertedMeshDescription.Get();
		}

		UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Component);

		ConvertedMeshDescription = MakeUnique<FMeshDescription>();
		FStaticMeshAttributes Attributes(*ConvertedMeshDescription);
		Attributes.Register();

		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(DynamicMeshComponent->GetMesh(), *ConvertedMeshDescription, true);

		bHaveMeshDescription = true;
		return ConvertedMeshDescription.Get();
	}
	return nullptr;
}


void UDynamicMeshComponentToolTarget::CommitMeshDescription(const FCommitter& Committer)
{
	if (ensure(IsValid()) == false) return;

	UDynamicMesh* DynamicMesh = GetDynamicMeshContainer();
	TUniquePtr<FDynamicMesh3> CurrentMesh = DynamicMesh->ExtractMesh();
	TSharedPtr<FDynamicMesh3> CurrentMeshShared(CurrentMesh.Release());

	FMeshDescription EditingMeshDescription(*GetMeshDescription());
	FCommitterParams CommitterParams;
	CommitterParams.MeshDescriptionOut = &EditingMeshDescription;
	Committer(CommitterParams);
	FMeshDescriptionToDynamicMesh Converter;
	TSharedPtr<FDynamicMesh3> NewMeshShared = MakeShared<FDynamicMesh3>();
	NewMeshShared->EnableAttributes();
	Converter.Convert(CommitterParams.MeshDescriptionOut, *NewMeshShared, true);

	DynamicMesh->EditMesh([&](FDynamicMesh3& EditMesh) { EditMesh = *NewMeshShared; });

	TUniquePtr<FMeshReplacementChange> ReplaceChange = MakeUnique<FMeshReplacementChange>(CurrentMeshShared, NewMeshShared);

	CommitDynamicMeshChange(MoveTemp(ReplaceChange), LOCTEXT("DynamicMeshComponentToolTargetCommit", "Update Mesh"));
}


UDynamicMesh* UDynamicMeshComponentToolTarget::GetDynamicMeshContainer()
{
	UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Component);
	return DynamicMeshComponent->GetDynamicMesh();
}

bool UDynamicMeshComponentToolTarget::HasDynamicMeshComponent() const
{
	return true;
}

UDynamicMeshComponent* UDynamicMeshComponentToolTarget::GetDynamicMeshComponent()
{
	return Cast<UDynamicMeshComponent>(Component);
}



void UDynamicMeshComponentToolTarget::CommitDynamicMeshChange(TUniquePtr<FToolCommandChange> Change, const FText& ChangeMessage)
{
	FScopedTransaction Transaction(ChangeMessage);
	check(GUndo != nullptr);
	UDynamicMesh* DynamicMesh = GetDynamicMeshContainer();
	DynamicMesh->Modify();
	GUndo->StoreUndo(DynamicMesh, MoveTemp(Change));
}





// Factory

bool UDynamicMeshComponentToolTargetFactory::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements) const
{
	UDynamicMeshComponent* Component = Cast<UDynamicMeshComponent>(SourceObject);
	return Component 
		&& !Component->IsPendingKillOrUnreachable() 
		&& Component->IsValidLowLevel() 
		&& Component->GetDynamicMesh()
		&& Requirements.AreSatisfiedBy(UDynamicMeshComponentToolTarget::StaticClass());
}

UToolTarget* UDynamicMeshComponentToolTargetFactory::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& Requirements)
{
	UDynamicMeshComponentToolTarget* Target = NewObject<UDynamicMeshComponentToolTarget>();// TODO: Should we set an outer here?
	Target->Component = Cast<UDynamicMeshComponent>(SourceObject);
	check(Target->Component && Requirements.AreSatisfiedBy(Target));

	return Target;
}


#undef LOCTEXT_NAMESPACE