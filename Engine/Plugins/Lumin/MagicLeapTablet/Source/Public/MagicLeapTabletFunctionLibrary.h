// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapTabletTypes.h"
#include "MagicLeapTabletFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPTABLET_API UMagicLeapTabletFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Tablet Function Library | MagicLeap")
	static void SetTabletConnectedDelegate(const FMagicLeapTabletOnConnectedDelegate& ConnectedDelegate);

	UFUNCTION(BlueprintCallable, Category = "Tablet Function Library | MagicLeap")
	static void SetTabletDisconnectedDelegate(const FMagicLeapTabletOnDisconnectedDelegate& DisconnectedDelegate);

	UFUNCTION(BlueprintCallable, Category = "Tablet Function Library | MagicLeap")
	static void SetPenTouchDelegate(const FMagicLeapTabletOnPenTouchDelegate& PenTouchDelegate);

	UFUNCTION(BlueprintCallable, Category = "Tablet Function Library | MagicLeap")
	static void SetRingTouchDelegate(const FMagicLeapTabletOnRingTouchDelegate& RingTouchDelegate);

	UFUNCTION(BlueprintCallable, Category = "Tablet Function Library | MagicLeap")
	static void SetButtonDownDelegate(const FMagicLeapTabletOnButtonDownDelegate& ButtonDownDelegate);

	UFUNCTION(BlueprintCallable, Category = "Tablet Function Library | MagicLeap")
	static void SetButtonUpDelegate(const FMagicLeapTabletOnButtonUpDelegate& ButtonDownDelegate);
};
