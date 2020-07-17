// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"

UWorldSubsystem::UWorldSubsystem()
	: USubsystem()
{

}

UWorld* UWorldSubsystem::GetWorld() const
{
	return Cast<UWorld>(GetOuter());
}

bool UWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	UWorld* World = Cast<UWorld>(Outer);
	check(World);
	return DoesSupportWorldType(World->WorldType);
}

bool UWorldSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE;
}