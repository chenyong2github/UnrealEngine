// Copyright Epic Games, Inc. All Rights Reserved.

#include "Atmosphere/AtmosphericFog.h"
#include "Atmosphere/AtmosphericFogComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineDefines.h"
#include "RenderingThread.h"
#include "Components/ArrowComponent.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "ComponentReregisterContext.h"
#include "Atmosphere/AtmosphericFog.h"
#include "Components/BillboardComponent.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/AtmosphereRendering.h"

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

AAtmosphericFog::AAtmosphericFog(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AtmosphericFogComponent = CreateDefaultSubobject<UAtmosphericFogComponent>(TEXT("AtmosphericFogComponent0"));
	RootComponent = AtmosphericFogComponent;

#if WITH_EDITORONLY_DATA
	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent0"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> FogTextureObject;
			FName ID_Fog;
			FText NAME_Fog;
			FConstructorStatics()
				: FogTextureObject(TEXT("/Engine/EditorResources/S_ExpoHeightFog"))
				, ID_Fog(TEXT("Fog"))
				, NAME_Fog(NSLOCTEXT("SpriteCategory", "Fog", "Fog"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.FogTextureObject.Get();
			GetSpriteComponent()->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_Fog;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_Fog;
			GetSpriteComponent()->SetupAttachment(AtmosphericFogComponent);
		}

		if (ArrowComponent)
		{
			ArrowComponent->ArrowColor = FColor(150, 200, 255);

			ArrowComponent->bTreatAsASprite = true;
			ArrowComponent->SpriteInfo.Category = ConstructorStatics.ID_Fog;
			ArrowComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Fog;
			ArrowComponent->SetupAttachment(AtmosphericFogComponent);
			ArrowComponent->bLightAttachment = true;
			ArrowComponent->bIsScreenSizeScaled = true;
		}
	}
#endif // WITH_EDITORONLY_DATA


	PrimaryActorTick.bCanEverTick = true;
	SetHidden(false);
}

#if WITH_EDITOR
// Prepare render targets when new actor spawned
void AAtmosphericFog::PostActorCreated()
{
	Super::PostActorCreated();
	if (GIsEditor)
	{
		if ( !IsTemplate() && AtmosphericFogComponent )
		{
			AtmosphericFogComponent->InitResource();
		}
	}
}

#endif
