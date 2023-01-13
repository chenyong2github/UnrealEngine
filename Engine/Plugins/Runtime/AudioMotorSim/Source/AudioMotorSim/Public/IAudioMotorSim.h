// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/ActorComponent.h"
#include "IAudioMotorSim.generated.h"

struct FAudioMotorSimInputContext;
struct FAudioMotorSimRuntimeContext;

UINTERFACE(BlueprintType)
class UAudioMotorSim : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class AUDIOMOTORSIM_API IAudioMotorSim
{
	GENERATED_IINTERFACE_BODY()

public:
	virtual void Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo) = 0;

	virtual void Reset() {};
};

UCLASS(Blueprintable, Category = "AudioMotorSim", meta=(BlueprintSpawnableComponent))
class AUDIOMOTORSIM_API UAudioMotorSimComponent : public UActorComponent, public IAudioMotorSim
{
	GENERATED_BODY()

public:
	UAudioMotorSimComponent(const FObjectInitializer& ObjectInitializer);
	
	virtual void Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo) override {};
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "AudioMotorSimTypes.h"
#endif
