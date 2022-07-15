// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegion.h"
#include "ColorCorrectRegionsSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Engine/Classes/Components/MeshComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

AColorCorrectRegion::AColorCorrectRegion(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
	, Type(EColorCorrectRegionsType::Sphere)
	, Priority(0)
	, Intensity(1.0)
	, Inner(0.5)
	, Outer(1.0)
	, Falloff(1.0)
	, Invert(false)
	, TemperatureType(EColorCorrectRegionTemperatureType::WhiteBalance)
	, Temperature(6500)
	, Tint(0)
	, Enabled(true)
	, ExcludeStencil(false)
	, ColorCorrectRegionsSubsystem(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;

	// Add a scene component as our root
	RootComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

#if WITH_EDITOR

	// Create billboard component

	if (GIsEditor && !IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
	
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SpriteTextureObject;
			FName ID_ColorCorrectRegion;
			FText NAME_ColorCorrectRegion;

			FConstructorStatics()
				: SpriteTextureObject(TEXT("/ColorCorrectRegions/Icons/S_ColorCorrectRegionIcon"))
				, ID_ColorCorrectRegion(TEXT("Color Correct Region"))
				, NAME_ColorCorrectRegion(NSLOCTEXT("SpriteCategory", "ColorCorrectRegion", "Color Correct Region"))
			{
			}
		};

		static FConstructorStatics ConstructorStatics;

		SpriteComponent = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UBillboardComponent>(this, TEXT("Color Correct Region Icon"));

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.SpriteTextureObject.Get();
			SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_ColorCorrectRegion;
			SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_ColorCorrectRegion;
			SpriteComponent->SetIsVisualizationComponent(true);
			SpriteComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			SpriteComponent->SetMobility(EComponentMobility::Movable);
			SpriteComponent->bHiddenInGame = true;
			SpriteComponent->bIsScreenSizeScaled = true;

			SpriteComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

#endif // WITH_EDITOR
}

void AColorCorrectRegion::BeginPlay()
{	
	Super::BeginPlay();
	if (const UWorld* World = GetWorld())
	{
		ColorCorrectRegionsSubsystem = World->GetSubsystem<UColorCorrectRegionsSubsystem>();
	}

	if (ColorCorrectRegionsSubsystem)
	{
		ColorCorrectRegionsSubsystem->OnActorSpawned(this);
	}
}

void AColorCorrectRegion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ColorCorrectRegionsSubsystem)
	{
		ColorCorrectRegionsSubsystem->OnActorDeleted(this);
		ColorCorrectRegionsSubsystem = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AColorCorrectRegion::BeginDestroy()
{
	if (ColorCorrectRegionsSubsystem)
	{
		ColorCorrectRegionsSubsystem->OnActorDeleted(this);
		ColorCorrectRegionsSubsystem = nullptr;
	}
	
	Super::BeginDestroy();
}

bool AColorCorrectRegion::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AColorCorrectRegion::TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::Tick(DeltaTime);
	FTransform CurrentFrameTransform = GetTransform();
	if (!PreviousFrameTransform.Equals(CurrentFrameTransform))
	{
		PreviousFrameTransform = CurrentFrameTransform;
		GetActorBounds(true, BoxOrigin, BoxExtent);
	}

	// Display Cluster uses HiddenPrimitives to hide Primitive components from view. 
	// Store component id to be used on render thread.
	if (const UStaticMeshComponent* FirstMeshComponent = FindComponentByClass<UStaticMeshComponent>())
	{
		if (!(FirstPrimitiveId == FirstMeshComponent->ComponentId))
		{
			FirstPrimitiveId = FirstMeshComponent->ComponentId;
		}
	}
}

void AColorCorrectRegion::Cleanup()
{
	ColorCorrectRegionsSubsystem = nullptr;
}

#if WITH_EDITOR
void AColorCorrectRegion::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (!ColorCorrectRegionsSubsystem)
	{
		if (const UWorld* World = GetWorld())
		{
			ColorCorrectRegionsSubsystem = World->GetSubsystem<UColorCorrectRegionsSubsystem>();
		}
	}

	// Reorder all CCRs after the Priority property has changed.
	// Also, in context of Multi-User: PropertyChangedEvent can be a stub without the actual property data. 
	// Therefore we need to refresh priority if PropertyChangedEvent.Property is nullptr. 
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AColorCorrectRegion, Priority) || PropertyChangedEvent.Property == nullptr)
	{
		if (ColorCorrectRegionsSubsystem)
		{
			ColorCorrectRegionsSubsystem->SortRegionsByPriority();
		}
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AColorCorrectRegion, Type) || PropertyChangedEvent.Property == nullptr)
	{
		if (ColorCorrectRegionsSubsystem)
		{
			ColorCorrectRegionsSubsystem->OnLevelsChanged();
		}
	}
	GetActorBounds(true, BoxOrigin, BoxExtent);
}
#endif //WITH_EDITOR

