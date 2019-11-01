// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapTabletComponent.h"
#include "MagicLeapTabletPlugin.h"

UMagicLeapTabletComponent::UMagicLeapTabletComponent()
{
	bAutoActivate = true;
}

void UMagicLeapTabletComponent::BeginPlay()
{
	Super::BeginPlay();

	GetMagicLeapTabletPlugin().SetConnectedDelegate(OnConnected);
	GetMagicLeapTabletPlugin().SetDisconnectedDelegate(OnDisconnected);
	GetMagicLeapTabletPlugin().SetPenTouchDelegate(OnPenTouch);
	GetMagicLeapTabletPlugin().SetRingTouchDelegate(OnRingTouch);
	GetMagicLeapTabletPlugin().SetButtonDownDelegate(OnButtonDown);
	GetMagicLeapTabletPlugin().SetButtonUpDelegate(OnButtonUp);
}
