// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "EnhancedInputPlayerController.generated.h"

/**
 * Extending your own player controller from AEnhancedInputPlayerController will enable enhanced input functionality for locally controlled players.
 */
UCLASS(config = Game, BlueprintType, Blueprintable, meta = (ShortTooltip = "A Player Controller with enhanced input functionality."))
class ENHANCEDINPUT_API AEnhancedInputPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void InitInputSystem() override;
	virtual void SetupInputComponent() override;

};