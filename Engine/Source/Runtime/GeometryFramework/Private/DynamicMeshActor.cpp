// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshActor.h"
#include "Materials/Material.h"


#define LOCTEXT_NAMESPACE "ADynamicMeshActor"

ADynamicMeshActor::ADynamicMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
	DynamicMeshComponent->SetMobility(EComponentMobility::Movable);
	DynamicMeshComponent->SetGenerateOverlapEvents(false);
	DynamicMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	DynamicMeshComponent->CollisionType = ECollisionTraceFlag::CTF_UseDefault;

	DynamicMeshComponent->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));		// is this necessary?

	SetRootComponent(DynamicMeshComponent);

	//
	// Configure ADynamicMeshActor to always Tick(). This is necessary for the bIsEditorGeneratedMeshActor 
	// Tick-in-Editor support below to work w/o any other user intervention. Generally always Ticking is bad behavior, 
	// however DynamicMeshActor is inherently expensive and so we do not expect that large numbers of them
	// will be used in a performance-sensitive context.
	//
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
}


UDynamicMeshPool* ADynamicMeshActor::GetComputeMeshPool()
{
	if (DynamicMeshPool == nullptr && bEnableComputeMeshPool)
	{
		DynamicMeshPool = NewObject<UDynamicMeshPool>();
	}
	return DynamicMeshPool;
}


UDynamicMesh* ADynamicMeshActor::AllocateComputeMesh()
{
	if (bEnableComputeMeshPool)
	{
		UDynamicMeshPool* UsePool = GetComputeMeshPool();
		if (UsePool)
		{
			return UsePool->RequestMesh();
		}
	}

	// if we could not return a pool mesh, allocate a new mesh that isn't owned by the pool
	return NewObject<UDynamicMesh>(this);
}


bool ADynamicMeshActor::ReleaseComputeMesh(UDynamicMesh* Mesh)
{
	if (bEnableComputeMeshPool && Mesh)
	{
		UDynamicMeshPool* UsePool = GetComputeMeshPool();
		if (UsePool != nullptr)
		{
			UsePool->ReturnMesh(Mesh);
			return true;
		}
	}
	return false;
}


void ADynamicMeshActor::ReleaseAllComputeMeshes()
{
	UDynamicMeshPool* UsePool = GetComputeMeshPool();
	if (UsePool)
	{
		UsePool->ReturnAllMeshes();
	}
}

void ADynamicMeshActor::FreeAllComputeMeshes()
{
	UDynamicMeshPool* UsePool = GetComputeMeshPool();
	if (UsePool)
	{
		UsePool->FreeAllMeshes();
	}
}






void ADynamicMeshActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if WITH_EDITOR
	if (bIsEditorGeneratedMeshActor && bGeneratedMeshRebuildPending)
	{
		OnEditorRebuildGeneratedMesh();
		bGeneratedMeshRebuildPending = false;
	}
#endif
}



void ADynamicMeshActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	if (bIsEditorGeneratedMeshActor)
	{
		bGeneratedMeshRebuildPending = true;
	}
#endif
}


#if WITH_EDITOR
bool ADynamicMeshActor::ShouldTickIfViewportsOnly() const
{
	if (bIsEditorGeneratedMeshActor && GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor)
	{
		return true;
	}
	return false;
}
#endif




#undef LOCTEXT_NAMESPACE