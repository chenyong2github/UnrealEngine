// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystem.h"
#include "Engine/EngineTypes.h"
#include "WorldSubsystem.generated.h"

/**
 * UWorldSubsystem
 * Base class for auto instanced and initialized systems that share the lifetime of a UWorld

 */

UCLASS(Abstract)
class ENGINE_API UWorldSubsystem : public USubsystem
{
	GENERATED_BODY()

public:
	UWorldSubsystem();

	virtual UWorld* GetWorld() const override final;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const;
};
