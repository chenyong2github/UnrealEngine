// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterIconHelper.h"

#if WITH_EDITOR

#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "WaterRuntimeSettings.h"

FWaterIconHelper::FWaterIconHelper(const TCHAR* IconTextureName)
: Texture(IconTextureName)
, ID_CategoryName(TEXT("Water"))
, NAME_DisplayName(NSLOCTEXT("Water", "WaterSpriteName", "Water"))
{
}

UTexture2D* FWaterIconHelper::GetTexture()
{
	return Texture.Get();
}

void FWaterIconHelper::UpdateSpriteTexture(AActor* Actor, UTexture2D* InTexture)
{
	if (InTexture)
	{
		if (UBillboardComponent* ActorIcon = Cast<UBillboardComponent>(Actor->GetComponentByClass(UBillboardComponent::StaticClass())))
		{
			float TargetSize = GetDefault<UWaterRuntimeSettings>()->WaterBodyIconWorldSize;
			FVector ZOffset(0.0f, 0.0f, GetDefault<UWaterRuntimeSettings>()->WaterBodyIconWorldZOffset);
			int32 TextureSize = FMath::Max(InTexture->GetSizeX(), InTexture->GetSizeY());
			float Scale = (float)TargetSize / TextureSize;
			ActorIcon->Sprite = InTexture;
			ActorIcon->SetRelativeLocation(ZOffset);
			ActorIcon->SetRelativeScale3D(FVector(Scale));
			ActorIcon->bIsScreenSizeScaled = true;
			ActorIcon->MarkRenderStateDirty();
			FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepRelative, false);
			ActorIcon->ConvertAttachLocation(EAttachLocation::KeepRelativeOffset, AttachmentRules.LocationRule, AttachmentRules.RotationRule, AttachmentRules.ScaleRule);
			ActorIcon->AttachToComponent(Actor->GetRootComponent(), AttachmentRules);
		}
	}
}

UBillboardComponent* FWaterIconHelper::CreateSprite(AActor* Actor, UTexture2D* InTexture, const FName& InCategoryName, const FText& InDisplayName)
{
	UBillboardComponent* ActorIcon = Actor->CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (ActorIcon)
	{
		ActorIcon->bHiddenInGame = true;
		ActorIcon->SpriteInfo.Category = InCategoryName;
		ActorIcon->SpriteInfo.DisplayName = InDisplayName;
		FWaterIconHelper::UpdateSpriteTexture(Actor, InTexture);
	}
	return ActorIcon;
}

#endif