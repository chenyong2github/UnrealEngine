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

	/** Called once all UWorldSubsystems have been initialized */
	virtual void PostInitialize() {}

	/** Updates sub-system required streaming levels (called by world's UpdateStreamingState function) */
	virtual void UpdateStreamingState() {}

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const;
};
