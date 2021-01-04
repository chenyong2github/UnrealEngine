// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ControllerComponent.h"

UControllerComponent::UControllerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UControllerComponent::IsLocalController() const
{
	return GetControllerChecked<AController>()->IsLocalController();
}

void UControllerComponent::GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const
{
	GetControllerChecked<AController>()->GetPlayerViewPoint(Location, Rotation);
}