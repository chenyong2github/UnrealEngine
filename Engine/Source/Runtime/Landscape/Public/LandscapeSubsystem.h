// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "LandscapeSubsystem.generated.h"

class ULandscapeInfoMap;

struct FLandscapeSubsystemTickFunction : public FTickFunction
{
	FLandscapeSubsystemTickFunction() : FLandscapeSubsystemTickFunction(nullptr) {}
	FLandscapeSubsystemTickFunction(ULandscapeSubsystem* InSubsystem) : Subsystem(InSubsystem) {}
	virtual ~FLandscapeSubsystemTickFunction() {}

	// Begin FTickFunction overrides
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
	// End FTickFunction overrides

	class ULandscapeSubsystem* Subsystem;
};

UCLASS()
class ULandscapeSubsystem : public UWorldSubsystem, public FTickFunction
{
	GENERATED_BODY()

	ULandscapeSubsystem();
	virtual ~ULandscapeSubsystem();

public:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem
		
private:
	void Tick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

	friend struct FLandscapeSubsystemTickFunction;
	FLandscapeSubsystemTickFunction TickFunction;

	ULandscapeInfoMap* LandscapeInfoMap;
};

