// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GameFrameworkComponent.h"

UGameFrameworkComponent::UGameFrameworkComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
}

bool UGameFrameworkComponent::HasAuthority() const
{
	AActor* Owner = GetOwner();
	check(Owner);
	return Owner->HasAuthority();
}

FTimerManager& UGameFrameworkComponent::GetWorldTimerManager() const
{
	AActor* Owner = GetOwner();
	check(Owner);
	return Owner->GetWorldTimerManager();
}