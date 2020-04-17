// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/EditorComponentSourceFactory.h"

#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "ComponentReregisterContext.h"
#include "PhysicsEngine/BodySetup.h"


FMeshDescription* FStaticMeshComponentTarget::GetMesh() 
{
	return IsValid() ? Cast<UStaticMeshComponent>(Component)->GetStaticMesh()->GetMeshDescription(LODIndex) : nullptr;
}


void FStaticMeshComponentTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bAssetMaterials) const
{
	if (IsValid() == false)
	{
		return;
	}

	if (bAssetMaterials)
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
		FPrimitiveComponentTarget::GetMaterialSet(MaterialSetOut, false);
	}
}


void FStaticMeshComponentTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	// we only support this right now...
	check(bApplyToAsset == true);
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
	if (NewNumMaterials != StaticMesh->StaticMaterials.Num())
	{
		StaticMesh->StaticMaterials.SetNum(NewNumMaterials);
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


bool FStaticMeshComponentTarget::HasSameSourceData(const FPrimitiveComponentTarget& OtherTarget) const
{
	if (IsValid())
	{
		const UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();
		const UStaticMesh* OtherStaticMesh = Cast<UStaticMeshComponent>(OtherTarget.Component)->GetStaticMesh();
		return StaticMesh && StaticMesh == OtherStaticMesh;
	}
	else
	{
		return false;
	}
}

void FStaticMeshComponentTarget::CommitMesh( const FCommitter& Committer )
{
	check(IsValid());

	//bool bSaved = Component->Modify();
	//check(bSaved);
	UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();

	// flush any pending rendering commands, which might touch this component while we are rebuilding it's mesh
	FlushRenderingCommands();

	// unregister the component while we update it's static mesh
	TUniquePtr<FComponentReregisterContext> ComponentReregisterContext = MakeUnique<FComponentReregisterContext>(Component);

	// make sure transactional flag is on for this asset
	StaticMesh->SetFlags(RF_Transactional);

	bool bSavedToTransactionBuffer = StaticMesh->Modify();
	check(bSavedToTransactionBuffer);

	FCommitParams CommitParams;
	CommitParams.MeshDescription = StaticMesh->GetMeshDescription(LODIndex);

	Committer(CommitParams);

	StaticMesh->CommitMeshDescription(LODIndex);
	StaticMesh->PostEditChange();

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();

	//Component->PostEditChange();
}

bool FStaticMeshComponentTargetFactory::CanBuild(UActorComponent* Component)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	return StaticMeshComponent != nullptr;
}

TUniquePtr<FPrimitiveComponentTarget> FStaticMeshComponentTargetFactory::Build(UPrimitiveComponent* Component)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComponent != nullptr)
	{
		return TUniquePtr<FPrimitiveComponentTarget> { new FStaticMeshComponentTarget{Component} };
	}
	return {};
}
