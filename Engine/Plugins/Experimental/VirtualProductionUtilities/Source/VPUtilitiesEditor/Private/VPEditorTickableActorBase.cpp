// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VPEditorTickableActorBase.h"


AVPEditorTickableActorBase::AVPEditorTickableActorBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetActorTickEnabled(true);
	//we don't want virtual production objects to be visible by cameras
	SetActorHiddenInGame(true);
}


 bool AVPEditorTickableActorBase::ShouldTickIfViewportsOnly() const
{
	 return true;
}
 

 void AVPEditorTickableActorBase::Tick(float DeltaSeconds)
 {
	 Super::Tick(DeltaSeconds);

	 FEditorScriptExecutionGuard ScriptGuard;
	 EditorTick(DeltaSeconds);
 }

 void AVPEditorTickableActorBase::Destroyed()
 {
	 FEditorScriptExecutionGuard ScriptGuard;
	 EditorDestroyed();

	 Super::Destroyed();
 }


 void AVPEditorTickableActorBase::EditorTick_Implementation(float DeltaSeconds)
 {

 }

 void AVPEditorTickableActorBase::EditorDestroyed_Implementation()
 {

 }

 void AVPEditorTickableActorBase::LockLocation(bool bSetLockLocation) 
 {
	 bLockLocation = bSetLockLocation;
 }
 