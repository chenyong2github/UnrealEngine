// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterOperatorViewModel.h"

#include "DisplayClusterRootActor.h"

#include "Framework/Docking/TabManager.h"

void FDisplayClusterOperatorViewModel::SetRootActor(ADisplayClusterRootActor* InRootActor)
{
	RootActor = InRootActor;
	RootActorChanged.Broadcast(RootActor.Get());
}

TSharedRef<FTabManager> FDisplayClusterOperatorViewModel::CreateTabManager(const TSharedRef<SDockTab>& MajorTabOwner)
{
	TabManager = FGlobalTabmanager::Get()->NewTabManager(MajorTabOwner);
	return TabManager.ToSharedRef();
}

void FDisplayClusterOperatorViewModel::ResetTabManager()
{
	TabManager.Reset();
}