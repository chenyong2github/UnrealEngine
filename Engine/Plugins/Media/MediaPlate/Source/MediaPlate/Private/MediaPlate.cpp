// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaComponent.h"
#include "MediaPlayer.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"
#include "MediaTextureTracker.h"
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
		// Do we have a material?
		if (StaticMeshComponent->GetNumOverrideMaterials() == 0)
		{
			// Add material.
			UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MediaPlate/M_MediaPlate"));
			if (Material != nullptr)
			{
				UMaterialInstanceDynamic* MaterialInstance = StaticMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Material);
			}
		}

		// Set up the material to point to our media texture.
		if (StaticMeshComponent->GetNumMaterials() > 0)
		{
			UMaterialInstanceDynamic* MaterialInstance = Cast< UMaterialInstanceDynamic>(StaticMeshComponent->GetMaterial(0));
			if (MaterialInstance != nullptr)
			{
				MaterialInstance->SetTextureParameterValue(MediaTextureName, MediaComponent->GetMediaTexture());
			}
		}
	}

	// Add our media texture to the tracker.
	RegisterWithMediaTextureTracker();
}

void AMediaPlate::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoPlay)
	{
		Play();
	}
}

void AMediaPlate::BeginDestroy()
{
	// Remove our media texture.
	UnregisterWithMediaTextureTracker();
	
	Super::BeginDestroy();
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

UMediaTexture* AMediaPlate::GetMediaTexture()
{
	TObjectPtr<UMediaTexture> MediaTexture = nullptr;
	if (MediaComponent != nullptr)
	{
		MediaTexture = MediaComponent->GetMediaTexture();
	}

	return MediaTexture;
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

void AMediaPlate::RegisterWithMediaTextureTracker()
{
	// Set up object.
	MediaTextureTrackerObject = MakeShared<FMediaTextureTrackerObject, ESPMode::ThreadSafe>();
	MediaTextureTrackerObject->Object = this;
	MediaTextureTrackerObject->MipMapLODBias = 0.0f;

	// Add our texture.
	TObjectPtr<UMediaTexture> MediaTexture = GetMediaTexture();
	if (MediaTexture != nullptr)
	{
		FMediaTextureTracker& MediaTextureTracker = FMediaTextureTracker::Get();
		MediaTextureTracker.RegisterTexture(MediaTextureTrackerObject, MediaTexture);
	}
}

void AMediaPlate::UnregisterWithMediaTextureTracker()
{
	// Remove out texture.
	if (MediaTextureTrackerObject != nullptr)
	{
		FMediaTextureTracker& MediaTextureTracker = FMediaTextureTracker::Get();
		TObjectPtr<UMediaTexture> MediaTexture = GetMediaTexture();
		MediaTextureTracker.UnregisterTexture(MediaTextureTrackerObject, MediaTexture);
	}
}

#undef LOCTEXT_NAMESPACE
