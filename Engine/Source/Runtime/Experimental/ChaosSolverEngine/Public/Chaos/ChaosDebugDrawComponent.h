// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Chaos/Declares.h"
#include "Engine/World.h"
#include "ChaosDebugDrawComponent.generated.h"


UCLASS(BlueprintType, ClassGroup = Chaos, meta = (BlueprintSpawnableComponent))
class CHAOSSOLVERENGINE_API UChaosDebugDrawComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UChaosDebugDrawComponent();

	//~ Begin UActorComponent interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
	//~ End UActorComponent interface

	static void BindWorldDelegates();

private:
	static void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS);
	static void CreateDebugDrawActor(UWorld* World);

	bool bInPlay;
};

