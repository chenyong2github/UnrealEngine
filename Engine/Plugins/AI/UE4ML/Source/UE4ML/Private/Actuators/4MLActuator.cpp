// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actuators/4MLActuator.h"
#include "4MLTypes.h"
#include "4MLManager.h"


namespace
{
	uint32 NextActuatorID = F4ML::InvalidActuatorID + 1;
}

U4MLActuator::U4MLActuator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ElementID = F4ML::InvalidActuatorID;
}

void U4MLActuator::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		if (GetClass()->HasAnyClassFlags(CLASS_Abstract) == false)
		{
			ElementID = NextActuatorID++;
		}
	}
	else
	{
		const U4MLActuator* CDO = GetDefault<U4MLActuator>(GetClass());
		// already checked 
		ElementID = CDO->ElementID;
	}
}
