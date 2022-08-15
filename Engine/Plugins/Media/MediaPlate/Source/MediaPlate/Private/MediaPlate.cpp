// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaPlateComponent.h"
#include "MediaPlateModule.h"
#include "MediaTexture.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

FLazyName AMediaPlate::MediaPlateComponentName(TEXT("MediaPlateComponent0"));
FLazyName AMediaPlate::MediaTextureName("MediaTexture");

AMediaPlate::AMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Set up media component.
	MediaPlateComponent = CreateDefaultSubobject<UMediaPlateComponent>(MediaPlateComponentName);

	// Set up static mesh component.
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent"));
	StaticMeshComponent->SetupAttachment(RootComponent);
	StaticMeshComponent->bCastStaticShadow = false;
	StaticMeshComponent->bCastDynamicShadow = false;
	MediaPlateComponent->StaticMeshComponent = StaticMeshComponent;

	// Hook up mesh.
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Plane;
		FConstructorStatics()
			: Plane(TEXT("/MediaPlate/SM_MediaPlateScreen"))
		{}
	};

	static FConstructorStatics ConstructorStatics;
	if (ConstructorStatics.Plane.Object != nullptr)
	{
		StaticMeshComponent->SetStaticMesh(ConstructorStatics.Plane.Object);
	}
}


void AMediaPlate::PostActorCreated()
{
	Super::PostActorCreated();

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Set which material to use.
		UseDefaultMaterial();
	}
#endif
}

void AMediaPlate::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (MediaPlateComponent != nullptr)
	{
		// Add our media texture to the tracker.
		MediaPlateComponent->RegisterWithMediaTextureTracker();
	}
}

void AMediaPlate::BeginDestroy()
{
	if (MediaPlateComponent != nullptr)
	{
		// Remove our media texture.
		MediaPlateComponent->UnregisterWithMediaTextureTracker();
	}
	
	Super::BeginDestroy();
}

#if WITH_EDITOR

void AMediaPlate::UseDefaultMaterial()
{
	UMaterial* DefaultMaterial = LoadObject<UMaterial>(NULL, TEXT("/MediaPlate/M_MediaPlate"), NULL, LOAD_None, NULL);
	
	ApplyMaterial(DefaultMaterial);
}

void AMediaPlate::ApplyMaterial(UMaterialInterface* Material)
{
	if (Material != nullptr && StaticMeshComponent != nullptr)
	{
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material);
		UMaterialInterface* Result = nullptr;

		if (MID != nullptr)
		{
			MID->SetTextureParameterValue(MediaTextureName, MediaPlateComponent->GetMediaTexture());

			Result = MID;
		}
		else
		{
			// Change M_ to MI_ in material name and then generate a unique one.
			FString MaterialName = Material->GetName();
			if (MaterialName.StartsWith(TEXT("M_")))
			{
				MaterialName.InsertAt(1, TEXT("I"));
			}
			FName MaterialUniqueName = MakeUniqueObjectName(StaticMeshComponent, UMaterialInstanceConstant::StaticClass(),
				FName(*MaterialName));

			// Create instance.
			UMaterialInstanceConstant* MaterialInstance =
				NewObject<UMaterialInstanceConstant>(StaticMeshComponent, MaterialUniqueName);
			MaterialInstance->SetParentEditorOnly(Material);
			MaterialInstance->CopyMaterialUniformParametersEditorOnly(Material);
			MaterialInstance->SetTextureParameterValueEditorOnly(
				FMaterialParameterInfo(MediaTextureName),
				MediaPlateComponent->GetMediaTexture());
			MaterialInstance->PostEditChange();

			// We force call post-load to indirectly call UpdateParameters() (for integration with VPUtilities plugin).
			MaterialInstance->PostLoad();

			Result = MaterialInstance;
		}

		// Update static mesh.
		if (Result != nullptr)
		{
			StaticMeshComponent->Modify();
			StaticMeshComponent->SetMaterial(0, Result);

			LastMaterial = Result;
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE
