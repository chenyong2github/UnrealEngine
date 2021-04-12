// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Agents/MLAdapterAgentElement.h"
#include "MLAdapterTypes.h"
#include "MLAdapterSensor.generated.h"


class UMLAdapterAgent;
struct FMLAdapterDescription;

UENUM()
enum class EMLAdapterTickPolicy : uint8
{
	EveryTick,
	EveryXSeconds,
	EveryNTicks,
	Never
};

UCLASS(Abstract, Blueprintable, EditInlineNew)
class MLADAPTER_API UMLAdapterSensor : public UMLAdapterAgentElement
{
    GENERATED_BODY()
public:
    UMLAdapterSensor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitProperties() override;

	/** Called before actuator's destruction. Can be called as part of new 
	 *	agent config application when old actuator get destroyed */
	virtual void Configure(const TMap<FName, FString>& Params) override;

	virtual void OnAvatarSet(AActor* Avatar) override;

	// AIGym leftovers. Potentially to be removed 
	FMLAdapter::FAgentID GetAgentID() const { return AgentID; }
	const UMLAdapterAgent& GetAgent() const;
	bool IsConfiguredForAgent(const UMLAdapterAgent& Agent) const;
	bool IsPolling() const { return bIsPolling; }

	/** @return True if config was successful. Only in that case will the sensor
	 *		instance be added to Agent's active sensors */
	virtual bool ConfigureForAgent(UMLAdapterAgent& Agent);

	void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);

	/** Called for every sense, regardless of whether it's a polling-type or not.
	 */
	void Sense(const float DeltaTime);

	/** 
	 *	@param bTransfer if set to true the Sensor is expected to clear all stored
	 *		observations after copying/moving them to OutObservations
	 */
	virtual void GetObservations(FMLAdapterMemoryWriter& Ar) PURE_VIRTUAL(UMLAdapterSensor::GetObservations, );

protected:	
	virtual void ClearPawn(APawn& InPawn);

	/** called from Sense based on TickPolicy */
	virtual void SenseImpl(const float DeltaTime) {}

protected:
	FMLAdapter::FAgentID AgentID;

	UPROPERTY(EditDefaultsOnly, Category=MLAdapter)
	uint32 bRequiresPawn : 1;

	UPROPERTY(EditDefaultsOnly, Category = MLAdapter)
	uint32 bIsPolling : 1;

	UPROPERTY(EditDefaultsOnly, Category = MLAdapter)
	EMLAdapterTickPolicy TickPolicy;

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
