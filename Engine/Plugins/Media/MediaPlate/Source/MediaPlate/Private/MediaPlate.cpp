// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MediaPlayer.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

AMediaPlate::AMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create player.
	MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UMediaPlayer::StaticClass()));

	// Create texture.
	MediaTexture = NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient | RF_Public);
	if (MediaTexture != nullptr)
	{
		MediaTexture->AutoClear = true;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	// Hook up mesh.
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Plane;
		FConstructorStatics()
			: Plane(TEXT("/Engine/BasicShapes/Plane")) {}
	};

	static FConstructorStatics ConstructorStatics;
	if (ConstructorStatics.Plane.Object != nullptr)
	{
		UStaticMeshComponent* LocalStaticMeshComponent = GetStaticMeshComponent();
		check(LocalStaticMeshComponent);
		LocalStaticMeshComponent->SetStaticMesh(ConstructorStatics.Plane.Object);
	}
}


#undef LOCTEXT_NAMESPACE
