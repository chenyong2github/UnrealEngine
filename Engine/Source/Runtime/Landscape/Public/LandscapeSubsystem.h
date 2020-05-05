// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "LandscapeSubsystem.generated.h"

class ALandscapeProxy;

UCLASS(MinimalAPI)
class ULandscapeSubsystem : public UWorldSubsystem, public FTickFunction
{
	GENERATED_BODY()

public:
	ULandscapeSubsystem();
	virtual ~ULandscapeSubsystem();

	void RegisterActor(ALandscapeProxy* Proxy);
	void UnregisterActor(ALandscapeProxy* Proxy);

#if WITH_EDITOR
	LANDSCAPE_API void BuildGrassMaps();
	LANDSCAPE_API int32 GetOutdatedGrassMapCount();
#endif

private:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	// Begin FTickFunction overrides
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
	// End FTickFunction overrides

	TArray<ALandscapeProxy*> Proxies;

#if WITH_EDITOR
	class FLandscapeGrassMapsBuilder* GrassMapsBuilder;
#endif
};
