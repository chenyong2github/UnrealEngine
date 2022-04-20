// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
//#include "TimeOfDaySubsystem.generated.h"

class ATimeOfDayActor;

class UTimeOfDaySubsystem : public UWorldSubsystem
{
public:
	/** UWorldSubsystem interface */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	TIMEOFDAY_API ATimeOfDayActor* GetTimeOfDayActor() const;
};