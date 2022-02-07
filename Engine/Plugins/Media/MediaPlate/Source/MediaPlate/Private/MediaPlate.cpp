// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaComponent.h"
#include "MediaPlayer.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

FLazyName AMediaPlate::MediaComponentName(TEXT("MediaComponent0"));
FLazyName AMediaPlate::MediaTextureName("MediaTexture");

AMediaPlate::AMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Set up media component.
	MediaComponent = CreateDefaultSubobject<UMediaComponent>(MediaComponentName);
	if (MediaComponent != nullptr)
	{
		// Set up media texture.
		UMediaTexture* MediaTexture = MediaComponent->GetMediaTexture();
		if (MediaTexture != nullptr)
		{
			MediaTexture->NewStyleOutput = true;
		}
	}

	// Set up static mesh component.
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent"));
	StaticMeshComponent->SetupAttachment(RootComponent);
	StaticMeshComponent->SetRelativeRotation(FRotator(0.0f, 90.0f, 90.0f));

	// Hook up mesh.
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Plane;
		FConstructorStatics()
			: Plane(TEXT("/Engine/BasicShapes/Plane"))
		{}
	};

	static FConstructorStatics ConstructorStatics;
	if (ConstructorStatics.Plane.Object != nullptr)
	{
		StaticMeshComponent->SetStaticMesh(ConstructorStatics.Plane.Object);
	}
}

void AMediaPlate::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (StaticMeshComponent != nullptr)
	{
		// Add material.
		UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MediaPlate/M_MediaPlate"));
		if (Material != nullptr)
		{
			UMaterialInstanceDynamic* MaterialInstance = StaticMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Material);
			if (MaterialInstance != nullptr)
			{
				MaterialInstance->SetTextureParameterValue(MediaTextureName, MediaComponent->GetMediaTexture());
			}
		}
	}
}

UMediaPlayer* AMediaPlate::GetMediaPlayer()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;
	if (MediaComponent != nullptr)
	{
		MediaPlayer = MediaComponent->GetMediaPlayer();
	}

	return MediaPlayer;
}

void AMediaPlate::Play()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->PlayOnOpen = true;
		MediaPlayer->OpenSource(MediaSource);
	}
}

void AMediaPlate::Stop()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
	}
}

#undef LOCTEXT_NAMESPACE
