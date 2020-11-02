// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterIconHelper.h"

#if WITH_EDITOR

#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "WaterRuntimeSettings.h"

UBillboardComponent* FWaterIconHelper::EnsureSpriteComponentCreated(AActor* Actor, const TCHAR* InIconTextureName, const FText& InDisplayName)
{
	UBillboardComponent* ActorIcon = nullptr;
	if (!Actor->IsTemplate())
	{
		ActorIcon = Actor->FindComponentByClass<UBillboardComponent>();
		if (ActorIcon == nullptr)
		{
			ActorIcon = Actor->CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"), true);
		}

		if (ActorIcon != nullptr)
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> Texture(InIconTextureName);
			ActorIcon->Sprite = Texture.Get();
			ActorIcon->bHiddenInGame = true;
			ActorIcon->SpriteInfo.Category = TEXT("Water");
			ActorIcon->SpriteInfo.DisplayName = InDisplayName;
			ActorIcon->SetupAttachment(Actor->GetRootComponent());
			UpdateSpriteComponent(Actor, ActorIcon->Sprite);
		}
	}
	return ActorIcon;
}

void FWaterIconHelper::UpdateSpriteComponent(AActor* Actor, UTexture2D* InTexture)
{
	if (UBillboardComponent* ActorIcon = Actor->FindComponentByClass<UBillboardComponent>())
	{
		float TargetSize = GetDefault<UWaterRuntimeSettings>()->WaterBodyIconWorldSize;
		FVector ZOffset(0.0f, 0.0f, GetDefault<UWaterRuntimeSettings>()->WaterBodyIconWorldZOffset);
		if (InTexture != nullptr)
		{
			int32 TextureSize = FMath::Max(InTexture->GetSizeX(), InTexture->GetSizeY());
			float Scale = (float)TargetSize / TextureSize;
			ActorIcon->SetRelativeScale3D(FVector((TextureSize > 0) ? Scale : 1.0f));
		}
		ActorIcon->Sprite = InTexture;
		ActorIcon->SetRelativeLocation(ZOffset);
		ActorIcon->bIsScreenSizeScaled = true;
		ActorIcon->MarkRenderStateDirty();
	}
}

#endif