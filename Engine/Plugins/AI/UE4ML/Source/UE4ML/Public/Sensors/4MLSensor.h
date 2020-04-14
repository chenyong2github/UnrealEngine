// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Agents/4MLAgentElement.h"
#include "4MLTypes.h"
#include "4MLSensor.generated.h"


class U4MLAgent;
struct F4MLDescription;

UENUM()
enum class E4MLTickPolicy : uint8
{
	EveryTick,
	EveryXSeconds,
	EveryNTicks,
	Never
};

UCLASS(Abstract, Blueprintable, EditInlineNew)
class UE4ML_API U4MLSensor : public U4MLAgentElement
{
    GENERATED_BODY()
public:
    U4MLSensor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitProperties() override;

	/** Called before actuator's destruction. Can be called as part of new 
	 *	agent config application when old actuator get destroyed */
	virtual void Configure(const TMap<FName, FString>& Params) override;

	virtual void OnAvatarSet(AActor* Avatar) override;

	// AIGym leftovers. Potentially to be removed 
	F4ML::FAgentID GetAgentID() const { return AgentID; }
	const U4MLAgent& GetAgent() const;
	bool IsConfiguredForAgent(const U4MLAgent& Agent) const;
	bool IsPolling() const { return bIsPolling; }

	/** @return True if config was successful. Only in that case will the sensor
	 *		instance be added to Agent's active sensors */
	virtual bool ConfigureForAgent(U4MLAgent& Agent);

	void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);

	/** Called for every sense, regardless of whether it's a polling-type or not.
	 */
	void Sense(const float DeltaTime);

	/** 
	 *	@param bTransfer if set to true the Sensor is expected to clear all stored
	 *		observations after copying/moving them to OutObservations
	 */
	virtual void GetObservations(F4MLMemoryWriter& Ar) PURE_VIRTUAL(U4MLSensor::GetObservations, );

protected:	
	virtual void ClearPawn(APawn& InPawn);

	/** called from Sense based on TickPolicy */
	virtual void SenseImpl(const float DeltaTime) {}

protected:
	F4ML::FAgentID AgentID;

	UPROPERTY(EditDefaultsOnly, Category=UE4ML)
	uint32 bRequiresPawn : 1;

	UPROPERTY(EditDefaultsOnly, Category = UE4ML)
	uint32 bIsPolling : 1;

	UPROPERTY(EditDefaultsOnly, Category = UE4ML)
	E4MLTickPolicy TickPolicy;

	struct FTicksOrSeconds
	{
		union 
		{
			int32 Ticks;
			float Seconds;
		};
		FTicksOrSeconds() : Ticks(0) {}
	};
	FTicksOrSeconds TickEvery;

	mutable FCriticalSection ObservationCS;
private:
	int32 AccumulatedTicks;
	float AccumulatedSeconds;
};	
