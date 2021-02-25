// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "LandscapeSubsystem.generated.h"

class ALandscapeProxy;

UCLASS(MinimalAPI)
class ULandscapeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	ULandscapeSubsystem();
	virtual ~ULandscapeSubsystem();

	void RegisterActor(ALandscapeProxy* Proxy);
	void UnregisterActor(ALandscapeProxy* Proxy);

	// Begin FTickableGameObject overrides
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// End FTickableGameObject overrides

#if WITH_EDITOR
	LANDSCAPE_API void BuildGrassMaps();
	LANDSCAPE_API int32 GetOutdatedGrassMapCount();
#endif

private:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	TArray<ALandscapeProxy*> Proxies;

#if WITH_EDITOR
	class FLandscapeGrassMapsBuilder* GrassMapsBuilder;
#endif
};
