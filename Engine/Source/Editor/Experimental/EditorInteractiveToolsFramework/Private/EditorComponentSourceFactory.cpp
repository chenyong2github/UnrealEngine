// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorComponentSourceFactory.h"

#include "Engine/StaticMesh.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"


TUniquePtr<IMeshDescriptionSource> FEditorComponentSourceFactory::MakeMeshDescriptionSource(UActorComponent* Component)
{
	UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComp != nullptr)
	{
		auto NewSource = new FStaticMeshComponentMeshDescriptionSource(StaticMeshComp, 0);
		return TUniquePtr<IMeshDescriptionSource>(NewSource);
	}
	return nullptr;
}





FStaticMeshComponentMeshDescriptionSource::FStaticMeshComponentMeshDescriptionSource(
	UStaticMeshComponent* ComponentIn, int LODIndex)
{
	this->Component = ComponentIn;
	this->LODIndex = LODIndex;
}


AActor* FStaticMeshComponentMeshDescriptionSource::GetOwnerActor() const
{
	return Component->GetOwner();
}

UActorComponent* FStaticMeshComponentMeshDescriptionSource::GetOwnerComponent() const
{
	return Component;
}


void FStaticMeshComponentMeshDescriptionSource::SetOwnerVisibility(bool bVisible) const
{
	Component->SetVisibility(bVisible);
}

FMeshDescription* FStaticMeshComponentMeshDescriptionSource::GetMeshDescription() const
{
	return Component->GetStaticMesh()->GetMeshDescription(0);
}

UMaterialInterface* FStaticMeshComponentMeshDescriptionSource::GetMaterial(int32 MaterialIndex) const
{
	return Component->GetMaterial(MaterialIndex);
}

FTransform FStaticMeshComponentMeshDescriptionSource::GetWorldTransform() const
{
	//return Component->GetOwner()->GetActorTransform();
	return Component->GetComponentTransform();
}



bool FStaticMeshComponentMeshDescriptionSource::IsReadOnly() const
{
	return false;
}


void FStaticMeshComponentMeshDescriptionSource::CommitInPlaceModification(const TFunction<void(FMeshDescription*)>& ModifyFunction) 
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


bool FStaticMeshComponentMeshDescriptionSource::HitTest(const FRay& WorldRay, FHitResult& OutHit) const
{
	FVector End = WorldRay.PointAt(HALF_WORLD_MAX);
	if (Component->LineTraceComponent(OutHit, WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
	{
		return true;
	}

	return false;
}