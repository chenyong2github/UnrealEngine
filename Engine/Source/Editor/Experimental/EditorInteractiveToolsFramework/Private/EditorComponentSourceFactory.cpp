// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorComponentSourceFactory.h"

#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"


namespace {
FMeshDescription *
GetMeshDescription(UStaticMeshComponent* Component, int LODIndex) {
	return Component->GetStaticMesh()->GetMeshDescription(LODIndex);
}

void
CommitInPlaceModification(UStaticMeshComponent* Component, int LODIndex,
						  const TFunction<void(FMeshDescription*)>& ModifyFunction)
{
	//bool bSaved = Component->Modify();
	//check(bSaved);
	UStaticMesh* StaticMesh = Component->GetStaticMesh();

	// make sure transactional flag is on
	StaticMesh->SetFlags(RF_Transactional);

	bool bSavedToTransactionBuffer = StaticMesh->Modify();
	check(bSavedToTransactionBuffer);
	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);

	ModifyFunction(MeshDescription);

	StaticMesh->CommitMeshDescription(LODIndex);
	StaticMesh->PostEditChange();

	// this rebuilds physics, but it doesn't undo!
	Component->RecreatePhysicsState();

	//Component->PostEditChange();
}
} // namespace

FMeshDescriptionBridge
MakeStaticMeshDescriptionBridge(UPrimitiveComponent* Component)
{
	const int LODIndex{0};
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComponent != nullptr)
	{
		return {
			[StaticMeshComponent, LODIndex]()
			{
				return GetMeshDescription(StaticMeshComponent, LODIndex);
			},
			[StaticMeshComponent, LODIndex](const FMeshDescriptionBridge::FCommitter& Committer)
			{
				CommitInPlaceModification(StaticMeshComponent, LODIndex, Committer);
			}
		};
	}
	return FMeshDescriptionBridge{};
}
