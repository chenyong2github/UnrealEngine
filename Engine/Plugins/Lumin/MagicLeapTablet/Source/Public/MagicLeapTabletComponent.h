// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapTabletTypes.h"
#include "MagicLeapTabletComponent.generated.h"

/**
	Component that provides access to the Tablet API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPTABLET_API UMagicLeapTabletComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMagicLeapTabletComponent();

	/** Passes the component delegates to the underlying tablet plugin. */
	virtual void BeginPlay() override;

private:
	// Delegate instances
	UPROPERTY(BlueprintAssignable, Category = "Tablet | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapTabletOnConnectedDelegateMulti OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "Tablet | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapTabletOnDisconnectedDelegateMulti OnDisconnected;

	UPROPERTY(BlueprintAssignable, Category = "Tablet | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapTabletOnPenTouchDelegateMulti OnPenTouch;

	UPROPERTY(BlueprintAssignable, Category = "Tablet | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapTabletOnRingTouchDelegateMulti OnRingTouch;

	UPROPERTY(BlueprintAssignable, Category = "Tablet | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapTabletOnButtonDownDelegateMulti OnButtonDown;

	UPROPERTY(BlueprintAssignable, Category = "Tablet | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapTabletOnButtonUpDelegateMulti OnButtonUp;
};
