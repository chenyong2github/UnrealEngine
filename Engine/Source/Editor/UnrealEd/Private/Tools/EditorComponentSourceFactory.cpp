// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tools/EditorComponentSourceFactory.h"

#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"


FMeshDescription *
FStaticMeshComponentTarget::GetMesh() {
	return Cast<UStaticMeshComponent>(Component)->GetStaticMesh()->GetMeshDescription(LODIndex);
}

void
FStaticMeshComponentTarget::CommitMesh( const FCommitter& Committer )
{
	//bool bSaved = Component->Modify();
	//check(bSaved);
	UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();

	// make sure transactional flag is on
	StaticMesh->SetFlags(RF_Transactional);

	bool bSavedToTransactionBuffer = StaticMesh->Modify();
	check(bSavedToTransactionBuffer);
	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);

	Committer(MeshDescription);

	StaticMesh->CommitMeshDescription(LODIndex);
	StaticMesh->PostEditChange();

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();

	//Component->PostEditChange();
}

bool
FStaticMeshComponentTargetFactory::CanBuild(UActorComponent* Component)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	return StaticMeshComponent != nullptr;
}

TUniquePtr< FPrimitiveComponentTarget >
FStaticMeshComponentTargetFactory::Build(UPrimitiveComponent* Component)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComponent != nullptr)
	{
		return TUniquePtr< FPrimitiveComponentTarget > { new FStaticMeshComponentTarget{Component} };
	}
	return {};
}
