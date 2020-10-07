// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/ConstructorHelpers.h"

class AActor;
class UTexture2D;
class UBillboardComponent;

// Structure to hold one-time initialization
struct FWaterIconHelper
{
	FWaterIconHelper(const TCHAR* IconTextureName);
	UTexture2D* GetTexture();
	const FName& GetCategoryName() const { return ID_CategoryName; }
	const FText& GetDisplayName() const { return NAME_DisplayName; }

	static UBillboardComponent* CreateSprite(AActor* Actor, UTexture2D* InTexture, const FName& InCategoryName, const FText& InDisplayName);
	static void UpdateSpriteTexture(AActor* Actor, UTexture2D* InTexture);

private:
	ConstructorHelpers::FObjectFinderOptional<UTexture2D> Texture;
	FName ID_CategoryName;
	FText NAME_DisplayName;
};

#endif