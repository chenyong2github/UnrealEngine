// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/ConstructorHelpers.h"

class AActor;
class UTexture2D;
class UBillboardComponent;

struct FWaterIconHelper
{
	/** Ensures a billboard component is created and added to the actor's components. 
	 * This is meant to be called in the constructor only (because of ConstructorHelpers::FObjectFinderOptional).
	*/
	static UBillboardComponent* EnsureSpriteComponentCreated(AActor* Actor, const TCHAR* InIconTextureName, const FText& InDisplayName);

	/** Updates the texture/scale/position of the actor's billboard component, if any */
	static void UpdateSpriteComponent(AActor* Actor, UTexture2D* InTexture);
};

#endif