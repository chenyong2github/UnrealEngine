// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystem.h"

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

	virtual UWorld* GetWorld() const override;
};
