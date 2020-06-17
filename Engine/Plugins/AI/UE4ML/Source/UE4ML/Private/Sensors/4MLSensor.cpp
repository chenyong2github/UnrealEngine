// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sensors/4MLSensor.h"
#include "Agents/4MLAgent.h"
#include "4MLTypes.h"
#include "4MLManager.h"


namespace
{
	uint32 NextSensorID = F4ML::InvalidSensorID + 1;
}

U4MLSensor::U4MLSensor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AgentID = F4ML::InvalidAgentID;
	bRequiresPawn = true;
	bIsPolling = true;

	TickPolicy = E4MLTickPolicy::EveryTick; 

	ElementID = F4ML::InvalidSensorID;
}

void U4MLSensor::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		if (GetClass()->HasAnyClassFlags(CLASS_Abstract) == false)
		{
			ElementID = NextSensorID++;
		}
	}
	else
	{
		const U4MLSensor* CDO = GetDefault<U4MLSensor>(GetClass());
		// already checked 
		ElementID = CDO->ElementID;
	}
}

void U4MLSensor::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_EveryTick = TEXT("tick_every_frame");
	const FName NAME_EveryNTicks = TEXT("tick_every_n_frames");
	const FName NAME_EveryXFrames = TEXT("tick_every_x_seconds");

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_EveryTick)
		{
			TickPolicy = E4MLTickPolicy::EveryTick;
		}
		else if (KeyValue.Key == NAME_EveryNTicks)
		{
			TickPolicy = E4MLTickPolicy::EveryNTicks;
			ensure(KeyValue.Value.Len() > 0);
			TickEvery.Ticks = FCString::Atoi(*KeyValue.Value);
		}
		else if (KeyValue.Key == NAME_EveryXFrames)
		{
			TickPolicy = E4MLTickPolicy::EveryXSeconds;
			ensure(KeyValue.Value.Len() > 0);
			TickEvery.Ticks = FCString::Atof(*KeyValue.Value);
		}
	}
}

void U4MLSensor::OnAvatarSet(AActor* Avatar)
{
	// kick off first sensing to populate observation data
	SenseImpl(0.f);
	AccumulatedTicks = 0;
	AccumulatedSeconds = 0.f;
}

void U4MLSensor::Sense(const float DeltaTime)
{
	++AccumulatedTicks;
	AccumulatedSeconds += DeltaTime;

	bool bTick = false;
	switch (TickPolicy)
	{
	case E4MLTickPolicy::EveryXSeconds:
		bTick = (AccumulatedSeconds >= TickEvery.Seconds);
		break;
	case E4MLTickPolicy::EveryNTicks:
		bTick = (AccumulatedTicks >= TickEvery.Ticks);
		break;
	case E4MLTickPolicy::Never:
		bTick = false;
		break;
	default:
		bTick = true;
		break;
	}

	if (bTick)
	{
		SenseImpl(AccumulatedSeconds);
		AccumulatedTicks = 0;
		AccumulatedSeconds = 0.f;
	}
}

bool U4MLSensor::IsConfiguredForAgent(const U4MLAgent& Agent) const
{
	return AgentID == Agent.GetAgentID();
}

bool U4MLSensor::ConfigureForAgent(U4MLAgent& Agent)
{
	AgentID = Agent.GetAgentID();
	return true;
}

const U4MLAgent& U4MLSensor::GetAgent() const
{
	return *CastChecked<U4MLAgent>(GetOuter());
}

void U4MLSensor::OnPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (OldPawn)
	{
		ClearPawn(*OldPawn);
	}
	if (NewPawn)
	{
		OnAvatarSet(NewPawn);
	}
}

void U4MLSensor::ClearPawn(APawn& InPawn)
{

}
