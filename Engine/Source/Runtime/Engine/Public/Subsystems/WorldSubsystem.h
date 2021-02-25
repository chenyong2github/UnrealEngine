// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystem.h"
#include "Engine/EngineTypes.h"
#include "Engine/Public/Tickable.h"
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

/**
 * UTickableWorldSubsystem
 * Base class for auto instanced and initialized systems that share the lifetime of a UWorld and are ticking along with it

 */

UCLASS(Abstract)
class ENGINE_API UTickableWorldSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UTickableWorldSubsystem();

	// FTickableGameObject implementation Begin
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsAllowedToTick() const override final;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override PURE_VIRTUAL(UTickableWorldSubsystem::GetStatId, return TStatId(););
	// FTickableGameObject implementation End

	// USubsystem implementation Begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// USubsystem implementation End

	bool IsInitialized() const { return bInitialized; }

private:
	bool bInitialized = false;
};