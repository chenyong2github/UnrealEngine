// Copyright Epic Games, Inc. All Rights Reserved.


#include "TimeOfDaySubsystem.h"
#include "EngineUtils.h"
#include "TimeOfDayActor.h"

bool UTimeOfDaySubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE;
}

void UTimeOfDaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UTimeOfDaySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

ATimeOfDayActor* UTimeOfDaySubsystem::GetTimeOfDayActor() const
{
	TActorIterator<ATimeOfDayActor> It(GetWorld());
	return It ? *It : nullptr;
}