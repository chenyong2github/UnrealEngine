// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapTabletFunctionLibrary.h"
#include "MagicLeapTabletPlugin.h"

void UMagicLeapTabletFunctionLibrary::SetTabletConnectedDelegate(const FMagicLeapTabletOnConnectedDelegate& InConnectedDelegate)
{
	FMagicLeapTabletOnConnectedDelegateMulti ConnectedDelegate;
	ConnectedDelegate.Add(InConnectedDelegate);
	GetMagicLeapTabletPlugin().SetConnectedDelegate(ConnectedDelegate);
}

void UMagicLeapTabletFunctionLibrary::SetTabletDisconnectedDelegate(const FMagicLeapTabletOnDisconnectedDelegate& InDisconnectedDelegate)
{
	FMagicLeapTabletOnDisconnectedDelegateMulti DisconnectedDelegate;
	DisconnectedDelegate.Add(InDisconnectedDelegate);
	GetMagicLeapTabletPlugin().SetDisconnectedDelegate(DisconnectedDelegate);
}

void UMagicLeapTabletFunctionLibrary::SetPenTouchDelegate(const FMagicLeapTabletOnPenTouchDelegate& InPenTouchDelegate)
{
	FMagicLeapTabletOnPenTouchDelegateMulti PenTouchDelegate;
	PenTouchDelegate.Add(InPenTouchDelegate);
	GetMagicLeapTabletPlugin().SetPenTouchDelegate(PenTouchDelegate);
}

void UMagicLeapTabletFunctionLibrary::SetRingTouchDelegate(const FMagicLeapTabletOnRingTouchDelegate& InRingTouchDelegate)
{
	FMagicLeapTabletOnRingTouchDelegateMulti RingTouchDelegate;
	RingTouchDelegate.Add(InRingTouchDelegate);
	GetMagicLeapTabletPlugin().SetRingTouchDelegate(RingTouchDelegate);
}

void UMagicLeapTabletFunctionLibrary::SetButtonDownDelegate(const FMagicLeapTabletOnButtonDownDelegate& InButtonDownDelegate)
{
	FMagicLeapTabletOnButtonDownDelegateMulti ButtonDownDelegate;
	ButtonDownDelegate.Add(InButtonDownDelegate);
	GetMagicLeapTabletPlugin().SetButtonDownDelegate(ButtonDownDelegate);
}

void UMagicLeapTabletFunctionLibrary::SetButtonUpDelegate(const FMagicLeapTabletOnButtonUpDelegate& InButtonUpDelegate)
{
	FMagicLeapTabletOnButtonUpDelegateMulti ButtonUpDelegate;
	ButtonUpDelegate.Add(InButtonUpDelegate);
	GetMagicLeapTabletPlugin().SetButtonUpDelegate(ButtonUpDelegate);
}
