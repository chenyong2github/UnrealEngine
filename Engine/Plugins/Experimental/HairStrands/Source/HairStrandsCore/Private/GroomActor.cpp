// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "GroomActor.h"
#include "GroomComponent.h"
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "HairStrands"

AGroomActor::AGroomActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GroomComponent = CreateDefaultSubobject<UGroomComponent>(TEXT("GroomComponent0"));
	RootComponent = GroomComponent;

#if WITH_EDITOR
	struct FConstructorStatics
	{
		FName ID_Sprite;
		FText NAME_Sprite;
		FColor SceneBaseColor;
		FVector SceneBaseSize;
		FConstructorStatics()
			: ID_Sprite(TEXT("GroomActor"))
			, NAME_Sprite(LOCTEXT("RootSpriteInfo", "Groom Actor"))
			, SceneBaseColor(100, 255, 255, 255)
			, SceneBaseSize(600.0f, 600.0f, 400.0f)
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> RootTextureObject = TEXT("/HairStrands/Icons/S_GroomActor");
		
		SpriteComponent->Sprite = RootTextureObject.Get();
		SpriteComponent->SetRelativeScale3D_Direct(FVector(1.0f, 1.0f, 1.0f));
		SpriteComponent->bHiddenInGame = false;
		SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Sprite;
		SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Sprite;
		SpriteComponent->bIsScreenSizeScaled = true;
		SpriteComponent->SetupAttachment(RootComponent);
	}
#endif
}

#undef LOCTEXT_NAMESPACE




