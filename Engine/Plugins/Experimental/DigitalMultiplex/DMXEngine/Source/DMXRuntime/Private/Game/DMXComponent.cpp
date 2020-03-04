// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DMXComponent.h"
#include "Library/DMXEntityFixturePatch.h"

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatch.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	FixturePatch.SetEntity(InFixturePatch);
}

// Sets default values for this component's properties
UDMXComponent::UDMXComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

// Called when the game starts
void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
}


// Called every frame
void UDMXComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

