// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterSceneComponentSyncThis.h"

#include "GameFramework/Actor.h"


UDisplayClusterSceneComponentSyncThis::UDisplayClusterSceneComponentSyncThis(const FObjectInitializer& ObjectInitializer) :
	UDisplayClusterSceneComponentSync(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDisplayClusterSceneComponentSyncThis::BeginPlay()
{
	Super::BeginPlay();

	// ...
}


void UDisplayClusterSceneComponentSyncThis::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent( DeltaTime, TickType, ThisTickFunction );

	// ...
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClusterSyncObject
//////////////////////////////////////////////////////////////////////////////////////////////
FString UDisplayClusterSceneComponentSyncThis::GetSyncId() const
{
	return FString::Printf(TEXT("ST_%s"), *GetOwner()->GetName());
}

bool UDisplayClusterSceneComponentSyncThis::IsDirty() const
{
	return (LastSyncLoc != GetRelativeLocation() || LastSyncRot != GetRelativeRotation() || LastSyncScale != GetRelativeScale3D());
}

void UDisplayClusterSceneComponentSyncThis::ClearDirty()
{
	LastSyncLoc = GetRelativeLocation();
	LastSyncRot = GetRelativeRotation();
	LastSyncScale = GetRelativeScale3D();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterSceneComponentSync
//////////////////////////////////////////////////////////////////////////////////////////////
FTransform UDisplayClusterSceneComponentSyncThis::GetSyncTransform() const
{
	return GetRelativeTransform();
}

void UDisplayClusterSceneComponentSyncThis::SetSyncTransform(const FTransform& t)
{
	SetRelativeTransform(t);
}
