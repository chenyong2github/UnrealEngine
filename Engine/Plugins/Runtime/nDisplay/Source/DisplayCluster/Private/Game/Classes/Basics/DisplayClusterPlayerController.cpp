// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPlayerController.h"
#include "Misc/DisplayClusterAppExit.h"


void ADisplayClusterPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (WasInputKeyJustPressed(EKeys::Escape))
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, FString("Exit on ESC requested"));
	}
}

void ADisplayClusterPlayerController::BeginPlay()
{
	Super::BeginPlay();
}
