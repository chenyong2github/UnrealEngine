// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegion.h"
#include "ColorCorrectRegionsSubsystem.h"
#include "Engine/Classes/Components/MeshComponent.h"
#include "CoreMinimal.h"
#include "UObject/ConstructorHelpers.h"

AColorCorrectRegion::AColorCorrectRegion(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
	, Type(EColorCorrectRegionsType::Sphere)
	, Priority(0)
	, Intensity(1.0)
	, Inner(0.5)
	, Outer(1.0)
	, Falloff(1.0)
	, Invert(false)
	, Temperature(6500)
	, Enabled(true)
	, ExcludeStencil(false)
	, ColorCorrectRegionsSubsystem(nullptr)
{

}

void AColorCorrectRegion::BeginPlay()
{	
	Super::BeginPlay();
	if (this->GetWorld())
	{
		ColorCorrectRegionsSubsystem = Cast<UColorCorrectRegionsSubsystem>(this->GetWorld()->GetSubsystemBase(UColorCorrectRegionsSubsystem::StaticClass()));
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

void AColorCorrectRegion::Cleanup()
{
	ColorCorrectRegionsSubsystem = nullptr;
}

#if WITH_EDITOR
void AColorCorrectRegion::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AColorCorrectRegion, Priority))
	{
		if (ColorCorrectRegionsSubsystem)
		{
			ColorCorrectRegionsSubsystem->SortRegionsByPriority();
		}
	}
}
#endif //WITH_EDITOR

